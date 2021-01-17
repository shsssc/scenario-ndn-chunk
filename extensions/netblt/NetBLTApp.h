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
#include "ns3/scheduler.h"

namespace ndn {
class NetBLTApp {
public:
  explicit NetBLTApp(std::string s) : m_nextSegment(0), m_prefix(std::move(s)),
                                      m_hasLastSegment(false), m_burstSz(256),
                                      m_burstInterval_ms(25) {
    m_dv = make_unique<chunks::DiscoverVersion>(m_face, m_prefix, m_opts);

  }

  void run() {
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
    if (!pq.empty()) {
      nextSegmentNo = pq.top();
      pq.pop();
    } else {
      nextSegmentNo = m_nextSegment;
      //beyond last
      if (m_hasLastSegment && nextSegmentNo > m_lastSegment) return false;
      //SOS reached
      //todo

      m_nextSegment++;
    }
    auto interest = Interest()
       .setName(Name(m_versionedName).appendSegment(nextSegmentNo))
       .setCanBePrefix(false)
       .setMustBeFresh(true)
       .setInterestLifetime(m_opts.interestLifetime);

    m_face.expressInterest(interest,
                           bind(&NetBLTApp::handleData, this, _1, _2),
                           bind(&NetBLTApp::handleNack, this, _1, _2),
                           bind(&NetBLTApp::handleTimeout, this, _1));
    return true;
  }

  void fetchLoop() {
    for (uint32_t i = 0; i < m_burstSz; i++) {
      if (!expressOneInterest())break;
    }
    ns3::Simulator::Schedule(ns3::MilliSeconds(m_burstInterval_ms), &NetBLTApp::fetchLoop, this);
  }

  void
  handleData(const Interest &interest, const Data &data) {
    //std::cerr << "data" << std::endl;
    //std::cerr << data.getName()[-1].toSegment() << std::endl;
    if (!m_hasLastSegment && data.getFinalBlock()) {
      m_hasLastSegment = true;
      m_lastSegment = data.getFinalBlock()->toSegment();
    }
    //std::cerr << data.getFinalBlock()->toSegment();
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
    pq.push(segment);
  }

  uint32_t getNextSegment() {
    return m_nextSegment++;
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
  std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<>> pq;

  //SOS
  uint32_t m_SOSSz;


  //flow control
  uint32_t m_burstSz;
  uint16_t m_burstInterval_ms;
};
}


#endif //EXTENSIONS_NETBLTAPP_H
