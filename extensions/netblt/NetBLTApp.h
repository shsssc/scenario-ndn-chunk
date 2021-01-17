//
// Created by developer on 1/17/21.
//

#ifndef EXTENSIONS_NETBLTAPP_H
#define EXTENSIONS_NETBLTAPP_H

#include <string>
#include "ns3/ndnSIM/ndn-cxx/face.hpp"
#include "../cat/discover-version.hpp"
#include "../cat/options.hpp"
#include <iostream>
#include <utility>
#include <queue>
#include <vector>
#include <set>
#include <map>
#include "ns3/scheduler.h"

namespace ndn {
class NetBLTApp {
public:
  explicit NetBLTApp(std::string s) : m_nextSegment(0), m_prefix(std::move(s)),
                                      m_hasLastSegment(false), m_burstSz(800),
                                      m_burstInterval_ms(25), m_SOSSz(10000),
                                      m_nextToPrint(0), m_bytesTransfered(0),
                                      m_retransmissionCount(0), m_startTime(), m_lastSegment(0) {
    m_dv = make_unique<chunks::DiscoverVersion>(m_face, m_prefix, m_opts);

  }

  void run() {
    m_startTime = time::steady_clock::now();
    m_dv->onDiscoverySuccess.connect([this](const Name &versionedName) {
        std::cerr << "Discovered version " << versionedName << std::endl;
        m_versionedName = versionedName;
        fetchLoop();
    });
    m_dv->onDiscoveryFailure.connect([](const std::string &msg) {
        std::cout << msg;
        NDN_THROW(std::runtime_error(msg));
    });
    m_dv->run();
  }


private:
  bool expressOneInterest() {
    uint32_t nextSegmentNo = 0;
    if (!retransmissionPQ.empty()) {
      nextSegmentNo = retransmissionPQ.top();
      retransmissionPQ.pop();
    } else {
      nextSegmentNo = m_nextSegment;
      //beyond last
      if (m_hasLastSegment && nextSegmentNo > m_lastSegment) return false;
      //SOS reached: either scheduled first resend or in-network first has low segment number
      if (!retransmissionPQ.empty() && nextSegmentNo - retransmissionPQ.top() > m_SOSSz ||
          !m_inNetworkPkt.empty() && nextSegmentNo - *m_inNetworkPkt.begin() > m_SOSSz)
        return false;

      m_nextSegment++;
    }
    auto interest = Interest()
       .setName(Name(m_versionedName).appendSegment(nextSegmentNo))
       .setCanBePrefix(false)
       .setMustBeFresh(true)
       .setInterestLifetime(m_opts.interestLifetime);//todo
    m_inNetworkPkt.insert(nextSegmentNo);
    m_face.expressInterest(interest,
                           bind(&NetBLTApp::handleData, this, _1, _2),
                           bind(&NetBLTApp::handleNack, this, _1, _2),
                           bind(&NetBLTApp::handleTimeout, this, _1));
    return true;
  }

  void fetchLoop() {
    if (finished()) return printStats();
    for (uint32_t i = 0; i < m_burstSz; i++) {
      if (!expressOneInterest())break;
    }
    ns3::Simulator::Schedule(ns3::MilliSeconds(m_burstInterval_ms), &NetBLTApp::fetchLoop, this);
  }

  void
  handleData(const Interest &interest, const Data &data) {
    //std::cerr << "data" << std::endl;
    if (!m_hasLastSegment && data.getFinalBlock()) {
      m_hasLastSegment = true;
      m_lastSegment = data.getFinalBlock()->toSegment();
      std::cerr << "last segment: " << m_lastSegment << std::endl;
    }
    uint32_t segment = data.getName()[-1].toSegment();
    m_inNetworkPkt.erase(segment);

    m_dataBuffer.emplace(segment, data.shared_from_this());
    outputData();
  }

  void
  handleNack(const Interest &interest, const lp::Nack &nack) {
    std::cerr << "nack" << std::endl;
  }

  void
  handleTimeout(const Interest &interest) {
    uint32_t segment = interest.getName()[-1].toSegment();
    std::cerr << "timeout" << std::endl;
    //std::cerr << interest.getName()[-1].toSegment();
    retransmissionPQ.push(segment);
    m_inNetworkPkt.erase(segment);
    m_retransmissionCount++;
  }

  void outputData() {
    for (auto it = m_dataBuffer.begin();
         it != m_dataBuffer.end() && it->first == m_nextToPrint;
         it = m_dataBuffer.erase(it), ++m_nextToPrint) {
      const Block &content = it->second->getContent();
      m_bytesTransfered += content.value_size();
      std::cerr << "acked " << m_nextToPrint << std::endl;
      //std::cerr.write(reinterpret_cast<const char*>(content.value()), content.value_size());
    }
  }

  bool finished() {
    return m_hasLastSegment && m_nextToPrint > m_lastSegment;
  }

  uint32_t getNextSegment() {
    return m_nextSegment++;
  }

  void printStats() {
    using namespace ndn::time;
    duration<double, seconds::period> timeElapsed = steady_clock::now() - m_startTime;
    double throughput = 8 * m_bytesTransfered / timeElapsed.count();

    std::cerr << "\n\nAll segments have been received.\n"
              << "Time elapsed: " << timeElapsed << "\n"
              << "Segments received: " << m_lastSegment + 1 << "\n" //todo this could be bad
              << "Transferred size: " << m_bytesTransfered / 1e3 << " kB" << "\n"
              << "Goodput: " << formatThroughput(throughput) << "\n";
  }

  std::string formatThroughput(double throughput)
  {
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

  //high level setup
  std::string m_prefix;
  Name m_versionedName;
  Face m_face;
  std::unique_ptr<chunks::DiscoverVersion> m_dv;
  chunks::Options m_opts;
  bool m_hasLastSegment;
  uint32_t m_lastSegment;

  //fetch
  uint32_t m_nextSegment;
  std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<>> retransmissionPQ;
  std::map<uint32_t, shared_ptr<const Data>> m_dataBuffer;

  //output
  uint32_t m_nextToPrint;

  //SOS
  uint32_t m_SOSSz;
  std::set<uint32_t> m_inNetworkPkt;

  //flow control
  uint32_t m_burstSz;
  uint16_t m_burstInterval_ms;

  //stats
  uint64_t m_bytesTransfered;
  uint32_t m_retransmissionCount;
  time::steady_clock::TimePoint m_startTime;
};
}


#endif //EXTENSIONS_NETBLTAPP_H
