#ifndef ENGINE_SHARED_UCLIENT_PRESENCE_PROTOCOL_H
#define ENGINE_SHARED_UCLIENT_PRESENCE_PROTOCOL_H

#include <base/hash.h>
#include <base/system.h>

#include <engine/shared/bestclient_indicator_protocol.h>
#include <engine/shared/uuid_manager.h>

#include <cstdint>
#include <string>
#include <vector>

namespace UClientPresence
{
constexpr uint32_t PROTOCOL_MAGIC = 0x55435031u; // UCP1
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr int DEFAULT_PORT = 8778;
constexpr int CLIENT_PACKET_PROOF_SIZE = BestClientIndicator::CLIENT_PACKET_PROOF_SIZE;

enum EPacketType : uint8_t
{
	PACKET_JOIN = 1,
	PACKET_HEARTBEAT = 2,
	PACKET_LEAVE = 3,
	PACKET_SWITCH = 4,
};

struct CClientPresencePacket
{
	EPacketType m_Type = PACKET_JOIN;
	std::string m_PlayerId;
	CUuid m_SessionId = UUID_ZEROED;
	CUuid m_Nonce = UUID_ZEROED;
	uint64_t m_Timestamp = 0;
	std::string m_ServerAddress;
	std::string m_PlayerName;
	int m_ClientId = -1;
	std::string m_FromServerAddress;
};

using BestClientIndicator::AppendProof;
using BestClientIndicator::ComputeProof;
using BestClientIndicator::ParseAddress;
using BestClientIndicator::ReadS16;
using BestClientIndicator::ReadString;
using BestClientIndicator::ReadU64;
using BestClientIndicator::ReadUuid;
using BestClientIndicator::ValidateProof;
using BestClientIndicator::WriteS16;
using BestClientIndicator::WriteString;
using BestClientIndicator::WriteU64;
using BestClientIndicator::WriteUuid;

void WriteHeader(std::vector<uint8_t> &vOut, EPacketType Type);
bool ReadHeader(const uint8_t *pData, int DataSize, EPacketType &Type, int &Offset);
bool ReadClientPresencePacket(const uint8_t *pData, int DataSize, CClientPresencePacket &Out);
}

#endif
