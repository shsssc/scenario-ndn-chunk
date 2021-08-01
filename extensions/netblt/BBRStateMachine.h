//
// Created by developer on 7/19/21.
//

#ifndef EXTENSIONS_BBRSTATEMACHINE_H
#define EXTENSIONS_BBRSTATEMACHINE_H

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
#include "RateMeasureNew.h"

namespace ndn {
struct SegmentInfo;

class NetBLTApp;

}

class BBRStateMachine {
  enum State {
    ProbeBW, ProbeRTT
  };
  /**
   * pacing gain index = 0-7
   */
  short probeBwStage;
  const double PACING_GAIN[8]{5 / 4., 3 / 4., 1, 1, 1, 1, 1, 1};
  State state;
  std::list<double> rateHistory;
  const unsigned rateFilterSize = 10;

  unsigned probeRttCounter;
  double cwnd_gain = 1.01;
  double savedBurstSize;
  /*
   * pass in variables from outside
   */
  LatencyCollector &m_sc;
  RateMeasureNew &m_rmn;
  double &m_sendRate;
  double m_burstSz = 3;
  ndn::NetBLTApp &app;
  ndn::Scheduler &m_scheduler;
  uint32_t &m_highRequested;
  uint32_t &m_lastProbeSegment;
  uint32_t &m_highAcked;
  std::map<uint32_t, ndn::SegmentInfo> &m_inNetworkPkt;

  unsigned windowSize() {
    return m_inNetworkPkt.size();
  }

  double getRate() {
    double rate = 5.4;
    for (auto i : rateHistory) {
      if (i > rate) rate = i;
    }
    return rate;
  }

  void reportRate(double rate) {
    rateHistory.push_back(rate);
    if (rateHistory.size() > rateFilterSize)rateHistory.pop_front();
  }

  void randomStartPacingGain() {
    std::random_device generator;
    std::uniform_int_distribution<int> distribution(1, 7);
    int number = distribution(generator);
    if (number == 1)number = 0;//never stop with 3/4
    probeBwStage = number;
  }

  void probeRTT() {
    m_burstSz = 1;
    probeRttCounter--;
    if (probeRttCounter < 1) {
      state = State::ProbeBW;
      m_lastProbeSegment = m_highRequested;
      randomStartPacingGain();
      std::cerr << "entering probeBW, Start with" << probeBwStage << std::endl;
    }
    //std::cerr << m_burstSz << std::endl;
  }

  void probeBW() {
    m_burstSz = getRate() * PACING_GAIN[probeBwStage++];
    probeBwStage %= 8;

    std::cerr << m_burstSz << std::endl;
  }

  void genRate() {
    double BDP = getRate() / 5. * m_sc.minInterval();
    if (BDP * cwnd_gain + 3 < windowSize()) {
      m_sendRate = .3;
      if (m_burstSz > 1.1)m_burstSz = getRate();
    } else {
      m_sendRate = m_burstSz;
    }

    //std::cerr << "BDP:" << BDP << "window" << windowSize() << "rate"<<m_sendRate<<std::endl;

  }

public:
  BBRStateMachine(RateMeasureNew &rmn, LatencyCollector &lc, double &burstSz, ndn::NetBLTApp &nb,
                  ndn::Scheduler &scheduler,
                  uint32_t &highRequested, uint32_t &lastProbeSegment, uint32_t &highAcked,
                  std::map<uint32_t, ndn::SegmentInfo> &window) :
          m_rmn(rmn),
          m_sc(lc),
          m_sendRate(burstSz),
          app(nb),
          m_scheduler(scheduler),
          m_highRequested(highRequested),
          m_lastProbeSegment(lastProbeSegment),
          m_highAcked(highAcked),
          m_inNetworkPkt(window) {
    state = State::ProbeBW;
    randomStartPacingGain();
  }

  void run() {
    //if  return;
    m_scheduler.schedule(ndn::time::microseconds(1000), [this] {
      run();
      genRate();
    });

    if (!m_sc.ready())return;
    if (!m_rmn.ready())return;

    if (m_sc.shouldProbeRTT()) {
      state = State::ProbeRTT;
      std::cerr << "force refresh RTT!" << std::endl;
      probeRttCounter = 200;
    }

    if (state == State::ProbeRTT) {
      probeRTT();
      return;
    }


    //below are packet timer;
    if (m_lastProbeSegment > m_highAcked)return;
    m_lastProbeSegment = m_highRequested;

    //update rate with packet timer;
    double rate = m_rmn.getRate1();
    reportRate(rate);


    if (state == State::ProbeBW) {
      probeBW();
    }

  }

};


#endif //EXTENSIONS_BBRSTATEMACHINE_H
