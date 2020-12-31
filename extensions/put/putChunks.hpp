#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"
#include "ns3/ndnSIM-module.h"
#include "ns3/application.h"
#include "ns3/integer.h"
#include "ns3/string.h"
#include "producer.hpp"

#include <strstream>
namespace ns3 {
namespace ndn{
namespace chunks = ::ndn::chunks;

// Class inheriting from ns3::Application
class PutChunks : public Application
{
public:
  static TypeId
  GetTypeId()
  {
    static TypeId tid = TypeId("PutChunks")
      .SetParent<Application>()
      .AddConstructor<PutChunks>()
    .AddAttribute("Prefix", "Prefix", StringValue("/"),
            MakeNameAccessor(&PutChunks::m_prefix), MakeNameChecker())
      .AddAttribute("size", "Size of file to send", IntegerValue(1),
                    MakeIntegerAccessor(&PutChunks::m_sz), MakeIntegerChecker<int32_t>());

    return tid;
  }

protected:
  // inherited from Application base class.
  virtual void
  StartApplication()
  {
    std::string s(m_sz,'a');
    std::istrstream ss(s.c_str());
    // Create an instance of the app, and passing the dummy version of KeyChain (no real signing)
    m_instance.reset(new chunks::Producer(m_prefix, ndn::StackHelper::getKeyChain(),ss, opts));
    m_instance->run(); // can be omitted
  }

  virtual void
  StopApplication()
  {
    // Stop and destroy the instance of the app
    m_instance.reset();
  }

private:
  std::unique_ptr<chunks::Producer> m_instance;
  Name m_prefix;
  uint32_t m_sz;
  chunks::Producer::Options opts;
};
}
} // namespace ns3
