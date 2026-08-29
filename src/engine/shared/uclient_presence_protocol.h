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
// Keep at 2: chat uses new packet types (12/13), not a wire-format bump.
// Bumping the version would break presence against older relays that only accept v1/v2.
constexpr uint8_t PROTOCOL_VERSION = 2;
constexpr uint8_t PROTOCOL_VERSION_MIN = 1;
constexpr int DEFAULT_PORT = 8778;
constexpr int CLIENT_PACKET_PROOF_SIZE = BestClientIndicator::CLIENT_PACKET_PROOF_SIZE;
constexpr int CHAT_MESSAGE_MAX_BYTES = 512;

enum EPacketType : uint8_t
{
	PACKET_JOIN = 1,
	PACKET_HEARTBEAT = 2,
	PACKET_LEAVE = 3,
	PACKET_SWITCH = 4,
	PACKET_PEER_STATE = 5,
	PACKET_PEER_REMOVE = 6,
	PACKET_PEER_LIST = 7,
	// Chat reactions (Discord-like emoji reactions on chat messages).
	PACKET_REACTION = 8, // Client -> Server (proof protected)
	PACKET_REACTION_BROADCAST = 9, // Server -> Client (relayed to peers on the same game server)
	// Live cursor sharing (broadcast the local aim cursor while a key is held).
	PACKET_CURSOR = 10, // Client -> Server (proof protected, no nonce replay tracking)
	PACKET_CURSOR_BROADCAST = 11, // Server -> Client (relayed to peers on the same game server)
	// Cross-server UClient chat channel.
	PACKET_CHAT = 12, // Client -> Server (proof protected)
	PACKET_CHAT_BROADCAST = 13, // Server -> Client
	// Read receipts for UClient chat (KakaoTalk-style "read up to here" markers).
	PACKET_READ = 14, // Client -> Server (proof protected)
	PACKET_READ_BROADCAST = 15, // Server -> Client (relayed globally)
	// Server join/leave announcements ("<name> joined <server>" / "<name> left the server.")
	// shown to all UClient users. The join/leave distinction is a field, not a packet type.
	PACKET_SERVER_JOIN = 16, // Client -> Server (proof protected)
	PACKET_SERVER_JOIN_BROADCAST = 17, // Server -> Client (relayed globally)
	// Server-authoritative chat rooms.
	PACKET_ROOM_CHAT = 18, // Client -> Server (proof protected)
	PACKET_ROOM_CHAT_BROADCAST = 19, // Server -> Client
	PACKET_ROOM_SERVER_JOIN = 20, // Client -> Server (proof protected)
	PACKET_ROOM_SERVER_JOIN_BROADCAST = 21, // Server -> Client
	PACKET_UCLIENT_REACTION = 22, // Client -> Server (proof protected)
	PACKET_UCLIENT_REACTION_BROADCAST = 23, // Server -> Client
};

enum EServerPresenceKind : uint8_t
{
	SERVER_PRESENCE_JOIN = 0,
	SERVER_PRESENCE_LEAVE = 1,
	// Switched straight from one server to another; announced once instead of leave + join.
	SERVER_PRESENCE_MOVE = 2,
};

enum EChatScope : uint8_t
{
	CHAT_SCOPE_SAME_SERVER = 0,
	CHAT_SCOPE_GLOBAL = 1,
	// Relayed globally, but receivers only display it when the sender is in their friend list.
	// No friend list is ever transmitted; the check always runs against the receiver's own list.
	CHAT_SCOPE_FRIENDS = 2,
};

enum EReactionAction : uint8_t
{
	REACTION_REMOVE = 0,
	REACTION_ADD = 1,
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
	std::string m_ClientVersion;
	std::string m_FromServerAddress;
};

struct CPeerState
{
	std::string m_ServerAddress;
	std::string m_PlayerName;
	int m_ClientId = -1;
};

struct CPeerListEntry
{
	int m_ClientId = -1;
	std::string m_PlayerName;
};

struct CPeerList
{
	std::string m_ServerAddress;
	std::vector<CPeerListEntry> m_vPeers;
};

// Reaction as received by peers (server -> client broadcast).
struct CReactionBroadcast
{
	std::string m_ServerAddress;
	std::string m_ReactorName;
	int m_ReactorClientId = -1;
	int m_TargetClientId = -1; // client id of the message author
	uint64_t m_MessageHash = 0; // hash of the reacted-to message text
	std::string m_Emoji;
	uint8_t m_Action = REACTION_ADD;
};

// Live cursor as received by peers (server -> client broadcast).
struct CCursorBroadcast
{
	std::string m_ServerAddress;
	std::string m_SenderName;
	int m_SenderClientId = -1;
	uint8_t m_Active = 1; // 1 = show cursor, 0 = hide (key released)
	int32_t m_WorldX = 0; // world pixel coordinates of the aim cursor
	int32_t m_WorldY = 0;
};

