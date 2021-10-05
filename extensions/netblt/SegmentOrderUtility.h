//
// Created by developer on 9/19/21.
//

#ifndef EXTENSIONS_SEGMENTORDERUTILITY_H
#define EXTENSIONS_SEGMENTORDERUTILITY_H

#include <random>
#include <cstdint>
#include <iostream>

class SegmentOrderUtility {
public:
  uint32_t patternInx;

  SegmentOrderUtility();

  uint64_t forward(uint64_t segment) const;

  uint64_t backward(uint64_t segment) const;
};


#endif //EXTENSIONS_SEGMENTORDERUTILITY_H
