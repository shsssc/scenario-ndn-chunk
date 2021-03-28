// ndn-congestion-topo-plugin.cpp

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ndnSIM-module.h"
#include "model/ndn-l3-protocol.hpp"
#include "ns3/mpi-interface.h"

#ifdef NS3_MPI
#include <mpi.h>
#else
#error "ndn-simple-mpi scenario can be compiled only if NS3_MPI is enabled"
#endif

#define INSTALL_NODE(n)\
Ptr<Node> consumer##n = Names::Find<Node>("Src"#n);\
Ptr<Node> producer##n = Names::Find<Node>("Dst"#n);\
ndn::AppHelper consumerHelper##n("NetBLT");\
consumerHelper##n.SetAttribute("Prefix", StringValue("/dst"#n));\
if (systemId == (n-1)) {\
consumerHelper##n.SetAttribute("logfile", StringValue("consumer_"#n".log"));\
ndn::L3RateTracer::Install(consumer##n, "consumer" #n ".txt", Seconds(0.2));\
}\
\
auto app##n = consumerHelper##n.Install(consumer##n);\
app##n.Start(Seconds(1+n));\
\
ndn::AppHelper producerHelper##n("PutChunks");\
producerHelper##n.SetAttribute("Prefix", StringValue("/dst"#n));\
producerHelper##n.SetAttribute("size", StringValue("1000000000"));\
\
\
ndnGlobalRoutingHelper.AddOrigins("/dst"#n, producer##n);\
producerHelper##n.SetPrefix("/dst"#n);\
producerHelper##n.Install(producer##n)

namespace ns3 {

int
main(int argc, char *argv[]) {
  bool nullmsg = false;

  // Read optional command-line parameters (e.g., enable visualizer with ./waf --run=<> --visualize
  CommandLine cmd;
  cmd.AddValue("nullmsg", "Enable the use of null-message synchronization", nullmsg);
  cmd.Parse(argc, argv);

  // Distributed simulation setup; by default use granted time window algorithm.
  if (nullmsg) {
    GlobalValue::Bind("SimulatorImplementationType",
                      StringValue("ns3::NullMessageSimulatorImpl"));
  } else {
    GlobalValue::Bind("SimulatorImplementationType",
                      StringValue("ns3::DistributedSimulatorImpl"));
  }

  // Enable parallel simulator with the command line arguments
  MpiInterface::Enable(&argc, &argv);

  uint32_t systemId = MpiInterface::GetSystemId();
  uint32_t systemCount = MpiInterface::GetSize();

  AnnotatedTopologyReader topologyReader("", 25);
  topologyReader.SetFileName("scenarios/topo-4-consumer.txt");
  topologyReader.Read();

  // Install NDN stack on all nodes
  ndn::StackHelper ndnHelper;
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.InstallAll();

  // Choosing forwarding strategy
  ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/best-route");

  // Installing global routing interface on all nodes
  ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll();

  // Getting containers for the consumer/producer

  INSTALL_NODE(1);
  INSTALL_NODE(2);
  INSTALL_NODE(3);
  INSTALL_NODE(4);


  // Calculate and install FIBs
  ndn::GlobalRoutingHelper::CalculateRoutes();

  Simulator::Stop(Seconds(120.0));

  Simulator::Run();
  MpiInterface::Disable();
  std::cerr << "end" << std::endl;
  Simulator::Destroy();

  return 0;
}

} // namespace ns3

int
main(int argc, char *argv[]) {
  return ns3::main(argc, argv);
}