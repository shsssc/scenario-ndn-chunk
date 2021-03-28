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
  explicit NetBLTApp(std::string s);

  void run();


private:

  void checkRto();

  /**
   * express next interest
   * @return true if can continue to request more, false if cannot continue since bound reached
   */
  bool expressOneInterest();

  void fetchLoop();

  void
  handleData(const Interest &interest, const Data &data);

  void
  handleNack(const Interest &interest, const lp::Nack &nack);

  void
  handleTimeout(const Interest &interest);

  void retransmit(uint32_t segment);

  void outputData();

  bool finished() const;

  /**
   * this is deprecated.
   * @param weak
   * @return
   */
  bool decreaseWindow(bool weak = false);


  //below is rate-based control
  //----------------------

  static bool compare(double a, double b, double tolerance) {
    return a - b < -tolerance;
  }

  void additiveIncrease() {
    m_burstSz += m_options.aiStep * (m_rttEstimator.getSmoothedRtt() / time::milliseconds(5));
  }

  void multiplitiveDecrease() {
    m_burstSz /= 1.5;
    m_last_bs = -1;//already bad recv rate, not reasonable to monitor the rate
    std::cerr << "decrease at" << m_burstSz << "," << "" << ","
              << time::steady_clock::now().time_since_epoch().count() / 1000000 << std::endl;
  }

  double averageRate() {
    if (m_rate_history.empty()) {
      std::cerr << "error: no history available\n";
      return 0;
    }
    double avg = 0;
    for (auto i :m_rate_history) {
      avg += i;
    }
    avg /= m_rate_history.size();
    return avg;
  }

  void rateStateMachine() {
    const int WAIT_STATE = 0;
    const int numStates = 15;
    const int wait = 4;
    const double tolerance = 0.5;
    if (finished()) return;
    m_scheduler.schedule(time::microseconds(500), [this] { rateStateMachine(); });
    if (m_lastProbeSegment >= m_highAcked) {
      //probe have not get back
      m_rate_history.push_back(m_rc.getRate());
      if (m_rate_history.size() > numStates - wait) m_rate_history.pop_front();//keep only last few measurements
      if (m_rate_history.size() < numStates - wait) return;
      double avg = averageRate();
      if (compare(avg, m_last_bs, tolerance)) {
        //sending rate is below previous sending rate
        m_lastProbeSegment = m_highRequested + 1;
        m_rate_history.clear();
        //Adjustment gets back
        multiplitiveDecrease();
        m_adjustmentState = numStates;
      }
      return;
    }
    //probe have get back
    if (m_adjustmentState == WAIT_STATE) {
      //we just get the probe
      m_adjustmentState = numStates;
      m_rate_history.clear();//history was for prev sending rate
    }
    if (m_adjustmentState <= numStates - wait) {
      //we have waited to get steady measurement
      m_rate_history.push_back(m_rc.getRate());
    }
    m_adjustmentState--;//goto next state
    if (m_adjustmentState != WAIT_STATE) return;
    //we conducted enough measurement to make a decision
    double average = averageRate();

    m_lastProbeSegment = m_highRequested + 1;
    m_rate_history.clear();
    //Adjustment gets back
    m_last_bs = m_burstSz;
    if (compare(average, m_burstSz, tolerance)) {
      multiplitiveDecrease();
    } else
      additiveIncrease();
  }

  //above is rate based control
  //---------------------------
  bool fastRetransmission(uint32_t segment);

  void printStats();

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
  uint32_t m_highRequested;
  uint32_t m_lastProbeSegment;
  uint32_t m_highAcked;

  //stats
  uint64_t m_bytesTransfered;
  uint32_t m_retransmissionCount;
  time::steady_clock::TimePoint m_startTime;
  util::RttEstimator m_rttEstimator;


  //experimental: flow balance
  uint32_t m_receivedThisRTT;
  uint32_t m_sentThisRTT;

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
