//
// Created by developer on 4/21/21.
//

#ifndef EXTENSIONS_RATEMEASURENEW_H
#define EXTENSIONS_RATEMEASURENEW_H

#include <list>
#include <iostream>

class RateMeasureNew {
  std::list<unsigned> history;
  const unsigned short minHistory = 50;
  unsigned short historySize = 60;

  unsigned total = 0;
public:
  RateMeasureNew() {
  }

  void reportReceived(unsigned count) {
    total += count;
    history.push_back(count);
    while (history.size() > historySize) {
      auto tmp = history.front();
      total -= tmp;
      history.pop_front();
    }
  }

  bool ready() {
    return history.size() >= historySize;
  }

  unsigned size() {
    return history.size();
  }

  double getRate(short unit = 5) {
    return (double) total / history.size() * unit;
  }

  void setHistory(unsigned short val) {
    historySize = val;
    if (val < minHistory) historySize = minHistory;
  }

  void clear() {
    history.clear();
    total = 0;
  }

  bool lessThan(double rate) {
    rate = rate / 5;
    int count = 0;
    std::cerr << '[';
    for (auto i : history) {
      std::cerr << i << ',';
      if (i < rate) count++;
    }
    std::cerr << ']';
    std::cerr << history.size() << " " << rate * 5 << " ";
    if (count > historySize / 10. * 9.) {
      return true;
    }
    return false;
  }
};


#endif //EXTENSIONS_RATEMEASURENEW_H
