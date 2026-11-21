/*
 * tp3-offload.cc -- TP3 Partie B : offloading de trafic WiFi/LTE (NS-3.48)
 * =========================================================================
 * Fichier UNIQUE parametrable, couvre toute la Partie B du sujet :
 *
 *  Q1-Q2  Scenario : 1 eNodeB (macro), 1 AP WiFi 802.11n (hotspot), 1 UE a double
 *         interface (LTE + WiFi) en deplacement, flux UDP CBR montant UE -> serveur.
 *         Traces : CSV par 100 ms, FlowMonitor, pcap (option).
 *  Q3     --criterion=distance --hyst=0         (seuil unique)
 *  Q4     bins de 100 ms (rx/tx par interface, delai max) -> interruption calculee en Python
 *  Q5     --criterion=distance --hyst=H          (seuils thrIn = R-H, thrOut = R+H)
 *  Q6     balayage --apRadius / --speed / --run  (taux d'offloading)
 *  Q7     --criterion=rssi                       (RSSI via MonitorSnifferRx, lisse EWMA)
 *  Q8     --criterion=network                    (politique PGW : UDP>seuil -> WiFi, TCP -> LTE)
 *  Q9-Q10 comparaison faite en Python a partir des CSV produits ici
 *
 * Topologie (flux MONTANT, la route statique de l'UE choisit l'interface) :
 *
 *   UE --LTE--> eNB --S1--> SGW/PGW --p2p 1.0.0.0/8--> RemoteHost
 *   UE --WiFi-> AP  ---------------------p2p 10.0.2.0/24--> RemoteHost
 *
 * Choix de modelisation importants :
 *  - Le sujet utilise SetDefaultRoute(ueWifiIpIface.GetAddress(0), ...) : la passerelle y est
 *    l'adresse de l'UE lui-meme. Ici la passerelle WiFi est l'adresse de l'AP (10.0.1.1).
 *  - SetDefaultRoute() AJOUTE une route a chaque appel (pas de remplacement) : on utilise donc
 *    une route "host" vers le serveur que l'on supprime puis recree a chaque bascule.
 *  - Puissance d'emission WiFi calculee pour que RSSI(d = R) = edgeRssi (couverture coherente
 *    avec le rayon R du hotspot). Les seuils en dBm se deduisent des seuils en metres.
 *  - Aleas pilotes par seed/run : decalage lateral b ~ U(0, offsetFrac*R) et shadowing
 *    log-normal (--shadowSigma) i.i.d. par trame (pas de correlation spatiale).
 *
 * Exemple :
 *   ./ns3 run "tp3-offload --criterion=distance --apRadius=50 --hyst=5 --speed=5 --run=3 \
 *              --outDir=results/tp3/test --tag=essai"
 */
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/seq-ts-header.h"
#include "ns3/wifi-module.h"

#if __has_include("ns3/netanim-module.h")
#include "ns3/netanim-module.h"
#define TP3_HAVE_NETANIM 1
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("Tp3Offload");

// ===========================================================================
// 1. Parametres (modifiables en ligne de commande)
// ===========================================================================
struct Cfg
{
    // --- critere de bascule ---
    std::string criterion = "distance"; // distance | rssi | network
    std::string pattern = "line";       // line (traversee) | border (aller-retour a la limite)
    double apRadius = 50.0;             // rayon nominal du hotspot R (m)
    double hyst = 0.0;                  // demi-largeur d'hysteresis H (m) : thrIn=R-H, thrOut=R+H
    double thrIn = -1.0;                // seuil d'entree (m)  (<0 : deduit de R et H)
    double thrOut = -1.0;               // seuil de sortie (m) (<0 : deduit de R et H)
    double rssiIn = 1000.0;             // seuil d'entree dBm  (>900 : deduit de thrIn)
    double rssiOut = 1000.0;            // seuil de sortie dBm (>900 : deduit de thrOut)
    double checkPeriod = 0.5;           // periode de verification du critere (s)
    double ewma = 0.3;                  // coefficient de lissage du RSSI
    double rssiTimeout = 1.0;           // RSSI considere perdu apres ce delai sans trame (s)
    double pingPongWin = 15.0;          // deux bascules separees de moins de X s = ping-pong
    double distNoise = 0.0;             // erreur de mesure de position (sigma, m) sur le critere distance
    bool requireAssoc = true;           // n'utiliser le WiFi que si l'UE est associe a l'AP
    // --- mobilite ---
    double speed = 5.0;       // vitesse de l'UE (m/s)
    double offset = 0.0;      // decalage lateral fixe b (m)
    double offsetFrac = 0.0;  // si >0 : b ~ U(0, offsetFrac*R) (aleatoire, pilote par la graine)
    double simTime = 80.0;    // duree totale (s)
    double borderAmp = 10.0;  // pattern=border : amplitude A de l'oscillation (m)
    // --- radio WiFi ---
    double edgeRssi = -75.0;   // RSSI vise a la distance R (dBm)
    double apTxPower = -100.0; // <= -99 : auto (calcule depuis edgeRssi et R)
    double shadowSigma = 0.0;  // ecart-type du shadowing (dB), 0 = canal deterministe
    // --- trafic ---
    double rate = 2.0;     // debit CBR UDP (Mb/s)
    uint32_t pktSize = 1200;
    double netThr = 1.0;   // seuil de debit de la politique reseau (Mb/s)
    // --- execution / sorties ---
    uint32_t seed = 1;
    uint32_t run = 1;
    std::string outDir = "results/tp3/test";
    std::string tag = "run";
    bool saveBins = true;   // ecrire le CSV detaille par 100 ms
    bool flowmon = true;
    bool pcap = false;
    bool lteTraces = false;
    bool anim = false;
    // --- geometrie ---
    double apX = 150.0;     // position de l'AP sur l'axe x (eNodeB en x = 0)
} c;

