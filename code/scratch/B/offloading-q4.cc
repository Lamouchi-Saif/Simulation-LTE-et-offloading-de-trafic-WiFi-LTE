#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/seq-ts-header.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <string>

using namespace ns3;

const double SIMULATION_TIME = 40.0;
const double WIFI_RANGE = 30.0;
const double WINDOW_SIZE = 0.100;       // Fenetre glissante : 100 ms
const double SAMPLE_STEP = 0.010;       // Pas : 10 ms
const double NOMINAL_THROUGHPUT = 0.8192;
const double INTERRUPTION_THRESHOLD = 0.4096;

bool wifiActive = false;
std::ofstream measurementFile;

struct PacketRecord
{
    uint32_t sequence;
    double txTime;
    double rxTime;
    uint32_t bytes;
    double delayMs;
    bool received;
};

struct WindowSample
{
    double time;
    double throughput;
    uint64_t txPackets;
    uint64_t rxPackets;
    double maxDelay;
    std::string interface;
};

std::vector<PacketRecord> packetRecords;
std::vector<WindowSample> samples;

// Associe chaque paquet recu a son paquet emis grace au numero de sequence.
void ReceivePacket(Ptr<const Packet> packet, const Address &address)
{
    Ptr<Packet> copy = packet->Copy();
    SeqTsHeader header;

    if (copy->GetSize() < header.GetSerializedSize())
    {
        return;
    }

    copy->RemoveHeader(header);

    const uint32_t sequence = header.GetSeq();
    const double receiveTime = Simulator::Now().GetSeconds();
    const double delayMs =
        (Simulator::Now() - header.GetTs()).GetMilliSeconds();

    for (auto &record : packetRecords)
    {
        if (record.sequence == sequence && !record.received)
        {
            record.received = true;
            record.rxTime = receiveTime;
            record.bytes = packet->GetSize();
            record.delayMs = delayMs;
            return;
        }
    }
}

// Calcule le debit recu sur [t - 100 ms, t], puis avance de 10 ms.
void SampleThroughput()
{
    const double currentTime = Simulator::Now().GetSeconds();
    const double windowStart = currentTime - WINDOW_SIZE;

    uint64_t receivedBytesInWindow = 0;
    uint64_t txPacketsInWindow = 0;
    uint64_t rxPacketsInWindow = 0;
    double maxDelayInWindow = 0.0;

    for (const auto &record : packetRecords)
    {
        if (record.txTime >= windowStart && record.txTime < currentTime)
        {
            txPacketsInWindow++;
        }

        if (record.received &&
            record.rxTime >= windowStart &&
            record.rxTime < currentTime)
        {
            receivedBytesInWindow += record.bytes;
            rxPacketsInWindow++;
            maxDelayInWindow =
                std::max(maxDelayInWindow, record.delayMs);
        }
    }

    const double throughput =
        receivedBytesInWindow * 8.0 / (WINDOW_SIZE * 1000000.0);

    const std::string interface = wifiActive ? "WiFi" : "LTE";

    WindowSample sample;
    sample.time = currentTime;
    sample.throughput = throughput;
    sample.txPackets = txPacketsInWindow;
    sample.rxPackets = rxPacketsInWindow;
    sample.maxDelay = maxDelayInWindow;
    sample.interface = interface;
    samples.push_back(sample);

    measurementFile
        << currentTime << ","
        << throughput << ","
        << interface << ","
        << txPacketsInWindow << ","
        << rxPacketsInWindow << ","
        << maxDelayInWindow
        << std::endl;

    if (currentTime + SAMPLE_STEP <= SIMULATION_TIME - 1.0 + 1e-9)
    {
        Simulator::Schedule(
            Seconds(SAMPLE_STEP),
            &SampleThroughput);
    }
}

