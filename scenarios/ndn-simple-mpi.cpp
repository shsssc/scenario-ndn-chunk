/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 *
 * This file is part of ndnSIM. See AUTHORS for complete list of ndnSIM authors and
 * contributors.
 *
 * ndnSIM is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * ndnSIM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 **/

// ndn-simple.cpp

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/mpi-interface.h"
#include <fcntl.h>

#ifdef NS3_MPI
#include <mpi.h>
#else
#error "ndn-simple-mpi scenario can be compiled only if NS3_MPI is enabled"
#endif
namespace ns3 {

/**
 * This scenario simulates a very simple network topology:
 *
 *
 *      +----------+     1Mbps      +--------+     1Mbps      +----------+
 *      | consumer | <------------> | router | <------------> | producer |
 *      +----------+         10ms   +--------+          10ms  +----------+
 *
 *
 * Consumer requests data from producer with frequency 10 interests per second
 * (interests contain constantly increasing sequence number).
 *
 * For every received interest, producer replies with a data packet, containing
 * 1024 bytes of virtual payload.
 *
 * To run scenario and see what is happening, use the following command:
 *
 *     NS_LOG=ndn.Consumer:ndn.Producer ./waf --run=ndn-simple
 */

void queueSizeTrace(Ptr<Node> node) {
  PointerValue txQueue;
  node->GetDevice(0)->GetAttribute("TxQueue", txQueue);
  uint32_t k = txQueue.Get<ns3::QueueBase>()->GetNPackets();
  std::cerr << "queue size, " << k << "," << ndn::time::steady_clock::now().time_since_epoch().count() / 1000000.
            << std::endl;
  Simulator::Schedule(Seconds(0.001), &queueSizeTrace, node);

}

int
main(int argc, char *argv[]) {
  // setting default parameters for PointToPoint links and channels
  Config::SetDefault("ns3::PointToPointNetDevice::DataRate", StringValue("0.5Gbps"));
  Config::SetDefault("ns3::PointToPointChannel::Delay", StringValue("15ms"));
  Config::SetDefault("ns3::QueueBase::MaxSize", StringValue("10000p"));

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

  MpiInterface::Enable(&argc, &argv);

  uint32_t systemId = MpiInterface::GetSystemId();
  uint32_t systemCount = MpiInterface::GetSize();

  // Creating nodes
  //NodeContainer nodes;
  Ptr<Node> node1 = CreateObject<Node>(0);
  Ptr<Node> node2 = CreateObject<Node>(1);
  Ptr<Node> node3 = CreateObject<Node>(2);


  //nodes.Add(node1);
  //nodes.Add(node2);
  //nodes.Add(node3);

  // Connecting nodes using two links
  PointToPointHelper p2p;
  p2p.Install(node1, node2);
  p2p.Install(node2, node3);

  // Install NDN stack on all nodes
  ndn::StackHelper ndnHelper;
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.InstallAll();

  // Choosing forwarding strategy
  ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/multicast");

  // Installing applications

  // Consumer
  ndn::AppHelper consumerHelper("NetBLT");
  // Consumer will request /prefix/0, /prefix/1, ...
  consumerHelper.SetAttribute("Prefix", StringValue("/ping"));
  consumerHelper.SetAttribute("logfile", StringValue("consumer_single.log"));
  auto apps = consumerHelper.Install(node1);
  apps.Start(Seconds(1.0));
  apps.Stop(Seconds(120.0));




  //todo weird behavior: I have to try install on system id 2 and id 1 although install does nothing
  // Producer
  ndn::AppHelper producerHelper("PutChunks");
  // Producer will reply to all requests starting with /prefix
  producerHelper.SetAttribute("Prefix", StringValue("/ping"));
  producerHelper.SetAttribute("size", StringValue("3000000000"));
  if (systemId == 2 || systemId == 1 || systemId == 0) {

    producerHelper.Install(node3); // last node
  }


  Simulator::Stop(Seconds(120.0));
  //ndn::L3RateTracer::InstallAll("rate-trace.txt", Seconds(0.2));

  if (systemId == 2) {
    int fd = open("queueTrace_single.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    close(STDERR_FILENO);
    dup(fd);
    std::cerr << "msg, size, time\n";
    Simulator::Schedule(Seconds(0.05), &queueSizeTrace, node3);
  }

  Simulator::Run();
  MpiInterface::Disable();
  Simulator::Destroy();

  return 0;
}


} // namespace ns3

int
main(int argc, char *argv[]) {
  return ns3::main(argc, argv);
}
