//
// Created by developer on 1/17/21.
//

#include "NetBLTApp.h"

std::string ndn::NetBLTApp::formatThroughput(double throughput) {
  int pow = 0;
  while (throughput >= 1000.0 && pow < 4) {
    throughput /= 1000.0;
    pow++;
  }
  switch (pow) {
    case 0:
      return to_string(throughput) + " bit/s";
    case 1:
      return to_string(throughput) + " kbit/s";
    case 2:
      return to_string(throughput) + " Mbit/s";
    case 3:
      return to_string(throughput) + " Gbit/s";
    case 4:
      return to_string(throughput) + " Tbit/s";
  }
  return "";
}

void ndn::NetBLTApp::printStats() {
  using namespace ndn::time;
  duration<double, seconds::period> timeElapsed = steady_clock::now() - m_startTime;
  double throughput = 8 * m_bytesTransfered / timeElapsed.count();

  std::cout << "\n\nAll segments have been received.\n"
            << "Time elapsed: " << timeElapsed << "\n"
            << "Segments received: " << m_lastSegment + 1 << "\n" //todo this could be bad
            << "Transferred size: " << m_bytesTransfered / 1e3 << " kB" << "\n"
            << "Goodput: " << formatThroughput(throughput) << "\n"
            << "Retransmission: " << m_retransmissionCount << "\n" << std::endl;
}

bool ndn::NetBLTApp::finished() const {
  return m_hasLastSegment && m_nextToPrint > m_lastSegment;
}

void ndn::NetBLTApp::outputData() {
  for (auto it = m_dataBuffer.begin();
       it != m_dataBuffer.end() && it->first == m_nextToPrint;
       it = m_dataBuffer.erase(it), ++m_nextToPrint) {
    const Block &content = it->second->getContent();
    m_bytesTransfered += content.value_size();
    //std::cerr << "acked " << m_nextToPrint << std::endl;
    //std::cerr.write(reinterpret_cast<const char*>(content.value()), content.value_size());
  }
}

ndn::NetBLTApp::NetBLTApp(std::string s) : m_nextSegment(0), m_prefix(std::move(s)),
                                           m_hasLastSegment(false), m_burstSz(100), m_minBurstSz(1),
                                           m_lastDecrease(),
                                           m_burstInterval_ms(5), m_SOSSz(10000), m_defaultRTT(250),
                                           m_nextToPrint(0), m_bytesTransfered(0),
                                           m_retransmissionCount(0), m_startTime(), m_lastSegment(0),
                                           m_highRequested(0),
                                           m_lastProbeSegment(0), m_highAcked(0), m_sc(), m_adjustmentState(0),
                                           m_scheduler(m_face.getIoService()) {
  m_dv = make_unique<chunks::DiscoverVersion>(m_face, m_prefix, m_optVD);
  m_SOSSz = m_options.m_SOSSz;
  m_burstSz = m_options.m_burstSz;
  m_minBurstSz = m_options.m_minBurstSz;
  m_defaultRTT = m_options.m_defaultRTT;
  m_burstInterval_ms = m_options.m_burstInterval_ms;
  m_rttEstimator.addMeasurement(m_defaultRTT);
  for (int i = 0; i < 50; i++) {
    m_rttEstimator.addMeasurement(m_defaultRTT);
  }
  std::cout << std::setprecision(10);
  std::cout << "event, segment, time, rate\n";
}

void ndn::NetBLTApp::run() {
  m_startTime = time::steady_clock::now();
  m_lastDecrease = m_startTime;
  m_dv->onDiscoverySuccess.connect([this](const Name &versionedName) {
    std::cerr << "Discovered version " << versionedName << std::endl;
    m_versionedName = versionedName;
    fetchLoop();
    checkRto();
    rateStateMachineNew();
  });
  m_dv->onDiscoveryFailure.connect([](const std::string &msg) {
    std::cout << msg;
    NDN_THROW(std::runtime_error(msg));
  });
  m_dv->run();
}

void ndn::NetBLTApp::checkRto() {

  bool shouldDecreaseWindow = false;
  for (auto &entry : m_inNetworkPkt) {
    SegmentInfo &segInfo = entry.second;
    if (!segInfo.inRtQueue) { // skip segments already in the retx queue
      auto timeElapsed = time::steady_clock::now() - segInfo.timeSent;
      if (timeElapsed > segInfo.rto) { // timer expired?
        if (segInfo.canTriggerTimeout) shouldDecreaseWindow = true;
        std::cout << "timeout, " << entry.first << ","
                  << time::steady_clock::now().time_since_epoch().count() / 1000000.
                  << ","
                  << m_burstSz
                  << std::endl;
        retransmit(entry.first);
      }
    }
  }
  if (shouldDecreaseWindow /*&& decreaseWindow()*/) {
    std::cerr << "rtoTimeout" << std::endl;
    for (auto &entry:m_inNetworkPkt) {
      //we disqualify all outstanding packet at a drop event to cause another window decrease.
      //They are not part of the "adjustment result"
      //They will be requalified if they time out and get send
      entry.second.canTriggerTimeout = false;
    }
  }

  // schedule the next check after predefined interval
  if (!finished())
    m_scheduler.schedule(m_options.rtoCheckInterval, [this] { checkRto(); });
}

