/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Kirill Andreev <andreev@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */

#include "ns3/mesh-wifi-interface-mac.h"


#include <iterator>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <iostream>
#include <sstream>
#include <fstream>

#include "ns3/assert.h"
#include "ns3/boolean.h"
#include "ns3/dca-txop.h"
#include "ns3/dcf-manager.h"
#include "ns3/double.h"
#include "ns3/edca-txop-n.h"
#include "ns3/fatal-error.h"
#include "ns3/log.h"
#include "ns3/log-macros-disabled.h"
#include "ns3/mac-low.h"
#include "ns3/mesh-wifi-beacon.h"
#include "ns3/node-container.h"
#include "ns3/object.h"
#include "ns3/object-base.h"
#include "ns3/ptr.h"
#include "ns3/qos-tag.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/type-id.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/yans-wifi-phy.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-header.h"
#include "ns3/dot11s-mac-header.h"

using namespace std;
namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MeshWifiInterfaceMac");

NS_OBJECT_ENSURE_REGISTERED (MeshWifiInterfaceMac);

TypeId
MeshWifiInterfaceMac::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::MeshWifiInterfaceMac")
    .SetParent<RegularWifiMac> ()
    .AddConstructor<MeshWifiInterfaceMac> ()
    .AddAttribute ( "BeaconInterval",
                    "Beacon Interval",
                    TimeValue (Seconds (0.5)),

                    MakeTimeAccessor (
                      &MeshWifiInterfaceMac::m_beaconInterval),
                    MakeTimeChecker ()
                    )
    .AddAttribute ( "RandomStart",
                    "Window when beacon generating starts (uniform random) in seconds",
                    TimeValue (Seconds (0.5)),
                    MakeTimeAccessor (
                      &MeshWifiInterfaceMac::m_randomStart),
                    MakeTimeChecker ()
                    )
    .AddAttribute ( "BeaconGeneration",
                    "Enable/Disable Beaconing.",
                    BooleanValue (true),
                    MakeBooleanAccessor (
                      &MeshWifiInterfaceMac::SetBeaconGeneration, &MeshWifiInterfaceMac::GetBeaconGeneration),
                    MakeBooleanChecker ()
                    )
  ;
  return tid;
}
MeshWifiInterfaceMac::MeshWifiInterfaceMac () :
  m_standard (WIFI_PHY_STANDARD_80211a)
{
  NS_LOG_FUNCTION (this);

  // Let the lower layers know that we are acting as a mesh node
  SetTypeOfStation (MESH);
  m_coefficient = CreateObject<UniformRandomVariable> ();
}
MeshWifiInterfaceMac::~MeshWifiInterfaceMac ()
{
  NS_LOG_FUNCTION (this);
}
//-----------------------------------------------------------------------------
// WifiMac inherited
//-----------------------------------------------------------------------------
void
MeshWifiInterfaceMac::Enqueue (Ptr<const Packet> packet, Mac48Address to, Mac48Address from)
{
  NS_LOG_FUNCTION (this << packet << to << from);
  ForwardDown (packet, from, to);
}
void
MeshWifiInterfaceMac::Enqueue (Ptr<const Packet> packet, Mac48Address to)
{
  NS_LOG_FUNCTION (this << packet << to);
  ForwardDown (packet, m_low->GetAddress (), to);
}
bool
MeshWifiInterfaceMac::SupportsSendFrom () const
{
  return true;
}
void
MeshWifiInterfaceMac::SetLinkUpCallback (Callback<void> linkUp)
{
  NS_LOG_FUNCTION (this);
  RegularWifiMac::SetLinkUpCallback (linkUp);

  // The approach taken here is that, from the point of view of a mesh
  // node, the link is always up, so we immediately invoke the
  // callback if one is set
  linkUp ();
}
void
MeshWifiInterfaceMac::DoDispose ()
{
  NS_LOG_FUNCTION (this);
  m_plugins.clear ();
  m_beaconSendEvent.Cancel ();

  RegularWifiMac::DoDispose ();
}
void
MeshWifiInterfaceMac::DoInitialize ()
{
  NS_LOG_FUNCTION (this);
  m_coefficient->SetAttribute ("Max", DoubleValue (m_randomStart.GetSeconds ()));
  if (m_beaconEnable)
    {
      Time randomStart = Seconds (m_coefficient->GetValue ());
      // Now start sending beacons after some random delay (to avoid collisions)
      NS_ASSERT (!m_beaconSendEvent.IsRunning ());
      m_beaconSendEvent = Simulator::Schedule (randomStart, &MeshWifiInterfaceMac::SendBeacon, this);
      m_tbtt = Simulator::Now () + randomStart;
    }
  else
    {
      // stop sending beacons
      m_beaconSendEvent.Cancel ();
    }
}