// UClient chat as received by peers (server -> client broadcast).
struct CChatBroadcast
{
	std::string m_ServerAddress;
	std::string m_SenderName;
	int m_SenderClientId = -1;
	uint8_t m_Scope = CHAT_SCOPE_GLOBAL;
	std::string m_Message;
	// Optional 0.6 skin snapshot so remote-server senders can still show a tee in chat.
	std::string m_SkinName;
	uint8_t m_UseCustomColor = 0;
	int32_t m_ColorBody = 0;
	int32_t m_ColorFeet = 0;
	// Globally unique message id so read receipts can target this exact message
	// even across servers (client ids are only unique per game server).
	CUuid m_MessageId = UUID_ZEROED;
};

struct CRoomChatBroadcast
{
	std::string m_RoomId;
	std::string m_RoomName;
	std::string m_ServerAddress;
	std::string m_SenderName;
	int m_SenderClientId = -1;
	std::string m_Message;
	std::string m_SkinName;
	uint8_t m_UseCustomColor = 0;
	int32_t m_ColorBody = 0;
	int32_t m_ColorFeet = 0;
	CUuid m_MessageId = UUID_ZEROED;
};

// UClient chat read receipt as received by peers (server -> client broadcast).
struct CReadBroadcast
{
	std::string m_ServerAddress;
	std::string m_ReaderName;
	CUuid m_ReaderKey = UUID_ZEROED; // stable per-reader identity (client instance uuid)
	CUuid m_MessageId = UUID_ZEROED; // last message the reader has read up to
};

// Server join/leave announcement as received by peers (server -> client broadcast).
struct CServerJoinBroadcast
{
	std::string m_ServerAddress; // ip:port that was joined/left (used to connect on click)
	std::string m_ServerName; // friendly server name (falls back to address on the sender)
	std::string m_JoinerName; // display name of the UClient user who joined/left
	CUuid m_JoinerKey = UUID_ZEROED; // joiner's client instance uuid (for self-suppression)
	uint8_t m_Kind = SERVER_PRESENCE_JOIN;
	// When set, receivers only display the announcement if the joiner is in their friend list.
	uint8_t m_FriendsOnly = 0;
	// 0.6 skin snapshot so the announcement can render the sender's tee like a chat line.
	std::string m_SkinName;
	uint8_t m_UseCustomColor = 0;
	int32_t m_ColorBody = 0;
	int32_t m_ColorFeet = 0;
};

struct CRoomServerJoinBroadcast
{
	std::string m_RoomId;
	std::string m_RoomName;
	std::string m_ServerAddress;
	std::string m_ServerName;
	std::string m_JoinerName;
	CUuid m_JoinerKey = UUID_ZEROED;
	uint8_t m_Kind = SERVER_PRESENCE_JOIN;
	uint8_t m_FriendsOnly = 0;
	std::string m_SkinName;
	uint8_t m_UseCustomColor = 0;
	int32_t m_ColorBody = 0;
	int32_t m_ColorFeet = 0;
};

struct CUClientReactionBroadcast
{
	uint8_t m_Scope = CHAT_SCOPE_GLOBAL;
	std::string m_RoomId;
	std::string m_RoomName;
	std::string m_OriginalServerAddress;
	CUuid m_MessageId = UUID_ZEROED;
	std::string m_ReactorName;
	CUuid m_ReactorKey = UUID_ZEROED;
	std::string m_Emoji;
	uint8_t m_Action = REACTION_ADD;
};

using BestClientIndicator::AppendProof;
using BestClientIndicator::ComputeProof;
using BestClientIndicator::ParseAddress;
using BestClientIndicator::ReadI32;
using BestClientIndicator::ReadS16;
using BestClientIndicator::ReadString;
using BestClientIndicator::ReadU16;
using BestClientIndicator::ReadU64;
using BestClientIndicator::ReadU8;
using BestClientIndicator::ReadUuid;
using BestClientIndicator::ValidateProof;
using BestClientIndicator::WriteI32;
using BestClientIndicator::WriteS16;
using BestClientIndicator::WriteString;
using BestClientIndicator::WriteU16;
using BestClientIndicator::WriteU64;
using BestClientIndicator::WriteU8;
using BestClientIndicator::WriteUuid;

void WriteHeader(std::vector<uint8_t> &vOut, EPacketType Type);
bool ReadHeader(const uint8_t *pData, int DataSize, EPacketType &Type, int &Offset, uint8_t *pProtocolVersion);
bool ReadClientPresencePacket(const uint8_t *pData, int DataSize, CClientPresencePacket &Out);

bool ReadPeerStatePacket(const uint8_t *pData, int DataSize, CPeerState &Out);
bool ReadPeerRemovePacket(const uint8_t *pData, int DataSize, CPeerState &Out);
bool ReadPeerListPacket(const uint8_t *pData, int DataSize, CPeerList &Out);

void WritePeerStatePacket(std::vector<uint8_t> &vOut, EPacketType Type, const char *pServerAddress, const char *pPlayerName, int ClientId);
void WritePeerListPacket(std::vector<uint8_t> &vOut, const char *pServerAddress, const std::vector<CPeerListEntry> &vPeers);

