#ifndef SWITCH_NODE_H
#define SWITCH_NODE_H

#include <unordered_map>
#include <ns3/node.h>
#include "qbb-net-device.h"
#include "switch-mmu.h"
#include "pint.h"

namespace ns3 {

class Packet;

class SwitchNode : public Node{
	static const uint32_t pCnt = 257;	// Number of ports used
	static const uint32_t qCnt = 8;	// Number of queues/priorities used
	uint32_t m_ecmpSeed;
	std::unordered_map<uint32_t, std::vector<int> > m_rtTable; // map from ip address (u32) to possible ECMP port (index of dev)

	// monitor of PFC
	uint32_t m_bytes[pCnt][pCnt][qCnt]; // m_bytes[inDev][outDev][qidx] is the bytes from inDev enqueued for outDev at qidx
	
	uint64_t m_txBytes[pCnt]; // counter of tx bytes

	uint32_t m_lastPktSize[pCnt];
	uint64_t m_lastPktTs[pCnt]; // ns
	double m_u[pCnt];

	
	// Record to calculate dequeue rate
	uint64_t m_lastUpdateDqRateTs[pCnt]; // ns
	uint64_t m_DqPktSize[pCnt]; // packet Bytes dequeued since last update
	uint64_t m_DqPktSizePerQueue[pCnt][qCnt]; // packet Bytes dequeued since last update

	// Record to calculate queue build-up rate
	uint64_t m_lastUpdateQBuildRateTs[pCnt][qCnt]; // ns
	uint64_t m_lastQLen[pCnt][qCnt]; // qLen Bytes  of last update



protected:
	bool m_ecnEnabled;
	uint32_t m_ccMode;
	uint32_t m_hpccMode;
	uint64_t m_maxRtt;
	double abc_dt;
	double abc_delta;
	double abc_eta;
	double abc_tokenLimit;
	double qBuildUpRate = 0.0;
	int abc_dqInterval = 0;
	int abc_tokenMinBound = 0; 
	int abc_markmode = 1;

	double abc_token[pCnt];
	double abc_token_perqueue[pCnt][qCnt];

	double avg_qlen = 0; // Use to calculate ewma of qlen

	uint32_t m_ackHighPrio; // set high priority for ACK/NACK

private:
	int GetOutDev(Ptr<const Packet>, CustomHeader &ch);
	void SendToDev(Ptr<Packet>p, CustomHeader &ch);
	static uint32_t EcmpHash(const uint8_t* key, size_t len, uint32_t seed);
	void CheckAndSendPfc(uint32_t inDev, uint32_t qIndex);
	void CheckAndSendResume(uint32_t inDev, uint32_t qIndex);
public:
	Ptr<SwitchMmu> m_mmu;

	double dqRate[pCnt][qCnt]; // dequeue rate

	// counter of tx bytes
	uint64_t m_txBytes_all[pCnt]; 
	uint64_t m_txBytes_udp[pCnt]; 
	uint64_t m_txBytes_udp_wholeheader[pCnt]; 
	uint64_t m_txBytes_udp_intheader[pCnt]; 
	uint64_t m_txBytes_ack[pCnt]; 
	uint64_t m_txBytes_ack_wholeheader[pCnt]; 
	uint64_t m_txBytes_ack_intheader[pCnt]; 


	static TypeId GetTypeId (void);
	SwitchNode();
	void SetEcmpSeed(uint32_t seed);
	void AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx);
	void ClearTable();
	bool SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader &ch);
	void SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);

	// for approximate calc in PINT
	int logres_shift(int b, int l);
	int log2apprx(int x, int b, int m, int l); // given x of at most b bits, use most significant m bits of x, calc the result in l bits
};

} /* namespace ns3 */

#endif /* SWITCH_NODE_H */