int64_t
MeshWifiInterfaceMac::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  int64_t currentStream = stream;
  m_coefficient->SetStream (currentStream++);
  for (PluginList::const_iterator i = m_plugins.begin (); i < m_plugins.end (); i++)
    {
      currentStream += (*i)->AssignStreams (currentStream);
    }
  return (currentStream - stream);
}

//-----------------------------------------------------------------------------
// Plugins
//-----------------------------------------------------------------------------
void
MeshWifiInterfaceMac::InstallPlugin (Ptr<MeshWifiInterfaceMacPlugin> plugin)
{
  NS_LOG_FUNCTION (this);

  plugin->SetParent (this);
  m_plugins.push_back (plugin);
}
//-----------------------------------------------------------------------------
// Switch channels
//-----------------------------------------------------------------------------
uint16_t
MeshWifiInterfaceMac::GetFrequencyChannel () const
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_phy != 0); // need PHY to set/get channel

  Ptr<YansWifiPhy> phy = m_phy->GetObject<YansWifiPhy> ();
  if (phy != 0)
    {
      return phy->GetChannelNumber ();
    }
  else
    {
      return 0;
    }
}
void
MeshWifiInterfaceMac::SwitchFrequencyChannel (uint16_t new_id)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_phy != 0); // need PHY to set/get channel
  /**
   * \todo
   * Correct channel switching is:
   *
   * 1. Interface down, e.g. to stop packets from layer 3
   * 2. Wait before all output queues will be empty
   * 3. Switch PHY channel
   * 4. Interface up
   *
   * Now we use dirty channel switch -- just change frequency
   */
  Ptr<YansWifiPhy> phy = m_phy->GetObject<YansWifiPhy> ();
  phy->SetChannelNumber (new_id);
  // Don't know NAV on new channel
  m_dcfManager->NotifyNavResetNow (Seconds (0));
}
//-----------------------------------------------------------------------------
// Forward frame down
//-----------------------------------------------------------------------------
void
MeshWifiInterfaceMac::ForwardDown (Ptr<const Packet> const_packet, Mac48Address from, Mac48Address to)
{
  // copy packet to allow modifications
  Ptr<Packet> packet = const_packet->Copy ();
  WifiMacHeader hdr;
  hdr.SetTypeData ();
  hdr.SetAddr2 (GetAddress ());
  hdr.SetAddr3 (to);
  hdr.SetAddr4 (from);
  hdr.SetDsFrom ();
  hdr.SetDsTo ();
  // Fill QoS fields:
  hdr.SetQosAckPolicy (WifiMacHeader::NORMAL_ACK);
  hdr.SetQosNoEosp ();
  hdr.SetQosNoAmsdu ();
  hdr.SetQosTxopLimit (0);
  // Address 1 is unknwon here. Routing plugin is responsible to correctly set it.
  hdr.SetAddr1 (Mac48Address ());
  // Filter packet through all installed plugins
  for (PluginList::const_iterator i = m_plugins.end () - 1; i != m_plugins.begin () - 1; i--)
    {
      bool drop = !((*i)->UpdateOutcomingFrame (packet, hdr, from, to));
      if (drop)
        {
          return; // plugin drops frame
        }
    }
  // Assert that address1 is set. Assert will fail e.g. if there is no installed routing plugin.
  NS_ASSERT (hdr.GetAddr1 () != Mac48Address ());
  // Queue frame
  if (m_stationManager->IsBrandNew (hdr.GetAddr1 ()))
    {
      // in adhoc mode, we assume that every destination
      // supports all the rates we support.
      for (uint32_t i = 0; i < m_phy->GetNModes (); i++)
        {
          m_stationManager->AddSupportedMode (hdr.GetAddr1 (), m_phy->GetMode (i));
        }
      m_stationManager->RecordDisassociated (hdr.GetAddr1 ());
    }
  //Classify: application sets a tag, which is removed here
  // Get Qos tag:
  AcIndex ac = AC_BE;
  QosTag tag;
  if (packet->RemovePacketTag (tag))
    {
      hdr.SetType (WIFI_MAC_QOSDATA);
      hdr.SetQosTid (tag.GetTid ());
      //Aftre setting type DsFrom and DsTo fields are reset.
      hdr.SetDsFrom ();
      hdr.SetDsTo ();
      ac = QosUtilsMapTidToAc (tag.GetTid ());
    }
  m_stats.sentFrames++;
  m_stats.sentBytes += packet->GetSize ();
  NS_ASSERT (m_edca.find (ac) != m_edca.end ());
  m_edca[ac]->Queue (packet, hdr);
}
void
MeshWifiInterfaceMac::SendManagementFrame (Ptr<Packet> packet, const WifiMacHeader& hdr)
{
  //Filter management frames:
  WifiMacHeader header = hdr;
  for (PluginList::const_iterator i = m_plugins.end () - 1; i != m_plugins.begin () - 1; i--)
    {
      bool drop = !((*i)->UpdateOutcomingFrame (packet, header, Mac48Address (), Mac48Address ()));
      if (drop)
        {
          return; // plugin drops frame
        }
    }
  m_stats.sentFrames++;
  m_stats.sentBytes += packet->GetSize ();
  if ((m_edca.find (AC_VO) == m_edca.end ()) || (m_edca.find (AC_BK) == m_edca.end ()))
    {
      NS_FATAL_ERROR ("Voice or Background queue is not set up!");
    }
  /*
   * When we send a management frame - it is better to enqueue it to
   * priority queue. But when we send a broadcast management frame,
   * like PREQ, little MinCw value may cause collisions during
   * retransmissions (two neighbor stations may choose the same window
   * size, and two packets will be collided). So, broadcast management
   * frames go to BK queue.
   */
  if (hdr.GetAddr1 () != Mac48Address::GetBroadcast ())
    {
      m_edca[AC_VO]->Queue (packet, header);
    }
  else
    {
      m_edca[AC_BK]->Queue (packet, header);
    }
}
SupportedRates
MeshWifiInterfaceMac::GetSupportedRates () const
{
  // set the set of supported rates and make sure that we indicate
  // the Basic Rate set in this set of supported rates.
  SupportedRates rates;
  for (uint32_t i = 0; i < m_phy->GetNModes (); i++)
    {
      WifiMode mode = m_phy->GetMode (i);
      rates.AddSupportedRate (mode.GetDataRate ());
    }
  // set the basic rates
  for (uint32_t j = 0; j < m_stationManager->GetNBasicModes (); j++)
    {
      WifiMode mode = m_stationManager->GetBasicMode (j);
      rates.SetBasicRate (mode.GetDataRate ());
    }
  return rates;
}
bool
MeshWifiInterfaceMac::CheckSupportedRates (SupportedRates rates) const
{
  for (uint32_t i = 0; i < m_stationManager->GetNBasicModes (); i++)
    {
      WifiMode mode = m_stationManager->GetBasicMode (i);
      if (!rates.IsSupportedRate (mode.GetDataRate ()))
        {
          return false;
        }
    }
  return true;
}
//-----------------------------------------------------------------------------
// Beacons
//-----------------------------------------------------------------------------
void
MeshWifiInterfaceMac::SetRandomStartDelay (Time interval)
{
  NS_LOG_FUNCTION (this << interval);
  m_randomStart = interval;
}
void
MeshWifiInterfaceMac::SetBeaconInterval (Time interval)
{
  NS_LOG_FUNCTION (this << interval);
  m_beaconInterval = interval;
}
Time
MeshWifiInterfaceMac::GetBeaconInterval () const
{
  return m_beaconInterval;
}
void
MeshWifiInterfaceMac::SetBeaconGeneration (bool enable)
{
  NS_LOG_FUNCTION (this << enable);
  m_beaconEnable = enable;
}
bool
MeshWifiInterfaceMac::GetBeaconGeneration () const
{
  return m_beaconSendEvent.IsRunning ();
}
Time
MeshWifiInterfaceMac::GetTbtt () const
{
  return m_tbtt;
}
uint64_t
MeshWifiInterfaceMac::GetSimTimePast()
{
	return simTimeBefore;
}
void
MeshWifiInterfaceMac::SetSimTimePast(uint64_t simTimePast)
{
	simTimeBefore = simTimePast;
}


