//
// Created by developer on 2/21/21.
//

#ifndef EXTENSIONS_LATENCYCOLLECTOR_H
#define EXTENSIONS_LATENCYCOLLECTOR_H

#include <string>
#include <ndn-cxx/util/rtt-estimator.hpp>
#include <iostream>
#include <utility>
#include <deque>
#include <list>

#include "STL_mono_wedge.h"

struct LatencyRecord {
  int64_t latencyNs;
  int64_t sampleTimeNs;

  bool operator<(const LatencyRecord &o) const;

  bool operator>(const LatencyRecord &o) const;
};

std::ostream &operator<<(std::ostream &os, const LatencyRecord &sample);


class LatencyCollector {
  const int64_t windowSizeNs = 500000000;
  std::deque<LatencyRecord> max_wedge;
  std::deque<LatencyRecord> min_wedge;
  std::deque<uint64_t> history;

public:
  int getMinRTT() {
    if (!hasData()) return -1;
    return min_wedge.front().latencyNs / 1000000;
  }

  int getMaxRTT() {
    if (!hasData()) return -1;
    return max_wedge.front().latencyNs / 1000000;

  }

  LatencyCollector() {
  }

  void report(uint64_t time) {
    int64_t now = ndn::time::steady_clock::now().time_since_epoch().count();
    LatencyRecord record = {static_cast<int64_t>(time),
                            now};
    mono_wedge::max_wedge_update(max_wedge, record);
    mono_wedge::min_wedge_update(min_wedge, record);

    while (now > windowSizeNs &&
           max_wedge.front().sampleTimeNs <= now - windowSizeNs &&
           max_wedge.size() > 1) {
      max_wedge.pop_front();
    }

    while (now > windowSizeNs &&
           min_wedge.front().sampleTimeNs <= now - windowSizeNs &&
           min_wedge.size() > 1) {
      min_wedge.pop_front();
    }

    history.push_back(time);
    if (history.size() > 3)history.pop_front();

  }


  bool hasData() {
    return !min_wedge.empty() && !max_wedge.empty() && history.size() == 3;
  }

  uint64_t minInterval() {

  }

  void clear() {

  }

  double getHighPassFilterResult() {
    if (history.size() != 3) return -1;
    return abs(history[0] * -0.5 + history[1] - history[2] * 0.5)/1000000.;
  }
};

#endif //EXTENSIONS_LATENCYCOLLECTOR_H
