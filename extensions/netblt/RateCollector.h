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
#include "cmath"

class RateCollector {
  int receiveInx;
  int receiveInterval;
  ndn::time::steady_clock::TimePoint m_startTime;
  bool started = false;
  double rate;
  double var;
  std::list<double> rate_history;
  int maxHistory = 32;
  double alpha = 0.875;

public:
  RateCollector() {
    receiveInx = 0;
    receiveInterval = 5;
    rate = 0;
    var = 0;
  }

  void receive(int sendingRate) {
    //receiveInterval = sendingRate * 1;
    if (!started) {
      started = true;
      m_startTime = ndn::time::steady_clock::now();
      receiveInx = 0;
      return;
    }
    receiveInx++;
    if (receiveInx >= receiveInterval) {
      //double alpha = 0.92;
      const double targetWeight = 0.85;
      const double intervalMs = 2.0;
      alpha = pow(targetWeight, 1.0 / (intervalMs / 5 * sendingRate / receiveInterval));
      double beta = 0.75;
      auto timeDiff = ndn::time::steady_clock::now() - m_startTime;
      double tmprate = receiveInx * 1.0 / (timeDiff.count() / 1e9);
      tmprate = tmprate / 1000 * 5;
      rate_history.push_back(tmprate);
      while (rate_history.size() > maxHistory) {
        rate_history.pop_front();
      }
      if (rate < .5) rate = tmprate;
      else {
        rate = rate * alpha + tmprate * (1 - alpha);

      }
      var = var * beta + abs(tmprate - rate) * (1 - beta);
      receiveInx = 0;
      m_startTime = ndn::time::steady_clock::now();
    }
  }

  double getRate() const {
    return rate;
  }

  double getVar() const {
    return var;
  }

  std::list<double> getHistory() {
    return rate_history;
  }

  bool hasHistory() {
    return rate_history.size() == maxHistory;
  }

  void printHistory() {
    std::cerr << '[';
    for (auto i : rate_history) {
      std::cerr << (int) i << ",";
    }
    std::cerr << std::endl;
  }
};

#endif //EXTENSIONS_RATECOLLECTOR_H