void
MeshWifiInterfaceMac::ShiftTbtt (Time shift)
{
  // User of ShiftTbtt () must take care don't shift it to the past
  NS_ASSERT (GetTbtt () + shift > Simulator::Now ());

  m_tbtt += shift;
  // Shift scheduled event
  Simulator::Cancel (m_beaconSendEvent);
  m_beaconSendEvent = Simulator::Schedule (GetTbtt () - Simulator::Now (), &MeshWifiInterfaceMac::SendBeacon,this);

}
void
MeshWifiInterfaceMac::ScheduleNextBeacon ()
{
  m_tbtt += GetBeaconInterval ();
  m_beaconSendEvent = Simulator::Schedule (GetBeaconInterval (), &MeshWifiInterfaceMac::SendBeacon, this);
}
void
MeshWifiInterfaceMac::SendBeacon ()
{

  UpdateNodeClock();//Update clocks with each beacon send
  NS_LOG_FUNCTION (this);
  NS_LOG_DEBUG (GetAddress () << " is sending beacon");
  NS_ASSERT (!m_beaconSendEvent.IsRunning ());
  // Form & send beacon
  MeshWifiBeacon beacon (GetNodeClock(), GetSsid (), GetSupportedRates (), m_beaconInterval.GetMicroSeconds ());
  //MeshWifiBeacon beacon (GetNodeClock(ConvAddressToInt(GetAddress())), GetSsid (), GetSupportedRates (), m_beaconInterval.GetMicroSeconds ());
  // Ask all plugins to add their specific information elements to beacon
  for (PluginList::const_iterator i = m_plugins.begin (); i != m_plugins.end (); ++i)
    {
      (*i)->UpdateBeacon (beacon);
    }

  m_dca->Queue (beacon.CreatePacket (), beacon.CreateHeader (GetAddress (), GetMeshPointAddress ()));
  ScheduleNextBeacon ();

}

