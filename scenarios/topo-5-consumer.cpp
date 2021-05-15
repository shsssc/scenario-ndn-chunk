// ndn-congestion-topo-plugin.cpp

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ndnSIM-module.h"
#include "model/ndn-l3-protocol.hpp"
#include "ns3/mpi-interface.h"
#include <unistd.h>
#include <fcntl.h>

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
}\
\
auto app##n = consumerHelper##n.Install(consumer##n);\
app##n.Start(Seconds(1+1.1*n));\
\
ndn::AppHelper producerHelper##n("PutChunks");\
producerHelper##n.SetAttribute("Prefix", StringValue("/dst"#n));\
producerHelper##n.SetAttribute("size", StringValue("3000000000"));\
\
\
ndnGlobalRoutingHelper.AddOrigins("/dst"#n, producer##n);\
producerHelper##n.SetPrefix("/dst"#n);\
producerHelper##n.Install(producer##n)

namespace ns3 {

void queueSizeTrace(Ptr<Node> node) {
  PointerValue txQueue;
  node->GetDevice(0)->GetAttribute("TxQueue", txQueue);
  uint32_t k = txQueue.Get<ns3::QueueBase>()->GetNPackets();
  std::cout << "queue size, " << k << "," << ndn::time::steady_clock::now().time_since_epoch().count() / 1000000.
            << std::endl;
  Simulator::Schedule(Seconds(0.0005), &queueSizeTrace, node);
}

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
  topologyReader.SetFileName("scenarios/topo-5-consumer.txt");
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
  INSTALL_NODE(5);


  // Calculate and install FIBs
  ndn::GlobalRoutingHelper::CalculateRoutes();

  Simulator::Stop(Seconds(330.0));

  if (systemId == 11) {
    int fd = open("queueTrace.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    close(STDOUT_FILENO);
    dup(fd);
    std::cout << "msg, size, time\n";
    Simulator::Schedule(Seconds(0.02), &queueSizeTrace, Names::Find<Node>("Rtr2"));
  }

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