static const double kRefLoss = 46.6777; // perte a 1 m (LogDistance par defaut NS-3)
static const double kExp = 3.0;         // exposant d'affaiblissement

// ===========================================================================
// 2. Etat global (necessaire pour les callbacks)
// ===========================================================================
struct Bin // un intervalle de 100 ms
{
    double txW = 0, txL = 0, rxW = 0, rxL = 0; // octets emis / recus par interface
    double dlySum = 0, dlyMax = 0;
    uint32_t nRx = 0;
    double dist = 0, rssi = -200;
    int wifi = 0;
};

struct SwitchEv
{
    double t;
    std::string dir;
    double dist;
    double rssi;
};

struct State
{
    Ptr<Node> ue, ap, remote;
    Ptr<Ipv4StaticRouting> ueRouting;
    Ipv4Address dst, gwWifi, gwLte, ueWifiAddr;
    uint32_t ifWifi = 0, ifLte = 0;
    bool onWifi = false;
    double txp = 20.0, thrIn = 0, thrOut = 0, rssiIn = 0, rssiOut = 0;
    double ewma = -200.0, lastRssiT = -1.0, lastSwitchT = -1.0;
    uint32_t nIn = 0, nOut = 0, nPP = 0, seq = 0;
    double txBytesW = 0, txBytesL = 0, rxBytesW = 0, rxBytesL = 0;
    uint32_t txPkts = 0, rxPkts = 0;
    uint32_t sendErrors = 0; // Send() retourne une erreur locale
    double dlySum = 0, dlyMax = 0;
    std::vector<Bin> bins;
    std::vector<SwitchEv> sw;
    Ptr<Socket> sock;
    Ptr<StaWifiMac> staMac;               // pour tester l'association WiFi
    Ptr<NormalRandomVariable> noiseRv;    // bruit de mesure de position
    double interval = 0.01, tStart = 1.0, tStop = 79.0;
} S;

// ===========================================================================
// 3. Fonctions utilitaires
// ===========================================================================
static size_t
BinIdx(double t)
{
    size_t i = static_cast<size_t>(std::max(0.0, t) * 10.0);
    return std::min(i, S.bins.size() - 1);
}

static double
Dist2D()
{
    Vector a = S.ue->GetObject<MobilityModel>()->GetPosition();
    Vector p = S.ap->GetObject<MobilityModel>()->GetPosition();
    return std::hypot(a.x - p.x, a.y - p.y);
}

static double
RssiAt(double d, double txp) // RSSI theorique (dBm) a la distance d (canal sans shadowing)
{
    return txp - kRefLoss - 10.0 * kExp * std::log10(std::max(d, 1.0));
}

static double
CurRssi() // RSSI lisse ; -200 dBm si aucune trame recue recemment (hors couverture)
{
    double now = Simulator::Now().GetSeconds();
    if (S.lastRssiT < 0 || now - S.lastRssiT > c.rssiTimeout)
    {
        return -200.0;
    }
    return S.ewma;
}

// Supprime puis recree la route "host" vers dst, via WiFi ou via LTE.
static void
SetHostRoute(Ipv4Address dst, bool wifi)
{
    for (uint32_t i = 0; i < S.ueRouting->GetNRoutes();)
    {
        Ipv4RoutingTableEntry r = S.ueRouting->GetRoute(i);
        if (r.IsHost() && r.GetDest() == dst)
        {
            S.ueRouting->RemoveRoute(i);
        }
        else
        {
            ++i;
        }
    }
    if (wifi)
    {
        S.ueRouting->AddHostRouteTo(dst, S.gwWifi, S.ifWifi);
    }
    else
    {
        S.ueRouting->AddHostRouteTo(dst, S.gwLte, S.ifLte);
    }
}

// Applique le choix d'interface du flux principal et journalise la bascule.
static void
ApplyPath(bool wifi, bool count)
{
    SetHostRoute(S.dst, wifi);
    if (count)
    {
        double now = Simulator::Now().GetSeconds();
        if (wifi)
        {
            S.nIn++;
        }
        else
        {
            S.nOut++;
        }
        if (S.lastSwitchT >= 0 && now - S.lastSwitchT < c.pingPongWin)
        {
            S.nPP++;
        }
        S.lastSwitchT = now;
        S.sw.push_back({now, wifi ? "LTE->WIFI" : "WIFI->LTE", Dist2D(), CurRssi()});
    }
    S.onWifi = wifi;
}

// Politique "reseau" simplifiee (role du PGW/ANDSF) : UDP au-dessus du seuil -> WiFi.
static bool
PolicyOffloadToWifi(bool isUdp, double rateMbps, double thrMbps)
{
    return isUdp && rateMbps > thrMbps;
}

