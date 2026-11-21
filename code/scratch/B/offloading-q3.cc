
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

#include <iostream>
#include <fstream>

using namespace ns3;

uint64_t bytesReceived = 0;
uint64_t lastBytesReceived = 0;

bool wifiActive = false;

std::ofstream throughputFile;

void ReceivePacket(Ptr<const Packet> packet, const Address &address)
{
    bytesReceived += packet->GetSize();
}

void SampleThroughput()
{
    double interval = 0.1;

    double throughput =
        (bytesReceived - lastBytesReceived) * 8.0 /
        (interval * 1000000.0);

    lastBytesReceived = bytesReceived;

    throughputFile << Simulator::Now().GetSeconds() << ","
                   << throughput << ","
                   << (wifiActive ? "WiFi" : "LTE")
                   << std::endl;

    if (Simulator::Now().GetSeconds() + interval <= 39.0)
    {
        Simulator::Schedule(
            Seconds(interval),
            &SampleThroughput);
    }
}

class OffloadingController
{
public:
    Ptr<Node> ue;
    Ptr<Node> ap;

    Ptr<Ipv4StaticRouting> ueRouting;

    Ipv4Address wifiGateway;
    Ipv4Address lteGateway;

    uint32_t wifiInterface;
    uint32_t lteInterface;

    double wifiRange;

    bool usingWifi = false;

    void Check()
    {
        double distance =
            ue->GetObject<MobilityModel>()->GetDistanceFrom(
                ap->GetObject<MobilityModel>());

        bool shouldUseWifi = (distance <= wifiRange);

        if (shouldUseWifi != usingWifi)
        {
            for (int32_t i = ueRouting->GetNRoutes() - 1; i >= 0; i--)
            {
                Ipv4RoutingTableEntry route = ueRouting->GetRoute(i);

                if (route.IsNetwork() &&
                    route.GetDestNetwork() == Ipv4Address::GetZero() &&
                    route.GetDestNetworkMask() == Ipv4Mask::GetZero())
                {
                    ueRouting->RemoveRoute(i);
                }
            }

            if (shouldUseWifi)
            {
                ueRouting->SetDefaultRoute(
                    wifiGateway,
                    wifiInterface);

                usingWifi = true;
                wifiActive = true;

                std::cout << "BASCULE LTE -> WIFI"
                          << " | Temps : "
                          << Simulator::Now().GetSeconds()
                          << " s | Distance : "
                          << distance << " m"
                          << std::endl;
            }
            else
            {
                ueRouting->SetDefaultRoute(
                    lteGateway,
                    lteInterface);

                usingWifi = false;
                wifiActive = false;

                std::cout << "BASCULE WIFI -> LTE"
                          << " | Temps : "
                          << Simulator::Now().GetSeconds()
                          << " s | Distance : "
                          << distance << " m"
                          << std::endl;
            }
        }

        Simulator::Schedule(
            Seconds(0.5),
            &OffloadingController::Check,
            this);
    }
};

