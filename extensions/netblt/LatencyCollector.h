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

  uint32_t averageInterval = 128;
  uint32_t history = 24;
  uint32_t increaseThreshold = 12;
  std::list<uint32_t> tmpList;
  std::list<uint32_t> statsList;
  bool hasNotChecked;
public:
  LatencyCollector() {
    hasNotChecked = true;
  }

  void report(uint32_t time) {
    tmpList.push_back(time);
    if (tmpList.size() < averageInterval) return;
    uint32_t sum = 0;
    for (uint32_t i : tmpList) {
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
    if (!hasNotChecked) return false;
    if (statsList.size() < increaseThreshold) return false;
    hasNotChecked = false;
    auto i = statsList.rbegin();
    uint32_t nextTime = *i;
    i++;
    for (int j = 0; j < increaseThreshold - 1; j++) {
      if (*i >= nextTime) return false;
      nextTime = *i;
      i++;
    }
    statsList.push_back(1 << 31);//add a large number to "break"
    return true;
  }

  bool hasData() {
    return !statsList.empty();
  }

  uint32_t minInterval() {
    uint32_t min = statsList.front();
    for (auto i : statsList) {
      if (i < min) min = i;
    }
    return min;
  }

  void clear() {

  }

};

#endif //EXTENSIONS_LATENCYCOLLECTOR_H