// Critere orientee terminal, evalue periodiquement (Q3, Q5, Q7).
static void
CheckOffload()
{
    bool want = S.onWifi;
    if (c.criterion == "distance")
    {
        double d = Dist2D();
        if (c.distNoise > 0.0)
        {
            d += S.noiseRv->GetValue(); // distance MESUREE = vraie distance + erreur gaussienne
        }
        if (!S.onWifi && d < S.thrIn)
        {
            want = true;
        }
        else if (S.onWifi && d > S.thrOut)
        {
            want = false;
        }
    }
    else // rssi
    {
        double r = CurRssi();
        if (!S.onWifi && r > S.rssiIn)
        {
            want = true;
        }
        else if (S.onWifi && r < S.rssiOut)
        {
            want = false;
        }
    }
    // Un terminal reel ne bascule que sur un WiFi auquel il est associe ; sans association,
    // les paquets sont perdus et l'entree ARP de la passerelle peut etre marquee "morte".
    if (c.requireAssoc && !(S.staMac && S.staMac->IsAssociated()))
    {
        want = false;
    }
    if (want != S.onWifi)
    {
        ApplyPath(want, true);
    }
    Simulator::Schedule(Seconds(c.checkPeriod), &CheckOffload);
}

// Rebond de l'UE entre xlo et xhi (pattern=border : aller-retour autour de la limite).
static void
Bounce(Ptr<ConstantVelocityMobilityModel> m, double xlo, double xhi)
{
    Vector p = m->GetPosition();
    Vector v = m->GetVelocity();
    if (p.x >= xhi && v.x > 0)
    {
        m->SetVelocity(Vector(-c.speed, 0, 0));
    }
    else if (p.x <= xlo && v.x < 0)
    {
        m->SetVelocity(Vector(c.speed, 0, 0));
    }
    Simulator::Schedule(Seconds(0.05), &Bounce, m, xlo, xhi);
}

// Trace source PHY WiFi de l'UE : RSSI de chaque trame recue (beacons, ACK, ...).
// Signature NS-3.48 a verifier : grep -n "m_phyMonitorSniffRxTrace" src/wifi/model/wifi-phy.h
static void
SnifferRx(Ptr<const Packet> p,
          uint16_t channelFreqMhz,
          WifiTxVector txVector,
          MpduInfo aMpdu,
          SignalNoiseDbm signalNoise,
          uint16_t staId)
{
    double r = signalNoise.signal; // dBm
    S.ewma = (S.lastRssiT < 0) ? r : c.ewma * r + (1.0 - c.ewma) * S.ewma;
    S.lastRssiT = Simulator::Now().GetSeconds();
}

// Emission CBR : un paquet = en-tete SeqTs (12 o) + remplissage = pktSize octets au total.
static void
SendPacket()
{
    double now = Simulator::Now().GetSeconds();
    if (now >= S.tStop)
    {
        return;
    }
    SeqTsHeader h;
    h.SetSeq(S.seq++);
    Ptr<Packet> p = Create<Packet>(c.pktSize - h.GetSerializedSize());
    p->AddHeader(h);
    Bin& b = S.bins[BinIdx(now)];
    // Ces compteurs representent les octets presentes au socket pour emission
    // sur le chemin choisi. Le succes de Send() ne garantit pas la reception radio.
    if (S.onWifi)
    {
        S.txBytesW += c.pktSize;
        b.txW += c.pktSize;
    }
    else
    {
        S.txBytesL += c.pktSize;
        b.txL += c.pktSize;
    }
    S.txPkts++;
    const int sent = S.sock->Send(p);
    if (sent < 0)
    {
        ++S.sendErrors;
    }
    Simulator::Schedule(Seconds(S.interval), &SendPacket);
}

// Reception sur le serveur distant : interface identifiee par l'adresse IP source de l'UE.
static void
RxUdp(Ptr<Socket> s)
{
    Ptr<Packet> p;
    Address from;
    while ((p = s->RecvFrom(from)))
    {
        SeqTsHeader h;
        p->RemoveHeader(h);
        double now = Simulator::Now().GetSeconds();
        double dly = (Simulator::Now() - h.GetTs()).GetSeconds();
        double bytes = p->GetSize() + h.GetSerializedSize();
        bool viaWifi = (InetSocketAddress::ConvertFrom(from).GetIpv4() == S.ueWifiAddr);
        Bin& b = S.bins[BinIdx(now)];
        if (viaWifi)
        {
            S.rxBytesW += bytes;
            b.rxW += bytes;
        }
        else
        {
            S.rxBytesL += bytes;
            b.rxL += bytes;
        }
        b.nRx++;
        b.dlySum += dly;
        b.dlyMax = std::max(b.dlyMax, dly);
        S.rxPkts++;
        S.dlySum += dly;
        S.dlyMax = std::max(S.dlyMax, dly);
    }
}

// Echantillonnage (distance, RSSI, interface active) toutes les 100 ms.
static void
Sample()
{
    Bin& b = S.bins[BinIdx(Simulator::Now().GetSeconds())];
    b.dist = Dist2D();
    b.rssi = CurRssi();
    b.wifi = S.onWifi ? 1 : 0;
    Simulator::Schedule(Seconds(0.1), &Sample);
}

