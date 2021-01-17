#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"
#include "ns3/ndnSIM-module.h"
#include "ns3/application.h"
#include "ns3/integer.h"
#include "ns3/string.h"
#include "NetBLTApp.h"
#include <memory>
#include <strstream>

namespace ns3 {
namespace ndn {

// Class inheriting from ns3::Application
class NetBLT : public Application {
public:
  static TypeId
  GetTypeId() {
    static TypeId tid = TypeId("NetBLT")
       .SetParent<Application>()
       .AddConstructor<NetBLT>().
          AddAttribute("Prefix", "Prefix", StringValue("/"),
                       MakeStringAccessor(&NetBLT::m_prefix), MakeStringChecker());
    return tid;
  }

protected:
  // inherited from Application base class.
  void
  StartApplication() override {
    // Create an instance of the app, and passing the dummy version of KeyChain (no real signing)
    m_instance = std::make_unique<::ndn::NetBLTApp>(m_prefix);
    m_instance->run();
  }

  void
  StopApplication() override {
    // Stop and destroy the instance of the app
    m_instance.reset();
  }

private:
  std::unique_ptr<::ndn::NetBLTApp> m_instance;
  std::string m_prefix;
};
}
} // namespace ns3
