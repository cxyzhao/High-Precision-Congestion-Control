#include "ns3/ipv4.h"
#include "ns3/packet.h"
#include "ns3/ipv4-header.h"
#include "ns3/pause-header.h"
#include "ns3/flow-id-tag.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/integer.h"
#include "ns3/double.h"
#include "switch-node.h"
#include "qbb-net-device.h"
#include "ppp-header.h"
#include "ns3/int-header.h"
#include "ns3/random-variable.h"
#include <cmath>

namespace ns3 {

TypeId SwitchNode::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SwitchNode")
    .SetParent<Node> ()
    .AddConstructor<SwitchNode> ()
	.AddAttribute("EcnEnabled",
			"Enable ECN marking.",
			BooleanValue(false),
			MakeBooleanAccessor(&SwitchNode::m_ecnEnabled),
			MakeBooleanChecker())
	.AddAttribute("CcMode",
			"CC mode.",
			UintegerValue(0),
			MakeUintegerAccessor(&SwitchNode::m_ccMode),
			MakeUintegerChecker<uint32_t>())
	.AddAttribute("AckHighPrio",
			"Set high priority for ACK/NACK or not",
			UintegerValue(0),
			MakeUintegerAccessor(&SwitchNode::m_ackHighPrio),
			MakeUintegerChecker<uint32_t>())
	.AddAttribute("MaxRtt",
			"Max Rtt of the network",
			UintegerValue(9000),
			MakeUintegerAccessor(&SwitchNode::m_maxRtt),
			MakeUintegerChecker<uint32_t>())
	.AddAttribute("AbcDt",
			"ABC D_t value (ns)",
			DoubleValue(64000.0),
			MakeDoubleAccessor(&SwitchNode::abc_dt),
			MakeDoubleChecker<double>())
	.AddAttribute("AbcDelta",
			"ABC Delta value (ns)",
			DoubleValue(64000.0),
			MakeDoubleAccessor(&SwitchNode::abc_delta),
			MakeDoubleChecker<double>())
	.AddAttribute("AbcToken",
			"ABC Token",
			DoubleValue(50),
			MakeDoubleAccessor(&SwitchNode::abc_tokenLimit),
			MakeDoubleChecker<double>())
	.AddAttribute("AbcEta",
			"ABC Eta",
			DoubleValue(0.95),
			MakeDoubleAccessor(&SwitchNode::abc_eta),
			MakeDoubleChecker<double>())
	.AddAttribute("AbcDqInterval",
			"ABC time interval to calculate dequeue rate",
			IntegerValue(1000),
			MakeIntegerAccessor(&SwitchNode::abc_dqInterval),
			MakeIntegerChecker<int>())
	.AddAttribute("AbcTokenMinBound",
			"ABC uses min to bound token",
			IntegerValue(1),
			MakeIntegerAccessor(&SwitchNode::abc_tokenMinBound),
			MakeIntegerChecker<int>())
	.AddAttribute("AbcMarkMode",
			"ABC mode to mark brake",
			IntegerValue(1),
			MakeIntegerAccessor(&SwitchNode::abc_markmode),
			MakeIntegerChecker<int>())
	.AddAttribute("HPCCMODE",
			"HPCC maintains per-queue/per-port INT",
			IntegerValue(0),
			MakeIntegerAccessor(&SwitchNode::m_hpccMode),
			MakeIntegerChecker<int>())
	
  ;
  return tid;
}





SwitchNode::SwitchNode(){
	m_ecmpSeed = m_id;
	m_node_type = 1;
	m_mmu = CreateObject<SwitchMmu>();
	for (uint32_t i = 0; i < pCnt; i++)
		for (uint32_t j = 0; j < pCnt; j++)
			for (uint32_t k = 0; k < qCnt; k++)
				m_bytes[i][j][k] = 0;
	for (uint32_t i = 0; i < pCnt; i++){
		m_txBytes[i] = 0;
		m_txBytes_all[i] = 0;
		m_txBytes_udp[i] = m_txBytes_udp_wholeheader[i] = m_txBytes_ack_intheader[i] = 0;
		m_txBytes_ack[i] = m_txBytes_ack_wholeheader[i] = m_txBytes_ack_intheader[i] = 0;
	}
	for (uint32_t i = 0; i < pCnt; i++)
		m_lastPktSize[i] = m_lastPktTs[i] = 0;
	for (uint32_t i = 0; i < pCnt; i++)
		m_u[i] = 0;
	for (uint32_t i = 0; i < pCnt; i++)
		m_lastUpdateDqRateTs[i] = m_DqPktSize[i]  =0;
	for (uint32_t i = 0; i < pCnt; i++)
		abc_token[i] = 0.0;
	for (uint32_t i = 0; i < pCnt; i++){
		for (uint32_t j = 0; j < qCnt; j++){
			dqRate[i][j] = m_lastQLen[i][j] = 0.0;
			abc_token_perqueue[i][j] = 0.0;
			m_DqPktSizePerQueue[i][j] = 0.0;
		}
	}
 }

