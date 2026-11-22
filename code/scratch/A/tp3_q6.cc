
#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/flow-monitor-module.h"

#include <fstream>
#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Tp3Q6");

void RecordThroughput(
    Ptr<FlowMonitor> monitor,
    Ptr<Ipv4FlowClassifier> classifier,
    uint16_t dlPort,
    uint16_t ulPort,
    std::ofstream* throughputFile,
    Time interval,
    Time stopTime,
    uint64_t* previousDlBytes,
    uint64_t* previousUlBytes)
{
    uint64_t currentDlBytes = 0;
    uint64_t currentUlBytes = 0;

    auto stats = monitor->GetFlowStats();

    // Collect cumulative received bytes for DL and UL
    for (auto it = stats.begin(); it != stats.end(); ++it)
    {
        Ipv4FlowClassifier::FiveTuple t =
            classifier->FindFlow(it->first);

        if (t.destinationPort == dlPort)
        {
            currentDlBytes += it->second.rxBytes;
        }
        else if (t.destinationPort == ulPort)
        {
            currentUlBytes += it->second.rxBytes;
        }
    }

    // Calculate bytes received during this interval
    double dlThroughput =
        (currentDlBytes - *previousDlBytes) * 8.0 /
        interval.GetSeconds() / 1000000.0;

    double ulThroughput =
        (currentUlBytes - *previousUlBytes) * 8.0 /
        interval.GetSeconds() / 1000000.0;

    // Save current counters for the next measurement
    *previousDlBytes = currentDlBytes;
    *previousUlBytes = currentUlBytes;

    // Write the measurements to CSV
    *throughputFile
        << Simulator::Now().GetSeconds() << ","
        << dlThroughput << ","
        << ulThroughput << "\n";

    // Schedule the next measurement
    if (Simulator::Now() + interval <= stopTime)
    {
        Simulator::Schedule(
            interval,
            &RecordThroughput,
            monitor,
            classifier,
            dlPort,
            ulPort,
            throughputFile,
            interval,
            stopTime,
            previousDlBytes,
            previousUlBytes);
    }
}

// Record the UE's distance from both eNodeBs every second
void
RecordDistances(Ptr<MobilityModel> ueMobility,
                Ptr<MobilityModel> enb1Mobility,
                Ptr<MobilityModel> enb2Mobility,
                std::ofstream* output,
                Time interval,
                Time stopTime)
{
    double currentTime = Simulator::Now().GetSeconds();

    double distance1 =
        CalculateDistance(ueMobility->GetPosition(),
                          enb1Mobility->GetPosition());

    double distance2 =
        CalculateDistance(ueMobility->GetPosition(),
                          enb2Mobility->GetPosition());

    *output << currentTime << ","
            << distance1 << ","
            << distance2 << std::endl;

    if (Simulator::Now() + interval <= stopTime)
    {
        Simulator::Schedule(
            interval,
            &RecordDistances,
            ueMobility,
            enb1Mobility,
            enb2Mobility,
            output,
            interval,
            stopTime);
    }
}

// Called when an eNodeB starts a handover
void
NotifyHandoverStart(uint64_t imsi,
                    uint16_t cellId,
                    uint16_t rnti,
                    uint16_t targetCellId)
{
    std::cout << "\n[HANDOVER START] Time: "
              << Simulator::Now().GetSeconds()
              << " s | IMSI: " << imsi
              << " | Source Cell: " << cellId
              << " | Target Cell: " << targetCellId
              << std::endl;
}

// Called when the UE successfully completes a handover
void
NotifyHandoverEndOk(uint64_t imsi,
                    uint16_t cellId,
                    uint16_t rnti)
{
    std::cout << "\n[HANDOVER COMPLETED] Time: "
              << Simulator::Now().GetSeconds()
              << " s | IMSI: " << imsi
              << " | New Serving Cell: " << cellId
              << std::endl;
}

