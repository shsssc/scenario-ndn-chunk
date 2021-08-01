//
// Created by developer on 2/21/21.
//

#ifndef EXTENSIONS_LATENCYCOLLECTOR_H
#define EXTENSIONS_LATENCYCOLLECTOR_H

#include <string>
#include "ns3/ndnSIM/ndn-cxx/face.hpp"
#include <ndn-cxx/util/rtt-estimator.hpp>
#include "../cat/discover-version.hpp"
#include "../cat/options.hpp"
#include <iostream>
#include <utility>
#include <queue>
#include <list>
#include <set>
#include <map>
#include "ns3/scheduler.h"
#include "options.h"


class LatencyCollector {
  const uint32_t threshold = 4000000;//2ms
  uint32_t averageInterval = 9;
  uint32_t historySize = 100;
  uint32_t minHistorySize = 6;
  //std::list<uint64_t> tmpList;
  double tmpVal = -1;
  std::list<uint64_t> statsList;
  bool hasNotChecked;
  double min_rtt = -1.;
  int lastUpdateCounter = 0;
  std::list<double> tocList;
  const unsigned RTT_HISTORY_SIZE = 10;
public:
  int getMinRTT() {
    return min_rtt;
  }

  LatencyCollector() {
    hasNotChecked = true;
  }

  void report(uint64_t time) {

    if (tmpVal == -1)tmpVal = time;
    else {
      tmpVal = tmpVal * 0.9 + time * 0.1;
    }
  }

  double getVal(){
    return tmpVal/1000000.;
  }

  void tic() {
    lastUpdateCounter++;
    if (tmpVal < 0)return;
    statsList.push_back(tmpVal);
    while (statsList.size() > historySize) statsList.pop_front();
    hasNotChecked = true;
  }

  bool shouldProbeRTT() {
    lastUpdateCounter++;
    if (!ready())return false;
    if (tmpVal == -1) return false;
    double min = tmpVal / 1000000.;

    if (min < min_rtt - 0.01 || min_rtt < 0) {
      min_rtt = min;
      lastUpdateCounter = 0;
      std::cerr << "[delay]got a better min_rtt" << std::endl;
      return false;
    }

    if (lastUpdateCounter > 5000) {
      //expired, we update value
      min_rtt = min;
      lastUpdateCounter = 0;
      std::cerr << "[delay] timeout min_RTT" << std::endl;
      return true;
    }

    //have not expired, get a larger sample
    return false;
  }

  bool ready() const {
    return tmpVal > 0;
  }

  double minInterval() {

    if (statsList.empty()) return min_rtt;
    uint64_t min = statsList.front();
    for (auto i : statsList) {
      if (i < min) min = i;
    }
    min_rtt = min_rtt > 0 && min_rtt <= min / 1000000. ? min_rtt : min / 1000000.;
    return min_rtt;
  }

  void clear() {
    statsList.clear();
  }

};

#endif //EXTENSIONS_LATENCYCOLLECTOR_H
