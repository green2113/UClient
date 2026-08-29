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

bool ReadHeader(const uint8_t *pData, int DataSize, EPacketType &Type, int &Offset, uint8_t *pProtocolVersion)
{
	Offset = 0;
	if(DataSize < 6)
		return false;
	const uint32_t Magic = ((uint32_t)pData[0] << 24) | ((uint32_t)pData[1] << 16) | ((uint32_t)pData[2] << 8) | (uint32_t)pData[3];
	const uint8_t WireVersion = pData[5];
	if(Magic != PROTOCOL_MAGIC || WireVersion < PROTOCOL_VERSION_MIN || WireVersion > PROTOCOL_VERSION)
		return false;
	Type = (EPacketType)pData[4];
	if(pProtocolVersion)
		*pProtocolVersion = WireVersion;
	Offset = 6;
	return true;
}

static bool ReadPeerPacketCommon(const uint8_t *pData, int DataSize, CPeerState &Out, EPacketType ExpectedType)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset, nullptr) || Type != ExpectedType)
		return false;

	int16_t ClientId = -1;
	if(!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_PlayerName) ||
		!ReadS16(pData, DataSize, Offset, ClientId))
	{
		return false;
	}

	Out.m_ClientId = ClientId;
	return Offset == DataSize;
}

bool ReadPeerStatePacket(const uint8_t *pData, int DataSize, CPeerState &Out)
{
	return ReadPeerPacketCommon(pData, DataSize, Out, PACKET_PEER_STATE);
}

bool ReadPeerRemovePacket(const uint8_t *pData, int DataSize, CPeerState &Out)
{
	return ReadPeerPacketCommon(pData, DataSize, Out, PACKET_PEER_REMOVE);
}

bool ReadPeerListPacket(const uint8_t *pData, int DataSize, CPeerList &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset, nullptr) || Type != PACKET_PEER_LIST)
		return false;

	uint16_t Count = 0;
	if(!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) || !ReadU16(pData, DataSize, Offset, Count))
		return false;

	Out.m_vPeers.clear();
	Out.m_vPeers.reserve(Count);
	for(uint16_t i = 0; i < Count; ++i)
	{
		CPeerListEntry Entry;
		int16_t ClientId = -1;
		if(!ReadS16(pData, DataSize, Offset, ClientId) || !ReadString(pData, DataSize, Offset, Entry.m_PlayerName))
			return false;
		Entry.m_ClientId = ClientId;
		Out.m_vPeers.push_back(std::move(Entry));
	}
	return Offset == DataSize;
}

void WritePeerStatePacket(std::vector<uint8_t> &vOut, EPacketType Type, const char *pServerAddress, const char *pPlayerName, int ClientId)
{
	vOut.clear();
	WriteHeader(vOut, Type);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pPlayerName);
	WriteS16(vOut, ClientId);
}

void WritePeerListPacket(std::vector<uint8_t> &vOut, const char *pServerAddress, const std::vector<CPeerListEntry> &vPeers)
{
	vOut.clear();
	WriteHeader(vOut, PACKET_PEER_LIST);
	WriteString(vOut, pServerAddress);
	WriteU16(vOut, (uint16_t)vPeers.size());
	for(const CPeerListEntry &Peer : vPeers)
	{
		WriteS16(vOut, Peer.m_ClientId);
		WriteString(vOut, Peer.m_PlayerName.c_str());
	}
}

void WriteReactionClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pServerAddress, const char *pReactorName, int ReactorClientId, int TargetClientId, uint64_t MessageHash,
	const char *pEmoji, uint8_t Action)
{
	WriteHeader(vOut, PACKET_REACTION);
	WriteString(vOut, pPlayerId);
	WriteUuid(vOut, SessionId);
	WriteUuid(vOut, Nonce);
	WriteU64(vOut, Timestamp);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pReactorName);
	WriteS16(vOut, (int16_t)ReactorClientId);
	WriteS16(vOut, (int16_t)TargetClientId);
	WriteU64(vOut, MessageHash);
	WriteString(vOut, pEmoji);
	WriteU8(vOut, Action);
}

void WriteReactionBroadcast(std::vector<uint8_t> &vOut, const char *pServerAddress, const char *pReactorName, int ReactorClientId,
	int TargetClientId, uint64_t MessageHash, const char *pEmoji, uint8_t Action)
{
	vOut.clear();
	WriteHeader(vOut, PACKET_REACTION_BROADCAST);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pReactorName);
	WriteS16(vOut, (int16_t)ReactorClientId);
	WriteS16(vOut, (int16_t)TargetClientId);
	WriteU64(vOut, MessageHash);
	WriteString(vOut, pEmoji);
	WriteU8(vOut, Action);
}