int SwitchNode::GetOutDev(Ptr<const Packet> p, CustomHeader &ch){
	// look up entries
	auto entry = m_rtTable.find(ch.dip);

	// no matching entry
	if (entry == m_rtTable.end())
		return -1;

	// entry found
	auto &nexthops = entry->second;

	// pick one next hop based on hash
	union {
		uint8_t u8[4+4+2+2];
		uint32_t u32[3];
	} buf;
	buf.u32[0] = ch.sip;
	buf.u32[1] = ch.dip;
	if (ch.l3Prot == 0x6)
		buf.u32[2] = ch.tcp.sport | ((uint32_t)ch.tcp.dport << 16);
	else if (ch.l3Prot == 0x11)
		buf.u32[2] = ch.udp.sport | ((uint32_t)ch.udp.dport << 16);
	else if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD)
		buf.u32[2] = ch.ack.sport | ((uint32_t)ch.ack.dport << 16);

	uint32_t idx = EcmpHash(buf.u8, 12, m_ecmpSeed) % nexthops.size();
	return nexthops[idx];
}

void SwitchNode::CheckAndSendPfc(uint32_t inDev, uint32_t qIndex){
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
	if (m_mmu->CheckShouldPause(inDev, qIndex)){
		device->SendPfc(qIndex, 0);
		m_mmu->SetPause(inDev, qIndex);
	}
}
void SwitchNode::CheckAndSendResume(uint32_t inDev, uint32_t qIndex){
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
	if (m_mmu->CheckShouldResume(inDev, qIndex)){
		device->SendPfc(qIndex, 1);
		m_mmu->SetResume(inDev, qIndex);
	}
}

void SwitchNode::SendToDev(Ptr<Packet>p, CustomHeader &ch){
	int idx = GetOutDev(p, ch);
	if (idx >= 0){
		NS_ASSERT_MSG(m_devices[idx]->IsLinkUp(), "The routing table look up should return link that is up");
		// determine the qIndex
		uint32_t qIndex;
		if (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE || (m_ackHighPrio && (ch.l3Prot == 0xFD || ch.l3Prot == 0xFC))){  //QCN or PFC or NACK, go highest priority
			qIndex = 0;
		}else{
			qIndex = (ch.l3Prot == 0x06 ? 1 : ch.udp.pg); // if TCP, put to queue 1
		}

		// admission control
		FlowIdTag t;
		p->PeekPacketTag(t);
		uint32_t inDev = t.GetFlowId();
		if (qIndex != 0){ //not highest priority
			if (m_mmu->CheckIngressAdmission(inDev, qIndex, p->GetSize()) && m_mmu->CheckEgressAdmission(idx, qIndex, p->GetSize())){			// Admission control
				m_mmu->UpdateIngressAdmission(inDev, qIndex, p->GetSize());
				m_mmu->UpdateEgressAdmission(idx, qIndex, p->GetSize());
			}else{
				return; // Drop
			}
			CheckAndSendPfc(inDev, qIndex);
		}
		m_bytes[inDev][idx][qIndex] += p->GetSize();
		m_devices[idx]->SwitchSend(qIndex, p, ch);
	}else
		return; // Drop
}

uint32_t SwitchNode::EcmpHash(const uint8_t* key, size_t len, uint32_t seed) {
  uint32_t h = seed;
  if (len > 3) {
    const uint32_t* key_x4 = (const uint32_t*) key;
    size_t i = len >> 2;
    do {
      uint32_t k = *key_x4++;
      k *= 0xcc9e2d51;
      k = (k << 15) | (k >> 17);
      k *= 0x1b873593;
      h ^= k;
      h = (h << 13) | (h >> 19);
      h += (h << 2) + 0xe6546b64;
    } while (--i);
    key = (const uint8_t*) key_x4;
  }
  if (len & 3) {
    size_t i = len & 3;
    uint32_t k = 0;
    key = &key[i - 1];
    do {
      k <<= 8;
      k |= *key--;
    } while (--i);
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    h ^= k;
  }
  h ^= len;
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

void SwitchNode::SetEcmpSeed(uint32_t seed){
	m_ecmpSeed = seed;
}

void SwitchNode::AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx){
	uint32_t dip = dstAddr.Get();
	m_rtTable[dip].push_back(intf_idx);
}

void SwitchNode::ClearTable(){
	m_rtTable.clear();
}

// This function can only be called in switch mode
bool SwitchNode::SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader &ch){
	SendToDev(packet, ch);
	return true;
}

