#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/mpi-interface.h"

#ifdef NS3_MPI
#include <mpi.h>
#else
#error "ndn-simple-mpi scenario can be compiled only if NS3_MPI is enabled"
#endif

namespace ns3 {

int
main(int argc, char *argv[]) {
  // setting default parameters for PointToPoint links and channels
  Config::SetDefault("ns3::PointToPointNetDevice::DataRate", StringValue("1Gbps"));
  Config::SetDefault("ns3::PointToPointChannel::Delay", StringValue("20ms"));
  Config::SetDefault("ns3::QueueBase::MaxSize", StringValue("4000p"));

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

  if (systemCount != 3) {
    std::cout << "Simulation will run on a single processor only" << std::endl
              << "To run using MPI, run" << std::endl
              << "  mpirun -np 3 ./waf --run=ndn-simple-mpi" << std::endl;
  }
  std::cerr << systemId << systemCount << std::endl;
  // Creating nodes

  // consumer node is associated with system id 0
  Ptr<Node> node1 = CreateObject<Node>(0);

  // producer node is associated with system id 1 (or 0 when running on single CPU)
  Ptr<Node> node2 = CreateObject<Node>(systemCount == 3 ? 1 : 0);

  Ptr<Node> node3 = CreateObject<Node>(systemCount == 3 ? 2 : 0);
  Ptr<Node> node4 = CreateObject<Node>(systemCount == 3 ? 2 : 0);

  // Connecting nodes using a link
  PointToPointHelper p2p;
  p2p.Install(node1, node3);
  p2p.Install(node3, node4);
  p2p.Install(node4, node2);

  // Install NDN stack on all nodes
  ndn::StackHelper ndnHelper;
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.InstallAll();

  ndn::StrategyChoiceHelper::InstallAll("/ping", "/localhost/nfd/strategy/multicast");
  //ndn::FibHelper::AddRoute(node1, "/ping", node3, 1);
  //ndn::FibHelper::AddRoute(node3, "/ping", node2, 1);
  //ndn::FibHelper::AddRoute(node2, "/prefix/2", node1, 1);

  // Installing applications
  ndn::AppHelper consumerHelper("NetBLT");
  // Consumer will request /prefix/0, /prefix/1, ...
  consumerHelper.SetAttribute("Prefix", StringValue("/ping"));

  ndn::AppHelper producerHelper("PutChunks");
  // Producer will reply to all requests starting with /prefix
  producerHelper.SetAttribute("Prefix", StringValue("/ping"));
  producerHelper.SetAttribute("size", StringValue("2000000000"));

  // Run consumer application on the first processor only (if running on 2 CPUs)
  if (systemCount != 2 || systemId == 0) {
    consumerHelper.Install(node1);
    ndn::L3RateTracer::Install(node1, "node1.txt", Seconds(0.5));
  }

  // Run consumer application on the second processor only (if running on 2 CPUs)
  if (systemCount != 2 || systemId == 1) {
    producerHelper.Install(node2);
    ndn::L3RateTracer::Install(node2, "node2.txt", Seconds(0.5));
  }

  Simulator::Stop(Seconds(30.0));

  Simulator::Run();
  Simulator::Destroy();
  MpiInterface::Disable();
  return 0;
}

} // namespace ns3


int
main(int argc, char *argv[]) {
  return ns3::main(argc, argv);
}