void
MeshWifiInterfaceMac::UpdateNodeClock()
{//Updating node clocks everytime a beacon is sent.
	NUMBER_OF_NODES = nodeL.GetNNodes()/2;
	uint64_t simTimeNow = Simulator::Now().GetMilliSeconds();
	uint64_t simTimeSub = GetSimTimePast();

	if (simTimeSub > 100000) {
		simTimeSub = 0;
	}

	int diff = simTimeNow - simTimeSub;
	for (uint32_t i = 0; i < nodeL.GetNNodes()-NUMBER_OF_NODES; i++){
		nodeL.GetNode(i)->SetNodeTime(nodeL.GetNode(i)->GetNodeTime() + diff);// updates node clock
	}

	SetSimTimePast(simTimeNow);

}
// Method which prints out nodeclock and offset
void
MeshWifiInterfaceMac::Output() {

	NUMBER_OF_NODES = nodeL.GetNNodes()/2;

	for (uint32_t j = 0; j < nodeL.GetNNodes() - NUMBER_OF_NODES; j++){
			cout << "Node: " << j << " has " << nodeL.GetNode(j)->GetMapNode().size() << " neighbors" << endl;
			for (uint32_t i = 0; i < nodeL.GetNNodes() - NUMBER_OF_NODES; i++){
				if (m_node->GetOffsetFromNodes(j,i) != 0) {
					cout << "Offset from node " << i << " is: "<< m_node->GetOffsetFromNodes(j,i) << endl;
				}
			}
		}

	for (uint32_t i = 0; i < nodeL.GetNNodes() - NUMBER_OF_NODES; i++){
		cout << "Node " << i << " TSF timer = " << nodeL.GetNode(i)->GetNodeTime() << endl;
	}

}
// Method which calculates offset and creates map
void
MeshWifiInterfaceMac::CalculateOffset (uint64_t receivedNodeClock, uint64_t beaconTimeStamp, int nodeID)
{

	NUMBER_OF_NODES = nodeL.GetNNodes()/2;
	int nodeID1 = m_node->GetId() - NUMBER_OF_NODES;
	int nodeID2 = nodeID;
	int tOffset;
	uint64_t n1 = receivedNodeClock;
	uint64_t n2 = nodeL.GetNode(nodeID2)->GetNodeTime();

	tOffset = n1 - n2;// calculate offset

	if (nodeID1 == nodeID2) {
		cout << nodeID1 << " Same value " << nodeID2 << endl;
		exit(1);
	}

	nodeL.GetNode(nodeID1)->SetMapNode(nodeID2, tOffset);// create map

}
// Method which synchronize nodes
void
MeshWifiInterfaceMac::Synchronize(int nodeID, uint64_t timeStamp)
{

	int n1Clock = 0;
	int n2Clock = timeStamp;
	int clockOffset = 0;

	n1Clock = nodeL.GetNode(nodeID)->GetNodeTime();// Get nodeclock

	clockOffset = n1Clock - n2Clock;//Calculate offset between nodes

	if (clockOffset < 0 ){//if offset is less than 0 then synchronize
		clockOffset = abs(clockOffset);
		nodeL.GetNode(nodeID)->SetNodeTime(n1Clock + clockOffset);
	}
}
/*
int
MeshWifiInterfaceMac::ConvAddressToInt(Mac48Address address)
{//Not using in the simulator, method was used earlier.
//Using the GetAddress method to get nodeID
	//cout << address << endl;

	int nodeID;
	ostringstream ss;
	ss << address;//object to string
	char chars[] = ":";
	string str (ss.str());

	for (unsigned int i = 0; i < strlen(chars); ++i) {
		str.erase(std::remove(str.begin(), str.end(), chars[i]), str.end());//remove chars 0 and :
	}
	nodeID = atoi(str.c_str())-1;//string to int. -1 because NodeID 1 = getNode(0)
	return nodeID;

}
*/
//method which returns true if network is synchronized
bool
MeshWifiInterfaceMac::IsSynchronized()
{
	bool sync;
	NUMBER_OF_NODES = nodeL.GetNNodes()/2;
	uint32_t count = 0;
	for (uint32_t i = 0; i < nodeL.GetNNodes() - NUMBER_OF_NODES; i++){
		if(nodeL.GetNode(0)->GetNodeTime() == nodeL.GetNode(i)->GetNodeTime()){//if node 1 has the same clock as all the other nodes then
			count ++; // count for each same nodeclock
			if (count == nodeL.GetNNodes() - NUMBER_OF_NODES) {// if count is equal to number of nodes then
				sync = true;// sync is equal to true
			} else {
				sync = false;
			}
		}
	}

	return sync;


}
// method which retrieves ID from MAC-address from a specific node
int
MeshWifiInterfaceMac::RetrieveIDFromMAC(WifiMacHeader const *hdr)
{
	int nodeID;
	Ptr <Node> nNode;
	NUMBER_OF_NODES = nodeL.GetNNodes()/2;
	Mac48Address tranAddress = hdr->GetAddr2();

	for (uint32_t j = 0; j < nodeL.GetNNodes() - NUMBER_OF_NODES; j++){
		nNode = nodeL.GetNode(j);
		for (uint32_t i = 0; i < nNode->GetNDevices(); i++) {
			if (nNode->GetDevice(i)->GetAddress() == tranAddress){
				nodeID = nNode->GetId();

			}

		}
	}

	return nodeID;
}
// Method which returns true if its the first beacon
bool
MeshWifiInterfaceMac::FirstBeacon()
{
	uint32_t firstBeacon = 0;
	firstBeacon++;

	if(firstBeacon == 1){
		return true;
	}

	return false;
}