void SwitchNode::SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p){
	FlowIdTag t;
	p->PeekPacketTag(t);
	p->RemovePacketTag(t);
	if(qIndex == 0){
		// Process ACk for updating ABC Token
		uint8_t* buf = p->GetBuffer();
		if (buf[PppHeader::GetStaticSize() + 9] == 0xFC){
			if (m_ccMode == 9){
				if (abc_markmode == 7){
					// ABC + Consider ACK
					//Count current pkt as DqPktSize
					uint32_t pkt_size = p->GetSize();

					double tr_t = 1.0; // target rate
					double yita = 0.95; //eta
					double delta = abc_delta; //nanoseconds
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps
					double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len
					double x_t = qLen / u_t * 1000000000 ; //queuing delay (nanoseconds)


					double d_t = abc_dt; // nanoseconds
					tr_t = abc_eta * u_t - u_t / delta * std::max(x_t - d_t, 0.0);

				

					double cr_t = 1.0; // dequeue rate BytesPerSecond
					double t = Simulator::Now().GetTimeStep();
					double update_interval = abc_dqInterval;
					double dt = t - m_lastUpdateDqRateTs[ifIndex];
					//Due to time resolution of ns, PicoSeconds are lost.
					if(update_interval < 10)
						dt += 1;


					if (dt > update_interval){ // update dqRate per x ns
						dqRate[ifIndex][qIndex] = m_DqPktSize[ifIndex] / (dt/1000000000); // Bps
	
						m_DqPktSize[ifIndex] = 0;
						m_lastUpdateDqRateTs[ifIndex] = t;
					}
					cr_t  = dqRate[ifIndex][qIndex];
				
					double f_t; 
					if(abc_tokenMinBound)
						f_t  = std::min(0.5 * tr_t / cr_t, 1.0);
					else 
						f_t  = 0.5 * tr_t / cr_t;
					
					f_t  = f_t * pkt_size;


					double tokenLimit = abc_tokenLimit * 1000; 
					//token limit  maxBdp=104000 Bytes
					//Here, 1000 is an approximated estimation of payload size
					abc_token[ifIndex] = std::min(abc_token[ifIndex]+ f_t, tokenLimit);


					m_DqPktSize[ifIndex] += pkt_size;
				}
				else if(abc_markmode == 9){
					// ABC + Consider ACK + consider queue_buildup_rate
					//Count current pkt as DqPktSize
					uint32_t pkt_size = p->GetSize();
					double t = Simulator::Now().GetTimeStep();
					double tr_t = 1.0; // target rate
					double yita = 0.95; //eta
					double delta = abc_delta; //nanoseconds
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps


					double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len
					qBuildUpRate = (qLen - m_lastQLen[ifIndex][qIndex]) / (t - m_lastUpdateQBuildRateTs[ifIndex][qIndex] + 1) * 1000000000; // Bps
					//qBuildUpRate = std::max(qBuildUpRate, 0.0);
					m_lastUpdateQBuildRateTs[ifIndex][qIndex] = t;
					m_lastQLen[ifIndex][qIndex] = qLen;


					double x_t = qLen / u_t * 1000000000 ; //queuing delay (nanoseconds)
					double d_t = abc_dt; // nanoseconds
					//tr_t = abc_eta * u_t - u_t / delta * std::max(x_t - d_t, 0.0);
					tr_t = abc_eta * u_t - u_t / delta * std::max(x_t - d_t, 0.0) - qBuildUpRate;
					tr_t = std::min(abc_eta * u_t, tr_t); // cannot exceed the link capacity
				

					double cr_t = 1.0; // dequeue rate BytesPerSecond
					double update_interval = abc_dqInterval;
					double dt = t - m_lastUpdateDqRateTs[ifIndex];
					//Due to time resolution of ns, PicoSeconds are lost.
					if(update_interval < 10)
						dt += 1;


					if (dt > update_interval){ // update dqRate per x ns
						dqRate[ifIndex][qIndex] = m_DqPktSize[ifIndex] / (dt/1000000000); // Bps
	
						m_DqPktSize[ifIndex] = 0;
						m_lastUpdateDqRateTs[ifIndex] = t;
					}
					cr_t  = dqRate[ifIndex][qIndex];
				
					double f_t; 
					if(abc_tokenMinBound)
						f_t  = std::min(0.5 * tr_t / cr_t, 1.0);
					else 
						f_t  = 0.5 * tr_t / cr_t;
					
					f_t  = f_t * pkt_size;


					double tokenLimit = abc_tokenLimit * 1000; 
					//token limit  maxBdp=104000 Bytes
					//Here, 1000 is an approximated estimation of payload size
					abc_token[ifIndex]= std::min(abc_token[ifIndex]+ f_t, tokenLimit);


					m_DqPktSize[ifIndex] += pkt_size;
				
				}
			}
		}

	}
	if (qIndex != 0){
		uint32_t inDev = t.GetFlowId();
		m_mmu->RemoveFromIngressAdmission(inDev, qIndex, p->GetSize());
		m_mmu->RemoveFromEgressAdmission(ifIndex, qIndex, p->GetSize());
		m_bytes[inDev][ifIndex][qIndex] -= p->GetSize();

		if (m_ccMode == 9){ //ABC
			uint8_t* buf = p->GetBuffer();
			if (buf[PppHeader::GetStaticSize() + 9] == 0x11){ // udp packet
				if(abc_markmode == 1){ //Vanilla ABC
					//Count current pkt as DqPktSize

					double tr_t = 1.0; // target rate
					double yita = 0.95; //eta
					double delta = abc_delta; //nanoseconds
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps
					double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len
					double x_t = qLen / u_t * 1000000000 ; //queuing delay (nanoseconds)


					double d_t = abc_dt; // nanoseconds
					tr_t = abc_eta * u_t - u_t / delta * std::max(x_t - d_t, 0.0);

				

					double cr_t = 1.0; // dequeue rate BytesPerSecond
					double t = Simulator::Now().GetTimeStep();
					double update_interval = abc_dqInterval;
					double dt = t - m_lastUpdateDqRateTs[ifIndex];
					//Due to time resolution of ns, PicoSeconds are lost.
					if(update_interval < 10)
						dt += 1;

					if (dt > update_interval){ // update dqRate per x ns
						dqRate[ifIndex][qIndex] = m_DqPktSize[ifIndex] / (dt/1000000000); // Bps
						m_DqPktSize[ifIndex] = 0;
						m_lastUpdateDqRateTs[ifIndex] = t;
					}
					cr_t  = dqRate[ifIndex][qIndex];
				
					double f_t; 
					if(abc_tokenMinBound)
						f_t  = std::min(0.5 * tr_t / cr_t, 1.0);
					else 
						f_t  = 0.5 * tr_t / cr_t;

					f_t = std::max(0.0, f_t);

					PppHeader ppp;
					Ipv4Header h;
					p->RemoveHeader(ppp);
					p->RemoveHeader(h);
					
					//printf("%ld, %ld, queueing delay %f, %f, %f %f %f %s\n", m_DqPktSize[ifIndex], t, x_t, dt, tr_t, cr_t, f_t, h.EcnTypeToString(h.GetEcn()).c_str());
					
					//printf("before %s \n ", h.EcnTypeToString(h.GetEcn()).c_str());

					double tokenLimit = abc_tokenLimit; //token limit  maxBdp=104000 Bytes
					abc_token[ifIndex]= std::min(abc_token[ifIndex] + f_t, tokenLimit);

					if (h.GetEcn() == (Ipv4Header::EcnType)0x01){ // Accel
						if (abc_token[ifIndex] > 1.0){
							abc_token[ifIndex]-= 1.0;
							//Mark Accelerate
							h.SetEcn((Ipv4Header::EcnType)0x01);  //Accelerate
						}
						else{
							//Mark brake
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
						}
					}
					p->AddHeader(h);
					p->AddHeader(ppp);
					//printf("%ld,%f,%f,%f\n",   Simulator::Now().GetTimeStep(), qLen, cr_t, abc_token);
					m_DqPktSize[ifIndex] += p->GetSize();
				}
				else if(abc_markmode == 2){//Piecewise function to mark
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double delta = abc_delta; //nanoseconds
					double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len
					//double qLen = dev->GetQueue()->GetPhantomNBytes(qIndex); // Get Queue Len
					double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps

					double kmax = (delta / 1000000000) * u_t;
					double kmin = 2100; //2pkt size
					double p_min = 0.5;

					PppHeader ppp;
					Ipv4Header h;
					p->RemoveHeader(ppp);
					p->RemoveHeader(h);
					if (qLen > kmax) { // Mark Brake
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
					}
					else if (qLen >= kmin){
						double prob = (1 - p_min) * double(qLen - kmin) / (kmax - kmin) + p_min;
						if (UniformVariable(0, 1).GetValue() < prob){ // Mark brake
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
						}
					}
					p->AddHeader(h);
					p->AddHeader(ppp);
				
				}
				else if(abc_markmode == 3 || abc_markmode == 4){ //WRED function to mark
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					
					
					double delta = abc_delta; //nanoseconds
					double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len
					double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps

					double kmax = (delta / 1000000000) * u_t;
					//(kmin+kmax)/2 = 2pkt_size
					//kmin is negative
					
					double kmin;
					if (abc_markmode == 3)
						kmin = 2100 * 2.0 - kmax; //2pkt size
					else if (abc_markmode == 4)
						kmin = 0.0;

					PppHeader ppp;
					Ipv4Header h;
					p->RemoveHeader(ppp);
					p->RemoveHeader(h);
					if (qLen > kmax) { // Mark Brake
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
					}
					else if (qLen > kmin){
						double p =  double(qLen - kmin) / (kmax - kmin) ;
						if (UniformVariable(0, 1).GetValue() < p){ // Mark brake
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
						}
					}
					p->AddHeader(h);
					p->AddHeader(ppp);
				}
				else if(abc_markmode == 5){ //Vanilla ABC + using Avg QLen
					//Count current pkt as DqPktSize

					double tr_t = 1.0; // target rate
					double yita = 0.95; //eta
					double delta = abc_delta; //nanoseconds
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps
					
					double cur_qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len

					avg_qlen = 0.9 * avg_qlen + cur_qLen * 0.1;


					double x_t = avg_qlen / u_t * 1000000000 ; //queuing delay (nanoseconds)


					double d_t = abc_dt; // nanoseconds
					tr_t = abc_eta * u_t - u_t / delta * std::max(x_t - d_t, 0.0);

				

					double cr_t = 1.0; // dequeue rate BytesPerSecond
					double t = Simulator::Now().GetTimeStep();
					double update_interval = abc_dqInterval;
					double dt = t - m_lastUpdateDqRateTs[ifIndex];
					//Due to time resolution of ns, PicoSeconds are lost.
					if(update_interval < 10)
						dt += 1;

					if (dt > update_interval){ // update dqRate per x ns
						dqRate[ifIndex][qIndex] = m_DqPktSize[ifIndex] / (dt/1000000000); // Bps
						m_DqPktSize[ifIndex] = 0;
						m_lastUpdateDqRateTs[ifIndex] = t;
					}
					cr_t  = dqRate[ifIndex][qIndex];
				
					double f_t; 
					if(abc_tokenMinBound)
						f_t  = std::min(0.5 * tr_t / cr_t, 1.0);
					else 
						f_t  = 0.5 * tr_t / cr_t;
					
				

					PppHeader ppp;
					Ipv4Header h;
					p->RemoveHeader(ppp);
					p->RemoveHeader(h);
					
					//printf("%ld, %ld, queueing delay %f, %f, %f %f %f %s\n", m_DqPktSize[ifIndex], t, x_t, dt, tr_t, cr_t, f_t, h.EcnTypeToString(h.GetEcn()).c_str());
					
					//printf("before %s \n ", h.EcnTypeToString(h.GetEcn()).c_str());

					double tokenLimit = abc_tokenLimit; 
					//token limit  maxBdp=104000 Bytes
					//Here, 1000 is an approximated estimation of payload size

					abc_token[ifIndex]= std::min(abc_token[ifIndex]+ f_t, tokenLimit);

					if (h.GetEcn() == (Ipv4Header::EcnType)0x02){ // Brake
						h.SetEcn((Ipv4Header::EcnType)0x02); //brake
					}
					else if (h.GetEcn() == (Ipv4Header::EcnType)0x01){ // Accel
						//printf("%f \n", abc_token);
						if (abc_token[ifIndex]> 1.0){
							abc_token[ifIndex]-= 1.0;
							//printf("Mark Accel,  %f %f %f \n", qLen, abc_token, f_t);
							//Mark Accelerate
							h.SetEcn((Ipv4Header::EcnType)0x01);  //Accelerate
						}
						else{
							//Mark brake
							//printf("Switch to Brake, %f %f %f \n", qLen, abc_token, f_t);
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
						}
					}
					p->AddHeader(h);
					p->AddHeader(ppp);
					//printf("%ld,%f,%f,%f\n",   Simulator::Now().GetTimeStep(), qLen, cr_t, abc_token);
					m_DqPktSize[ifIndex] += p->GetSize();
				}
				else if(abc_markmode == 6 || abc_markmode == 7){ //Vanilla ABC + Token In bytes
					//Count current pkt as DqPktSize
					uint32_t pkt_size = p->GetSize();

					double tr_t = 1.0; // target rate
					double yita = 0.95; //eta
					double delta = abc_delta; //nanoseconds
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps
					
					double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len

					double x_t = qLen / u_t * 1000000000 ; //queuing delay (nanoseconds)


					double d_t = abc_dt; // nanoseconds
					tr_t = abc_eta * u_t - u_t / delta * std::max(x_t - d_t, 0.0);

				

					double cr_t = 1.0; // dequeue rate BytesPerSecond
					double t = Simulator::Now().GetTimeStep();
					double update_interval = abc_dqInterval;
					double dt = t - m_lastUpdateDqRateTs[ifIndex];
					//Due to time resolution of ns, PicoSeconds are lost.
					if(update_interval < 10)
						dt += 1;

					if (dt > update_interval){ // update dqRate per x ns
						dqRate[ifIndex][qIndex] = m_DqPktSize[ifIndex] / (dt/1000000000); // Bps
						m_DqPktSize[ifIndex] = 0;
						m_lastUpdateDqRateTs[ifIndex] = t;
					}
					cr_t  = dqRate[ifIndex][qIndex];
				
					double f_t; 
					if(abc_tokenMinBound)
						f_t  = std::min(0.5 * tr_t / cr_t, 1.0);
					else 
						f_t  = 0.5 * tr_t / cr_t;
					f_t = std::max(0.0, f_t);
					f_t  = f_t * pkt_size;

					PppHeader ppp;
					Ipv4Header h;
					p->RemoveHeader(ppp);
					p->RemoveHeader(h);
					
					//printf("%ld, %ld, queueing delay %f, %f, %f %f %f %s\n", m_DqPktSize[ifIndex], t, x_t, dt, tr_t, cr_t, f_t, h.EcnTypeToString(h.GetEcn()).c_str());
					
					//printf("before %s \n ", h.EcnTypeToString(h.GetEcn()).c_str());

					double tokenLimit = abc_tokenLimit * 1000; 
					//token limit  maxBdp=104000 Bytes
					//Here, 1000 is an approximated estimation of payload size

					abc_token[ifIndex]= std::min(abc_token[ifIndex]+ f_t, tokenLimit);

					if (h.GetEcn() == (Ipv4Header::EcnType)0x02){ // Brake
						h.SetEcn((Ipv4Header::EcnType)0x02); //brake
					}
					else if (h.GetEcn() == (Ipv4Header::EcnType)0x01){ // Accel
						//printf("%f \n", abc_token);
						if (abc_token[ifIndex]> pkt_size){
							abc_token[ifIndex]-= pkt_size;
							//printf("Mark Accel,  %f %f %f \n", qLen, abc_token, f_t);
							//Mark Accelerate
							h.SetEcn((Ipv4Header::EcnType)0x01);  //Accelerate
						}
						else{
							//Mark brake
							//printf("Switch to Brake, %f %f %f \n", qLen, abc_token, f_t);
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
						}
					}
					p->AddHeader(h);
					p->AddHeader(ppp);
					//printf("%ld,%f,%f,%f\n",   Simulator::Now().GetTimeStep(), qLen, cr_t, abc_token);
					m_DqPktSize[ifIndex] += p->GetSize();
				}else if(abc_markmode == 8 || abc_markmode == 9){ // ABC + Queue Build up rate
					uint32_t pkt_size = p->GetSize();
					double t = Simulator::Now().GetTimeStep();
					double tr_t = 1.0; // target rate
					double yita = 0.95; //eta
					double delta = abc_delta; //nanoseconds
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps


					double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len
					// Note: plus 1 due to time resolution with ns
					qBuildUpRate = (qLen - m_lastQLen[ifIndex][qIndex]) / (t - m_lastUpdateQBuildRateTs[ifIndex][qIndex] + 1) * 1000000000; // Bps
					//qBuildUpRate = std::max(qBuildUpRate, 0.0);
					m_lastUpdateQBuildRateTs[ifIndex][qIndex] = t;
					m_lastQLen[ifIndex][qIndex] = qLen;


					double x_t = qLen / u_t * 1000000000 ; //queuing delay (nanoseconds)
					double d_t = abc_dt; // nanoseconds
					//tr_t = abc_eta * u_t - u_t / delta * std::max(x_t - d_t, 0.0);
					tr_t = abc_eta * u_t - u_t / delta * std::max(x_t - d_t, 0.0) - qBuildUpRate;
					tr_t = std::min(abc_eta * u_t, tr_t); // cannot exceed the link capacity
				

					double cr_t = 1.0; // dequeue rate BytesPerSecond
					double update_interval = abc_dqInterval;
					double dt = t - m_lastUpdateDqRateTs[ifIndex];
					//Due to time resolution of ns, PicoSeconds are lost.
					if(update_interval < 10)
						dt += 1;

					if (dt > update_interval){ // update dqRate per x ns
						dqRate[ifIndex][qIndex] = m_DqPktSize[ifIndex] / (dt/1000000000); // Bps
						m_DqPktSize[ifIndex] = 0;
						m_lastUpdateDqRateTs[ifIndex] = t;
					}
					cr_t  = dqRate[ifIndex][qIndex];
				
					double f_t; 
					if(abc_tokenMinBound)
						f_t  = std::min(0.5 * tr_t / cr_t, 1.0);
					else 
						f_t  = 0.5 * tr_t / cr_t;

					f_t  = f_t * pkt_size;
				
					PppHeader ppp;
					Ipv4Header h;
					p->RemoveHeader(ppp);
					p->RemoveHeader(h);
					
					//printf("%ld, %ld, queueing delay %f, %f, %f %f %f %s\n", m_DqPktSize[ifIndex], t, x_t, dt, tr_t, cr_t, f_t, h.EcnTypeToString(h.GetEcn()).c_str());
					
					//printf("before %s \n ", h.EcnTypeToString(h.GetEcn()).c_str());

					double tokenLimit = abc_tokenLimit * 1000; //token limit  maxBdp=104000 Bytes
					abc_token[ifIndex]= std::min(abc_token[ifIndex]+ f_t, tokenLimit);

					if (h.GetEcn() == (Ipv4Header::EcnType)0x02){ // Brake
						h.SetEcn((Ipv4Header::EcnType)0x02); //brake
					}
					else if (h.GetEcn() == (Ipv4Header::EcnType)0x01){ // Accel
						//printf("%f \n", abc_token);
						if (abc_token[ifIndex]>  pkt_size){
							abc_token[ifIndex]-=  pkt_size;
							//printf("Mark Accel,  %f %f %f \n", qLen, abc_token, f_t);
							//Mark Accelerate
							h.SetEcn((Ipv4Header::EcnType)0x01);  //Accelerate
						}
						else{
							//Mark brake
							//printf("Switch to Brake, %f %f %f \n", qLen, abc_token, f_t);
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
						}
					}
					p->AddHeader(h);
					p->AddHeader(ppp);
					//printf("%ld,%f,%f,%f\n",   Simulator::Now().GetTimeStep(), qLen, cr_t, abc_token);
					m_DqPktSize[ifIndex] += p->GetSize();
				}
				else if(abc_markmode == 10){//Piecewise function to mark + using Phantom Queue
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double delta = abc_delta; //nanoseconds
					//double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len
					double qLen = dev->GetQueue()->GetPhantomNBytes(qIndex); // Get Queue Len
					double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps

					double kmax = (delta / 1000000000) * u_t;
					double kmin = 2100; //2pkt size
					double p_min = 0.5;

					PppHeader ppp;
					Ipv4Header h;
					p->RemoveHeader(ppp);
					p->RemoveHeader(h);
					if (qLen > kmax) { // Mark Brake
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
					}
					else if (qLen > kmin){
						double p = (1 - p_min) * double(qLen - kmin) / (kmax - kmin) + p_min;
						if (UniformVariable(0, 1).GetValue() < p){ // Mark brake
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
						}
					}
					p->AddHeader(h);
					p->AddHeader(ppp);
				
				}
				else if(abc_markmode == 11){//Piecewise function to mark + Per Port Token
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double delta = abc_delta; //nanoseconds
					double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len
					// double qLen = dev->GetQueue()->GetPhantomNBytes(qIndex); // Get Queue Len
					double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps

					double kmax = (delta / 1000000000) * u_t;
					double kmin = 2100; //2pkt size
					double p_min = 0.5;

					double tokenLimit = abc_tokenLimit; 
					//token limit  maxBdp=104000 Bytes
					//Here, 1000 is an approximated estimation of payload size

					double pr_brake; 
					if (qLen > kmax) 
						pr_brake  = 1.0;
					else if (qLen > kmin)
						pr_brake = (1 - p_min) * double(qLen - kmin) / (kmax - kmin) + p_min;
					
					double f_t = 1 - pr_brake;
					abc_token[ifIndex]= std::min(abc_token[ifIndex]+ f_t, tokenLimit);
					
					PppHeader ppp;
					Ipv4Header h;
					p->RemoveHeader(ppp);
					p->RemoveHeader(h);

					if (h.GetEcn() == (Ipv4Header::EcnType)0x01){ // Accel
						if (abc_token[ifIndex] > 1.0){
							abc_token[ifIndex] -= 1.0;
							//Mark Accelerate
							h.SetEcn((Ipv4Header::EcnType)0x01);  //Accelerate
						}
						else{
							//Mark brake
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
						}
					}

					p->AddHeader(h);
					p->AddHeader(ppp);
				}
				else if(abc_markmode == 12){//Piecewise function to mark + Per Queue Token
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double delta = abc_delta; //nanoseconds
					double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len
					// double qLen = dev->GetQueue()->GetPhantomNBytes(qIndex); // Get Queue Len
					
					double u_t = dev->GetDataRate().GetBitRate() / 8 * dev->GetQueue()->GetLinkRatio(qIndex); //Link capacity Bps
					//double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps


					double kmax = (delta / 1000000000) * u_t;
					double kmin = 2100; //2pkt size
					double p_min = 0.5;

					double tokenLimit = abc_tokenLimit; 
					//token limit  maxBdp=104000 Bytes
					//Here, 1000 is an approximated estimation of payload size

					double pr_brake; 
					if (qLen > kmax) 
						pr_brake  = 1.0;
					else if (qLen > kmin)
						pr_brake = (1 - p_min) * double(qLen - kmin) / (kmax - kmin) + p_min;
					
					double f_t = 1 - pr_brake;
					abc_token_perqueue[ifIndex][qIndex] = std::min(abc_token_perqueue[ifIndex][qIndex] + f_t, tokenLimit);
					// abc_token[ifIndex]= std::min(abc_token[ifIndex]+ f_t, tokenLimit);
					
					PppHeader ppp;
					Ipv4Header h;
					p->RemoveHeader(ppp);
					p->RemoveHeader(h);

					if (h.GetEcn() == (Ipv4Header::EcnType)0x01){ // Accel
						if (abc_token_perqueue[ifIndex][qIndex] > 1.0){
							abc_token_perqueue[ifIndex][qIndex] -= 1.0;
							//Mark Accelerate
							h.SetEcn((Ipv4Header::EcnType)0x01);  //Accelerate
						}
						else{
							//Mark brake
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
						}
					}

					p->AddHeader(h);
					p->AddHeader(ppp);
				}
				else if(abc_markmode == 13){ //Vanilla ABC + Per Queue Token
					//Count current pkt as DqPktSize

					double tr_t = 1.0; // target rate
					double yita = 0.95; //eta
					double delta = abc_delta; //nanoseconds
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
					double u_t = dev->GetDataRate().GetBitRate() / 8 * dev->GetQueue()->GetLinkRatio(qIndex); //Link capacity Bps
					//double u_t = dev->GetDataRate().GetBitRate() / 8; //Link capacity Bps

					double qLen = dev->GetQueue()->GetNBytes(qIndex); // Get Queue Len
					double x_t = qLen / u_t * 1000000000 ; //queuing delay (nanoseconds)


					double d_t = abc_dt; // nanoseconds
					tr_t = abc_eta * u_t - u_t / delta * std::max(x_t - d_t, 0.0);

					double cr_t = 1.0; // dequeue rate BytesPerSecond
					double t = Simulator::Now().GetTimeStep();
					double update_interval = abc_dqInterval;
					double dt = t - m_lastUpdateDqRateTs[ifIndex];
					//Due to time resolution of ns, PicoSeconds are lost.
					if(update_interval < 10)
						dt += 1;

					if (dt > update_interval){ // update dqRate per x ns
						dqRate[ifIndex][qIndex] = m_DqPktSizePerQueue[ifIndex][qIndex] / (dt/1000000000); // Bps
						m_DqPktSizePerQueue[ifIndex][qIndex] = 0;
						m_lastUpdateDqRateTs[ifIndex] = t;
					}
					cr_t  = dqRate[ifIndex][qIndex];
				
					double f_t; 
					if(abc_tokenMinBound)
						f_t  = std::min(0.5 * tr_t / cr_t, 1.0);
					else 
						f_t  = 0.5 * tr_t / cr_t;

					f_t = std::max(0.0, f_t);

					PppHeader ppp;
					Ipv4Header h;
					p->RemoveHeader(ppp);
					p->RemoveHeader(h);
					
					double tokenLimit = abc_tokenLimit; //token limit  maxBdp=104000 Bytes

					abc_token_perqueue[ifIndex][qIndex] = std::min(abc_token_perqueue[ifIndex][qIndex] + f_t, tokenLimit);

					if (h.GetEcn() == (Ipv4Header::EcnType)0x01){ // Accel
						if (abc_token_perqueue[ifIndex][qIndex] > 1.0){
							abc_token_perqueue[ifIndex][qIndex] -= 1.0;
							//Mark Accelerate
							h.SetEcn((Ipv4Header::EcnType)0x01);  //Accelerate
						}
						else{
							//Mark brake
							h.SetEcn((Ipv4Header::EcnType)0x02); //brake
						}
					}
					p->AddHeader(h);
					p->AddHeader(ppp);
					m_DqPktSizePerQueue[ifIndex][qIndex] += p->GetSize();
				}
			}
		}
		else if (m_ecnEnabled){ //Other CC_Mode to process ECN
			bool egressCongested = m_mmu->ShouldSendCN(ifIndex, qIndex);
			if (egressCongested){
				PppHeader ppp;
				Ipv4Header h;
				p->RemoveHeader(ppp);
				p->RemoveHeader(h);
				h.SetEcn((Ipv4Header::EcnType)0x03);
				p->AddHeader(h);
				p->AddHeader(ppp);
			}
		}

		//CheckAndSendPfc(inDev, qIndex);
		CheckAndSendResume(inDev, qIndex);
	}
	if (1){
		uint8_t* buf = p->GetBuffer();
		if (buf[PppHeader::GetStaticSize() + 9] == 0x11){ // udp packet
			IntHeader *ih = (IntHeader*)&buf[PppHeader::GetStaticSize() + 20 + 8 + 6]; // ppp, ip, udp, SeqTs, INT
			Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
			if (m_ccMode == 3){ // HPCC
				if(m_hpccMode == 0)
					ih->PushHop(Simulator::Now().GetTimeStep(), m_txBytes[ifIndex], dev->GetQueue()->GetNBytes(qIndex), dev->GetDataRate().GetBitRate());
				else if(m_hpccMode ==1){
					uint64_t linkCapacity = dev->GetDataRate().GetBitRate() * dev->GetQueue()->GetLinkRatio(qIndex);
					ih->PushHop(Simulator::Now().GetTimeStep(), dev->GetQueue()->GetNBytesTX(qIndex), dev->GetQueue()->GetNBytes(qIndex), linkCapacity);
				}

			}else if (m_ccMode == 10){ // HPCC-PINT
				uint64_t t = Simulator::Now().GetTimeStep();
				uint64_t dt = t - m_lastPktTs[ifIndex];
				if (dt > m_maxRtt)
					dt = m_maxRtt;
				uint64_t B = dev->GetDataRate().GetBitRate() / 8; //Bps
				uint64_t qlen = dev->GetQueue()->GetNBytesTotal();
				double newU;

				/**************************
				 * approximate calc
				 *************************/
				int b = 20, m = 16, l = 20; // see log2apprx's paremeters
				int sft = logres_shift(b,l);
				double fct = 1<<sft; // (multiplication factor corresponding to sft)
				double log_T = log2(m_maxRtt)*fct; // log2(T)*fct
				double log_B = log2(B)*fct; // log2(B)*fct
				double log_1e9 = log2(1e9)*fct; // log2(1e9)*fct
				double qterm = 0;
				double byteTerm = 0;
				double uTerm = 0;
				if ((qlen >> 8) > 0){
					int log_dt = log2apprx(dt, b, m, l); // ~log2(dt)*fct
					int log_qlen = log2apprx(qlen >> 8, b, m, l); // ~log2(qlen / 256)*fct
					qterm = pow(2, (
								log_dt + log_qlen + log_1e9 - log_B - 2*log_T
								)/fct
							) * 256;
					// 2^((log2(dt)*fct+log2(qlen/256)*fct+log2(1e9)*fct-log2(B)*fct-2*log2(T)*fct)/fct)*256 ~= dt*qlen*1e9/(B*T^2)
				}
				if (m_lastPktSize[ifIndex] > 0){
					int byte = m_lastPktSize[ifIndex];
					int log_byte = log2apprx(byte, b, m, l);
					byteTerm = pow(2, (
								log_byte + log_1e9 - log_B - log_T
								)/fct
							);
					// 2^((log2(byte)*fct+log2(1e9)*fct-log2(B)*fct-log2(T)*fct)/fct) ~= byte*1e9 / (B*T)
				}
				if (m_maxRtt > dt && m_u[ifIndex] > 0){
					int log_T_dt = log2apprx(m_maxRtt - dt, b, m, l); // ~log2(T-dt)*fct
					int log_u = log2apprx(int(round(m_u[ifIndex] * 8192)), b, m, l); // ~log2(u*512)*fct
					uTerm = pow(2, (
								log_T_dt + log_u - log_T
								)/fct
							) / 8192;
					// 2^((log2(T-dt)*fct+log2(u*512)*fct-log2(T)*fct)/fct)/512 = (T-dt)*u/T
				}
				newU = qterm+byteTerm+uTerm;

				#if 0
				/**************************
				 * accurate calc
				 *************************/
				double weight_ewma = double(dt) / m_maxRtt;
				double u;
				if (m_lastPktSize[ifIndex] == 0)
					u = 0;
				else{
					double txRate = m_lastPktSize[ifIndex] / double(dt); // B/ns
					u = (qlen / m_maxRtt + txRate) * 1e9 / B;
				}
				newU = m_u[ifIndex] * (1 - weight_ewma) + u * weight_ewma;
				printf(" %lf\n", newU);
				#endif

				/************************
				 * update PINT header
				 ***********************/
				uint16_t power = Pint::encode_u(newU);
				if (power > ih->GetPower())
					ih->SetPower(power);

				m_u[ifIndex] = newU;
			}
		}
	}
	m_txBytes[ifIndex] += p->GetSize();
	m_lastPktSize[ifIndex] = p->GetSize();
	m_lastPktTs[ifIndex] = Simulator::Now().GetTimeStep();


	CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);

	m_txBytes_all[ifIndex] += p->GetSize();
	uint8_t* buf = p->GetBuffer();
	if (buf[PppHeader::GetStaticSize() + 9] == 0x11){ // UDP pkts
		m_txBytes_udp[ifIndex] += p->GetSize();
		m_txBytes_udp_wholeheader[ifIndex] +=  ch.GetStaticWholeHeaderSize();
		m_txBytes_udp_intheader[ifIndex] += IntHeader::GetStaticSize();
	}
	else{
		m_txBytes_ack[ifIndex] += p->GetSize();
		m_txBytes_ack_wholeheader[ifIndex] +=  ch.GetStaticWholeHeaderSize();
		m_txBytes_ack_intheader[ifIndex] += IntHeader::GetStaticSize();
	}
	
	
}

int SwitchNode::logres_shift(int b, int l){
	static int data[] = {0,0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5};
	return l - data[b];
}

int SwitchNode::log2apprx(int x, int b, int m, int l){
	int x0 = x;
	int msb = int(log2(x)) + 1;
	if (msb > m){
		x = (x >> (msb - m) << (msb - m));
		#if 0
		x += + (1 << (msb - m - 1));
		#else
		int mask = (1 << (msb-m)) - 1;
		if ((x0 & mask) > (rand() & mask))
			x += 1<<(msb-m);
		#endif
	}
	return int(log2(x) * (1<<logres_shift(b, l)));
}

} /* namespace ns3 */