// Controleur de bascule LTE / Wi-Fi selon la distance au point d'acces.
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
        const double distance =
            ue->GetObject<MobilityModel>()->GetDistanceFrom(
                ap->GetObject<MobilityModel>());

        const bool shouldUseWifi = (distance <= wifiRange);

        if (shouldUseWifi != usingWifi)
        {
            for (int32_t i = ueRouting->GetNRoutes() - 1; i >= 0; --i)
            {
                Ipv4RoutingTableEntry route = ueRouting->GetRoute(i);
                if (route.IsDefault())
                {
                    ueRouting->RemoveRoute(i);
                }
            }

            if (shouldUseWifi)
            {
                ueRouting->SetDefaultRoute(wifiGateway, wifiInterface);
                usingWifi = true;
                wifiActive = true;

                std::cout << "BASCULE LTE -> WIFI"
                          << " | Temps : " << Simulator::Now().GetSeconds()
                          << " s | Distance : " << distance << " m"
                          << std::endl;
            }
            else
            {
                ueRouting->SetDefaultRoute(lteGateway, lteInterface);
                usingWifi = false;
                wifiActive = false;

                std::cout << "BASCULE WIFI -> LTE"
                          << " | Temps : " << Simulator::Now().GetSeconds()
                          << " s | Distance : " << distance << " m"
                          << std::endl;
            }
        }

        Simulator::Schedule(
            Seconds(0.5),
            &OffloadingController::Check,
            this);
    }
};

// Client UDP personnalise : chaque paquet contient un numero de sequence
// et un horodatage SeqTsHeader pour mesurer le delai de bout en bout.
class TimestampedUdpClient : public Application
{
public:
    void Setup(Ipv4Address destination,
               uint16_t port,
               uint32_t packetSize,
               Time interval)
    {
        m_peer = InetSocketAddress(destination, port);
        m_packetSize = packetSize;
        m_interval = interval;
        m_sequence = 0;
    }

private:
    void StartApplication() override
    {
        m_socket = Socket::CreateSocket(
            GetNode(), UdpSocketFactory::GetTypeId());
        m_socket->Bind();
        m_running = true;
        SendPacket();
    }

    void StopApplication() override
    {
        m_running = false;

        if (m_sendEvent.IsPending())
        {
            Simulator::Cancel(m_sendEvent);
        }

        if (m_socket)
        {
            m_socket->Close();
        }
    }

    void SendPacket()
    {
        SeqTsHeader header;
        const uint32_t sequence = m_sequence++;
        header.SetSeq(sequence);

        const uint32_t headerSize = header.GetSerializedSize();
        if (m_packetSize < headerSize)
        {
            NS_FATAL_ERROR("PacketSize trop petit pour SeqTsHeader");
        }

        Ptr<Packet> packet =
            Create<Packet>(m_packetSize - headerSize);
        packet->AddHeader(header);

        PacketRecord record;
        record.sequence = sequence;
        record.txTime = Simulator::Now().GetSeconds();
        record.rxTime = 0.0;
        record.bytes = 0;
        record.delayMs = 0.0;
        record.received = false;
        packetRecords.push_back(record);

        m_socket->SendTo(packet, 0, m_peer);

        if (m_running)
        {
            m_sendEvent = Simulator::Schedule(
                m_interval,
                &TimestampedUdpClient::SendPacket,
                this);
        }
    }

    Ptr<Socket> m_socket;
    Address m_peer;
    EventId m_sendEvent;
    uint32_t m_packetSize = 1024;
    uint32_t m_sequence = 0;
    Time m_interval = MilliSeconds(10);
    bool m_running = false;
};