bool ReadReactionBroadcast(const uint8_t *pData, int DataSize, CReactionBroadcast &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset, nullptr) || Type != PACKET_REACTION_BROADCAST)
		return false;

	int16_t ReactorClientId = -1;
	int16_t TargetClientId = -1;
	if(!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_ReactorName) ||
		!ReadS16(pData, DataSize, Offset, ReactorClientId) ||
		!ReadS16(pData, DataSize, Offset, TargetClientId) ||
		!ReadU64(pData, DataSize, Offset, Out.m_MessageHash) ||
		!ReadString(pData, DataSize, Offset, Out.m_Emoji) ||
		!ReadU8(pData, DataSize, Offset, Out.m_Action))
	{
		return false;
	}
	Out.m_ReactorClientId = ReactorClientId;
	Out.m_TargetClientId = TargetClientId;
	return Offset == DataSize;
}

void WriteCursorClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pServerAddress, const char *pSenderName, int SenderClientId, uint8_t Active, int32_t WorldX, int32_t WorldY)
{
	WriteHeader(vOut, PACKET_CURSOR);
	WriteString(vOut, pPlayerId);
	WriteUuid(vOut, SessionId);
	WriteUuid(vOut, Nonce);
	WriteU64(vOut, Timestamp);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pSenderName);
	WriteS16(vOut, (int16_t)SenderClientId);
	WriteU8(vOut, Active);
	WriteI32(vOut, WorldX);
	WriteI32(vOut, WorldY);
}

void WriteCursorBroadcast(std::vector<uint8_t> &vOut, const char *pServerAddress, const char *pSenderName, int SenderClientId,
	uint8_t Active, int32_t WorldX, int32_t WorldY)
{
	vOut.clear();
	WriteHeader(vOut, PACKET_CURSOR_BROADCAST);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pSenderName);
	WriteS16(vOut, (int16_t)SenderClientId);
	WriteU8(vOut, Active);
	WriteI32(vOut, WorldX);
	WriteI32(vOut, WorldY);
}

bool ReadCursorBroadcast(const uint8_t *pData, int DataSize, CCursorBroadcast &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset, nullptr) || Type != PACKET_CURSOR_BROADCAST)
		return false;

	int16_t SenderClientId = -1;
	if(!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_SenderName) ||
		!ReadS16(pData, DataSize, Offset, SenderClientId) ||
		!ReadU8(pData, DataSize, Offset, Out.m_Active) ||
		!ReadI32(pData, DataSize, Offset, Out.m_WorldX) ||
		!ReadI32(pData, DataSize, Offset, Out.m_WorldY))
	{
		return false;
	}
	Out.m_SenderClientId = SenderClientId;
	return Offset == DataSize;
}

void WriteChatClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pServerAddress, const char *pSenderName, int SenderClientId, uint8_t Scope, const char *pMessage,
	const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet, CUuid MessageId)
{
	WriteHeader(vOut, PACKET_CHAT);
	WriteString(vOut, pPlayerId);
	WriteUuid(vOut, SessionId);
	WriteUuid(vOut, Nonce);
	WriteU64(vOut, Timestamp);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pSenderName);
	WriteS16(vOut, (int16_t)SenderClientId);
	WriteU8(vOut, Scope);
	WriteString(vOut, pMessage);
	WriteString(vOut, pSkinName && pSkinName[0] != '\0' ? pSkinName : "default");
	WriteU8(vOut, UseCustomColor);
	WriteI32(vOut, ColorBody);
	WriteI32(vOut, ColorFeet);
	WriteUuid(vOut, MessageId);
}

void WriteChatBroadcast(std::vector<uint8_t> &vOut, const char *pServerAddress, const char *pSenderName, int SenderClientId,
	uint8_t Scope, const char *pMessage, const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet, CUuid MessageId)
{
	vOut.clear();
	WriteHeader(vOut, PACKET_CHAT_BROADCAST);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pSenderName);
	WriteS16(vOut, (int16_t)SenderClientId);
	WriteU8(vOut, Scope);
	WriteString(vOut, pMessage);
	WriteString(vOut, pSkinName && pSkinName[0] != '\0' ? pSkinName : "default");
	WriteU8(vOut, UseCustomColor);
	WriteI32(vOut, ColorBody);
	WriteI32(vOut, ColorFeet);
	WriteUuid(vOut, MessageId);
}