int main(int argc, char *argv[])
{
    double simulationTime = 40.0;
    double wifiRange = 30.0;

    uint16_t port = 9000;

    // Creation du reseau LTE et EPC

    Ptr<LteHelper> lteHelper =
        CreateObject<LteHelper>();

    Ptr<PointToPointEpcHelper> epcHelper =
        CreateObject<PointToPointEpcHelper>();

    lteHelper->SetEpcHelper(epcHelper);

    Ptr<Node> pgw = epcHelper->GetPgwNode();

    // Creation des noeuds

    NodeContainer enbNodes;
    enbNodes.Create(1);

    NodeContainer ueNodes;
    ueNodes.Create(1);

    NodeContainer apNode;
    apNode.Create(1);

    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);

    Ptr<Node> remoteHost =
        remoteHostContainer.Get(0);

    // Installation de la pile Internet

    InternetStackHelper internet;

    internet.Install(ueNodes);
    internet.Install(apNode);
    internet.Install(remoteHostContainer);

    // Position de l'eNodeB

    MobilityHelper enbMobility;

    enbMobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");

    enbMobility.Install(enbNodes);

    enbNodes.Get(0)->GetObject<MobilityModel>()
        ->SetPosition(Vector(0.0, 50.0, 0.0));

    // Position du point d'acces Wi-Fi

    MobilityHelper apMobility;

    apMobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");

    apMobility.Install(apNode);

    apNode.Get(0)->GetObject<MobilityModel>()
        ->SetPosition(Vector(0.0, 0.0, 0.0));

    // Mobilite de l'UE

    MobilityHelper ueMobility;

    ueMobility.SetMobilityModel(
        "ns3::ConstantVelocityMobilityModel");

    ueMobility.Install(ueNodes);

    Ptr<ConstantVelocityMobilityModel> ueModel =
        ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();

    ueModel->SetPosition(Vector(-100.0, 0.0, 0.0));

    ueModel->SetVelocity(Vector(5.0, 0.0, 0.0));

    // Installation des equipements LTE

    NetDeviceContainer enbLteDevs =
        lteHelper->InstallEnbDevice(enbNodes);

    NetDeviceContainer ueLteDevs =
        lteHelper->InstallUeDevice(ueNodes);

    Ipv4InterfaceContainer ueLteIpIface =
        epcHelper->AssignUeIpv4Address(ueLteDevs);

    lteHelper->Attach(ueLteDevs);

    // Configuration du Wi-Fi 802.11n

    YansWifiChannelHelper wifiChannel =
        YansWifiChannelHelper::Default();

    YansWifiPhyHelper wifiPhy;

    wifiPhy.SetChannel(wifiChannel.Create());

    WifiHelper wifi;

    wifi.SetStandard(WIFI_STANDARD_80211n);

    WifiMacHelper wifiMac;

    Ssid ssid = Ssid("offload-ap");

    wifiMac.SetType(
        "ns3::StaWifiMac",
        "Ssid", SsidValue(ssid),
        "ActiveProbing", BooleanValue(false));

    NetDeviceContainer ueWifiDev =
        wifi.Install(wifiPhy, wifiMac, ueNodes);

    wifiMac.SetType(
        "ns3::ApWifiMac",
        "Ssid", SsidValue(ssid));

    NetDeviceContainer apWifiDev =
        wifi.Install(wifiPhy, wifiMac, apNode);

    // Adressage du reseau Wi-Fi

    NetDeviceContainer wifiDevices;

    wifiDevices.Add(ueWifiDev);
    wifiDevices.Add(apWifiDev);

    Ipv4AddressHelper ipv4Wifi;

    ipv4Wifi.SetBase(
        "10.0.1.0",
        "255.255.255.0");

    Ipv4InterfaceContainer wifiInterfaces =
        ipv4Wifi.Assign(wifiDevices);

    // Liaison entre le PGW et le serveur distant

    PointToPointHelper p2p;

    p2p.SetDeviceAttribute(
        "DataRate",
        StringValue("100Gb/s"));

    p2p.SetChannelAttribute(
        "Delay",
        StringValue("10ms"));

    NetDeviceContainer internetDevices =
        p2p.Install(pgw, remoteHost);

    Ipv4AddressHelper ipv4Internet;

    ipv4Internet.SetBase(
        "1.0.0.0",
        "255.0.0.0");

    Ipv4InterfaceContainer internetInterfaces =
        ipv4Internet.Assign(internetDevices);

    // Liaison entre le PGW et le point d'acces

    NetDeviceContainer apBackhaulDevices =
        p2p.Install(pgw, apNode.Get(0));

    Ipv4AddressHelper ipv4Backhaul;

    ipv4Backhaul.SetBase(
        "10.0.2.0",
        "255.255.255.0");

    Ipv4InterfaceContainer backhaulInterfaces =
        ipv4Backhaul.Assign(apBackhaulDevices);

    // Routage du serveur distant

    Ipv4StaticRoutingHelper routingHelper;

    Ptr<Ipv4> remoteIpv4 =
        remoteHost->GetObject<Ipv4>();

    uint32_t remoteInterface =
        remoteIpv4->GetInterfaceForDevice(
            internetDevices.Get(1));

    Ptr<Ipv4StaticRouting> remoteRouting =
        routingHelper.GetStaticRouting(remoteIpv4);

    remoteRouting->SetDefaultRoute(
        internetInterfaces.GetAddress(0),
        remoteInterface);

    // Routage du PGW vers le reseau Wi-Fi

    Ptr<Ipv4> pgwIpv4 =
        pgw->GetObject<Ipv4>();

    uint32_t pgwWifiInterface =
        pgwIpv4->GetInterfaceForDevice(
            apBackhaulDevices.Get(0));

    Ptr<Ipv4StaticRouting> pgwRouting =
        routingHelper.GetStaticRouting(pgwIpv4);

    pgwRouting->AddNetworkRouteTo(
        Ipv4Address("10.0.1.0"),
        Ipv4Mask("255.255.255.0"),
        backhaulInterfaces.GetAddress(1),
        pgwWifiInterface);

    // Routage et transfert des paquets sur le point d'acces

    Ptr<Ipv4> apIpv4 =
        apNode.Get(0)->GetObject<Ipv4>();

    uint32_t apBackhaulInterface =
        apIpv4->GetInterfaceForDevice(
            apBackhaulDevices.Get(1));

    uint32_t apWifiInterface =
        apIpv4->GetInterfaceForDevice(
            apWifiDev.Get(0));

    Ptr<Ipv4StaticRouting> apRouting =
        routingHelper.GetStaticRouting(apIpv4);

    apRouting->SetDefaultRoute(
        backhaulInterfaces.GetAddress(0),
        apBackhaulInterface);

    apIpv4->SetForwarding(apWifiInterface, true);
    apIpv4->SetForwarding(apBackhaulInterface, true);

    // Activation du transfert sur le PGW

    pgwIpv4->SetForwarding(pgwWifiInterface, true);

    // Configuration du routage initial LTE sur l'UE

    Ptr<Ipv4> ueIpv4 =
        ueNodes.Get(0)->GetObject<Ipv4>();

    uint32_t ueLteInterface =
        ueIpv4->GetInterfaceForDevice(
            ueLteDevs.Get(0));

    uint32_t ueWifiInterface =
        ueIpv4->GetInterfaceForDevice(
            ueWifiDev.Get(0));

    Ptr<Ipv4StaticRouting> ueRouting =
        routingHelper.GetStaticRouting(ueIpv4);

    ueRouting->SetDefaultRoute(
        epcHelper->GetUeDefaultGatewayAddress(),
        ueLteInterface);

    // Creation du controleur d'offloading

    OffloadingController controller;

    controller.ue = ueNodes.Get(0);
    controller.ap = apNode.Get(0);

    controller.ueRouting = ueRouting;

    controller.wifiGateway =
        wifiInterfaces.GetAddress(1);

    controller.lteGateway =
        epcHelper->GetUeDefaultGatewayAddress();

    controller.wifiInterface =
        ueWifiInterface;

    controller.lteInterface =
        ueLteInterface;

    controller.wifiRange = wifiRange;

    // Serveur UDP distant

    PacketSinkHelper sinkHelper(
        "ns3::UdpSocketFactory",
        InetSocketAddress(
            Ipv4Address::GetAny(),
            port));

    ApplicationContainer sinkApp =
        sinkHelper.Install(remoteHost);

    sinkApp.Start(Seconds(0.5));
    sinkApp.Stop(Seconds(simulationTime));

    sinkApp.Get(0)->TraceConnectWithoutContext(
        "Rx",
        MakeCallback(&ReceivePacket));

    // Generation du trafic UDP CBR

    UdpClientHelper udpClient(
        internetInterfaces.GetAddress(1),
        port);

    udpClient.SetAttribute(
        "MaxPackets",
        UintegerValue(1000000));

    udpClient.SetAttribute(
        "Interval",
        TimeValue(MilliSeconds(10)));

    udpClient.SetAttribute(
        "PacketSize",
        UintegerValue(1024));

    ApplicationContainer clientApp =
        udpClient.Install(ueNodes.Get(0));

    clientApp.Start(Seconds(1.0));
    clientApp.Stop(Seconds(simulationTime - 1.0));

    // Activation des traces LTE

    lteHelper->EnableTraces();

    // Activation des traces Wi-Fi

    wifiPhy.EnablePcap(
        "offloading-q3-ue",
        ueWifiDev.Get(0));

    wifiPhy.EnablePcap(
        "offloading-q3-ap",
        apWifiDev.Get(0));

    // Activation de FlowMonitor

    FlowMonitorHelper flowHelper;

    Ptr<FlowMonitor> monitor =
        flowHelper.InstallAll();

    // Affichage des informations

    std::cout << "===== SCENARIO OFFLOADING LTE / WIFI ====="
              << std::endl;

    std::cout << "IP LTE de l'UE : "
              << ueLteIpIface.GetAddress(0)
              << std::endl;

    std::cout << "IP Wi-Fi de l'UE : "
              << wifiInterfaces.GetAddress(0)
              << std::endl;

    std::cout << "IP du point d'acces : "
              << wifiInterfaces.GetAddress(1)
              << std::endl;

    std::cout << "IP du serveur distant : "
              << internetInterfaces.GetAddress(1)
              << std::endl;

    std::cout << "Rayon du hotspot : "
              << wifiRange << " m"
              << std::endl;

    std::cout << "Debut de la simulation..."
              << std::endl;

    // Fichier de mesure du debit

    throughputFile.open("offloading-throughput.csv");

    throughputFile
        << "Time(s),Throughput(Mbps),Interface"
        << std::endl;

    // Programmation de la bascule et des mesures

    Simulator::Schedule(
        Seconds(0.5),
        &OffloadingController::Check,
        &controller);

    Simulator::Schedule(
        Seconds(1.1),
        &SampleThroughput);

    // Execution

    Simulator::Stop(Seconds(simulationTime));

    Simulator::Run();

    throughputFile.close();

    // Statistiques FlowMonitor

    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(
            flowHelper.GetClassifier());

    FlowMonitor::FlowStatsContainer stats =
        monitor->GetFlowStats();

    std::cout << std::endl;
    std::cout << "===== STATISTIQUES FLOWMONITOR ====="
              << std::endl;

    for (auto flow = stats.begin(); flow != stats.end(); ++flow)
    {
        Ipv4FlowClassifier::FiveTuple t =
            classifier->FindFlow(flow->first);

        std::cout << std::endl;

        std::cout << "Flow ID : "
                  << flow->first << std::endl;

        std::cout << "Source : "
                  << t.sourceAddress << std::endl;

        std::cout << "Destination : "
                  << t.destinationAddress << std::endl;

        std::cout << "Paquets emis : "
                  << flow->second.txPackets << std::endl;

        std::cout << "Paquets recus : "
                  << flow->second.rxPackets << std::endl;

        std::cout << "Paquets perdus : "
                  << flow->second.lostPackets << std::endl;

        if (flow->second.rxPackets > 0)
        {
            double duration =
                (flow->second.timeLastRxPacket -
                 flow->second.timeFirstTxPacket).GetSeconds();

            if (duration > 0)
            {
                double throughput =
                    (flow->second.rxBytes * 8.0) /
                    (duration * 1000000.0);

                double meanDelay =
                    flow->second.delaySum.GetSeconds() /
                    flow->second.rxPackets;

                std::cout << "Debit recu : "
                          << throughput
                          << " Mbit/s" << std::endl;

                std::cout << "Delai moyen : "
                          << meanDelay * 1000.0
                          << " ms" << std::endl;
            }
        }
    }

    monitor->SerializeToXmlFile(
        "offloading-q3-flowmon.xml",
        true,
        true);

    Simulator::Destroy();

    std::cout << std::endl;
    std::cout << "Simulation terminee !"
              << std::endl;

    return 0;
}
