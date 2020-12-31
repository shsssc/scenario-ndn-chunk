#include <ndn-cxx/face.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/util/scheduler.hpp>

#include <iostream>

namespace app {

class RealApp
{
public:
  RealApp(ndn::KeyChain& keyChain)
    : m_keyChain(keyChain)
    , m_faceProducer(m_faceConsumer.getIoService())
    , m_scheduler(m_faceConsumer.getIoService())
  {
   
  }

  void
  run()
  {
    m_faceConsumer.processEvents(); // ok (will not block and do nothing)
    // m_faceConsumer.getIoService().run(); // will crash
  }

private:
  void
  respondToAnyInterest(const ndn::Interest& interest)
  {
    auto data = std::make_shared<ndn::Data>(interest.getName());
    m_keyChain.sign(*data);
    m_faceProducer.put(*data);
  }

private:
  ndn::KeyChain& m_keyChain;
  ndn::Face m_faceConsumer;
  ndn::Face m_faceProducer;
  ndn::Scheduler m_scheduler;
};
}
