//
// Created by developer on 2/26/21.
//

#ifndef EXTENSIONS_RATECOLLECTOR_H
#define EXTENSIONS_RATECOLLECTOR_H

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

class RateCollector {
  int receiveInx;
  int receiveInterval;
  ndn::time::steady_clock::TimePoint m_startTime;
  bool started = false;
  double rate;
  std::list<double> rate_history;
  int maxHistory = 10;
public:
  RateCollector() {
    receiveInx = 0;
    receiveInterval = 300;
    rate = 0;
  }

  void receive(int sendingRate) {
    receiveInterval = sendingRate * 1;
    if (!started) {
      started = true;
      m_startTime = ndn::time::steady_clock::now();
      receiveInx = 0;
      return;
    }
    receiveInx++;
    if (receiveInx >= receiveInterval) {
      double alpha = 0.875;
      double tmprate = receiveInx * 1.0 / ((ndn::time::steady_clock::now() - m_startTime).count() / 1e9);
      tmprate = tmprate / 1000 * 5;
      rate_history.push_back(tmprate);
      while (rate_history.size() > maxHistory) {
        rate_history.pop_front();
      }
      rate = rate * alpha + tmprate * (1 - alpha);
      receiveInx = 0;
      started = false;
    }
  }

  double getRate() const {
    return rate;
  }

  std::list<double> getHistory() {
    return rate_history;
  }

  bool hasHistory() {
    return rate_history.size() == maxHistory;
  }

  double printHistory() {
    std::cerr << '[';
    for (auto i : rate_history) {
      std::cerr << (int) i << ",";
    }
    std::cerr << std::endl;
  }
};

#endif //EXTENSIONS_RATECOLLECTOR_H
