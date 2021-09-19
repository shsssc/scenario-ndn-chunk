#include "LatencyCollector.h"

std::ostream &operator<<(std::ostream &os, const LatencyRecord &sample) {
  os << sample.sampleTimeNs << '/' << sample.latencyNs;
  return os;
}

bool LatencyRecord::operator<(const LatencyRecord &o) const { return latencyNs < o.latencyNs; }

bool LatencyRecord::operator>(const LatencyRecord &o) const { return latencyNs > o.latencyNs; }
