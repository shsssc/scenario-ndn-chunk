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
  uint32_t averageInterval = 20;
  uint32_t history = 25;
  uint32_t minHistorySize = 22;
  std::list<uint64_t> tmpList;
  std::list<uint64_t> statsList;
  bool hasNotChecked;
  int min_rtt = -1;
public:
  int getMinRTT() {
    return min_rtt;
  }

  LatencyCollector() {
    hasNotChecked = true;
  }

  void report(uint64_t time) {
    tmpList.push_back(time);
    if (tmpList.size() < averageInterval) return;
    uint64_t sum = 0;
    for (uint64_t i : tmpList) {
      sum += i;
    }
    sum /= tmpList.size();
    //std::cerr << "average: " << sum << std::endl;
    statsList.push_back(sum);
    tmpList.clear();
    while (statsList.size() > history) statsList.pop_front();
    hasNotChecked = true;
  }

  bool shouldDecrease() {
    return false;
    if (!hasNotChecked) return false;
    if (statsList.size() < minHistorySize) return false;
    //std::cerr << minInterval() << std::endl;
    if (*statsList.rbegin() > minInterval() + threshold) {
      return true;
    } else {
      return false;
    }
    hasNotChecked = false;
    auto i = statsList.rbegin();
    uint32_t nextTime = *i;
    i++;
    for (int j = 0; j < minHistorySize - 1; j++) {
      if (*i > nextTime) return false;
      nextTime = *i;
      i++;
    }
    statsList.push_back(1 << 31);//add a large number to "break"
    return true;
  }

  bool hasData() {
    return !statsList.empty();
  }

  uint64_t minInterval() {
    uint64_t min = statsList.front();
    for (auto i : statsList) {
      if (i < min) min = i;
    }
    min_rtt = min / 1000000;
    return min;
  }

  void clear() {
    statsList.clear();
    tmpList.clear();
  }

};

#endif //EXTENSIONS_LATENCYCOLLECTOR_H