int
main(int argc, char* argv[])
{
    // --------------------------------------------------
    // 1. Simulation configuration
    // --------------------------------------------------

    Time simTime = Seconds(100.0);

    double speed = 10.0;

    Time interPacketInterval = MilliSeconds(1);

    bool disableDl = false;
    bool disableUl = false;

    CommandLine cmd(__FILE__);

    cmd.AddValue("simTime",
                 "Total simulation duration",
                 simTime);

    cmd.AddValue("speed",
                 "UE speed in m/s",
                 speed);

    cmd.AddValue("interPacketInterval",
                 "Interval between UDP packets",
                 interPacketInterval);

    cmd.AddValue("disableDl",
                 "Disable downlink traffic",
                 disableDl);

    cmd.AddValue("disableUl",
                 "Disable uplink traffic",
                 disableUl);

    cmd.Parse(argc, argv);

    ConfigStore inputConfig;
    inputConfig.ConfigureDefaults();

    // Parse again to allow command-line overrides
    cmd.Parse(argc, argv);

    // --------------------------------------------------
    // 2. Create LTE and EPC helpers
    // --------------------------------------------------

    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();

    Ptr<PointToPointEpcHelper> epcHelper =
        CreateObject<PointToPointEpcHelper>();

    lteHelper->SetEpcHelper(epcHelper);

    // Configure automatic handover algorithm
    lteHelper->SetHandoverAlgorithmType(
        "ns3::A2A4RsrqHandoverAlgorithm");

    lteHelper->SetHandoverAlgorithmAttribute(
        "ServingCellThreshold",
        UintegerValue(30));

    lteHelper->SetHandoverAlgorithmAttribute(
        "NeighbourCellOffset",
        UintegerValue(1));

    Ptr<Node> pgw = epcHelper->GetPgwNode();

    // --------------------------------------------------
    // 3. Create Remote Host
    // --------------------------------------------------

    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);

    Ptr<Node> remoteHost = remoteHostContainer.Get(0);

    InternetStackHelper internet;
    internet.Install(remoteHostContainer);

    // Connect PGW to Remote Host
    PointToPointHelper p2ph;

    p2ph.SetDeviceAttribute(
        "DataRate",
        DataRateValue(DataRate("100Gb/s")));

    p2ph.SetDeviceAttribute(
        "Mtu",
        UintegerValue(1500));

    p2ph.SetChannelAttribute(
        "Delay",
        TimeValue(MilliSeconds(10)));

    NetDeviceContainer internetDevices =
        p2ph.Install(pgw, remoteHost);

    Ipv4AddressHelper ipv4h;

    ipv4h.SetBase("1.0.0.0", "255.0.0.0");

    Ipv4InterfaceContainer internetIpIfaces =
        ipv4h.Assign(internetDevices);

    Ipv4Address remoteHostAddr =
        internetIpIfaces.GetAddress(1);

    // Configure Remote Host routing
    Ipv4StaticRoutingHelper ipv4RoutingHelper;

    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(
            remoteHost->GetObject<Ipv4>());

    remoteHostStaticRouting->AddNetworkRouteTo(
        Ipv4Address("7.0.0.0"),
        Ipv4Mask("255.0.0.0"),
        1);

    // --------------------------------------------------
    // 4. Create TWO eNodeBs and ONE UE
    // --------------------------------------------------

    NodeContainer enbNodes;
    NodeContainer ueNodes;

    enbNodes.Create(2);
    ueNodes.Create(1);

    // --------------------------------------------------
    // 5. Configure eNodeB mobility
    // --------------------------------------------------

    MobilityHelper enbMobility;

    enbMobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");

    Ptr<ListPositionAllocator> enbPosition =
        CreateObject<ListPositionAllocator>();

    // First eNodeB
    enbPosition->Add(Vector(0.0, 0.0, 0.0));

    // Second eNodeB
    enbPosition->Add(Vector(500.0, 0.0, 0.0));

    enbMobility.SetPositionAllocator(enbPosition);

    enbMobility.Install(enbNodes);

    // --------------------------------------------------
    // 6. Configure UE straight-line mobility
    // --------------------------------------------------

    MobilityHelper ueMobility;

    ueMobility.SetMobilityModel(
        "ns3::ConstantVelocityMobilityModel");

    Ptr<ListPositionAllocator> uePosition =
        CreateObject<ListPositionAllocator>();

    // UE starts 100 meters before the first eNodeB
    uePosition->Add(Vector(-100.0, 0.0, 0.0));

    ueMobility.SetPositionAllocator(uePosition);

    ueMobility.Install(ueNodes);

    Ptr<ConstantVelocityMobilityModel> ueMobilityModel =
        ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();

    // UE moves towards the second eNodeB
    ueMobilityModel->SetVelocity(
        Vector(speed, 0.0, 0.0));

    // --------------------------------------------------
    // 7. Install LTE devices
    // --------------------------------------------------

    NetDeviceContainer enbLteDevs =
        lteHelper->InstallEnbDevice(enbNodes);

    NetDeviceContainer ueLteDevs =
        lteHelper->InstallUeDevice(ueNodes);

    // --------------------------------------------------
    // 8. Enable X2 interface between the eNodeBs
    // --------------------------------------------------

    lteHelper->AddX2Interface(enbNodes);

    // --------------------------------------------------
    // 9. Install Internet stack on UE
    // --------------------------------------------------

    internet.Install(ueNodes);

    Ipv4InterfaceContainer ueIpIface =
        epcHelper->AssignUeIpv4Address(
            NetDeviceContainer(ueLteDevs));

    // Configure UE default gateway
    Ptr<Ipv4StaticRouting> ueStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(
            ueNodes.Get(0)->GetObject<Ipv4>());

    ueStaticRouting->SetDefaultRoute(
        epcHelper->GetUeDefaultGatewayAddress(),
        1);

    // Initially attach UE to the FIRST eNodeB
    lteHelper->Attach(
        ueLteDevs.Get(0),
        enbLteDevs.Get(0));

    // --------------------------------------------------
    // 10. Configure handover event callbacks
    // --------------------------------------------------

    Config::ConnectWithoutContext(
        "/NodeList/*/DeviceList/*/LteEnbRrc/HandoverStart",
        MakeCallback(&NotifyHandoverStart));

    Config::ConnectWithoutContext(
        "/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
        MakeCallback(&NotifyHandoverEndOk));

    // --------------------------------------------------
    // 11. Configure applications
    // --------------------------------------------------

    uint16_t dlPort = 1100;
    uint16_t ulPort = 2001;

    ApplicationContainer clientApps;
    ApplicationContainer serverApps;

    // Downlink: Remote Host -> UE
    if (!disableDl)
    {
        PacketSinkHelper dlSink(
            "ns3::UdpSocketFactory",
            InetSocketAddress(Ipv4Address::GetAny(), dlPort));

        serverApps.Add(dlSink.Install(ueNodes.Get(0)));

        UdpClientHelper dlClient(
            ueIpIface.GetAddress(0),
            dlPort);

        dlClient.SetAttribute(
            "Interval",
            TimeValue(interPacketInterval));

        dlClient.SetAttribute(
            "MaxPackets",
            UintegerValue(1000000));

        clientApps.Add(dlClient.Install(remoteHost));
    }

    // Uplink: UE -> Remote Host
    if (!disableUl)
    {
        PacketSinkHelper ulSink(
            "ns3::UdpSocketFactory",
            InetSocketAddress(Ipv4Address::GetAny(), ulPort));

        serverApps.Add(ulSink.Install(remoteHost));

        UdpClientHelper ulClient(
            remoteHostAddr,
            ulPort);

        ulClient.SetAttribute(
            "Interval",
            TimeValue(interPacketInterval));

        ulClient.SetAttribute(
            "MaxPackets",
            UintegerValue(1000000));

        clientApps.Add(ulClient.Install(ueNodes.Get(0)));
    }

    // --------------------------------------------------
    // 12. Configure Ping
    // --------------------------------------------------

    PingHelper ping(ueIpIface.GetAddress(0));

    ping.SetAttribute(
        "Interval",
        TimeValue(Seconds(1)));

    ping.SetAttribute(
        "Size",
        UintegerValue(56));

    ping.SetAttribute(
        "VerboseMode",
        EnumValue(Ping::VerboseMode::VERBOSE));

    ApplicationContainer pingApps =
        ping.Install(remoteHost);

    pingApps.Start(Seconds(1.0));
    pingApps.Stop(Seconds(4.5));

    // Start applications
    serverApps.Start(MilliSeconds(500));
    clientApps.Start(MilliSeconds(500));

    // --------------------------------------------------
    // 13. Enable LTE and PHY traces
    // --------------------------------------------------

    lteHelper->EnableTraces();

    lteHelper->EnablePhyTraces();

    // --------------------------------------------------
    // 14. Record UE distances from both eNodeBs
    // --------------------------------------------------

    std::ofstream distanceFile("UeDistances.txt");

    distanceFile << "Time,DistanceToEnb1,DistanceToEnb2\n";

    Ptr<MobilityModel> enb1MobilityModel =
        enbNodes.Get(0)->GetObject<MobilityModel>();

    Ptr<MobilityModel> enb2MobilityModel =
        enbNodes.Get(1)->GetObject<MobilityModel>();

    Time measurementInterval = MilliSeconds(100);

    Simulator::Schedule(
        Seconds(0),
        &RecordDistances,
        ueMobilityModel,
        enb1MobilityModel,
        enb2MobilityModel,
        &distanceFile,
        measurementInterval,
        simTime);

    // --------------------------------------------------
    // 15. Install FlowMonitor
    // --------------------------------------------------

    FlowMonitorHelper flowmon;

    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // Get the classifier BEFORE running the simulation
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(
            flowmon.GetClassifier());

    // Create throughput output file
    std::ofstream throughputFile("Q6_Throughput.txt");

    throughputFile << "Time,DownlinkMbps,UplinkMbps\n";

    // Previous cumulative received bytes
    uint64_t previousDlBytes = 0;
    uint64_t previousUlBytes = 0;

    // Start measuring throughput every second
    Simulator::Schedule(
        measurementInterval,
        &RecordThroughput,
        monitor,
        classifier,
        dlPort,
        ulPort,
        &throughputFile,
        measurementInterval,
        simTime,
        &previousDlBytes,
        &previousUlBytes);

    // --------------------------------------------------
    // 16. Run simulation
    // --------------------------------------------------

    std::cout << "\n========== Q6 SIMULATION ==========\n";

    std::cout << "Number of eNodeBs: 2\n";
    std::cout << "eNodeB 1 position: (0, 0, 0)\n";
    std::cout << "eNodeB 2 position: (500, 0, 0)\n";
    std::cout << "UE starting position: (-100, 0, 0)\n";
    std::cout << "UE speed: " << speed << " m/s\n";
    std::cout << "Simulation duration: "
              << simTime.GetSeconds() << " seconds\n";

    std::cout << "==================================\n";

    Simulator::Stop(simTime);

    Simulator::Run();

    // --------------------------------------------------
    // 17. Collect FlowMonitor statistics
    // --------------------------------------------------

    monitor->CheckForLostPackets();


    auto stats = monitor->GetFlowStats();

    std::cout << "\n========== FLOW MONITOR RESULTS ==========\n";

    for (auto it = stats.begin(); it != stats.end(); ++it)
    {
        Ipv4FlowClassifier::FiveTuple t =
            classifier->FindFlow(it->first);

        // Only consider our UDP downlink and uplink flows
        if (t.destinationPort == dlPort ||
            t.destinationPort == ulPort)
        {
            double duration =
                simTime.GetSeconds() - 0.5;

            double throughput =
                it->second.rxBytes * 8.0 /
                duration / 1000000.0;

            double lossRatio = 0.0;

            if (it->second.txPackets > 0)
            {
                lossRatio =
                    100.0 * it->second.lostPackets /
                    it->second.txPackets;
            }

            std::cout << "\nFlow: "
                      << (t.destinationPort == dlPort
                              ? "Downlink"
                              : "Uplink")
                      << "\n";

            std::cout << "Transmitted packets: "
                      << it->second.txPackets << "\n";

            std::cout << "Received packets: "
                      << it->second.rxPackets << "\n";

            std::cout << "Lost packets: "
                      << it->second.lostPackets << "\n";

            std::cout << "Packet loss: "
                      << lossRatio << " %\n";

            std::cout << "Average throughput: "
                      << throughput << " Mbps\n";
        }
    }

    std::cout << "\n==========================================\n";

    // Close output file
    distanceFile.close();
    throughputFile.close();

    Simulator::Destroy();

    return 0;
}