bool ndn::NetBLTApp::expressOneInterest() {
  uint32_t segToFetch = 0;
  while (!retransmissionPQ.empty()
         && m_inNetworkPkt.find(retransmissionPQ.top()) == m_inNetworkPkt.end()) {
    retransmissionPQ.pop();
  }
  if (!retransmissionPQ.empty()) {
    segToFetch = retransmissionPQ.top();
    m_inNetworkPkt[segToFetch].inRtQueue = false;
    retransmissionPQ.pop();
  } else {
    segToFetch = m_nextSegment;
    //beyond last
    if (m_hasLastSegment && segToFetch > m_lastSegment) return false;
    //SOS reached: either scheduled first resend or in-network first has low segment number
    if (!retransmissionPQ.empty() && segToFetch - retransmissionPQ.top() > m_SOSSz ||
        !m_inNetworkPkt.empty() && segToFetch - m_inNetworkPkt.begin()->first > m_SOSSz) {
      decreaseWindow();//this is really bad
      std::cerr << "SOS Bound reached" << std::endl;
      return false;
    }


    m_nextSegment++;
  }
  auto interest = Interest()
          .setName(Name(m_versionedName).appendSegment(segToFetch))
          .setCanBePrefix(false)
          .setMustBeFresh(true)
          .setInterestLifetime(time::milliseconds(4000));
  //if (m_rttEstimator.hasSamples()) {
  //  interest.setInterestLifetime(
  //          time::milliseconds(m_rttEstimator.getEstimatedRto().count() / 1000000));
  //}
  //make sure we do not have one already in flight
  auto now = time::steady_clock::now();
  SegmentInfo &tmp = m_inNetworkPkt[segToFetch];
  tmp.timeSent = now;
  tmp.unsatisfiedInterest++;
  tmp.canTriggerTimeout = true;
  tmp.rto = m_rttEstimator.getEstimatedRto();
  m_face.expressInterest(interest,
                         bind(&NetBLTApp::handleData, this, _1, _2),
                         bind(&NetBLTApp::handleNack, this, _1, _2),
                         bind(&NetBLTApp::handleTimeout, this, _1));
  if (m_options.isVerbose) {
    std::cout << "express, " << segToFetch << "," << time::steady_clock::now().time_since_epoch().count() / 1000000.
              << "," << m_burstSz
              << std::endl;
  }
  if (segToFetch > m_highRequested)m_highRequested = segToFetch;
  return true;
}

void ndn::NetBLTApp::fetchLoop() {
  if (finished()) return printStats();
  //for (uint32_t i = 0; i < m_burstSz; i++) {
  //  if (!expressOneInterest())break;
  //}
  expressOneInterest();
  //ns3::Simulator::Schedule(ns3::MilliSeconds(m_burstInterval_ms), &NetBLTApp::fetchLoop, this);
  m_scheduler.schedule(time::nanoseconds((unsigned) (m_burstInterval_ms.count() * 1000000 / m_burstSz)),
                       [this] { fetchLoop(); });
}

void ndn::NetBLTApp::handleData(const ndn::Interest &interest, const ndn::Data &data) {
  m_recvCount++;
  m_rc.receive(static_cast<int>(m_burstSz));
  //std::cerr << m_rc.getRate() << std::endl;
  if (!m_hasLastSegment && data.getFinalBlock()) {
    m_hasLastSegment = true;
    m_lastSegment = data.getFinalBlock()->toSegment();
    std::cerr << "last segment: " << m_lastSegment << std::endl;
  }
  uint32_t segment = data.getName()[-1].toSegment();



  //measure
  if (m_inNetworkPkt.find(segment) != m_inNetworkPkt.end()) {
    if (!m_inNetworkPkt[segment].retransmitted) {
      auto t = time::steady_clock::now() - m_inNetworkPkt[segment].timeSent;
      m_rttEstimator.addMeasurement(t);
      //if (t < time::milliseconds(1)) {
      //  std::cerr << t << std::endl;
      //  std::cerr << time::steady_clock::now() << "  " << m_inNetworkPkt[segment].timeSent;
      //  std::cerr << segment << std::endl;
      //}
      m_sc.report(t.count());
    }
    m_inNetworkPkt.erase(segment);
  }

  //duplicate data?
  if (segment < m_nextToPrint)return;
  //now we already removed current segment
  fastRetransmission(segment);
  m_dataBuffer.emplace(segment, data.shared_from_this());
  //std::cerr << "data buffer sz" << m_dataBuffer.size() << "rtq sz" << retransmissionPQ.size() << "set sz"
  //          << m_inNetworkPkt.size() << std::endl;
  outputData();
  //we output window after change
  if (m_options.isVerbose) {
    std::cout << "ack, " << segment << "," << time::steady_clock::now().time_since_epoch().count() / 1000000. << ","
              << m_rateCollected__ //m_burstSz
              << std::endl;
  }
  if (segment > m_highAcked)m_highAcked = segment;
}

