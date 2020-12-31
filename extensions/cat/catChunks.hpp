#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"
#include "ns3/ndnSIM-module.h"
#include "ns3/application.h"
#include "ns3/integer.h"
#include "ns3/string.h"
#include "consumerApp.hpp"

#include <strstream>
namespace ns3 {
namespace ndn{
namespace chunks = ::ndn::chunks;

// Class inheriting from ns3::Application
class CatChunks : public Application
{
public:
  static TypeId
  GetTypeId()
  {
    static TypeId tid = TypeId("CatChunks")
      .SetParent<Application>()
      .AddConstructor<CatChunks>()
    .AddAttribute("Prefix", "Prefix", StringValue("/"),
            MakeStringAccessor(&CatChunks::m_prefix), MakeStringChecker());

    return tid;
  }

protected:
  // inherited from Application base class.
  virtual void
  StartApplication()
  {
    // Create an instance of the app, and passing the dummy version of KeyChain (no real signing)
    m_instance.reset(new chunks::consumerApp(m_prefix));
    m_instance->run();
  }

  virtual void
  StopApplication()
  {
    // Stop and destroy the instance of the app
    m_instance.reset();
  }

private:
  std::unique_ptr<chunks::consumerApp> m_instance;
  std::string m_prefix;
};
}
} // namespace ns3
