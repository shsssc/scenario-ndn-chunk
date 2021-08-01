//
// Created by developer on 7/20/21.
//

#ifndef EXTENSIONS_OVERSHOOTSTATEMACHINE_H
#define EXTENSIONS_OVERSHOOTSTATEMACHINE_H

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
class OvershootStateMachine {
  /*
   * pass in variables from outside
   */
  LatencyCollector &m_sc;
  RateMeasureNew &m_rmn;
  double &m_burstSz;
  ndn::NetBLTApp &app;
  ndn::Scheduler &m_scheduler;
  uint32_t &m_highRequested;
  uint32_t &m_lastProbeSegment;
  uint32_t &m_highAcked;
  std::map<uint32_t, ndn::SegmentInfo> &m_inNetworkPkt;

  unsigned windowSize() {
    return m_inNetworkPkt.size();
  }

  static double getOvershootTarget(double receivingRate) {
    //return 10;
    double result = 20 - 0.2 * receivingRate;
    result = 30 - log(receivingRate) / log(1.25);
    if (result <= 4)result = 4;
    if (result > 20) result = 20;
    return result;
  }

public:
  OvershootStateMachine(RateMeasureNew &rmn, LatencyCollector &lc, double &burstSz, ndn::NetBLTApp &nb,
                        ndn::Scheduler &scheduler,
                        uint32_t &highRequested, uint32_t &lastProbeSegment, uint32_t &highAcked,
                        std::map<uint32_t, ndn::SegmentInfo> &window) :
          m_rmn(rmn),
          m_sc(lc),
          m_burstSz(burstSz),
          app(nb),
          m_scheduler(scheduler),
          m_highRequested(highRequested),
          m_lastProbeSegment(lastProbeSegment),
          m_highAcked(highAcked),
          m_inNetworkPkt(window) {
  }

  void run() {
    //if (finished()) return;
    m_scheduler.schedule(ndn::time::microseconds(1000), [this] { run(); });
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
    double overShootTarget = getOvershootTarget(m_rmn.getRate1());
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
      consecutiveBelowCount /= 2;
    } else if (e < 0) {
      consecutiveBelowCount++;
      consecutiveAboveCount /= 2;
    }
    const int historyOffset = (consecutiveAboveCount > consecutiveBelowCount) ?
                              consecutiveAboveCount : consecutiveBelowCount;
    i_history.push_back(e);
    i += e;

    while (i_history.size() > ilimit + historyOffset) {
      i -= i_history.front();
      i_history.pop_front();
    }

    m_burstSz += (i * 0.000 / m_sc.getVal() * 60 + e * 0.07/(m_sc.getVal() + 100/(rate)*5) *60 + d * 0.00) ;/// min_rtt * 60;histor


    std::cerr << overShoot - overShootTarget << ',' << m_burstSz << std::endl;
  }

};


#endif //EXTENSIONS_OVERSHOOTSTATEMACHINE_H