void ndn::NetBLTApp::handleNack(const ndn::Interest &interest, const ndn::lp::Nack &nack) {
  std::cerr << "nack" << std::endl;
}

void ndn::NetBLTApp::handleTimeout(const ndn::Interest &interest) {
  uint32_t segment = interest.getName()[-1].toSegment();
  m_inNetworkPkt[segment].unsatisfiedInterest--;
  //only use timeout on the last instance of a single segment
  if (m_inNetworkPkt[segment].unsatisfiedInterest > 0) {
    //std::cerr << "supress" << std::endl;
    return;
  }
  if (m_inNetworkPkt.at(segment).canTriggerTimeout &&
      !m_inNetworkPkt.at(segment).retransmitted /*&& decreaseWindow()*/) {
    for (auto &item:m_inNetworkPkt) {
      item.second.canTriggerTimeout = false;
    }
  }

  if (m_options.isVerbose) {
    std::cout << "timeout, " << segment << "," << time::steady_clock::now().time_since_epoch().count() / 1000000.
              << ","
              << m_burstSz
              << std::endl;
  }

  retransmit(segment);
}

void ndn::NetBLTApp::retransmit(uint32_t segment) {
  if (m_inNetworkPkt[segment].inRtQueue) return;

  m_inNetworkPkt[segment].inRtQueue = true;
  m_inNetworkPkt[segment].retransmitted = true;
  m_retransmissionCount++;
  retransmissionPQ.push(segment);
}

bool ndn::NetBLTApp::decreaseWindow(bool weak) {
  //return false;
  //m_fbEnabled = false;//when we decrease, we should stop taking actions on flow inbalance untill we observe one RTT
  auto now = time::steady_clock::now();
  auto diff = now - m_lastDecrease;
  //only decrease once in one rtt.
  //if (m_lastProbeSegment >= m_highAcked) return false;

  if (diff < m_defaultRTT && !m_rttEstimator.hasSamples() ||
      m_rttEstimator.hasSamples() && diff < m_rttEstimator.getSmoothedRtt() * 1.1)
    return false;
  return true;//will stop from functioning
  if (!m_options.simpleWindowAdj) {
    //this formula is experimental
    m_burstSz = diff / m_burstInterval_ms * m_options.aiStep + m_baseBurstSz;
    if (m_rttEstimator.hasSamples() && false) {
      //decrease one more RTT for detection latency
      m_burstSz -= m_rttEstimator.getSmoothedRtt() / m_burstInterval_ms * m_options.aiStep;
    }
  }

  m_burstSz /= weak ? 1.5 : 2;
  m_baseBurstSz = m_burstSz;
  std::cerr << "just reduced window size to " << m_burstSz << std::endl;
  if (m_burstSz < m_minBurstSz)m_burstSz = m_minBurstSz;
  m_lastDecrease = now;
  m_lastProbeSegment = m_highRequested + 1;
  return true;
}

bool ndn::NetBLTApp::fastRetransmission(uint32_t segment) {
  if (segment == m_nextToPrint) {
    //m_frtCounter = 0;
    return false;
  }
  if (segment < m_nextToPrint) {
    return false;
  }

  //always retransmit
  const uint32_t waitReorder = 4;
  bool shouldRetransmit = segment > waitReorder && m_inNetworkPkt.find(segment - waitReorder) != m_inNetworkPkt.end();
  for (int i = segment; i > segment - waitReorder && shouldRetransmit; i--) {
    //recent 5 must all be ready
    if (m_inNetworkPkt.find(i) != m_inNetworkPkt.end())shouldRetransmit = false;
  }
  if (!shouldRetransmit) return false;
  for (int i = segment - waitReorder;
       i >= segment - waitReorder - m_burstSz && m_inNetworkPkt.find(i) != m_inNetworkPkt.end(); i--) {
    retransmit(i);
    if (m_inNetworkPkt.at(i).canTriggerTimeout && decreaseWindow()) {
      std::cerr << "FRT" << std::endl;
      for (auto &item:m_inNetworkPkt) {
        item.second.canTriggerTimeout = false;
      }
    }
  }
  return true;
}
