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
#include "RateMeasureNew.h"

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

  unsigned windowSize() {
    return m_inNetworkPkt.size();
  }

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
    m_burstSz +=
            m_options.aiStep * ((m_rttEstimator.getSmoothedRtt()) / time::milliseconds(5));
    //m_burstSz = 134;
  }

  void multiplitiveDecrease() {
    m_burstSz /= 1.5;
    m_last_bs = -1;//already bad recv rate, not reasonable to monitor the rate
    m_sc.clear();
    std::cerr << "decrease at" << m_burstSz << "," << "" << ","
              << time::steady_clock::now().time_since_epoch().count() / 1000000 << std::endl;
  }

  void cubicIncrease() {
    constexpr double CUBIC_C = 0.05;
    // Slow start phase
    if (m_burstSz < m_options.initSsthresh) {
      m_burstSz *= 2;
    }
      // Congestion avoidance phase
    else {
      // If wmax is still 0, set it to the current cwnd. Usually unnecessary,
      // if m_ssthresh is large enough.
      if (m_wmax < m_options.initCwnd) {
        m_wmax = m_burstSz / m_options.cubicBeta; //todo
      }

      // 1. Time since last congestion event in seconds
      const double t = (time::steady_clock::now() - m_lastDecrease).count() / 1e9;

      // 2. Time it takes to increase the window to m_wmax = the cwnd right before the last
      // window decrease.
      // K = cubic_root(wmax*(1-beta_cubic)/C) (Eq. 2)
      double k = std::cbrt(m_wmax * (1 - m_options.cubicBeta) / CUBIC_C);
      if (m_wmax * (m_options.cubicBeta) > (-.5 * k + m_wmax)) {
        k = 1 / 0.5 * m_wmax * (1 - m_options.cubicBeta);
      }

      // 3. Target: W_cubic(t) = C*(t-K)^3 + wmax (Eq. 1)
      const double wCubic = CUBIC_C * std::pow(t - k, 3) + m_wmax;
      const double wAI = .5 * (t - k) + m_wmax;

      //std::cerr << t - k << ",!" << m_wmax << std::endl;
      // 4. Estimate of Reno Increase (Eq. 4)
      //const double rtt = m_rttEstimator.getSmoothedRtt().count() / 1e9;
      //const double wEst = 0;

      // Actual adaptation
      //double cubicIncrement = wCubic - m_burstSz;
      // Cubic increment must be positive
      // Note: This change is not part of the RFC, but I added it to improve performance.
      //cubicIncrement = std::max(0.0, cubicIncrement);

      m_burstSz = (t <= k) ? std::min(wCubic, wAI) : std::max(wCubic, wAI);
      //std::cerr << "set to " << m_burstSz << m_wmax << std::endl;
    }
  }

  void cubicDecrease() {
    m_last_bs = -1;
    m_sc.clear();
    m_options.initSsthresh = 2;
    constexpr double CUBIC_C = 0.4;
    if (m_options.enableFastConv && m_burstSz < m_lastWmax) {
      m_lastWmax = m_burstSz;
      m_wmax = m_burstSz * (1.0 + m_options.cubicBeta) / 2.0;
      std::cerr << "!!!!!!!!!!!";
    } else {
      // Save old cwnd as wmax
      m_lastWmax = m_burstSz;
      m_wmax = m_burstSz;
    }
    //std::cerr << "reduced from " << m_burstSz << m_wmax << std::endl;
    m_burstSz = std::max(1., m_wmax * m_options.cubicBeta);
    //std::cerr << "reduced to " << m_burstSz << m_wmax << std::endl;

    m_lastDecrease = time::steady_clock::now();
  }

  void resetRate() {
    m_recvCount = 0;
    m_lastRecvCheckpoint = time::steady_clock::now();
    m_rateCollected__ = 0;
  }

  void rateStateMachine() {
    if (finished()) return;
    m_scheduler.schedule(time::microseconds(1000), [this] { rateStateMachine(); });
    m_sc.tic();
    if (m_sc.minInterval() == -1)return;
    if (m_rmn.ready() == false)return;

    //only control if the receiving rate has updated
    if (m_lastProbeSegment > m_highAcked)return;
    m_lastProbeSegment = m_highRequested + m_rmn.averageSize();

    //calculate overshoot
    double min_rtt = m_sc.minInterval();
    double rate = m_rmn.getRate1() / 5;
    //std::cerr << rate << "," << min_rtt << "," << "calculated window" << min_rtt * rate << "real_window" << windowSize()
    //          << std::endl;
    double overShoot = windowSize() - min_rtt * rate;

    //PID controller
    double overShootTarget = 8;
    double e = overShootTarget - overShoot;
    static double prev_e;
    const double d = e - prev_e;
    prev_e = e;

    //exponential slow start
    static bool startedUp;
    if (!startedUp && e > overShootTarget * 0.7) {
      m_burstSz *= pow(2, min_rtt / 1000);//double rate each second
      return;
    } else if (!startedUp) {
      startedUp = true;
      std::cerr << "started up with burst size" << m_burstSz << std::endl;
    }

    static double i;
    static std::list<double> i_history;
    const short ilimit = 20;
    static int consecutiveBelowCount;
    static int consecutiveAboveCount;
    if (e > 0) {
      consecutiveAboveCount++;
      consecutiveBelowCount = 0;
    } else if (e < 0) {
      consecutiveBelowCount++;
      consecutiveAboveCount = 0;
    }
    const int historyOffset = (consecutiveAboveCount > consecutiveBelowCount) ?
                              consecutiveAboveCount : consecutiveBelowCount;
    i_history.push_back(e);
    i += e;

    while (i_history.size() > ilimit + historyOffset) {
      i -= i_history.front();
      i_history.pop_front();
    }

    m_burstSz += (i * 0.002 / min_rtt * 50 + e * 0.05 + d * 0.02) / min_rtt * 60;


    std::cerr << overShoot - overShootTarget << ',' << m_burstSz << std::endl;

  }

  void rateStateMachineNew() {
    const int WAIT_STATE = 0;
    const int GOTBACK_STATE = 1;
    const int HOLDON_STATE = 2;
    const double allowedPacketerror = 0.6;
    const double tolerance = allowedPacketerror / m_rmn.measurementDelay() * 1000 / 200;
    const int target_RTT = -1;
    const int makeup_stages =
            m_sc.getMinRTT() < 0 || target_RTT <= m_sc.getMinRTT() ? 0 : target_RTT - m_sc.getMinRTT();
    if (finished()) return;
    m_scheduler.schedule(time::microseconds(1000), [this] { rateStateMachineNew(); });
    m_sc.tic();

    if (m_adjustmentState == WAIT_STATE && m_lastProbeSegment < m_highAcked) {
      //just got back
      m_makeupCount = 0;
      m_adjustmentState = GOTBACK_STATE;
      resetRate();
      m_rmn.nextStage(m_last_bs);//we do not use data for this interval since its transition
      /*
      if (m_sc.shouldDecrease()) {
        std::cerr << "!!!!!!!!!SC DECREASE" << std::endl;
      }
      if (m_sc.shouldDecrease()) {
        m_adjustmentState = WAIT_STATE;
        m_lastProbeSegment = m_highRequested + 1;//aft
        cubicDecrease();
        m_rmn.reportDecrease();
        std::cerr << m_last_bs << ",!!!!!!!earlyDecrease " << m_last_bs << std::endl;
      }*/
      return;
    }
    m_overshootFor++;

    //run makeup
    if (m_adjustmentState == HOLDON_STATE) {
      //if (m_makeupCount == 0)m_burstSz += tolerance * 2;
      m_makeupCount++;
      if (m_makeupCount >= makeup_stages /*m_overshootFor >= 40*/) {
        //std::cerr << "!!!!! Over shoot for" << m_overshootFor <<" min_RTT"<<m_sc.getMinRTT()  <<std::endl;
        cubicDecrease();
        m_adjustmentState = WAIT_STATE;//rate must change once ready
        m_lastProbeSegment = m_highRequested + 1;//after change, we need to know when result is ready
        m_rmn.reportDecrease();
        resetRate();
      }
      return;
    }

    m_rmn.reportReceived(m_recvCount);
    resetRate();

    double rate = m_rateCollected__ = m_rmn.getRate1();
    if (!m_rmn.ready())return;
    //we have at least waited measurement window size




    if (m_adjustmentState == GOTBACK_STATE) {
      if (compare(rate, m_burstSz, tolerance) && m_sc.shouldDecrease()) {
        m_adjustmentState = HOLDON_STATE;
      } else {
        m_adjustmentState = WAIT_STATE;//rate must change once ready
        m_lastProbeSegment = m_highRequested + 1;//after change, we need to know when result is ready
        m_last_bs = m_burstSz;
        cubicIncrease();
        m_overshootFor = 0;

      }
      std::cerr << rate << ",current " << m_burstSz << std::endl;

      return;
    }

    //m_adjustmentState == WAIT_STATE
    //we are waiting for probe to get back
    if (compare(rate, m_last_bs, tolerance) && m_sc.shouldDecrease()) {
      //m_adjustmentState = WAIT_STATE;//rate must change once ready
      m_adjustmentState = HOLDON_STATE;
      std::cerr << rate << ",lastdecrease " << m_last_bs << std::endl;
      //next wait_state will not adjust
    }

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
  double m_rateCollected__ = 0;


  //experimental: flow balance
  uint32_t m_recvCount;
  time::steady_clock::TimePoint m_lastRecvCheckpoint;
  //rtt
  LatencyCollector m_sc;
  RateCollector m_rc;
  RateMeasureNew m_rmn;
  //state machine
  int m_adjustmentState;
  int m_makeupCount = 0;
  int m_overshootFor = 0;
  //std::list<double> m_rate_history;
  double m_last_bs;


  //cubic
  double m_wmax = 0.0; ///< window size before last window decrease
  double m_lastWmax = 0.0; ///< last wmax
};
}


#endif //EXTENSIONS_NETBLTAPP_H