bool ReadChatBroadcast(const uint8_t *pData, int DataSize, CChatBroadcast &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset, nullptr) || Type != PACKET_CHAT_BROADCAST)
		return false;

	int16_t SenderClientId = -1;
	int32_t ColorBody = 0;
	int32_t ColorFeet = 0;
	if(!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_SenderName) ||
		!ReadS16(pData, DataSize, Offset, SenderClientId) ||
		!ReadU8(pData, DataSize, Offset, Out.m_Scope) ||
		!ReadString(pData, DataSize, Offset, Out.m_Message) ||
		!ReadString(pData, DataSize, Offset, Out.m_SkinName) ||
		!ReadU8(pData, DataSize, Offset, Out.m_UseCustomColor) ||
		!ReadI32(pData, DataSize, Offset, ColorBody) ||
		!ReadI32(pData, DataSize, Offset, ColorFeet))
	{
		return false;
	}
	Out.m_SenderClientId = SenderClientId;
	Out.m_ColorBody = ColorBody;
	Out.m_ColorFeet = ColorFeet;
	// Message id is appended after the color fields. Tolerate its absence so a relay
	// or peer that predates read receipts still parses (the id stays zeroed).
	if(DataSize - Offset >= (int)sizeof(CUuid))
	{
		if(!ReadUuid(pData, DataSize, Offset, Out.m_MessageId))
			return false;
	}
	else
	{
		Out.m_MessageId = UUID_ZEROED;
	}
	return Offset == DataSize;
}

void WriteRoomChatClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pRoomId, const char *pServerAddress, const char *pSenderName, int SenderClientId, const char *pMessage,
	const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet, CUuid MessageId)
{
	WriteHeader(vOut, PACKET_ROOM_CHAT);
	WriteString(vOut, pPlayerId);
	WriteUuid(vOut, SessionId);
	WriteUuid(vOut, Nonce);
	WriteU64(vOut, Timestamp);
	WriteString(vOut, pRoomId);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pSenderName);
	WriteS16(vOut, (int16_t)SenderClientId);
	WriteString(vOut, pMessage);
	WriteString(vOut, pSkinName && pSkinName[0] != '\0' ? pSkinName : "default");
	WriteU8(vOut, UseCustomColor);
	WriteI32(vOut, ColorBody);
	WriteI32(vOut, ColorFeet);
	WriteUuid(vOut, MessageId);
}

void WriteRoomChatBroadcast(std::vector<uint8_t> &vOut, const char *pRoomId, const char *pRoomName, const char *pServerAddress,
	const char *pSenderName, int SenderClientId, const char *pMessage, const char *pSkinName, uint8_t UseCustomColor,
	int32_t ColorBody, int32_t ColorFeet, CUuid MessageId)
{
	vOut.clear();
	WriteHeader(vOut, PACKET_ROOM_CHAT_BROADCAST);
	WriteString(vOut, pRoomId);
	WriteString(vOut, pRoomName);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pSenderName);
	WriteS16(vOut, (int16_t)SenderClientId);
	WriteString(vOut, pMessage);
	WriteString(vOut, pSkinName && pSkinName[0] != '\0' ? pSkinName : "default");
	WriteU8(vOut, UseCustomColor);
	WriteI32(vOut, ColorBody);
	WriteI32(vOut, ColorFeet);
	WriteUuid(vOut, MessageId);
}

bool ReadRoomChatBroadcast(const uint8_t *pData, int DataSize, CRoomChatBroadcast &Out)
{
	int Offset = 0;
	EPacketType Type;
	int16_t SenderClientId = -1;
	if(!ReadHeader(pData, DataSize, Type, Offset, nullptr) || Type != PACKET_ROOM_CHAT_BROADCAST ||
		!ReadString(pData, DataSize, Offset, Out.m_RoomId) ||
		!ReadString(pData, DataSize, Offset, Out.m_RoomName) ||
		!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_SenderName) ||
		!ReadS16(pData, DataSize, Offset, SenderClientId) ||
		!ReadString(pData, DataSize, Offset, Out.m_Message) ||
		!ReadString(pData, DataSize, Offset, Out.m_SkinName) ||
		!ReadU8(pData, DataSize, Offset, Out.m_UseCustomColor) ||
		!ReadI32(pData, DataSize, Offset, Out.m_ColorBody) ||
		!ReadI32(pData, DataSize, Offset, Out.m_ColorFeet) ||
		!ReadUuid(pData, DataSize, Offset, Out.m_MessageId))
	{
		return false;
	}
	Out.m_SenderClientId = SenderClientId;
	return Offset == DataSize;
}

void WriteReadClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pServerAddress, const char *pReaderName, CUuid ReaderKey, CUuid MessageId)
{
	WriteHeader(vOut, PACKET_READ);
	WriteString(vOut, pPlayerId);
	WriteUuid(vOut, SessionId);
	WriteUuid(vOut, Nonce);
	WriteU64(vOut, Timestamp);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pReaderName);
	WriteUuid(vOut, ReaderKey);
	WriteUuid(vOut, MessageId);
}

void WriteReadBroadcast(std::vector<uint8_t> &vOut, const char *pServerAddress, const char *pReaderName, CUuid ReaderKey, CUuid MessageId)
{
	vOut.clear();
	WriteHeader(vOut, PACKET_READ_BROADCAST);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pReaderName);
	WriteUuid(vOut, ReaderKey);
	WriteUuid(vOut, MessageId);
}

bool ReadReadBroadcast(const uint8_t *pData, int DataSize, CReadBroadcast &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset, nullptr) || Type != PACKET_READ_BROADCAST)
		return false;

	if(!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_ReaderName) ||
		!ReadUuid(pData, DataSize, Offset, Out.m_ReaderKey) ||
		!ReadUuid(pData, DataSize, Offset, Out.m_MessageId))
	{
		return false;
	}
	return Offset == DataSize;
}

void WriteServerJoinClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pServerAddress, const char *pServerName, const char *pJoinerName, CUuid JoinerKey, uint8_t Kind, uint8_t FriendsOnly,
	const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet)
{
	WriteHeader(vOut, PACKET_SERVER_JOIN);
	WriteString(vOut, pPlayerId);
	WriteUuid(vOut, SessionId);
	WriteUuid(vOut, Nonce);
	WriteU64(vOut, Timestamp);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pServerName);
	WriteString(vOut, pJoinerName);
	WriteUuid(vOut, JoinerKey);
	WriteU8(vOut, Kind);
	WriteU8(vOut, FriendsOnly);
	WriteString(vOut, pSkinName && pSkinName[0] != '\0' ? pSkinName : "default");
	WriteU8(vOut, UseCustomColor);
	WriteI32(vOut, ColorBody);
	WriteI32(vOut, ColorFeet);
}

void WriteServerJoinBroadcast(std::vector<uint8_t> &vOut, const char *pServerAddress, const char *pServerName, const char *pJoinerName,
	CUuid JoinerKey, uint8_t Kind, uint8_t FriendsOnly, const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet)
{
	vOut.clear();
	WriteHeader(vOut, PACKET_SERVER_JOIN_BROADCAST);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pServerName);
	WriteString(vOut, pJoinerName);
	WriteUuid(vOut, JoinerKey);
	WriteU8(vOut, Kind);
	WriteU8(vOut, FriendsOnly);
	WriteString(vOut, pSkinName && pSkinName[0] != '\0' ? pSkinName : "default");
	WriteU8(vOut, UseCustomColor);
	WriteI32(vOut, ColorBody);
	WriteI32(vOut, ColorFeet);
}

bool ReadServerJoinBroadcast(const uint8_t *pData, int DataSize, CServerJoinBroadcast &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset, nullptr) || Type != PACKET_SERVER_JOIN_BROADCAST)
		return false;

	int32_t ColorBody = 0;
	int32_t ColorFeet = 0;
	if(!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_ServerName) ||
		!ReadString(pData, DataSize, Offset, Out.m_JoinerName) ||
		!ReadUuid(pData, DataSize, Offset, Out.m_JoinerKey) ||
		!ReadU8(pData, DataSize, Offset, Out.m_Kind) ||
		!ReadU8(pData, DataSize, Offset, Out.m_FriendsOnly) ||
		!ReadString(pData, DataSize, Offset, Out.m_SkinName) ||
		!ReadU8(pData, DataSize, Offset, Out.m_UseCustomColor) ||
		!ReadI32(pData, DataSize, Offset, ColorBody) ||
		!ReadI32(pData, DataSize, Offset, ColorFeet))
	{
		return false;
	}
	Out.m_ColorBody = ColorBody;
	Out.m_ColorFeet = ColorFeet;
	return Offset == DataSize;
}

void WriteRoomServerJoinClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pRoomId, const char *pServerAddress, const char *pServerName, const char *pJoinerName, CUuid JoinerKey, uint8_t Kind,
	uint8_t FriendsOnly, const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet)
{
	WriteHeader(vOut, PACKET_ROOM_SERVER_JOIN);
	WriteString(vOut, pPlayerId);
	WriteUuid(vOut, SessionId);
	WriteUuid(vOut, Nonce);
	WriteU64(vOut, Timestamp);
	WriteString(vOut, pRoomId);
	WriteString(vOut, pServerAddress);
	WriteString(vOut, pServerName);
	WriteString(vOut, pJoinerName);
	WriteUuid(vOut, JoinerKey);
	WriteU8(vOut, Kind);
	WriteU8(vOut, FriendsOnly);
	WriteString(vOut, pSkinName && pSkinName[0] != '\0' ? pSkinName : "default");
	WriteU8(vOut, UseCustomColor);
	WriteI32(vOut, ColorBody);
	WriteI32(vOut, ColorFeet);
}