// Reaction packets. The client->server body (everything the proof covers) is written by
// WriteReactionClientBody; the caller appends the proof afterwards. The server->client
// broadcast is written by WriteReactionBroadcast and parsed with ReadReactionBroadcast.
void WriteReactionClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pServerAddress, const char *pReactorName, int ReactorClientId, int TargetClientId, uint64_t MessageHash,
	const char *pEmoji, uint8_t Action);
void WriteReactionBroadcast(std::vector<uint8_t> &vOut, const char *pServerAddress, const char *pReactorName, int ReactorClientId,
	int TargetClientId, uint64_t MessageHash, const char *pEmoji, uint8_t Action);
bool ReadReactionBroadcast(const uint8_t *pData, int DataSize, CReactionBroadcast &Out);

// Live cursor packets. The client->server body (everything the proof covers) is written by
// WriteCursorClientBody; the caller appends the proof afterwards. The server->client
// broadcast is written by WriteCursorBroadcast and parsed with ReadCursorBroadcast.
void WriteCursorClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pServerAddress, const char *pSenderName, int SenderClientId, uint8_t Active, int32_t WorldX, int32_t WorldY);
void WriteCursorBroadcast(std::vector<uint8_t> &vOut, const char *pServerAddress, const char *pSenderName, int SenderClientId,
	uint8_t Active, int32_t WorldX, int32_t WorldY);
bool ReadCursorBroadcast(const uint8_t *pData, int DataSize, CCursorBroadcast &Out);

void WriteChatClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pServerAddress, const char *pSenderName, int SenderClientId, uint8_t Scope, const char *pMessage,
	const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet, CUuid MessageId);
void WriteChatBroadcast(std::vector<uint8_t> &vOut, const char *pServerAddress, const char *pSenderName, int SenderClientId,
	uint8_t Scope, const char *pMessage, const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet, CUuid MessageId);
bool ReadChatBroadcast(const uint8_t *pData, int DataSize, CChatBroadcast &Out);

void WriteRoomChatClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pRoomId, const char *pServerAddress, const char *pSenderName, int SenderClientId, const char *pMessage,
	const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet, CUuid MessageId);
void WriteRoomChatBroadcast(std::vector<uint8_t> &vOut, const char *pRoomId, const char *pRoomName, const char *pServerAddress,
	const char *pSenderName, int SenderClientId, const char *pMessage, const char *pSkinName, uint8_t UseCustomColor,
	int32_t ColorBody, int32_t ColorFeet, CUuid MessageId);
bool ReadRoomChatBroadcast(const uint8_t *pData, int DataSize, CRoomChatBroadcast &Out);

// Read receipt packets. The client->server body (proof covered) is written by
// WriteReadClientBody; the caller appends the proof afterwards. The server->client
// broadcast is written by WriteReadBroadcast and parsed with ReadReadBroadcast.
void WriteReadClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pServerAddress, const char *pReaderName, CUuid ReaderKey, CUuid MessageId);
void WriteReadBroadcast(std::vector<uint8_t> &vOut, const char *pServerAddress, const char *pReaderName, CUuid ReaderKey, CUuid MessageId);
bool ReadReadBroadcast(const uint8_t *pData, int DataSize, CReadBroadcast &Out);

// Server join/leave announcement packets. The client->server body (proof covered) is written by
// WriteServerJoinClientBody; the caller appends the proof afterwards. The server->client
// broadcast is written by WriteServerJoinBroadcast and parsed with ReadServerJoinBroadcast.
void WriteServerJoinClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pServerAddress, const char *pServerName, const char *pJoinerName, CUuid JoinerKey, uint8_t Kind, uint8_t FriendsOnly,
	const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet);
void WriteServerJoinBroadcast(std::vector<uint8_t> &vOut, const char *pServerAddress, const char *pServerName, const char *pJoinerName,
	CUuid JoinerKey, uint8_t Kind, uint8_t FriendsOnly, const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet);
bool ReadServerJoinBroadcast(const uint8_t *pData, int DataSize, CServerJoinBroadcast &Out);

void WriteRoomServerJoinClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	const char *pRoomId, const char *pServerAddress, const char *pServerName, const char *pJoinerName, CUuid JoinerKey, uint8_t Kind,
	uint8_t FriendsOnly, const char *pSkinName, uint8_t UseCustomColor, int32_t ColorBody, int32_t ColorFeet);
bool ReadRoomServerJoinBroadcast(const uint8_t *pData, int DataSize, CRoomServerJoinBroadcast &Out);

void WriteUClientReactionClientBody(std::vector<uint8_t> &vOut, const char *pPlayerId, CUuid SessionId, CUuid Nonce, uint64_t Timestamp,
	uint8_t Scope, const char *pRoomId, const char *pOriginalServerAddress, CUuid MessageId, const char *pReactorName,
	CUuid ReactorKey, const char *pEmoji, uint8_t Action);
bool ReadUClientReactionBroadcast(const uint8_t *pData, int DataSize, CUClientReactionBroadcast &Out);
}

#endif
