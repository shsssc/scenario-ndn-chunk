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
  //std::list<double> stageMismatchHistory;
  std::list<uint64_t> time_history;
  std::vector<unsigned> msHistory;
  const unsigned short minHistory = 50;
  unsigned short msHistorySize = 30;
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
    //stageMismatchHistory.clear();
    totalMismatch = 0;
  }

  void reportPacket(uint64_t t) {
    time_history.push_back(t);
    if (ready()) {
      time_history.pop_front();
    }
  }

  double getRate1() const {
    if (msHistory.size() < msHistorySize) return 0;
    auto r = *time_history.rbegin();
    auto l = *time_history.begin();
    auto sz = time_history.size();
    return (sz - 1) * 1e9 / (r - l) * 0.005;
    //return 0;
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
    return msHistory.size() >= msHistorySize;
  }

  unsigned size() {
    return msHistory.size();
  }

  double getRate(short unit = 5) const {
    if (msHistory.size() < msHistorySize) return 0;
    return (double) stagePacketCount / msHistorySize * unit;
  }


  bool nextStage(double last_bs) {


    // if (last_bs <= 0) {
    msHistory.clear();
    stagePacketCount = 0;
    time_history.clear();
    //stageMismatchHistory.clear();
    return false;
    //}
    /*
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
    */
  }

  /*
  void setHistory(unsigned short val) {
    msHistorySize = val;
    if (val < minHistory) msHistorySize = minHistory;
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

   */
};


#endif //EXTENSIONS_RATEMEASURENEW_H
