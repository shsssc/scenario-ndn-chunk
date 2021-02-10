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

namespace ndn {
struct SegmentInfo {
  ScopedPendingInterestHandle interestHdl;
  time::steady_clock::TimePoint timeSent;
  time::nanoseconds rto;
  bool inRtQueue = false;
  bool retransmitted = false;
};

class NetBLTApp {
public:
  explicit NetBLTApp(std::string s) : m_nextSegment(0), m_prefix(std::move(s)),
                                      m_hasLastSegment(false), m_burstSz(100), m_minBurstSz(1), m_lastDecrease(),
                                      m_burstInterval_ms(5), m_SOSSz(10000), m_defaultRTT(140),
                                      m_nextToPrint(0), m_bytesTransfered(0),
                                      m_retransmissionCount(0), m_startTime(), m_lastSegment(0),
                                      m_frtCounter(0), m_fbEnabled(false), m_highRequested(0),
                                      m_lastDecreaseSegment(0), m_highAcked(0),
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
      checkBalance();
      fetchLoop();
    });
    m_dv->onDiscoveryFailure.connect([](const std::string &msg) {
      std::cout << msg;
      NDN_THROW(std::runtime_error(msg));
    });
    m_dv->run();
  }


private:
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
            .setInterestLifetime(time::milliseconds(1000));
    if (m_rttEstimator.hasSamples()) {
      interest.setInterestLifetime(
              time::milliseconds(m_rttEstimator.getEstimatedRto().count() / 1000000));
    }
    //.setInterestLifetime(m_optVD.interestLifetime);//todo
    SegmentInfo &tmp = m_inNetworkPkt[segToFetch];
    tmp.timeSent = time::steady_clock::now();
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
    for (uint32_t i = 0; i < m_burstSz; i++) {
      if (!expressOneInterest())break;
    }
    //ns3::Simulator::Schedule(ns3::MilliSeconds(m_burstInterval_ms), &NetBLTApp::fetchLoop, this);
    m_scheduler.schedule(m_burstInterval_ms, [this] { fetchLoop(); });
  }

  void
  handleData(const Interest &interest, const Data &data) {
    m_receivedThisRTT++;
    //std::cerr << "data" << std::endl;
    if (!m_hasLastSegment && data.getFinalBlock()) {
      m_hasLastSegment = true;
      m_lastSegment = data.getFinalBlock()->toSegment();
      std::cerr << "last segment: " << m_lastSegment << std::endl;
    }
    uint32_t segment = data.getName()[-1].toSegment();



    //measure
    if (m_inNetworkPkt.find(segment) != m_inNetworkPkt.end()) {
      if (!m_inNetworkPkt[segment].retransmitted)
        m_rttEstimator.addMeasurement(time::steady_clock::now() - m_inNetworkPkt[segment].timeSent);
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
    increaseWindow();
    //we output window after change
    if (m_options.isVerbose) {
      std::cout << "ack, " << segment << "," << time::steady_clock::now().time_since_epoch().count() / 1000000 << ","
                << m_burstSz
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
    //std::cerr << "timeout" << std::endl;
    decreaseWindow();
    if (m_options.isVerbose) {
      std::cout << "timeout, " << segment << "," << time::steady_clock::now().time_since_epoch().count() / 1000000
                << ","
                << m_burstSz
                << std::endl;
    }
    //std::cerr << interest.getName()[-1].toSegment();
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
    //if (m_lastDecreaseSegment >= m_highAcked) return false;

    if (diff < m_defaultRTT && !m_rttEstimator.hasSamples() ||
        m_rttEstimator.hasSamples() && diff < m_rttEstimator.getSmoothedRtt() * 1.2)
      return false;

    if (!m_options.simpleWindowAdj) {
      //this formula is experimental
      m_burstSz = diff / m_burstInterval_ms * m_options.aiStep + m_baseBurstSz;
      if (m_rttEstimator.hasSamples() && false) {
        //decrease one more RTT for detection latency
        m_burstSz -= m_rttEstimator.getSmoothedRtt() / m_burstInterval_ms * m_options.aiStep;
      }
    }

    m_burstSz /= weak ? 1.1 : 2;
    m_baseBurstSz = m_burstSz;
    std::cerr << "just reduced window size to " << m_burstSz << std::endl;
    if (m_burstSz < m_minBurstSz)m_burstSz = m_minBurstSz;
    m_lastDecrease = now;
    m_lastDecreaseSegment = m_highRequested+1;
    return true;
  }

  void increaseWindow() {
    if (!m_options.simpleWindowAdj) {
      auto now = time::steady_clock::now();
      auto diff = now - m_lastDecrease;
      m_burstSz = diff / m_burstInterval_ms * m_options.aiStep + m_baseBurstSz;
    } else {
      m_burstSz += m_options.aiStep / m_burstSz;
    }

    //std::cout << now <<"\t" <<m_burstSz <<std::endl;
    //return;
    //slow start
    //if (m_burstSz < 100) {
    //  m_burstSz++;
    //  return;
    //}
    //congestion avoidance
    //m_burstSz += m_options.aiStep / m_burstSz;
    //std::cerr <<"just increased window size" << m_burstSz<<std::endl;
  }

  bool fastRetransmission(uint32_t segment) {
    if (segment == m_nextToPrint) {
      m_frtCounter = 0;
      return false;
    }
    if (segment < m_nextToPrint) {
      return false;
    }
    //if (m_frtCounter == 0) m_afterGap = segment;
    m_frtCounter++;
    //we have waited a burst
    //we only do FRT once per RTT
    if (m_frtCounter > m_burstSz && decreaseWindow()) {
      std::cerr << "FRT\n";
      //we retransmit the entire unhandled
      //std::cerr <<  (m_dataBuffer.find(m_nextToPrint) != m_dataBuffer.end()) <<std::endl;
      for (int i = m_nextToPrint;
           m_inNetworkPkt.find(i) != m_inNetworkPkt.end() && i < m_burstSz + m_nextToPrint; i++) {
        //  for (uint32_t i = m_nextToPrint; i < m_burstSz + m_nextToPrint; i++) {
        std::cerr << i << "\n";
        retransmit(i);
      }
      m_frtCounter = 0;//not so useful since decrease window is checking for us.
    }

    if (m_options.isVerbose) {
      std::cout << "ackgap, " << m_nextToPrint << "," << time::steady_clock::now().time_since_epoch().count() / 1000000
                << ","
                << m_burstSz
                << std::endl;
    }

    return true;
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
  uint32_t m_lastDecreaseSegment;
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
};
}


#endif //EXTENSIONS_NETBLTAPP_H
