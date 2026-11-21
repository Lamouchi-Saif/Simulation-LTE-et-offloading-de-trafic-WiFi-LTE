
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"

#include <iostream>

using namespace ns3;

int main(int argc, char *argv[])
{
    double simulationTime = 40.0;

    // Creation du reseau LTE et EPC
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper =
        CreateObject<PointToPointEpcHelper>();

    lteHelper->SetEpcHelper(epcHelper);

    // Creation des noeuds
    NodeContainer enbNodes;
    enbNodes.Create(1);

    NodeContainer ueNodes;
    ueNodes.Create(1);

    NodeContainer apNode;
    apNode.Create(1);

    // Installation de la pile Internet
    InternetStackHelper internet;
    internet.Install(ueNodes);
    internet.Install(apNode);

    // Mobilite de la station LTE
    MobilityHelper enbMobility;
    enbMobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);

    enbNodes.Get(0)->GetObject<MobilityModel>()
        ->SetPosition(Vector(0.0, 50.0, 0.0));

    // Mobilite du point d'acces Wi-Fi
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

    // Interface Wi-Fi de l'UE
    wifiMac.SetType("ns3::StaWifiMac",
        "Ssid", SsidValue(ssid),
        "ActiveProbing", BooleanValue(false));

    NetDeviceContainer ueWifiDev =
        wifi.Install(wifiPhy, wifiMac, ueNodes);

    // Interface Wi-Fi du point d'acces
    wifiMac.SetType("ns3::ApWifiMac",
        "Ssid", SsidValue(ssid));

    NetDeviceContainer apWifiDev =
        wifi.Install(wifiPhy, wifiMac, apNode);

    // Attribution des adresses IP Wi-Fi
    NetDeviceContainer wifiDevices;
    wifiDevices.Add(ueWifiDev);
    wifiDevices.Add(apWifiDev);

    Ipv4AddressHelper ipv4Wifi;
    ipv4Wifi.SetBase("10.0.1.0", "255.255.255.0");

    Ipv4InterfaceContainer wifiInterfaces =
        ipv4Wifi.Assign(wifiDevices);

    // Affichage des informations
    std::cout << "===== SCENARIO LTE / WIFI =====" << std::endl;

    std::cout << "Adresse IP LTE de l'UE : "
              << ueLteIpIface.GetAddress(0) << std::endl;

    std::cout << "Adresse IP Wi-Fi de l'UE : "
              << wifiInterfaces.GetAddress(0) << std::endl;

    std::cout << "Adresse IP du point d'acces : "
              << wifiInterfaces.GetAddress(1) << std::endl;

    std::cout << "Position initiale de l'UE : "
              << ueModel->GetPosition() << std::endl;

    std::cout << "Vitesse de l'UE : "
              << ueModel->GetVelocity() << std::endl;

    Simulator::Stop(Seconds(simulationTime));

    Simulator::Run();
    Simulator::Destroy();

    std::cout << "Simulation terminee !" << std::endl;

    return 0;
}