bool ReadRoomServerJoinBroadcast(const uint8_t *pData, int DataSize, CRoomServerJoinBroadcast &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset, nullptr) || Type != PACKET_ROOM_SERVER_JOIN_BROADCAST ||
		!ReadString(pData, DataSize, Offset, Out.m_RoomId) ||
		!ReadString(pData, DataSize, Offset, Out.m_RoomName) ||
		!ReadString(pData, DataSize, Offset, Out.m_ServerAddress) ||
		!ReadString(pData, DataSize, Offset, Out.m_ServerName) ||
		!ReadString(pData, DataSize, Offset, Out.m_JoinerName) ||
		!ReadUuid(pData, DataSize, Offset, Out.m_JoinerKey) ||
		!ReadU8(pData, DataSize, Offset, Out.m_Kind) ||
		!ReadU8(pData, DataSize, Offset, Out.m_FriendsOnly) ||
		!ReadString(pData, DataSize, Offset, Out.m_SkinName) ||
		!ReadU8(pData, DataSize, Offset, Out.m_UseCustomColor) ||
		!ReadI32(pData, DataSize, Offset, Out.m_ColorBody) ||
		!ReadI32(pData, DataSize, Offset, Out.m_ColorFeet))
	{
		return false;
	}
	return Offset == DataSize;
}

void WriteUClientReactionClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	uint8_t Scope, const char *pRoomId, const char *pOriginalServerAddress, CUuid MessageId, const char *pReactorName,
	CUuid ReactorKey, const char *pEmoji, uint8_t Action)
{
	WriteHeader(vOut, PACKET_UCLIENT_REACTION);
	WriteString(vOut, pPlayerId);
	WriteUuid(vOut, SessionId);
	WriteUuid(vOut, Nonce);
	WriteU64(vOut, Timestamp);
	WriteU8(vOut, Scope);
	WriteString(vOut, pRoomId);
	WriteString(vOut, pOriginalServerAddress);
	WriteUuid(vOut, MessageId);
	WriteString(vOut, pReactorName);
	WriteUuid(vOut, ReactorKey);
	WriteString(vOut, pEmoji);
	WriteU8(vOut, Action);
}

bool ReadUClientReactionBroadcast(const uint8_t *pData, int DataSize, CUClientReactionBroadcast &Out)
{
	int Offset = 0;
	EPacketType Type;
	if(!ReadHeader(pData, DataSize, Type, Offset, nullptr) || Type != PACKET_UCLIENT_REACTION_BROADCAST ||
		!ReadU8(pData, DataSize, Offset, Out.m_Scope) ||
		!ReadString(pData, DataSize, Offset, Out.m_RoomId) ||
		!ReadString(pData, DataSize, Offset, Out.m_RoomName) ||
		!ReadString(pData, DataSize, Offset, Out.m_OriginalServerAddress) ||
		!ReadUuid(pData, DataSize, Offset, Out.m_MessageId) ||
		!ReadString(pData, DataSize, Offset, Out.m_ReactorName) ||
		!ReadUuid(pData, DataSize, Offset, Out.m_ReactorKey) ||
		!ReadString(pData, DataSize, Offset, Out.m_Emoji) ||
		!ReadU8(pData, DataSize, Offset, Out.m_Action))
	{
		return false;
	}
	return Offset == DataSize;
}

bool ReadClientPresencePacket(const uint8_t *pData, int DataSize, CClientPresencePacket &Out)
{
	int Offset = 0;
	EPacketType Type;
	uint8_t WireVersion = PROTOCOL_VERSION_MIN;
	if(!ReadHeader(pData, DataSize, Type, Offset, &WireVersion) ||
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
	Out.m_ClientVersion.clear();

	if(WireVersion >= 2 && !ReadString(pData, DataSize, Offset, Out.m_ClientVersion))
		return false;

	if(Out.m_Type == PACKET_SWITCH)
	{
		if(!ReadString(pData, DataSize, Offset, Out.m_FromServerAddress))
			return false;
	}

	return Offset + CLIENT_PACKET_PROOF_SIZE == DataSize;
}
}
