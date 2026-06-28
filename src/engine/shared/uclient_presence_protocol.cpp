#include "uclient_presence_protocol.h"

namespace UClientPresence
{
void WriteHeader(std::vector<uint8_t> &vOut, EPacketType Type)
{
	vOut.push_back((uint8_t)((PROTOCOL_MAGIC >> 24) & 0xff));
	vOut.push_back((uint8_t)((PROTOCOL_MAGIC >> 16) & 0xff));
	vOut.push_back((uint8_t)((PROTOCOL_MAGIC >> 8) & 0xff));
	vOut.push_back((uint8_t)(PROTOCOL_MAGIC & 0xff));
	BestClientIndicator::WriteU8(vOut, (uint8_t)Type);
	BestClientIndicator::WriteU8(vOut, PROTOCOL_VERSION);
}

bool ReadHeader(const uint8_t *pData, int DataSize, EPacketType &Type, int &Offset)
{
	Offset = 0;
	if(DataSize < 6)
		return false;
	const uint32_t Magic = ((uint32_t)pData[0] << 24) | ((uint32_t)pData[1] << 16) | ((uint32_t)pData[2] << 8) | (uint32_t)pData[3];
	if(Magic != PROTOCOL_MAGIC || pData[5] != PROTOCOL_VERSION)
		return false;
	Type = (EPacketType)pData[4];
	Offset = 6;
	return true;
}

bool ReadClientPresencePacket(const uint8_t *pData, int DataSize, CClientPresencePacket &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset) ||
		(Type != PACKET_JOIN && Type != PACKET_HEARTBEAT && Type != PACKET_LEAVE && Type != PACKET_SWITCH))
	{
		return false;
	}

	int16_t ClientId = -1;
	if(!ReadString(pData, DataSize, Offset, Out.m_PlayerId) ||
		!ReadUuid(pData, DataSize, Offset, Out.m_SessionId) ||
		!ReadUuid(pData, DataSize, Offset, Out.m_Nonce) ||
		!ReadU64(pData, DataSize, Offset, Out.m_Timestamp) ||
		!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_PlayerName) ||
		!ReadS16(pData, DataSize, Offset, ClientId))
	{
		return false;
	}

	Out.m_Type = Type;
	Out.m_ClientId = ClientId;

	if(Out.m_Type == PACKET_SWITCH)
	{
		if(!ReadString(pData, DataSize, Offset, Out.m_FromServerAddress))
			return false;
	}

	return Offset + CLIENT_PACKET_PROOF_SIZE == DataSize;
}
}
