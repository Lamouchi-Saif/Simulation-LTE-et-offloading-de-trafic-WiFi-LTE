
#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Tp3Q4");

int
main(int argc, char* argv[])
{
    // Simulation configuration
    Time simTime = MilliSeconds(5000);
    double distance = 60.0;
    Time interPacketInterval = MilliSeconds(1);

    bool useCa = false;
    bool disableDl = false;
    bool disableUl = false;

    // Command-line arguments
    CommandLine cmd(__FILE__);

    cmd.AddValue("simTime", "Total simulation duration", simTime);
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

    // UE is fixed at (distance, 0, 0)
    MobilityHelper ueMobility;

    ueMobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");

    Ptr<ListPositionAllocator> uePosition =
        CreateObject<ListPositionAllocator>();

    uePosition->Add(Vector(distance, 0.0, 0.0));

    ueMobility.SetPositionAllocator(uePosition);
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

    // Run simulation
    std::cout << "UE - eNodeB distance: "
              << distance << " meters" << std::endl;

    Simulator::Stop(simTime);
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
