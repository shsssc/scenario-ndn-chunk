//
// Created by developer on 1/17/21.
//

#ifndef EXTENSIONS_NETBLTAPP_H
#define EXTENSIONS_NETBLTAPP_H

#include <string>
#include "ns3/ndnSIM/ndn-cxx/face.hpp"
#include <ndn-cxx/util/rtt-estimator.hpp>
#include "../cat/discover-version.hpp"
#include "../cat/options.hpp"
#include <iostream>
#include <utility>
#include <queue>
#include <vector>
#include <set>
#include <map>
#include "ns3/scheduler.h"
#include "options.h"
#include "LatencyCollector.h"
#include "RateCollector.h"

namespace ndn {
struct SegmentInfo {
  ScopedPendingInterestHandle interestHdl;
  time::steady_clock::TimePoint timeSent;
  time::nanoseconds rto;
  bool inRtQueue = false;
  bool retransmitted = false;
  bool canTriggerTimeout = true;
  int unsatisfiedInterest = 0;
};

class NetBLTApp {
public:
  explicit NetBLTApp(std::string s) : m_nextSegment(0), m_prefix(std::move(s)),
                                      m_hasLastSegment(false), m_burstSz(100), m_minBurstSz(1),
                                      m_lastDecrease(),
                                      m_burstInterval_ms(5), m_SOSSz(10000), m_defaultRTT(140),
                                      m_nextToPrint(0), m_bytesTransfered(0),
                                      m_retransmissionCount(0), m_startTime(), m_lastSegment(0),
                                      m_frtCounter(0), m_fbEnabled(false), m_highRequested(0),
                                      m_lastProbeSegment(0), m_highAcked(0), m_sc(), m_adjustmentState(0),
                                      m_scheduler(m_face.getIoService()) {
    m_dv = make_unique<chunks::DiscoverVersion>(m_face, m_prefix, m_optVD);
    m_SOSSz = m_options.m_SOSSz;
    m_burstSz = m_options.m_burstSz;
    m_minBurstSz = m_options.m_minBurstSz;
    m_defaultRTT = m_options.m_defaultRTT;
    m_unbalanceCountThreshold = m_options.m_unbalanceCountThreshold;
    m_burstInterval_ms = m_options.m_burstInterval_ms;
  }

  void run() {
    m_startTime = time::steady_clock::now();
    m_lastDecrease = m_startTime;
    m_dv->onDiscoverySuccess.connect([this](const Name &versionedName) {
      std::cerr << "Discovered version " << versionedName << std::endl;
      m_versionedName = versionedName;
      //checkRate();
      checkDelay();
      checkBalance();
      fetchLoop();
      checkRto();
      rateStateMachine();
    });
    m_dv->onDiscoveryFailure.connect([](const std::string &msg) {
      std::cout << msg;
      NDN_THROW(std::runtime_error(msg));
    });
    m_dv->run();
  }


private:

  void checkRto() {

    bool shouldDecreaseWindow = false;
    for (auto &entry : m_inNetworkPkt) {
      SegmentInfo &segInfo = entry.second;
      if (!segInfo.inRtQueue) { // skip segments already in the retx queue
        auto timeElapsed = time::steady_clock::now() - segInfo.timeSent;
        if (timeElapsed > segInfo.rto) { // timer expired?
          if (segInfo.canTriggerTimeout) shouldDecreaseWindow = true;
          std::cout << "timeout, " << entry.first << ","
                    << time::steady_clock::now().time_since_epoch().count() / 1000000
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

  /**
   * express next interest
   * @return true if can continue to request more, false if cannot continue since bound reached
   */
  bool expressOneInterest() {
    uint32_t segToFetch = 0;
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
      std::cout << "express, " << segToFetch << "," << time::steady_clock::now().time_since_epoch().count() / 1000000
                << "," << m_burstSz
                << std::endl;
    }

    m_sentThisRTT++;
    if (segToFetch > m_highRequested)m_highRequested = segToFetch;
    return true;
  }

  void fetchLoop() {
    if (finished()) return printStats();
    //for (uint32_t i = 0; i < m_burstSz; i++) {
    //  if (!expressOneInterest())break;
    //}
    expressOneInterest();
    //ns3::Simulator::Schedule(ns3::MilliSeconds(m_burstInterval_ms), &NetBLTApp::fetchLoop, this);
    m_scheduler.schedule(time::microseconds((unsigned) (m_burstInterval_ms.count() * 1000 / m_burstSz)),
                         [this] { fetchLoop(); });
  }

  void
  handleData(const Interest &interest, const Data &data) {
    m_receivedThisRTT++;
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
        m_sc.report(t.count() / 1e4);
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
      std::cout << "ack, " << segment << "," << time::steady_clock::now().time_since_epoch().count() / 1000000 << ","
                << m_rc.getRate() //m_burstSz
                << std::endl;
    }
    if (segment > m_highAcked)m_highAcked = segment;
  }

  void
  handleNack(const Interest &interest, const lp::Nack &nack) {
    std::cerr << "nack" << std::endl;
  }

  void
  handleTimeout(const Interest &interest) {
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
      std::cout << "timeout, " << segment << "," << time::steady_clock::now().time_since_epoch().count() / 1000000
                << ","
                << m_burstSz
                << std::endl;
    }

    retransmit(segment);
  }

  void retransmit(uint32_t segment) {
    if (m_inNetworkPkt[segment].inRtQueue) return;
    retransmissionPQ.push(segment);
    m_inNetworkPkt[segment].inRtQueue = true;
    m_inNetworkPkt[segment].retransmitted = true;
    m_retransmissionCount++;
  }

  void outputData();

  bool finished() const;

  bool decreaseWindow(bool weak = false) {
    //return false;
    m_fbEnabled = false;//when we decrease, we should stop taking actions on flow inbalance untill we observe one RTT
    auto now = time::steady_clock::now();
    auto diff = now - m_lastDecrease;
    //only decrease once in one rtt.
    //if (m_lastProbeSegment >= m_highAcked) return false;

    if (diff < m_defaultRTT && !m_rttEstimator.hasSamples() ||
        m_rttEstimator.hasSamples() && diff < m_rttEstimator.getSmoothedRtt() * 1.1)
      return false;

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

  void rateStateMachine() {
    const int numStates = 15;
    const int wait = 4;
    if (finished()) return;
    m_scheduler.schedule(time::microseconds(500), [this] { rateStateMachine(); });
    if (m_lastProbeSegment >= m_highAcked) {
      //we are waiting for the current adjustment to be measurable
      m_rate_history.push_back(m_rc.getRate());
      if (m_rate_history.size() > numStates - wait)m_rate_history.pop_front();
      if (m_rate_history.size() < numStates - wait) return;
      double avg = 0;
      for (auto i :m_rate_history) {
        avg += i;
      }
      avg /= m_rate_history.size();
      if (avg < m_last_bs - 0.3) {
        //sending rate is below previous sending rate
        m_lastProbeSegment = m_highRequested + 1;
        m_rate_history.clear();
        //Adjustment gets back
        m_last_bs = -1;//we disable this feature since we do not have good assumption
        m_burstSz /= 1.5;
        std::cerr << "decrease1 at" << m_burstSz << "," << avg << ","
                  << time::steady_clock::now().time_since_epoch().count() / 1000000 << std::endl;
        m_adjustmentState = numStates;
      }
      return;
    }
    //now we have waited 1 RTT
    if (m_adjustmentState == 0) {
      //probe interest is back, we are in "wait" state
      m_adjustmentState = numStates;
      m_rate_history.clear();
    }
    if (m_adjustmentState <= numStates - wait)
      m_rate_history.push_back(m_rc.getRate());
    m_adjustmentState--;
    if (m_adjustmentState != 0) return;
    //now it must be 0, we can react
    double average = 0;
    for (double i : m_rate_history) {
      average += i;
    }
    average /= m_rate_history.size();

    m_lastProbeSegment = m_highRequested + 1;
    m_rate_history.clear();
    //Adjustment gets back
    m_last_bs = m_burstSz;
    if (average < m_burstSz - 0.3) {
      m_burstSz /= 1.5;
      m_last_bs = -1;//already bad, not reasonable to monitor the rate
      std::cerr << "decrease at" << m_burstSz << "," << average << ","
                << time::steady_clock::now().time_since_epoch().count() / 1000000 << std::endl;
    } else
      m_burstSz += m_options.aiStep * (m_rttEstimator.getSmoothedRtt() / time::milliseconds(5));
  }

  bool fastRetransmission(uint32_t segment) {
    if (segment == m_nextToPrint) {
      //m_frtCounter = 0;
      return false;
    }
    if (segment < m_nextToPrint) {
      return false;
    }
    //if (m_frtCounter == 0) m_afterGap = segment;
    //m_frtCounter++;
    //we have waited a burst
    //we only do FRT once per RTT
    //if (m_frtCounter > m_burstSz && decreaseWindow()) {
    //  std::cerr << "FRT\n";
    //we retransmit the entire unhandled
    //std::cerr <<  (m_dataBuffer.find(m_nextToPrint) != m_dataBuffer.end()) <<std::endl;
    //  m_frtCounter = 0;//not so useful since decrease window is checking for us.
    //}

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
    /*
    for (int i = m_nextToPrint;
         m_inNetworkPkt.find(i) != m_inNetworkPkt.end() && i < m_burstSz + m_nextToPrint; i++) {
      //  for (uint32_t i = m_nextToPrint; i < m_burstSz + m_nextToPrint; i++) {
      std::cerr << i << "\n";
      retransmit(i);
    }

    if (m_options.isVerbose) {
      std::cout << "ackgap, " << m_nextToPrint << "," << time::steady_clock::now().time_since_epoch().count() / 1000000
                << ","
                << m_burstSz
                << std::endl;
    }

    return true;
     */
  }

  void printStats();

  void checkBalance() {
    return;
    if (!m_fbEnabled && !m_rttEstimator.hasSamples()) {
      //wait if lack rtt
      m_scheduler.schedule(m_defaultRTT, [this] { checkBalance(); });
      return;
    }
    if (!m_fbEnabled) {
      m_fbEnabled = true;
      m_sentLastRTT = 0;
      m_sentThisRTT = 0;
      m_receivedThisRTT = 0;
      m_scheduler.schedule(m_rttEstimator.getSmoothedRtt(), [this] { checkBalance(); });
      m_unbalance_count = 0;
      return;
    }
    //now we have fb enabled and estimator ready
    if (m_sentLastRTT > m_receivedThisRTT + 5) m_unbalance_count++;
    else m_unbalance_count = 0;
    std::cerr << m_sentLastRTT << "\t" << m_receivedThisRTT << std::endl;
    m_sentLastRTT = m_sentThisRTT;
    m_sentThisRTT = 0;
    m_receivedThisRTT = 0;
    if (m_unbalanceCountThreshold < m_unbalance_count) {
      decreaseWindow();
      std::cerr << "balance decrease" << std::endl;
    }
    m_scheduler.schedule(m_rttEstimator.getSmoothedRtt(), [this] { checkBalance(); });
  }

  void checkDelay() {
    return;
    m_scheduler.schedule(m_burstInterval_ms * 4, [this] { checkDelay(); });
    if (m_sc.shouldDecrease()) {
      std::cerr << "delay decrease" << std::endl;
      decreaseWindow(true);
    }
  }

  void checkRate() {
    return;
    if (finished())return;
    // static std::list<bool> h;
    //static const unsigned limit = 30;
    //static const unsigned interval = 8;

    m_scheduler.schedule(time::microseconds(1500), [this] { checkRate(); });
    double observed = m_rc.getRate();
    double theory = m_burstSz -
                    ((m_sc.hasData() ? time::milliseconds(m_sc.minInterval() / 100) : m_rttEstimator.getSmoothedRtt())
                     / m_burstInterval_ms
                     * m_options.aiStep);

    if (m_rc.getHistory().size() < 2) return;

    /*h.push_back(observed < theory - .3);
    while (h.size() > limit) {
      h.pop_front();
    }

    if (h.size() < limit) return;
    bool shouldReduce = true;
    for (auto i : h) {
      if (!i) shouldReduce = false;
    }
    if (shouldReduce) {
      std::cerr << m_rc.getRate() << "," << m_rc.getVar() << " and theory->" << theory << std::endl;
      decreaseWindow(true);
    }
    return;
    */

    //todo
    /*
     * variance vs sending rate vs numsample
     */
    //std::cerr << m_rc.getRate() << ","
    //          << m_burstSz - (m_rttEstimator.getSmoothedRtt() / m_burstInterval_ms * m_options.aiStep) << std::endl;
    if (observed < theory - .5 /* && observed < theory - 2 * m_rc.getVar()*/ ) {
      std::cerr << time::milliseconds(m_sc.minInterval() / 100) << std::endl;
      std::cerr << m_rc.getRate() << "," << m_rc.getVar() << " and theory->" << theory << std::endl;
      decreaseWindow(true);
    }
    return;
    //m_rc.printHistory();

    if (!m_rc.hasHistory())return;
    auto history = m_rc.getHistory();
    double first = history.front();
    double last = history.back();
    if (last - first < m_options.aiStep && last - first > 0) {
      //if observed > burst size - RTT , no point to decrease
      if (observed > theory) {
        return;
      }
      double mean = 0;
      for (auto i : history) mean += i;
      mean /= history.size();
      double mae = 0;
      for (auto i:history) {
        mae += abs(i - mean);
      }
      mae /= history.size();
      if (mae < m_options.aiStep / 2 && decreaseWindow(true)) {
        std::cerr << mae << std::endl;

      }

    }
  }

  std::string formatThroughput(double throughput);

  //high level setup
  std::string m_prefix;
  Name m_versionedName;
  Face m_face;
  std::unique_ptr<chunks::DiscoverVersion> m_dv;
  chunks::Options m_optVD;//bad practice!
  ndn::Options m_options;
  bool m_hasLastSegment;
  uint32_t m_lastSegment;
  Scheduler m_scheduler;

  //fetch
  uint32_t m_nextSegment;
  std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<>> retransmissionPQ;
  std::map<uint32_t, shared_ptr<const Data>> m_dataBuffer;

  //output
  uint32_t m_nextToPrint;

  //SOS
  uint32_t m_SOSSz;
  std::map<uint32_t, SegmentInfo> m_inNetworkPkt; //must be map to check SOS

  //flow control
  double m_burstSz;
  double m_baseBurstSz = 0;
  uint32_t m_minBurstSz;
  time::milliseconds m_burstInterval_ms;
  time::steady_clock::TimePoint m_lastDecrease;
  time::milliseconds m_defaultRTT;
  //uint32_t m_frtThreshold;
  uint32_t m_frtCounter;
  //uint32_t m_afterGap;
  uint32_t m_highRequested;
  uint32_t m_lastProbeSegment;
  uint32_t m_highAcked;

  //stats
  uint64_t m_bytesTransfered;
  uint32_t m_retransmissionCount;
  time::steady_clock::TimePoint m_startTime;
  util::RttEstimator m_rttEstimator;


  //experimental: flow balance
  uint32_t m_sentLastRTT;
  uint32_t m_receivedThisRTT;
  uint32_t m_sentThisRTT;
  uint32_t m_unbalance_count;
  uint8_t m_unbalanceCountThreshold;
  bool m_fbEnabled;

  //rtt
  LatencyCollector m_sc;
  RateCollector m_rc;

  //state machine
  int m_adjustmentState;
  std::list<double> m_rate_history;
  double m_last_bs;
};
}


#endif //EXTENSIONS_NETBLTAPP_H
