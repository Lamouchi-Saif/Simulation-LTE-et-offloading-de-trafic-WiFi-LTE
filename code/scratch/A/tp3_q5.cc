
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

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Tp3Q4");

void
RecordDistance(Ptr<MobilityModel> ueMobility,
               Ptr<MobilityModel> enbMobility,
               std::ofstream* output,
               Time interval,
               Time stopTime)
{
    double currentTime = Simulator::Now().GetSeconds();

    Vector uePosition = ueMobility->GetPosition();
    Vector enbPosition = enbMobility->GetPosition();

    double distance = CalculateDistance(uePosition, enbPosition);

    *output << currentTime << "," << distance << std::endl;

    // Schedule the next measurement
    if (Simulator::Now() + interval <= stopTime)
    {
        Simulator::Schedule(
            interval,
            &RecordDistance,
            ueMobility,
            enbMobility,
            output,
            interval,
            stopTime);
    }
}

int
main(int argc, char* argv[])
{
    // Simulation configuration
    Time simTime = Seconds(300.0);
    double speed = 20.0; // New variable for UE speed in m/s (approx 72 km/h)
    double distance = 2000.0;
    Time interPacketInterval = MilliSeconds(1);

    bool useCa = false;
    bool disableDl = false;
    bool disableUl = false;

    // Command-line arguments
    CommandLine cmd(__FILE__);

    cmd.AddValue("simTime", "Total simulation duration", simTime);
    cmd.AddValue("speed", "Speed of the UE in m/s", speed);
    cmd.AddValue("distance", "Distance between UE and eNodeB in meters", distance);
    cmd.AddValue("interPacketInterval", "Interval between UDP packets", interPacketInterval);
    cmd.AddValue("useCa", "Enable carrier aggregation", useCa);
    cmd.AddValue("disableDl", "Disable downlink traffic", disableDl);
    cmd.AddValue("disableUl", "Disable uplink traffic", disableUl);

    cmd.Parse(argc, argv);

    ConfigStore inputConfig;
    inputConfig.ConfigureDefaults();

    // Parse again to allow command-line overrides
    cmd.Parse(argc, argv);

    if (useCa)
    {
        Config::SetDefault("ns3::LteHelper::UseCa", BooleanValue(true));
        Config::SetDefault("ns3::LteHelper::NumberOfComponentCarriers",
                           UintegerValue(2));
        Config::SetDefault("ns3::LteHelper::EnbComponentCarrierManager",
                           StringValue("ns3::RrComponentCarrierManager"));
    }

    // Create LTE and EPC helpers
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper =
        CreateObject<PointToPointEpcHelper>();

    lteHelper->SetEpcHelper(epcHelper);

    Ptr<Node> pgw = epcHelper->GetPgwNode();

    // Create Remote Host
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

    // Create exactly ONE eNodeB and ONE UE
    NodeContainer enbNodes;
    NodeContainer ueNodes;

    enbNodes.Create(1);
    ueNodes.Create(1);

    // Install Mobility Models

    // eNodeB is fixed at (0, 0, 0)
    MobilityHelper enbMobility;

    enbMobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");

    Ptr<ListPositionAllocator> enbPosition =
        CreateObject<ListPositionAllocator>();

    enbPosition->Add(Vector(0.0, 0.0, 0.0));

    enbMobility.SetPositionAllocator(enbPosition);
    enbMobility.Install(enbNodes);

    // UE mobility: Random walk within a 2km x 2km area around the eNodeB
    MobilityHelper ueMobility;
    
    Ptr<ListPositionAllocator> uePosition = CreateObject<ListPositionAllocator>();

    uePosition->Add(Vector(5000.0, 0.0, 0.0));

    ueMobility.SetPositionAllocator(uePosition);

    // Format the speed variable as an NS-3 ConstantRandomVariable string
    std::stringstream speedStr;
    speedStr << "ns3::ConstantRandomVariable[Constant=" << speed << "]";

    // Configure the random walk bounds and speed
    ueMobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                                "Bounds", RectangleValue(Rectangle(-10000, 10000, -10000, 10000)),
                                "Speed", StringValue(speedStr.str()),
                                "Mode", StringValue("Distance"), // Tell the model to use distance intervals
                                "Distance", DoubleValue(2000.0)); // Provide a simple DoubleValue
    
    ueMobility.Install(ueNodes);

    // Install LTE devices
    NetDeviceContainer enbLteDevs =
        lteHelper->InstallEnbDevice(enbNodes);

    NetDeviceContainer ueLteDevs =
        lteHelper->InstallUeDevice(ueNodes);

    // Install Internet stack on UE
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

    // Attach UE to eNodeB
    lteHelper->Attach(
        ueLteDevs.Get(0),
        enbLteDevs.Get(0));

    // Applications
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

    // Ping Remote Host -> UE
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

    // Enable LTE traces
    lteHelper->EnableTraces();

    // Enable PHY traces
    lteHelper->EnablePhyTraces();

    // Record UE-eNodeB distance over time
    std::ofstream distanceFile("UeDistance.txt");

    distanceFile << "Time,Distance\n";

    Ptr<MobilityModel> ueMobilityModel =
        ueNodes.Get(0)->GetObject<MobilityModel>();

    Ptr<MobilityModel> enbMobilityModel =
        enbNodes.Get(0)->GetObject<MobilityModel>();

    Time measurementInterval = Seconds(1);

    Simulator::Schedule(
        Seconds(0),
        &RecordDistance,
        ueMobilityModel,
        enbMobilityModel,
        &distanceFile,
        measurementInterval,
        simTime);

    // Install FlowMonitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // Run simulation
    std::cout << "UE - eNodeB distance: "
              << distance << " meters" << std::endl;

    Simulator::Stop(simTime);
    Simulator::Run();
    // Collect FlowMonitor statistics
monitor->CheckForLostPackets();

Ptr<Ipv4FlowClassifier> classifier =
    DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());

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
        double duration = simTime.GetSeconds() - 0.5;

        double throughput =
            it->second.rxBytes * 8.0 / duration / 1000000.0;

        double lossRatio = 0.0;

        if (it->second.txPackets > 0)
        {
            lossRatio =
                100.0 * it->second.lostPackets /
                it->second.txPackets;
        }

        std::cout << "\nFlow: "
                  << (t.destinationPort == dlPort ? "Downlink" : "Uplink")
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

    distanceFile.close();
    Simulator::Destroy();

    return 0;
}