// ===========================================================================
// 4. Programme principal
// ===========================================================================
int
main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.AddValue("criterion", "distance | rssi | network", c.criterion);
    cmd.AddValue("pattern", "line | border", c.pattern);
    cmd.AddValue("apRadius", "Rayon nominal du hotspot R (m)", c.apRadius);
    cmd.AddValue("hyst", "Demi-hysteresis H (m) : thrIn=R-H, thrOut=R+H", c.hyst);
    cmd.AddValue("thrIn", "Seuil d'entree (m), <0 = auto", c.thrIn);
    cmd.AddValue("thrOut", "Seuil de sortie (m), <0 = auto", c.thrOut);
    cmd.AddValue("rssiIn", "Seuil d'entree (dBm), >900 = auto", c.rssiIn);
    cmd.AddValue("rssiOut", "Seuil de sortie (dBm), >900 = auto", c.rssiOut);
    cmd.AddValue("checkPeriod", "Periode de test du critere (s)", c.checkPeriod);
    cmd.AddValue("ewma", "Coefficient EWMA du RSSI", c.ewma);
    cmd.AddValue("rssiTimeout", "Delai sans trame => RSSI perdu (s)", c.rssiTimeout);
    cmd.AddValue("pingPongWin", "Fenetre de detection du ping-pong (s)", c.pingPongWin);
    cmd.AddValue("speed", "Vitesse de l'UE (m/s)", c.speed);
    cmd.AddValue("offset", "Decalage lateral fixe b (m)", c.offset);
    cmd.AddValue("offsetFrac", "b ~ U(0, offsetFrac*R), 0 = desactive", c.offsetFrac);
    cmd.AddValue("simTime", "Duree de simulation (s)", c.simTime);
    cmd.AddValue("borderAmp", "pattern=border : amplitude A (m)", c.borderAmp);
    cmd.AddValue("edgeRssi", "RSSI vise a d=R (dBm)", c.edgeRssi);
    cmd.AddValue("apTxPower", "Puissance Tx WiFi (dBm), <=-99 = auto", c.apTxPower);
    cmd.AddValue("shadowSigma", "Ecart-type du shadowing (dB)", c.shadowSigma);
    cmd.AddValue("distNoise", "Bruit de mesure de position, sigma (m)", c.distNoise);
    cmd.AddValue("requireAssoc", "Bascule WiFi seulement si associe", c.requireAssoc);
    cmd.AddValue("rate", "Debit CBR UDP (Mb/s)", c.rate);
    cmd.AddValue("pktSize", "Taille des paquets (octets)", c.pktSize);
    cmd.AddValue("netThr", "Seuil de debit politique reseau (Mb/s)", c.netThr);
    cmd.AddValue("seed", "Graine RNG", c.seed);
    cmd.AddValue("run", "Numero de run RNG", c.run);
    cmd.AddValue("outDir", "Repertoire de sortie", c.outDir);
    cmd.AddValue("tag", "Prefixe des fichiers de sortie", c.tag);
    cmd.AddValue("saveBins", "Ecrire le CSV par 100 ms", c.saveBins);
    cmd.AddValue("flowmon", "Activer FlowMonitor", c.flowmon);
    cmd.AddValue("pcap", "Activer les pcap", c.pcap);
    cmd.AddValue("lteTraces", "Activer lteHelper->EnableTraces()", c.lteTraces);
    cmd.AddValue("anim", "Generer le XML NetAnim", c.anim);
    cmd.AddValue("apX", "Position x de l'AP (m)", c.apX);
    cmd.Parse(argc, argv);

    // Validation des parametres pour eviter les divisions par zero,
    // les tailles de paquets invalides et les configurations incoherentes.
    SeqTsHeader seqHeader;
    NS_ABORT_MSG_IF(c.criterion != "distance" && c.criterion != "rssi" &&
                        c.criterion != "network",
                    "criterion doit etre distance, rssi ou network");
    NS_ABORT_MSG_IF(c.pattern != "line" && c.pattern != "border",
                    "pattern doit etre line ou border");
    NS_ABORT_MSG_IF(c.apRadius <= 0.0 || c.hyst < 0.0 ||
                        c.apRadius <= c.hyst,
                    "Il faut apRadius > hyst >= 0");
    NS_ABORT_MSG_IF(c.speed <= 0.0 || c.rate <= 0.0 || c.checkPeriod <= 0.0 ||
                        c.simTime <= 2.0 || c.pktSize < seqHeader.GetSerializedSize(),
                    "speed, rate et checkPeriod doivent etre positifs, simTime > 2 s et pktSize >= SeqTsHeader");
    NS_ABORT_MSG_IF(c.ewma <= 0.0 || c.ewma > 1.0,
                    "ewma doit etre dans ]0,1]");

    RngSeedManager::SetSeed(c.seed);
    RngSeedManager::SetRun(c.run);
    // Evite qu'un echec ARP initial (UE pas encore associe) bloque la passerelle pendant 100 s
    Config::SetDefault("ns3::ArpCache::DeadTimeout", TimeValue(Seconds(1.0)));
    std::filesystem::create_directories(c.outDir);
    const std::string prefix = c.outDir + "/" + c.tag;

    // ---------- Grandeurs derivees ----------
    Ptr<UniformRandomVariable> u = CreateObject<UniformRandomVariable>();
    double b = c.offset;
    if (c.offsetFrac > 0.0)
    {
        b = u->GetValue(0.0, c.offsetFrac * c.apRadius);
    }
    S.txp = (c.apTxPower > -99.0)
                ? c.apTxPower
                : c.edgeRssi + kRefLoss + 10.0 * kExp * std::log10(c.apRadius);
    S.thrIn = (c.thrIn >= 0) ? c.thrIn : c.apRadius - c.hyst;
    S.thrOut = (c.thrOut >= 0) ? c.thrOut : c.apRadius + c.hyst;
    S.rssiIn = (c.rssiIn < 900) ? c.rssiIn : RssiAt(S.thrIn, S.txp);
    S.rssiOut = (c.rssiOut < 900) ? c.rssiOut : RssiAt(S.thrOut, S.txp);
    S.tStart = 1.0;
    S.tStop = c.simTime - 1.0;
    S.interval = c.pktSize * 8.0 / (c.rate * 1e6);
    S.bins.assign(static_cast<size_t>(c.simTime * 10.0) + 2, Bin());

    // ---------- Noeuds ----------
    NodeContainer enbNodes, ueNodes, apNodes, remoteNodes;
    enbNodes.Create(1);
    ueNodes.Create(1);
    apNodes.Create(1);
    remoteNodes.Create(1);
    S.ue = ueNodes.Get(0);
    S.ap = apNodes.Get(0);
    S.remote = remoteNodes.Get(0);

    // ---------- Mobilite ----------
    MobilityHelper mob;
    mob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mob.Install(enbNodes);
    mob.Install(apNodes);
    mob.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mob.Install(ueNodes);
    enbNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0, 0, 30));
    apNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(c.apX, 0, 3));
    Ptr<ConstantVelocityMobilityModel> ueMob =
        DynamicCast<ConstantVelocityMobilityModel>(S.ue->GetObject<MobilityModel>());
    if (c.pattern == "border")
    {
        // aller-retour autour de la limite d'entree x = apX - R, amplitude A
        double xlo = c.apX - c.apRadius - c.borderAmp;
        double xhi = c.apX - c.apRadius + c.borderAmp;
        ueMob->SetPosition(Vector(xlo, b, 1.5));
        ueMob->SetVelocity(Vector(c.speed, 0, 0));
        Simulator::Schedule(Seconds(0.05), &Bounce, ueMob, xlo, xhi);
    }
    else
    {
        // traversee rectiligne centree sur l'AP : x0 = apX - v*T/2
        double half = c.speed * c.simTime / 2.0;
        if (half < S.thrOut + 5.0)
        {
            std::cerr << "ATTENTION: l'UE demarre dans/pres du hotspot (v*T/2 < thrOut)."
                      << " Augmenter simTime ou speed." << std::endl;
        }
        ueMob->SetPosition(Vector(c.apX - half, b, 1.5));
        ueMob->SetVelocity(Vector(c.speed, 0, 0));
    }

    // ---------- LTE / EPC ----------
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);
    Ptr<Node> pgw = epcHelper->GetPgwNode();

    InternetStackHelper internet;
    internet.Install(remoteNodes);
    internet.Install(apNodes);
    internet.Install(ueNodes);

    // liaison PGW <-> RemoteHost (reseau 1.0.0.0/8, comme les exemples EPC de NS-3)
    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
    p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));
    p2ph.SetChannelAttribute("Delay", TimeValue(MilliSeconds(10)));
    NetDeviceContainer internetDevs = p2ph.Install(pgw, S.remote);
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIfaces = ipv4h.Assign(internetDevs);
    Ipv4Address remoteLte = internetIfaces.GetAddress(1); // 1.0.0.2

    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice(ueNodes);
    Ipv4InterfaceContainer ueLteIfaces = epcHelper->AssignUeIpv4Address(ueLteDevs);
    lteHelper->Attach(ueLteDevs, enbDevs.Get(0));

    // ---------- WiFi 802.11n (AP + STA) ----------
    YansWifiChannelHelper wch;
    wch.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wch.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                           "Exponent", DoubleValue(kExp),
                           "ReferenceDistance", DoubleValue(1.0),
                           "ReferenceLoss", DoubleValue(kRefLoss));
    if (c.shadowSigma > 0.0)
    {
        std::ostringstream v;
        v << "ns3::NormalRandomVariable[Mean=0.0|Variance=" << c.shadowSigma * c.shadowSigma
          << "]";
        wch.AddPropagationLoss("ns3::RandomPropagationLossModel", "Variable", StringValue(v.str()));
    }
    YansWifiPhyHelper wphy;
    wphy.SetChannel(wch.Create());
    wphy.Set("TxPowerStart", DoubleValue(S.txp));
    wphy.Set("TxPowerEnd", DoubleValue(S.txp));
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");
    WifiMacHelper wmac;
    Ssid ssid("offload-ap");
    wmac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer ueWifiDev = wifi.Install(wphy, wmac, ueNodes);
    wmac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDev = wifi.Install(wphy, wmac, apNodes);

    Ipv4AddressHelper ipv4w;
    ipv4w.SetBase("10.0.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apWifiIf = ipv4w.Assign(apDev);     // AP = 10.0.1.1
    Ipv4InterfaceContainer ueWifiIf = ipv4w.Assign(ueWifiDev); // UE = 10.0.1.2

    // liaison AP <-> RemoteHost (10.0.2.0/24)
    PointToPointHelper p2pAp;
    p2pAp.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2pAp.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
    NetDeviceContainer apRemoteDevs = p2pAp.Install(S.ap, S.remote);
    Ipv4AddressHelper ipv4a;
    ipv4a.SetBase("10.0.2.0", "255.255.255.0");
    Ipv4InterfaceContainer apRemoteIf = ipv4a.Assign(apRemoteDevs);
    Ipv4Address remoteAp = apRemoteIf.GetAddress(1); // 10.0.2.2

    // ---------- Routage statique ----------
    Ipv4StaticRoutingHelper srh;
    // RemoteHost : routes de retour vers les deux plans d'acces.
    // Le reseau LTE des UE (7.0.0.0/8) est joignable via le PGW ;
    // le sous-reseau Wi-Fi (10.0.1.0/24) est joignable via l'AP.
    Ptr<Ipv4> remoteIpv4 = S.remote->GetObject<Ipv4>();
    const int32_t remoteLteIf = remoteIpv4->GetInterfaceForDevice(internetDevs.Get(1));
    const int32_t remoteApIf = remoteIpv4->GetInterfaceForDevice(apRemoteDevs.Get(1));
    NS_ABORT_MSG_IF(remoteLteIf < 0 || remoteApIf < 0,
                    "Interface IPv4 introuvable sur RemoteHost");
    Ptr<Ipv4StaticRouting> rRoute = srh.GetStaticRouting(remoteIpv4);
    rRoute->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"),
                              internetIfaces.GetAddress(0),
                              static_cast<uint32_t>(remoteLteIf));
    rRoute->AddNetworkRouteTo(Ipv4Address("10.0.1.0"), Ipv4Mask("255.255.255.0"),
                              apRemoteIf.GetAddress(0),
                              static_cast<uint32_t>(remoteApIf));

    // AP : le serveur UDP est adresse sur l'interface LTE du RemoteHost
    // (1.0.0.2). L'AP doit donc router ce trafic vers son lien point-a-point.
    Ptr<Ipv4> apIpv4 = S.ap->GetObject<Ipv4>();
    const int32_t apWifiInterface = apIpv4->GetInterfaceForDevice(apDev.Get(0));
    const int32_t apBackhaulIf = apIpv4->GetInterfaceForDevice(apRemoteDevs.Get(0));
    NS_ABORT_MSG_IF(apWifiInterface < 0 || apBackhaulIf < 0,
                    "Interface IPv4 introuvable sur l'AP");
    Ptr<Ipv4StaticRouting> aRoute = srh.GetStaticRouting(apIpv4);
    aRoute->AddHostRouteTo(remoteLte, remoteAp,
                           static_cast<uint32_t>(apBackhaulIf));

    // CORRECTION ESSENTIELLE (Q3/Q4) : un noeud NS-3 ne route pas les paquets
    // recus entre interfaces tant que le forwarding IPv4 n'est pas active.
    // Sans ces deux lignes, l'UE emet sur Wi-Fi mais l'AP jette le paquet.
    apIpv4->SetForwarding(static_cast<uint32_t>(apWifiInterface), true);
    apIpv4->SetForwarding(static_cast<uint32_t>(apBackhaulIf), true);
    // UE : passerelles et indices d'interface
    S.ueRouting = srh.GetStaticRouting(S.ue->GetObject<Ipv4>());
    S.ifLte = S.ue->GetObject<Ipv4>()->GetInterfaceForDevice(ueLteDevs.Get(0));
    S.ifWifi = S.ue->GetObject<Ipv4>()->GetInterfaceForDevice(ueWifiDev.Get(0));
    S.gwLte = epcHelper->GetUeDefaultGatewayAddress();
    S.gwWifi = apWifiIf.GetAddress(0); // passerelle = AP (et non l'adresse de l'UE)
    S.ueWifiAddr = ueWifiIf.GetAddress(0);
    S.ueRouting->SetDefaultRoute(S.gwLte, S.ifLte);

    // ---------- Applications ----------
    // Recepteur UDP (RemoteHost, port 9000)
    Ptr<Socket> sink = Socket::CreateSocket(S.remote, UdpSocketFactory::GetTypeId());
    sink->Bind(InetSocketAddress(Ipv4Address::GetAny(), 9000));
    sink->SetRecvCallback(MakeCallback(&RxUdp));

    Ptr<PacketSink> tcpSink;
    if (c.criterion == "network")
    {
        // Q8 : politique reseau, decision une fois par flux, independante de la position
        bool udpWifi = PolicyOffloadToWifi(true, c.rate, c.netThr);
        bool tcpWifi = PolicyOffloadToWifi(false, 0.0, c.netThr); // TCP -> LTE
        S.dst = udpWifi ? remoteAp : remoteLte;
        SetHostRoute(S.dst, udpWifi);
        S.onWifi = udpWifi;
        // flux FTP/TCP (BulkSend) vers l'adresse LTE du serveur
        SetHostRoute(remoteLte, tcpWifi);
        BulkSendHelper bulk("ns3::TcpSocketFactory", InetSocketAddress(remoteLte, 5001));
        bulk.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer bApp = bulk.Install(S.ue);
        bApp.Start(Seconds(S.tStart));
        bApp.Stop(Seconds(S.tStop));
        PacketSinkHelper psh("ns3::TcpSocketFactory",
                             InetSocketAddress(Ipv4Address::GetAny(), 5001));
        ApplicationContainer sApp = psh.Install(S.remote);
        sApp.Start(Seconds(0.0));
        tcpSink = DynamicCast<PacketSink>(sApp.Get(0));
    }
    else
    {
        S.dst = remoteLte;
        ApplyPath(false, false); // depart sur LTE
        Simulator::Schedule(Seconds(c.checkPeriod), &CheckOffload);
    }
    S.sock = Socket::CreateSocket(S.ue, UdpSocketFactory::GetTypeId());
    S.sock->Connect(InetSocketAddress(S.dst, 9000));
    Simulator::Schedule(Seconds(S.tStart), &SendPacket);
    Simulator::Schedule(Seconds(0.0), &Sample);

    // RSSI via la trace PHY WiFi de l'UE (utile pour criterion=rssi, journalise toujours)
    Ptr<WifiNetDevice> wd = DynamicCast<WifiNetDevice>(ueWifiDev.Get(0));
    wd->GetPhy()->TraceConnectWithoutContext("MonitorSnifferRx", MakeCallback(&SnifferRx));
    S.staMac = DynamicCast<StaWifiMac>(wd->GetMac());
    S.noiseRv = CreateObject<NormalRandomVariable>();
    S.noiseRv->SetAttribute("Mean", DoubleValue(0.0));
    S.noiseRv->SetAttribute("Variance", DoubleValue(c.distNoise * c.distNoise));

    // ---------- Traces ----------
    FlowMonitorHelper fmh;
    Ptr<FlowMonitor> monitor;
    if (c.flowmon)
    {
        NodeContainer fmNodes;
        fmNodes.Add(S.ue);
        fmNodes.Add(S.remote);
        monitor = fmh.Install(fmNodes);
    }
    if (c.pcap)
    {
        wphy.EnablePcap(prefix + "_wifi", ueWifiDev.Get(0), true);
        p2ph.EnablePcap(prefix + "_remote-lte", internetDevs.Get(1), true);
        p2pAp.EnablePcap(prefix + "_remote-ap", apRemoteDevs.Get(1), true);
    }
    if (c.lteTraces)
    {
        lteHelper->EnableTraces();
    }