int main(int argc, char *argv[])
{
    uint16_t port = 9000;

    // Creation du reseau LTE et EPC.
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper =
        CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);
    Ptr<Node> pgw = epcHelper->GetPgwNode();

    // Creation des noeuds.
    NodeContainer enbNodes;
    enbNodes.Create(1);

    NodeContainer ueNodes;
    ueNodes.Create(1);

    NodeContainer apNode;
    apNode.Create(1);

    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);

    // Installation de la pile Internet.
    InternetStackHelper internet;
    internet.Install(ueNodes);
    internet.Install(apNode);
    internet.Install(remoteHostContainer);

    // Position de l'eNodeB.
    MobilityHelper enbMobility;
    enbMobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);
    enbNodes.Get(0)->GetObject<MobilityModel>()
        ->SetPosition(Vector(0.0, 50.0, 0.0));

    // Position du point d'acces Wi-Fi.
    MobilityHelper apMobility;
    apMobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");
    apMobility.Install(apNode);
    apNode.Get(0)->GetObject<MobilityModel>()
        ->SetPosition(Vector(0.0, 0.0, 0.0));

    // Mobilite de l'UE.
    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel(
        "ns3::ConstantVelocityMobilityModel");
    ueMobility.Install(ueNodes);

    Ptr<ConstantVelocityMobilityModel> ueModel =
        ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
    ueModel->SetPosition(Vector(-100.0, 0.0, 0.0));
    ueModel->SetVelocity(Vector(5.0, 0.0, 0.0));

    // Installation des equipements LTE.
    NetDeviceContainer enbLteDevs =
        lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueLteDevs =
        lteHelper->InstallUeDevice(ueNodes);
    Ipv4InterfaceContainer ueLteIpIface =
        epcHelper->AssignUeIpv4Address(ueLteDevs);
    lteHelper->Attach(ueLteDevs);

    // Configuration du Wi-Fi 802.11n.
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

    // Adressage du reseau Wi-Fi.
    NetDeviceContainer wifiDevices;
    wifiDevices.Add(ueWifiDev);
    wifiDevices.Add(apWifiDev);

    Ipv4AddressHelper ipv4Wifi;
    ipv4Wifi.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer wifiInterfaces =
        ipv4Wifi.Assign(wifiDevices);

    // Liaison entre le PGW et le serveur distant.
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Gb/s"));
    p2p.SetChannelAttribute("Delay", StringValue("10ms"));

    NetDeviceContainer internetDevices =
        p2p.Install(pgw, remoteHost);

    Ipv4AddressHelper ipv4Internet;
    ipv4Internet.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetInterfaces =
        ipv4Internet.Assign(internetDevices);

    // Liaison entre le PGW et le point d'acces.
    NetDeviceContainer apBackhaulDevices =
        p2p.Install(pgw, apNode.Get(0));

    Ipv4AddressHelper ipv4Backhaul;
    ipv4Backhaul.SetBase("10.0.2.0", "255.255.255.0");
    Ipv4InterfaceContainer backhaulInterfaces =
        ipv4Backhaul.Assign(apBackhaulDevices);

    // Routage du serveur distant.
    Ptr<Ipv4> remoteIpv4 = remoteHost->GetObject<Ipv4>();
    uint32_t remoteInterface =
        remoteIpv4->GetInterfaceForDevice(internetDevices.Get(1));

    Ipv4StaticRoutingHelper routingHelper;
    Ptr<Ipv4StaticRouting> remoteRouting =
        routingHelper.GetStaticRouting(remoteIpv4);
    remoteRouting->SetDefaultRoute(
        internetInterfaces.GetAddress(0), remoteInterface);

    // Routage du PGW vers le reseau Wi-Fi.
    Ptr<Ipv4> pgwIpv4 = pgw->GetObject<Ipv4>();
    uint32_t pgwWifiInterface =
        pgwIpv4->GetInterfaceForDevice(apBackhaulDevices.Get(0));

    Ptr<Ipv4StaticRouting> pgwRouting =
        routingHelper.GetStaticRouting(pgwIpv4);
    pgwRouting->AddNetworkRouteTo(
        Ipv4Address("10.0.1.0"),
        Ipv4Mask("255.255.255.0"),
        backhaulInterfaces.GetAddress(1),
        pgwWifiInterface);

    // Routage et transfert des paquets sur le point d'acces.
    Ptr<Ipv4> apIpv4 = apNode.Get(0)->GetObject<Ipv4>();
    uint32_t apBackhaulInterface =
        apIpv4->GetInterfaceForDevice(apBackhaulDevices.Get(1));
    uint32_t apWifiInterface =
        apIpv4->GetInterfaceForDevice(apWifiDev.Get(0));

    Ptr<Ipv4StaticRouting> apRouting =
        routingHelper.GetStaticRouting(apIpv4);
    apRouting->SetDefaultRoute(
        backhaulInterfaces.GetAddress(0), apBackhaulInterface);
    apIpv4->SetForwarding(apWifiInterface, true);
    apIpv4->SetForwarding(apBackhaulInterface, true);
    pgwIpv4->SetForwarding(pgwWifiInterface, true);

    // Routage initial LTE sur l'UE.
    Ptr<Ipv4> ueIpv4 = ueNodes.Get(0)->GetObject<Ipv4>();
    uint32_t ueLteInterface =
        ueIpv4->GetInterfaceForDevice(ueLteDevs.Get(0));
    uint32_t ueWifiInterface =
        ueIpv4->GetInterfaceForDevice(ueWifiDev.Get(0));

    Ptr<Ipv4StaticRouting> ueRouting =
        routingHelper.GetStaticRouting(ueIpv4);
    ueRouting->SetDefaultRoute(
        epcHelper->GetUeDefaultGatewayAddress(), ueLteInterface);

    // Creation du controleur d'offloading.
    OffloadingController controller;
    controller.ue = ueNodes.Get(0);
    controller.ap = apNode.Get(0);
    controller.ueRouting = ueRouting;
    controller.wifiGateway = wifiInterfaces.GetAddress(1);
    controller.lteGateway = epcHelper->GetUeDefaultGatewayAddress();
    controller.wifiInterface = ueWifiInterface;
    controller.lteInterface = ueLteInterface;
    controller.wifiRange = WIFI_RANGE;

    // Serveur UDP distant.
    PacketSinkHelper sinkHelper(
        "ns3::UdpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sinkHelper.Install(remoteHost);
    sinkApp.Start(Seconds(0.5));
    sinkApp.Stop(Seconds(SIMULATION_TIME));

    sinkApp.Get(0)->TraceConnectWithoutContext(
        "Rx", MakeCallback(&ReceivePacket));

    // Generation du trafic UDP CBR horodate.
    Ptr<TimestampedUdpClient> client =
        CreateObject<TimestampedUdpClient>();
    client->Setup(
        internetInterfaces.GetAddress(1),
        port,
        1024,
        MilliSeconds(10));
    ueNodes.Get(0)->AddApplication(client);
    client->SetStartTime(Seconds(1.0));
    client->SetStopTime(Seconds(SIMULATION_TIME - 1.0));

    // Activation des traces LTE et Wi-Fi.
    lteHelper->EnableTraces();
    wifiPhy.EnablePcap("offloading-q4-ue", ueWifiDev.Get(0));
    wifiPhy.EnablePcap("offloading-q4-ap", apWifiDev.Get(0));

    // Activation de FlowMonitor.
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    std::cout << "===== SCENARIO OFFLOADING LTE / WIFI Q4 ====="
              << std::endl;
    std::cout << "IP LTE de l'UE : "
              << ueLteIpIface.GetAddress(0) << std::endl;
    std::cout << "IP Wi-Fi de l'UE : "
              << wifiInterfaces.GetAddress(0) << std::endl;
    std::cout << "IP du point d'acces : "
              << wifiInterfaces.GetAddress(1) << std::endl;
    std::cout << "IP du serveur distant : "
              << internetInterfaces.GetAddress(1) << std::endl;
    std::cout << "Rayon du hotspot : " << WIFI_RANGE << " m" << std::endl;
    std::cout << "Debit nominal : "
              << NOMINAL_THROUGHPUT << " Mbit/s" << std::endl;
    std::cout << "Seuil d'interruption : "
              << INTERRUPTION_THRESHOLD << " Mbit/s" << std::endl;
    std::cout << "Fenetre glissante : " << WINDOW_SIZE * 1000.0
              << " ms | Pas : " << SAMPLE_STEP * 1000.0
              << " ms" << std::endl;

    // Fichier CSV : une ligne toutes les 10 ms, debit calcule sur 100 ms.
    measurementFile.open("offloading-q4.csv");
    measurementFile
        << "Time(s),Throughput(Mbps),Interface,"
        << "TxPacketsInTxWindow,RxPacketsInRxWindow,MaxDelay(ms)"
        << std::endl;

    Simulator::Schedule(
        Seconds(0.5), &OffloadingController::Check, &controller);

    // A 1.1 s, la premiere fenetre complete couvre [1.0 s, 1.1 s].
    Simulator::Schedule(Seconds(1.1), &SampleThroughput);

    std::cout << "Debut de la simulation..." << std::endl;
    Simulator::Stop(Seconds(SIMULATION_TIME));
    Simulator::Run();
    measurementFile.close();

    // Statistiques FlowMonitor.
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    std::cout << std::endl
              << "===== STATISTIQUES FLOWMONITOR ====="
              << std::endl;

    for (auto flow = stats.begin(); flow != stats.end(); ++flow)
    {
        Ipv4FlowClassifier::FiveTuple tuple =
            classifier->FindFlow(flow->first);

        std::cout << std::endl
                  << "Flow ID : " << flow->first << std::endl
                  << "Source : " << tuple.sourceAddress << std::endl
                  << "Destination : " << tuple.destinationAddress << std::endl
                  << "Paquets emis : " << flow->second.txPackets << std::endl
                  << "Paquets recus : " << flow->second.rxPackets << std::endl
                  << "Paquets perdus : " << flow->second.lostPackets
                  << std::endl;

        if (flow->second.rxPackets > 0)
        {
            const double duration =
                (flow->second.timeLastRxPacket -
                 flow->second.timeFirstTxPacket).GetSeconds();

            if (duration > 0.0)
            {
                const double throughput =
                    flow->second.rxBytes * 8.0 /
                    (duration * 1000000.0);
                const double meanDelay =
                    flow->second.delaySum.GetSeconds() /
                    flow->second.rxPackets;

                std::cout << "Debit recu : " << throughput
                          << " Mbit/s" << std::endl;
                std::cout << "Delai moyen : " << meanDelay * 1000.0
                          << " ms" << std::endl;
            }
        }
    }

    // Analyse des deux periodes entourant les transitions.
    // Les pertes sont calculees sur les paquets emis dans la periode,
    // puis verifiees apres la fin de la simulation : aucun paquet n'est
    // declare perdu simplement parce qu'il n'est pas encore arrive au bord
    // d'une fenetre de mesure.
    std::cout << std::endl
              << "===== ANALYSE DES INTERRUPTIONS Q4 ====="
              << std::endl;

    const double transitions[] = {14.5, 26.5};

    for (int i = 0; i < 2; ++i)
    {
        const double start = transitions[i] - 1.0;
        const double end = transitions[i] + 1.0;

        double interruptionDuration = 0.0;
        double peakDelay = 0.0;
        uint64_t txPackets = 0;
        uint64_t rxPackets = 0;
        uint64_t lostPackets = 0;

        for (const auto &sample : samples)
        {
            if (sample.time >= start && sample.time <= end &&
                sample.throughput < INTERRUPTION_THRESHOLD)
            {
                interruptionDuration += SAMPLE_STEP;
            }
        }

        for (const auto &record : packetRecords)
        {
            if (record.txTime >= start && record.txTime < end)
            {
                txPackets++;

                if (record.received)
                {
                    rxPackets++;
                    peakDelay = std::max(peakDelay, record.delayMs);
                }
                else
                {
                    lostPackets++;
                }
            }
        }

        std::cout << std::endl;
        std::cout << (i == 0 ? "BASCULE LTE -> WIFI" : "BASCULE WIFI -> LTE")
                  << std::endl;
        std::cout << "Instant de bascule : "
                  << transitions[i] << " s" << std::endl;
        std::cout << "Fenetre d'analyse : "
                  << start << " s a " << end << " s" << std::endl;
        std::cout << "Duree estimee sous le seuil : "
                  << interruptionDuration << " s" << std::endl;
        std::cout << "Paquets emis dans la periode : "
                  << txPackets << std::endl;
        std::cout << "Paquets recus parmi ces emissions : "
                  << rxPackets << std::endl;
        std::cout << "Paquets non recus parmi ces emissions : "
                  << lostPackets << std::endl;
        std::cout << "Pic de delai des paquets recus : "
                  << peakDelay << " ms" << std::endl;
    }

    monitor->SerializeToXmlFile(
        "offloading-q4-flowmon.xml", true, true);

    Simulator::Destroy();

    std::cout << std::endl
              << "Fichier de mesures : offloading-q4.csv"
              << std::endl
              << "Simulation Q4 terminee !" << std::endl;

    return 0;
}
