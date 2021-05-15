#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"
#include "ns3/ndnSIM-module.h"
#include "ns3/application.h"
#include "ns3/integer.h"
#include "ns3/string.h"
#include "consumerApp.hpp"

#include <strstream>

namespace ns3 {
namespace ndn {
namespace chunks = ::ndn::chunks;

// Class inheriting from ns3::Application
class CatChunks : public Application {
public:
  static TypeId
  GetTypeId() {
    static TypeId tid = TypeId("CatChunks")
            .SetParent<Application>()
            .AddConstructor<CatChunks>()
            .AddAttribute("logfile", "logfile", StringValue(""),
                          MakeStringAccessor(&CatChunks::m_logfile), MakeStringChecker())
            .AddAttribute("Prefix", "Prefix", StringValue("/"),
                          MakeStringAccessor(&CatChunks::m_prefix), MakeStringChecker());

    return tid;
  }

protected:
  // inherited from Application base class.
  virtual void
  StartApplication() {
    if (!m_logfile.empty()) {
      int fd = open(m_logfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
      close(STDOUT_FILENO);
      dup(fd);
    }
    // Create an instance of the app, and passing the dummy version of KeyChain (no real signing)
    m_instance.reset(new chunks::consumerApp(m_prefix));
    m_instance->run();
  }

  virtual void
  StopApplication() {
    // Stop and destroy the instance of the app
    m_instance.reset();
  }

private:
  std::unique_ptr<chunks::consumerApp> m_instance;
  std::string m_prefix;
  std::string m_logfile;

};
}
} // namespace ns3