#ifdef TP3_HAVE_NETANIM
    std::unique_ptr<AnimationInterface> anim;
    if (c.anim)
    {
        anim = std::make_unique<AnimationInterface>(prefix + "_anim.xml");
        anim->UpdateNodeDescription(enbNodes.Get(0), "eNodeB");
        anim->UpdateNodeDescription(S.ap, "AP-WiFi");
        anim->UpdateNodeDescription(S.ue, "UE");
        anim->UpdateNodeDescription(S.remote, "Serveur");
    }
#endif

    // ---------- Execution ----------
    Simulator::Stop(Seconds(c.simTime));
    Simulator::Run();

    // ===================================================================
    // 5. Post-traitement et ecriture des CSV
    // ===================================================================
    // 5.1 Statistiques sur la fenetre de trafic [tStart, tStop)
    size_t i0 = static_cast<size_t>(std::ceil(S.tStart * 10.0));
    size_t i1 = std::min(static_cast<size_t>(std::floor(S.tStop * 10.0)), S.bins.size() - 1);
    double nomBytesBin = c.rate * 1e6 / 8.0 * 0.1;
    uint32_t outBins = 0;
    for (size_t i = i0; i < i1; ++i)
    {
        if (S.bins[i].rxW + S.bins[i].rxL < 0.5 * nomBytesBin)
        {
            outBins++;
        }
    }
    std::vector<double> perSec; // debit (Mb/s) par seconde
    for (size_t i = i0; i + 10 <= i1; i += 10)
    {
        double by = 0;
        for (size_t j = 0; j < 10; ++j)
        {
            by += S.bins[i + j].rxW + S.bins[i + j].rxL;
        }
        perSec.push_back(by * 8.0 / 1e6);
    }
    double mean = 0, var = 0;
    for (double x : perSec)
    {
        mean += x;
    }
    mean = perSec.empty() ? 0 : mean / perSec.size();
    for (double x : perSec)
    {
        var += (x - mean) * (x - mean);
    }
    double sd = perSec.size() > 1 ? std::sqrt(var / (perSec.size() - 1)) : 0;
    double cv = mean > 0 ? sd / mean : 0;

    double txTot = S.txBytesW + S.txBytesL, rxTot = S.rxBytesW + S.rxBytesL;
    double offTx = txTot > 0 ? S.txBytesW / txTot : 0;
    double offRx = rxTot > 0 ? S.rxBytesW / rxTot : 0;
    double lossPct = S.txPkts > 0 ? 100.0 * (1.0 - double(S.rxPkts) / S.txPkts) : 0;
    double dlyMean = S.rxPkts > 0 ? 1e3 * S.dlySum / S.rxPkts : 0;
    // valeur theorique (canal ideal, detection instantanee) : corde / chemin parcouru
    double chord = (b < c.apRadius) ? 2.0 * std::sqrt(c.apRadius * c.apRadius - b * b) : 0.0;
    double offTheory =
        (c.pattern == "line") ? std::min(1.0, chord / (c.speed * (S.tStop - S.tStart))) : -1.0;
    double tcpMb = tcpSink ? tcpSink->GetTotalRx() * 8.0 / 1e6 / (S.tStop - S.tStart) : 0.0;

    // 5.2 Resume (une ligne par run)
    {
        std::ofstream f(prefix + "_summary.csv");
        f << "criterion,pattern,apRadius,hyst,thrIn,thrOut,rssiIn,rssiOut,speed,offset_b,"
             "shadowSigma,distNoise,seed,run,nIn,nOut,nSwitch,nPingPong,txBytesWifi,txBytesLte,"
             "rxBytesWifi,rxBytesLte,offloadTx,offloadRx,offloadTheory,tputMean_Mbps,"
             "tputStd_Mbps,tputCV,lossPct,delayMean_ms,delayMax_ms,outage_s,tcpTput_Mbps\n";
        f << std::fixed << std::setprecision(5) << c.criterion << "," << c.pattern << ","
          << c.apRadius << "," << c.hyst << "," << S.thrIn << "," << S.thrOut << "," << S.rssiIn
          << "," << S.rssiOut << "," << c.speed << "," << b << "," << c.shadowSigma << "," << c.distNoise << ","
          << c.seed << "," << c.run << "," << S.nIn << "," << S.nOut << "," << (S.nIn + S.nOut)
          << "," << S.nPP << "," << S.txBytesW << "," << S.txBytesL << "," << S.rxBytesW << ","
          << S.rxBytesL << "," << offTx << "," << offRx << "," << offTheory << "," << mean << ","
          << sd << "," << cv << "," << lossPct << "," << dlyMean << "," << 1e3 * S.dlyMax << ","
          << outBins * 0.1 << "," << tcpMb << "\n";
    }
    // 5.3 Bascules
    {
        std::ofstream f(prefix + "_switches.csv");
        f << "t,direction,dist_m,rssi_dBm\n";
        for (auto& e : S.sw)
        {
            f << e.t << "," << e.dir << "," << e.dist << "," << e.rssi << "\n";
        }
    }
    // 5.4 CSV detaille par 100 ms (Q3/Q4/Q5/Q7)
    if (c.saveBins)
    {
        std::ofstream f(prefix + "_bins.csv");
        f << "t,dist_m,rssi_dBm,onWifi,txWifi_B,txLte_B,rxWifi_B,rxLte_B,nRx,"
             "delayMean_ms,delayMax_ms\n";
        for (size_t i = 0; i < S.bins.size(); ++i)
        {
            const Bin& x = S.bins[i];
            f << i * 0.1 << "," << x.dist << "," << x.rssi << "," << x.wifi << "," << x.txW << ","
              << x.txL << "," << x.rxW << "," << x.rxL << "," << x.nRx << ","
              << (x.nRx ? 1e3 * x.dlySum / x.nRx : 0) << "," << 1e3 * x.dlyMax << "\n";
        }
    }
    // 5.5 FlowMonitor (Q2) : un flux par couple (IP source, IP destination, protocole)
    if (c.flowmon)
    {
        monitor->CheckForLostPackets();
        Ptr<Ipv4FlowClassifier> cls = DynamicCast<Ipv4FlowClassifier>(fmh.GetClassifier());
        std::ofstream f(prefix + "_flows.csv");
        f << "flowId,src,dst,proto,txPkts,rxPkts,lostPkts,tput_Mbps,delayMean_ms,jitterMean_ms\n";
        for (auto const& kv : monitor->GetFlowStats())
        {
            Ipv4FlowClassifier::FiveTuple t = cls->FindFlow(kv.first);
            const FlowMonitor::FlowStats& st = kv.second;
            double dur = st.timeLastRxPacket.GetSeconds() - st.timeFirstTxPacket.GetSeconds();
            f << kv.first << "," << t.sourceAddress << "," << t.destinationAddress << ","
              << int(t.protocol) << "," << st.txPackets << "," << st.rxPackets << ","
              << st.lostPackets << "," << (dur > 0 ? st.rxBytes * 8.0 / dur / 1e6 : 0) << ","
              << (st.rxPackets ? 1e3 * st.delaySum.GetSeconds() / st.rxPackets : 0) << ","
              << (st.rxPackets > 1 ? 1e3 * st.jitterSum.GetSeconds() / (st.rxPackets - 1) : 0)
              << "\n";
        }
    }

    std::cout << "[" << c.tag << "] crit=" << c.criterion << " R=" << c.apRadius << " H=" << c.hyst
              << " v=" << c.speed << " b=" << b << " run=" << c.run << " | bascules=" << S.nIn + S.nOut
              << " (pingpong=" << S.nPP << ") offloadTx=" << offTx << " offloadRx=" << offRx
              << " sendErrors=" << S.sendErrors
              << " theorie=" << offTheory << " CV=" << cv << " perte=" << lossPct << "%"
              << std::endl;

    Simulator::Destroy();
    S.ue = nullptr;
    S.ap = nullptr;
    S.remote = nullptr;
    S.ueRouting = nullptr;
    S.sock = nullptr;
    S.staMac = nullptr;
    S.noiseRv = nullptr;
    return 0;
}