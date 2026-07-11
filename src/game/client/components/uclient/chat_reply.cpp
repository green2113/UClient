#include "chat_reply.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <algorithm>
#include <cstdio>

namespace CUClientChatReply
{
namespace
{
constexpr char LEGACY_FIELD_SEP = '\x1F';
constexpr char LEGACY_RECORD_SEP = '\x1E';
constexpr char PREFIX[] = "UCR";
constexpr char WIRE_PREFIX[] = "[UCR:";

void CopyField(char *pOut, int OutSize, const char *pIn)
{
	if(!pOut || OutSize <= 0)
		return;
	if(!pIn)
	{
		pOut[0] = '\0';
		return;
	}
	str_copy(pOut, pIn, OutSize);
	SanitizeField(pOut, OutSize);
}

bool FieldContainsWireSeparator(const char *pText)
{
	if(!pText)
		return false;
	for(const char *p = pText; *p; ++p)
	{
		if(*p == ':' || *p == ']' || *p == '[' || *p == LEGACY_FIELD_SEP || *p == LEGACY_RECORD_SEP)
			return true;
	}
	return false;
}

bool IsDigitsOnlyField(const char *pText)
{
	if(!pText || pText[0] == '\0')
		return false;
	for(const char *p = pText; *p; ++p)
	{
		if(*p < '0' || *p > '9')
			return false;
	}
	return true;
}

bool TryParseLegacyReply(const char *pLine, SReplyMeta &OutMeta, char *pDisplayBody, int DisplayBodySize)
{
	if(!pLine || pLine[0] != LEGACY_RECORD_SEP)
		return false;

	const char *p = pLine + 1;
	if(strncmp(p, PREFIX, str_length(PREFIX)) != 0)
		return false;
	p += str_length(PREFIX);
	if(*p != LEGACY_RECORD_SEP)
		return false;
	++p;

	char aClientId[16];
	int ClientIdLength = 0;
	while(p[ClientIdLength] != '\0' && p[ClientIdLength] != LEGACY_FIELD_SEP && ClientIdLength + 1 < (int)sizeof(aClientId))
	{
		aClientId[ClientIdLength] = p[ClientIdLength];
		++ClientIdLength;
	}
	aClientId[ClientIdLength] = '\0';
	if(ClientIdLength == 0 || p[ClientIdLength] != LEGACY_FIELD_SEP)
		return false;
	p += ClientIdLength + 1;

	const char *pNameStart = p;
	const char *pNameEnd = str_find(pNameStart, "\x1F");
	if(!pNameEnd)
		return false;
	const char *pPreviewStart = pNameEnd + 1;
	const char *pPreviewEnd = str_find(pPreviewStart, "\x1E");
	if(!pPreviewEnd)
		return false;
	const char *pBodyStart = pPreviewEnd + 1;

	str_truncate(OutMeta.m_aReplyToName, sizeof(OutMeta.m_aReplyToName), pNameStart, pNameEnd - pNameStart);
	str_truncate(OutMeta.m_aReplyPreview, sizeof(OutMeta.m_aReplyPreview), pPreviewStart, pPreviewEnd - pPreviewStart);
	OutMeta.m_ReplyToClientId = str_toint(aClientId);
	if(OutMeta.m_ReplyToClientId < 0 || OutMeta.m_ReplyToClientId >= MAX_CLIENTS)
		return false;

	if(pDisplayBody && DisplayBodySize > 0)
		str_copy(pDisplayBody, pBodyStart, DisplayBodySize);

	OutMeta.m_Valid = true;
	return true;
}

bool TryParseDegradedReply(const char *pLine, SReplyMeta &OutMeta, char *pDisplayBody, int DisplayBodySize)
{
	if(!pLine || !str_startswith(pLine, "UCR "))
		return false;

	const char *p = pLine + 4;
	while(*p == ' ')
		++p;

	char aClientId[16];
	int ClientIdLength = 0;
	while(p[ClientIdLength] >= '0' && p[ClientIdLength] <= '9' && ClientIdLength + 1 < (int)sizeof(aClientId))
	{
		aClientId[ClientIdLength] = p[ClientIdLength];
		++ClientIdLength;
	}
	aClientId[ClientIdLength] = '\0';
	if(ClientIdLength == 0)
		return false;
	p += ClientIdLength;
	if(*p != ' ')
		return false;
	++p;

	const char *pNameStart = p;
	const char *pNameEnd = str_find(pNameStart, " ");
	if(!pNameEnd || pNameEnd == pNameStart)
		return false;
	const char *pPreviewStart = pNameEnd + 1;
	const char *pPreviewEnd = str_find(pPreviewStart, " ");
	if(!pPreviewEnd)
		pPreviewEnd = pPreviewStart + str_length(pPreviewStart);
	const char *pBodyStart = pPreviewEnd;
	if(*pBodyStart == ' ')
		++pBodyStart;

	str_truncate(OutMeta.m_aReplyToName, sizeof(OutMeta.m_aReplyToName), pNameStart, pNameEnd - pNameStart);
	if(pPreviewEnd > pPreviewStart)
		str_truncate(OutMeta.m_aReplyPreview, sizeof(OutMeta.m_aReplyPreview), pPreviewStart, pPreviewEnd - pPreviewStart);
	else
		OutMeta.m_aReplyPreview[0] = '\0';
	OutMeta.m_ReplyToClientId = str_toint(aClientId);
	if(OutMeta.m_ReplyToClientId < 0 || OutMeta.m_ReplyToClientId >= MAX_CLIENTS)
		return false;

	if(pDisplayBody && DisplayBodySize > 0)
		str_copy(pDisplayBody, pBodyStart, DisplayBodySize);

	OutMeta.m_Valid = true;
	return true;
}

bool TryParseWireReply(const char *pLine, SReplyMeta &OutMeta, char *pDisplayBody, int DisplayBodySize)
{
	if(!pLine || !str_startswith(pLine, WIRE_PREFIX))
		return false;

	const char *pMetaStart = pLine + str_length(WIRE_PREFIX);
	const char *pMetaEnd = str_find(pMetaStart, "]");
	if(!pMetaEnd)
		return false;

	const char *pBodyStart = pMetaEnd + 1;

	char aMeta[192];
	str_truncate(aMeta, sizeof(aMeta), pMetaStart, pMetaEnd - pMetaStart);

	char *apParts[3];
	int PartCount = 0;
	char *pWrite = aMeta;
	while(PartCount < 3)
	{
		char *pPart = pWrite;
		char *pSep = (char *)str_find(pWrite, ":");
		if(!pSep)
		{
			apParts[PartCount++] = pPart;
			break;
		}
		*pSep = '\0';
		apParts[PartCount++] = pPart;
		pWrite = pSep + 1;
	}

	if(PartCount < 3)
		return false;

	if(str_comp(apParts[0], "-") == 0)
		OutMeta.m_ReplyToClientId = -1;
	else
	{
		OutMeta.m_ReplyToClientId = str_toint(apParts[0]);
		if(OutMeta.m_ReplyToClientId < 0 || OutMeta.m_ReplyToClientId >= MAX_CLIENTS)
			return false;
	}

	str_copy(OutMeta.m_aReplyToName, apParts[1], sizeof(OutMeta.m_aReplyToName));
	if(IsDigitsOnlyField(apParts[2]))
	{
		OutMeta.m_ReplyMessageIndex = str_toint(apParts[2]);
		if(OutMeta.m_ReplyMessageIndex <= 0)
			return false;
		OutMeta.m_aReplyPreview[0] = '\0';
	}
	else
	{
		OutMeta.m_ReplyMessageIndex = 0;
		str_copy(OutMeta.m_aReplyPreview, apParts[2], sizeof(OutMeta.m_aReplyPreview));
	}

	if(pDisplayBody && DisplayBodySize > 0)
		str_copy(pDisplayBody, pBodyStart, DisplayBodySize);

	OutMeta.m_Valid = true;
	return true;
}
}

bool IsReplyFeatureEnabled()
{
	return g_Config.m_UcChatReply != 0 && g_Config.m_UcInstallUuid[0] != '\0';
}

void SanitizeField(char *pBuffer, int Size)
{
	if(!pBuffer || Size <= 0)
		return;
	for(int i = 0; pBuffer[i] != '\0'; ++i)
	{
		if(pBuffer[i] == ':' || pBuffer[i] == ']' || pBuffer[i] == '[' ||
			pBuffer[i] == LEGACY_FIELD_SEP || pBuffer[i] == LEGACY_RECORD_SEP ||
			pBuffer[i] == '\n' || pBuffer[i] == '\r')
			pBuffer[i] = ' ';
	}
}

void BuildPreviewFromText(const char *pText, char *pPreview, int PreviewSize)
{
	if(!pPreview || PreviewSize <= 0)
		return;
	pPreview[0] = '\0';
	if(!pText || pText[0] == '\0')
		return;

	char aNormalized[CHAT_LINE_LENGTH];
	str_copy(aNormalized, pText, sizeof(aNormalized));
	for(char *p = aNormalized; *p; ++p)
	{
		if(*p == '\n' || *p == '\r')
			*p = ' ';
	}

	int CharCount = 0;
	int ByteLen = 0;
	const char *p = aNormalized;
	while(*p && CharCount < PREVIEW_MAX_CHARS)
	{
		str_utf8_decode(&p);
		ByteLen = (int)(p - aNormalized);
		++CharCount;
	}
	aNormalized[ByteLen] = '\0';
	str_copy(pPreview, aNormalized, PreviewSize);
	SanitizeField(pPreview, PreviewSize);
}

static int CountUtf8Chars(const char *pText, int *pByteLen = nullptr)
{
	int CharCount = 0;
	int ByteLen = 0;
	if(!pText)
	{
		if(pByteLen)
			*pByteLen = 0;
		return 0;
	}

	const char *p = pText;
	while(*p)
	{
		str_utf8_decode(&p);
		ByteLen = (int)(p - pText);
		++CharCount;
	}
	if(pByteLen)
		*pByteLen = ByteLen;
	return CharCount;
}

static void CopyUtf8CharRange(const char *pText, int StartChar, int EndChar, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(!pText || StartChar >= EndChar)
		return;

	const char *p = pText;
	int CharIndex = 0;
	int StartByte = 0;
	int EndByte = 0;
	while(*p)
	{
		const char *pBefore = p;
		str_utf8_decode(&p);
		if(CharIndex == StartChar)
			StartByte = (int)(pBefore - pText);
		++CharIndex;
		if(CharIndex == EndChar)
		{
			EndByte = (int)(p - pText);
			break;
		}
	}
	if(EndByte <= StartByte)
		return;
	str_truncate(pOut, OutSize, pText + StartByte, EndByte - StartByte);
}

void BuildWirePreviewFromText(const char *pText, char *pPreview, int PreviewSize)
{
	if(!pPreview || PreviewSize <= 0)
		return;
	pPreview[0] = '\0';
	if(!pText || pText[0] == '\0')
		return;

	char aNormalized[CHAT_LINE_LENGTH];
	str_copy(aNormalized, pText, sizeof(aNormalized));
	for(char *p = aNormalized; *p; ++p)
	{
		if(*p == '\n' || *p == '\r')
			*p = ' ';
	}

	int ByteLen = 0;
	const int CharCount = CountUtf8Chars(aNormalized, &ByteLen);
	aNormalized[ByteLen] = '\0';

	if(CharCount <= WIRE_PREVIEW_MAX_CHARS)
	{
		str_copy(pPreview, aNormalized, PreviewSize);
		SanitizeField(pPreview, PreviewSize);
		return;
	}

	const int EdgeChars = minimum(WIRE_PREVIEW_EDGE_CHARS, CharCount / 3);
	char aStart[32];
	char aEnd[32];
	CopyUtf8CharRange(aNormalized, 0, EdgeChars, aStart, sizeof(aStart));
	CopyUtf8CharRange(aNormalized, CharCount - EdgeChars, CharCount, aEnd, sizeof(aEnd));
	str_format(pPreview, PreviewSize, "%s..%s", aStart, aEnd);
	SanitizeField(pPreview, PreviewSize);
}

bool TextMatchesWirePreview(const char *pWirePreview, const char *pFullText)
{
	if(!pWirePreview || !pFullText)
		return false;
	if(pWirePreview[0] == '\0')
		return pFullText[0] == '\0';

	const char *pEllipsis = str_find(pWirePreview, "..");
	if(!pEllipsis)
		return str_comp_nocase(pFullText, pWirePreview) == 0;

	char aPrefix[32];
	char aSuffix[32];
	aPrefix[0] = '\0';
	aSuffix[0] = '\0';
	if(pEllipsis > pWirePreview)
		str_truncate(aPrefix, sizeof(aPrefix), pWirePreview, pEllipsis - pWirePreview);
	if(pEllipsis[2] != '\0')
		str_copy(aSuffix, pEllipsis + 2, sizeof(aSuffix));

	if(aPrefix[0] != '\0' && !str_startswith_nocase(pFullText, aPrefix))
		return false;
	if(aSuffix[0] != '\0' && !str_endswith_nocase(pFullText, aSuffix))
		return false;
	return aPrefix[0] != '\0' || aSuffix[0] != '\0';
}

void BuildReplyBodyPrefix(const char *pName, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(!pName || pName[0] == '\0')
		return;
	str_format(pOut, OutSize, "%s: ", pName);
}

bool EncodeReply(const char *pName, int ReplyMessageIndex, const char *pBody, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return false;
	if(!pName || pName[0] == '\0' || !pBody || ReplyMessageIndex <= 0)
		return false;
	if(FieldContainsWireSeparator(pName))
		return false;

	char aName[64];
	char aBody[CHAT_LINE_LENGTH];
	CopyField(aName, sizeof(aName), pName);
	str_copy(aBody, pBody, sizeof(aBody));

	char aReplyPrefix[72];
	BuildReplyBodyPrefix(aName, aReplyPrefix, sizeof(aReplyPrefix));
	if(aReplyPrefix[0] != '\0' && !str_startswith(aBody, aReplyPrefix))
	{
		char aCombined[CHAT_LINE_LENGTH];
		str_format(aCombined, sizeof(aCombined), "%s%s", aReplyPrefix, aBody);
		str_copy(aBody, aCombined, sizeof(aBody));
	}

	char aEncoded[CHAT_LINE_LENGTH];
	str_format(aEncoded, sizeof(aEncoded), "[UCR:-:%s:%d]%s", aName, ReplyMessageIndex, aBody);

	if(str_length(aEncoded) >= OutSize)
		return false;

	str_copy(pOut, aEncoded, OutSize);
	return true;
}

bool TryParseReply(const char *pLine, SReplyMeta &OutMeta, char *pDisplayBody, int DisplayBodySize)
{
	OutMeta = {};
	if(pDisplayBody && DisplayBodySize > 0)
		pDisplayBody[0] = '\0';
	if(!pLine || pLine[0] == '\0')
		return false;

	if(TryParseWireReply(pLine, OutMeta, pDisplayBody, DisplayBodySize))
		return true;
	if(TryParseLegacyReply(pLine, OutMeta, pDisplayBody, DisplayBodySize))
		return true;
	return TryParseDegradedReply(pLine, OutMeta, pDisplayBody, DisplayBodySize);
}
}
