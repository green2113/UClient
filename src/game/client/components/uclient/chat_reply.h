#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_CHAT_REPLY_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_CHAT_REPLY_H

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

namespace CUClientChatReply
{
constexpr int CHAT_LINE_LENGTH = 256;
constexpr int PREVIEW_MAX_CHARS = 40;
constexpr int WIRE_PREVIEW_MAX_CHARS = 16;
constexpr int WIRE_PREVIEW_EDGE_CHARS = 2;

struct SReplyMeta
{
	bool m_Valid = false;
	int m_ReplyToClientId = -1;
	int m_ReplyMessageIndex = 0;
	char m_aReplyToName[64] = "";
	char m_aReplyPreview[64] = "";
};

bool IsReplyFeatureEnabled();

void SanitizeField(char *pBuffer, int Size);
void BuildPreviewFromText(const char *pText, char *pPreview, int PreviewSize);
void BuildWirePreviewFromText(const char *pText, char *pPreview, int PreviewSize);
bool TextMatchesWirePreview(const char *pWirePreview, const char *pFullText);
bool EncodeReply(const char *pName, int ReplyMessageIndex, const char *pBody, char *pOut, int OutSize);
bool TryParseReply(const char *pLine, SReplyMeta &OutMeta, char *pDisplayBody, int DisplayBodySize);
void BuildReplyBodyPrefix(const char *pName, char *pOut, int OutSize);
}

#endif
