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
  uint32_t history = 7;
  uint32_t minHistorySize = 6;
  //std::list<uint64_t> tmpList;
  double tmpVal = -1;
  std::list<uint64_t> statsList;
  bool hasNotChecked;
  double min_rtt = -1.;
public:
  int getMinRTT() {
    return min_rtt;
  }

  LatencyCollector() {
    hasNotChecked = true;
  }

  void report(uint64_t time) {
    //tmpList.push_back(time);
    //if (tmpList.size() < averageInterval) return;
    //uint64_t sum = 0;
    //for (uint64_t i : tmpList) {
    //  sum += i;
    // }
    //sum /= tmpList.size();
    //std::cerr << "average: " << sum << std::endl;
    if (tmpVal==-1)tmpVal=time;
    else{
      tmpVal = tmpVal * 0.8 + time * 0.2;
    }//statsList.push_back(sum);
    //tmpList.clear();
    //while (statsList.size() > history) statsList.pop_front();
    //hasNotChecked = true;
  }

  void tic() {
    if (tmpVal<0)return;
    statsList.push_back(tmpVal);
    while (statsList.size() > history) statsList.pop_front();
    hasNotChecked = true;
  }

  bool shouldDecrease() {
    if (!hasNotChecked) return false;
    if (statsList.size() < minHistorySize) return false;
    //std::cerr << minInterval() << std::endl;
    //if (*statsList.rbegin() > minInterval() + threshold) {
    //  return true;
    //} else {
    //  return false;
    //}
    hasNotChecked = false;
    auto diff = *statsList.rbegin() - *statsList.begin();
    if (diff < 50000) return false;
    auto i = statsList.rbegin();
    uint32_t nextTime = *i;
    i++;
    std::cerr << '[';
    for (auto i :statsList) {
      std::cerr << i << ',';
    }
    std::cerr << ']' << std::endl;
    int tolerance = 2;
    for (int j = 0; j < minHistorySize - 1; j++) {
      if (*i > nextTime) tolerance--;
      nextTime = *i;
      i++;
    }
    //we comment this line since we are using another mechanism to prevent keep breaking
    //statsList.push_back(1 << 31);//add a large number to "break"
    return tolerance >= 0;
  }

  bool hasData() {
    return !statsList.empty();
  }

  double minInterval() {
    if (min_rtt > 0.) return min_rtt;//todo
    if (statsList.empty())return min_rtt;
    uint64_t min = statsList.front();
    for (auto i : statsList) {
      if (i < min) min = i;
    }
    min_rtt = min / 1000000.;
    return min_rtt;
  }

  void clear() {
    statsList.clear();
    //tmpList.clear();
  }

};

#endif //EXTENSIONS_LATENCYCOLLECTOR_H