void
MeshWifiInterfaceMac::Receive (Ptr<Packet> packet, WifiMacHeader const *hdr)
{

	NUMBER_OF_NODES = nodeL.GetNNodes()/2;
	m_node = node.GetObject<ns3::Node>();
	int nodeID1 = m_node->GetId() - NUMBER_OF_NODES;

  //Process beacon
  if ((hdr->GetAddr1 () != GetAddress ()) && (hdr->GetAddr1 () != Mac48Address::GetBroadcast ()))
    {
      return;
    }
  if (hdr->IsBeacon ())
    {
      m_stats.recvBeacons++;
      MgtBeaconHeader beacon_hdr;
      packet->PeekHeader (beacon_hdr);
      Synchronize(nodeID1, beacon_hdr.GetTime());// calls synchronization method
      CalculateOffset(GetNodeClock(),beacon_hdr.GetTime(), RetrieveIDFromMAC(hdr));//Calculate offset
      nodeL.GetNode(nodeID1)->SetReceivedBeacons(m_stats.recvBeacons);

      if(IsSynchronized()) {// If network is synchronized

    	  //endTime = Simulator::Now().GetMilliSeconds();
    	  //uint64_t totalTime = endTime - startTime;
    	 // Output();
      }

      NS_LOG_DEBUG ("Beacon received from " << hdr->GetAddr2 () << " I am " << GetAddress () << " at "
                                          << Simulator::Now ().GetMicroSeconds () << " microseconds");
      // update supported rates
      if (beacon_hdr.GetSsid ().IsEqual (GetSsid ()))
        {
          SupportedRates rates = beacon_hdr.GetSupportedRates ();

          for (uint32_t i = 0; i < m_phy->GetNModes (); i++)
            {
              WifiMode mode = m_phy->GetMode (i);
              if (rates.IsSupportedRate (mode.GetDataRate ()))
                {
                  m_stationManager->AddSupportedMode (hdr->GetAddr2 (), mode);
                  if (rates.IsBasicRate (mode.GetDataRate ()))
                    {
                      m_stationManager->AddBasicMode (mode);
                    }
                }
            }
        }
    }
  else
    {
      m_stats.recvBytes += packet->GetSize ();
      m_stats.recvFrames++;
    }
  // Filter frame through all installed plugins
  for (PluginList::iterator i = m_plugins.begin (); i != m_plugins.end (); ++i)
    {
      bool drop = !((*i)->Receive (packet, *hdr));
      if (drop)
        {
          return; // plugin drops frame
        }
    }
  // Check if QoS tag exists and add it:
  if (hdr->IsQosData ())
    {
      packet->AddPacketTag (QosTag (hdr->GetQosTid ()));
    }
  // Forward data up
  if (hdr->IsData ())
    {
      ForwardUp (packet, hdr->GetAddr4 (), hdr->GetAddr3 ());
    }

  // We don't bother invoking RegularWifiMac::Receive() here, because
  // we've explicitly handled all the frames we care about. This is in
  // contrast to most classes which derive from RegularWifiMac.
}
uint32_t
MeshWifiInterfaceMac::GetLinkMetric (Mac48Address peerAddress)
{
  uint32_t metric = 1;
  if (!m_linkMetricCallback.IsNull ())
    {
      metric = m_linkMetricCallback (peerAddress, this);
    }
  return metric;
}
void
MeshWifiInterfaceMac::SetLinkMetricCallback (Callback<uint32_t, Mac48Address, Ptr<MeshWifiInterfaceMac> > cb)
{
  m_linkMetricCallback = cb;
}
void
MeshWifiInterfaceMac::SetMeshPointAddress (Mac48Address a)
{
  m_mpAddress = a;
}
void
MeshWifiInterfaceMac::SetNodeContainer(NodeContainer node){

	m_nodes = node;
}
Mac48Address
MeshWifiInterfaceMac::GetMeshPointAddress () const
{
  return m_mpAddress;
}
//Statistics:
MeshWifiInterfaceMac::Statistics::Statistics () :
  recvBeacons (0), sentFrames (0), sentBytes (0), recvFrames (0), recvBytes (0)
{
}
void
MeshWifiInterfaceMac::Statistics::Print (std::ostream & os) const
{
  os << "<Statistics "
  /// \todo txBeacons
  "rxBeacons=\"" << recvBeacons << "\" "
  "txFrames=\"" << sentFrames << "\" "
  "txBytes=\"" << sentBytes << "\" "
  "rxFrames=\"" << recvFrames << "\" "
  "rxBytes=\"" << recvBytes << "\"/>" << std::endl;
}
void
MeshWifiInterfaceMac::Report (std::ostream & os) const
{
  os << "<Interface "
  "BeaconInterval=\"" << GetBeaconInterval ().GetSeconds () << "\" "
  "Channel=\"" << GetFrequencyChannel () << "\" "
  "Address = \"" << GetAddress () << "\">" << std::endl;
  m_stats.Print (os);
  os << "</Interface>" << std::endl;
}
void
MeshWifiInterfaceMac::ResetStats ()
{
  m_stats = Statistics ();
}

void
MeshWifiInterfaceMac::FinishConfigureStandard (enum WifiPhyStandard standard)
{
  RegularWifiMac::FinishConfigureStandard (standard);
  m_standard = standard;

  // We use the single DCF provided by WifiMac for the purpose of
  // Beacon transmission. For this we need to reconfigure the channel
  // access parameters slightly, and do so here.
  m_dca->SetMinCw (0);
  m_dca->SetMaxCw (0);
  m_dca->SetAifsn (1);
}
WifiPhyStandard
MeshWifiInterfaceMac::GetPhyStandard () const
{
  return m_standard;
}
} // namespace ns3

