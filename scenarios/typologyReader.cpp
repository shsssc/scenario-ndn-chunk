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
  topologyReader.SetFileName("scenarios/topo-6-node.txt");
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
  Ptr<Node> consumer1 = Names::Find<Node>("Src1");
  Ptr<Node> consumer2 = Names::Find<Node>("Src2");

  Ptr<Node> producer1 = Names::Find<Node>("Dst1");
  Ptr<Node> producer2 = Names::Find<Node>("Dst2");
  ndn::AppHelper consumerHelper("NetBLT");
  ndn::AppHelper consumerHelper1("NetBLT");

  // on the first consumer node install a Consumer application
  // that will express interests in /dst1 namespace
  consumerHelper.SetAttribute("Prefix", StringValue("/dst1"));
  if (systemId == 0) {
    consumerHelper.SetAttribute("logfile", StringValue("consumer0.log"));
    ndn::L3RateTracer::Install(consumer1, "consumer1.txt", Seconds(0.2));
  }
  auto app = consumerHelper.Install(consumer1);
  app.Start(Seconds(2));

  // on the second consumer node install a Consumer application
  // that will express interests in /dst2 namespace
  consumerHelper1.SetAttribute("Prefix", StringValue("/dst2"));
  if (systemId == 1) {
    consumerHelper1.SetAttribute("logfile", StringValue("consumer1.log"));
    ndn::L3RateTracer::Install(consumer2, "consumer2.txt", Seconds(0.2));
  }
  consumerHelper1.Install(consumer2);


  ndn::AppHelper producerHelper("PutChunks");
  producerHelper.SetAttribute("Prefix", StringValue("/dst1"));
  producerHelper.SetAttribute("size", StringValue("2000000000"));

  ndn::AppHelper producerHelper1("PutChunks");
  producerHelper1.SetAttribute("Prefix", StringValue("/dst2"));
  producerHelper1.SetAttribute("size", StringValue("2000000000"));

  // Register /dst1 prefix with global routing controller and
  // install producer that will satisfy Interests in /dst1 namespace
  ndnGlobalRoutingHelper.AddOrigins("/dst1", producer1);
  producerHelper.SetPrefix("/dst1");
  producerHelper.Install(producer1);

  // Register /dst2 prefix with global routing controller and
  // install producer that will satisfy Interests in /dst2 namespace
  ndnGlobalRoutingHelper.AddOrigins("/dst2", producer2);
  producerHelper1.SetPrefix("/dst2");
  producerHelper1.Install(producer2);

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