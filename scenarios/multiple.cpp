//
// Created by developer on 2/2/21.
//

/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

namespace ns3 {

int
main(int argc, char *argv[]) {
  /*
   *   0---------------1----------------2---------------3
   *   4---------------1
   *   2---------------5
   *
   *
   */
  // setting default parameters for PointToPoint links and channels
  Config::SetDefault("ns3::PointToPointNetDevice::DataRate", StringValue("200Mbps"));
  Config::SetDefault("ns3::PointToPointChannel::Delay", StringValue("20ms"));//still 120ms RTT
  Config::SetDefault("ns3::QueueBase::MaxSize", StringValue("2000p"));

  // Read optional command-line parameters (e.g., enable visualizer with ./waf --run=<> --visualize
  CommandLine cmd;
  cmd.Parse(argc, argv);

  // Creating nodes
  NodeContainer nodes;
  nodes.Create(6);

  // Connecting nodes using two links
  PointToPointHelper p2p;
  p2p.Install(nodes.Get(0), nodes.Get(1));
  p2p.Install(nodes.Get(1), nodes.Get(2));
  p2p.Install(nodes.Get(2), nodes.Get(3));
  p2p.Install(nodes.Get(4), nodes.Get(1));
  p2p.Install(nodes.Get(2), nodes.Get(5));

  // Install NDN stack on all nodes
  ndn::StackHelper ndnHelper;
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.InstallAll();

  // Choosing forwarding strategy
  ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/best-route");
  ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll();

  // Installing applications

  // Consumer
  ndn::AppHelper consumerHelper("NetBLT");
  consumerHelper.SetAttribute("Prefix", StringValue("/ping1"));
  auto consumer1 = consumerHelper.Install(nodes.Get(0));                        // first node
  consumer1.Start(Seconds(1.0));
  consumer1.Stop(Seconds(60.0));

  ndn::AppHelper consumerHelper2("NetBLT");
  consumerHelper2.SetAttribute("Prefix", StringValue("/ping2"));
  auto consumer2 = consumerHelper2.Install(nodes.Get(4));                        // first node
  consumer2.Start(Seconds(2.0));
  consumer2.Stop(Seconds(60.0));

  // Producer
  ndn::AppHelper producerHelper("PutChunks");
  producerHelper.SetAttribute("Prefix", StringValue("/ping1"));
  producerHelper.SetAttribute("size", StringValue("2000000000"));
  producerHelper.Install(nodes.Get(3)); // last node

  ndn::AppHelper producerHelper2("PutChunks");
  producerHelper2.SetAttribute("Prefix", StringValue("/ping2"));
  producerHelper2.SetAttribute("size", StringValue("2000000000"));
  producerHelper2.Install(nodes.Get(5)); // last node

  Simulator::Stop(Seconds(60.0));
  ndn::L3RateTracer::InstallAll("rate-trace.txt", Seconds(0.2));

  //FIB
  ndnGlobalRoutingHelper.AddOrigins("/ping1", nodes.Get(3));
  ndnGlobalRoutingHelper.AddOrigins("/ping2", nodes.Get(5));
  // Calculate and install FIBs
  ndn::GlobalRoutingHelper::CalculateRoutes();

  Simulator::Run();
  Simulator::Destroy();

  return 0;
}

} // namespace ns3

int
main(int argc, char *argv[]) {
  return ns3::main(argc, argv);
}
