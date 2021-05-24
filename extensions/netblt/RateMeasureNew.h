//
// Created by developer on 4/21/21.
//

#ifndef EXTENSIONS_RATEMEASURENEW_H
#define EXTENSIONS_RATEMEASURENEW_H

#include <vector>
#include <list>
#include <iostream>
#include <random>

class RateMeasureNew {
  std::list<double> stageMismatchHistory;
  std::vector<unsigned> msHistory;
  const unsigned short minHistory = 50;
  unsigned short msHistorySize = 60;
  unsigned short stageHistorySize = 5;
  unsigned stagePacketCount = 0;
  double totalMismatch = 0;
public:
  RateMeasureNew() {
  }

  unsigned short measurementDelay() {
    return msHistorySize;
  }

  void reportDecrease() {
    stageMismatchHistory.clear();
    totalMismatch = 0;
  }

  void reportReceived(unsigned count) {
    stagePacketCount += count;
    msHistory.push_back(count);
    if (msHistory.size() > msHistorySize) {
      auto tmp = msHistory[msHistory.size() - msHistorySize - 1];
      stagePacketCount -= tmp;
      //msHistory.pop_front();
    }
  }

  bool ready() {
    const bool USE_RANDOM = false;
    std::random_device rd;
    std::uniform_int_distribution<int> dist(-3, 3);
    if (!USE_RANDOM) {
      return msHistory.size() >= msHistorySize;
    }
    return msHistory.size() >= msHistorySize
           || msHistory.size() >= msHistorySize + dist(rd);
  }

  unsigned size() {
    return msHistory.size();
  }

  double getRate(short unit = 5) const {
    return (double) stagePacketCount / msHistorySize * unit;
  }

  void setHistory(unsigned short val) {
    msHistorySize = val;
    if (val < minHistory) msHistorySize = minHistory;
  }

  bool nextStage(double last_bs) {


    if (last_bs <= 0) {
      msHistory.clear();
      stagePacketCount = 0;
      stageMismatchHistory.clear();
      return false;
    }
    unsigned tmp = 0;
    for (auto i : msHistory) {
      tmp += i;
    }
    double result = tmp - last_bs * msHistory.size() / 5.;
    totalMismatch += result;
    stageMismatchHistory.push_back(result);
    if (stageMismatchHistory.size() > stageHistorySize) {
      auto tmp = stageMismatchHistory.front();
      stageMismatchHistory.pop_front();
      totalMismatch -= tmp;
    }

    //std::cerr << "mismatch last stage" << totalMismatch << std::endl;
    msHistory.clear();
    stagePacketCount = 0;
    return false;

  }

  bool queueUsageHigh() {
    return false;
    if (totalMismatch < -5.5 && stageMismatchHistory.size() >= stageHistorySize) return true;
    return false;
  }

  bool lessThan(double rate) {
    rate = rate / 5;
    int count = 0;
    std::cerr << '[';
    for (auto i : msHistory) {
      std::cerr << i << ',';
      if (i < rate) count++;
    }
    std::cerr << ']';
    std::cerr << msHistory.size() << " " << rate * 5 << " ";
    if (count > msHistorySize / 10. * 9.) {
      return true;
    }
    return false;
  }
};


#endif //EXTENSIONS_RATEMEASURENEW_H
