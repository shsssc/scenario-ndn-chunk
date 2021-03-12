//
// Created by developer on 1/17/21.
//

#include "NetBLTApp.h"

std::string ndn::NetBLTApp::formatThroughput(double throughput) {
  int pow = 0;
  while (throughput >= 1000.0 && pow < 4) {
    throughput /= 1000.0;
    pow++;
  }
  switch (pow) {
    case 0:
      return to_string(throughput) + " bit/s";
    case 1:
      return to_string(throughput) + " kbit/s";
    case 2:
      return to_string(throughput) + " Mbit/s";
    case 3:
      return to_string(throughput) + " Gbit/s";
    case 4:
      return to_string(throughput) + " Tbit/s";
  }
  return "";
}

void ndn::NetBLTApp::printStats() {
  using namespace ndn::time;
  duration<double, seconds::period> timeElapsed = steady_clock::now() - m_startTime;
  double throughput = 8 * m_bytesTransfered / timeElapsed.count();

  std::cout << "\n\nAll segments have been received.\n"
            << "Time elapsed: " << timeElapsed << "\n"
            << "Segments received: " << m_lastSegment + 1 << "\n" //todo this could be bad
            << "Transferred size: " << m_bytesTransfered / 1e3 << " kB" << "\n"
            << "Goodput: " << formatThroughput(throughput) << "\n"
            << "Retransmission: " << m_retransmissionCount << "\n" << std::endl;
}

bool ndn::NetBLTApp::finished() const {
  return m_hasLastSegment && m_nextToPrint > m_lastSegment;
}

void ndn::NetBLTApp::outputData() {
  for (auto it = m_dataBuffer.begin();
       it != m_dataBuffer.end() && it->first == m_nextToPrint;
       it = m_dataBuffer.erase(it), ++m_nextToPrint) {
    const Block &content = it->second->getContent();
    m_bytesTransfered += content.value_size();
    //std::cerr << "acked " << m_nextToPrint << std::endl;
    //std::cerr.write(reinterpret_cast<const char*>(content.value()), content.value_size());
  }
}
