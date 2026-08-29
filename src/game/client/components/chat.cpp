/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "chat.h"
#include "background.h"

#include <base/io.h>
#include <base/log.h>
#include <base/os.h>
#include <base/time.h>

#include <engine/external/json-parser/json.h>
#include <engine/shared/json.h>

#include <engine/engine.h>
#include <engine/editor.h>
#include <engine/external/regex.h>
#include <engine/font_icons.h>
#include <engine/friends.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/csv.h>
#include <engine/shared/http.h>
#include <engine/shared/uclient_presence_protocol.h>
#include <engine/textrender.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/animstate.h>
#include <game/client/components/censor.h>
#include <game/client/components/scoreboard.h>
#include <game/client/components/uclient/chat_nearby_tab.h>
#include <game/client/components/uclient/chat_reply.h>
#include <game/client/components/uclient/uclient.h>
#include <game/client/components/bestclient/bestclient.h>
#include <game/client/components/bestclient/clientindicator/client_indicator.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tclient/colored_parts.h>
#include <game/client/gameclient.h>
#include <game/client/bc_ui_animations.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cctype>
#include <cinttypes>
#include <limits>
#include <string>
#include <utility>
#include <vector>

char CChat::ms_aDisplayText[MAX_LINE_LENGTH] = "";
static constexpr float CHAT_SCROLLBAR_WIDTH = 5.0f;
static constexpr float CHAT_SCROLLBAR_MARGIN = 0.0f;
static constexpr int CHAT_TYPING_ANIM_MAX_TEXT_BYTES = 16;
static constexpr int CHAT_MEDIA_MAX_CONCURRENT_DOWNLOADS = 3;
static constexpr int CHAT_MEDIA_MAX_COMPLETED_DECODE_PER_FRAME = 1;
static constexpr int CHAT_MEDIA_MAX_TEXTURE_UPLOADS_PER_FRAME = 3;
static constexpr int64_t CHAT_MEDIA_TEXTURE_UPLOAD_BUDGET_US = 2500; // keep frame hitches low while filling short animations faster
static constexpr int64_t CHAT_MEDIA_MAX_RESPONSE_SIZE = 64 * 1024 * 1024;
static constexpr int CHAT_MEDIA_MAX_GIF_FRAMES = 360;
static constexpr int CHAT_MEDIA_MAX_DIMENSION = 960;
// The fullscreen viewer decodes the original bytes at a much higher cap so screenshots and other
// high-resolution images (4K/5K/ultrawide) look sharp instead of the upscaled inline preview.
// 8192 is supported by virtually all desktop GPUs; if a texture is still too large the upload
// simply fails and we fall back to the inline preview, so this is safe.
static constexpr int CHAT_MEDIA_VIEWER_MAX_DIMENSION = 8192;
// Only retain original (compressed) bytes for static images up to this size to bound memory usage.
// These are the encoded PNG/JPEG bytes, not decoded pixels, so even large screenshots stay small.
static constexpr int64_t CHAT_MEDIA_ORIGINAL_RETAIN_MAX_BYTES = 64 * 1024 * 1024;
static constexpr int CHAT_MEDIA_DOUBLE_CLICK_MS = 300;
static constexpr int CHAT_MEDIA_MAX_RESOLVE_DEPTH = 2;
static constexpr int CHAT_MEDIA_MAX_VIDEO_ANIMATION_MS = 15000;
static constexpr int CHAT_MEDIA_MAX_RETRIES = 3;
static constexpr float CHAT_MEDIA_MAX_PREVIEW_HEIGHT = 70.0f;
static constexpr float CHAT_MEDIA_MAX_PREVIEW_HEIGHT_SCOREBOARD = 56.0f;
static constexpr float CHAT_MEDIA_PREVIEW_SIZE_SCALE = 0.9f;
static constexpr float CHAT_MEDIA_COMPACT_EXPANDED_HEIGHT = 150.0f;
static constexpr int CHAT_MEDIA_MAX_URL_LENGTH = 240;
static constexpr int CHAT_MEDIA_MAX_HTML_CANDIDATES = 32;
static constexpr size_t CHAT_MEDIA_MAX_ANIMATED_MEMORY_BYTES = 48ull * 1024ull * 1024ull;
static constexpr bool CHAT_MEDIA_ANIMATE_VIDEOS = true;
static constexpr float CHAT_MEDIA_MIN_PREVIEW_SIDE = 28.0f;

static float NormalizeMediaPreviewCoord(float Value, float Start, float Length)
{
	if(Length <= 0.0f)
		return 0.0f;
	return std::clamp((Value - Start) / Length, 0.0f, 1.0f);
}

static void QuadsSetSubsetRelative(IGraphics *pGraphics, float X, float Y, float W, float H, float OriginX, float OriginY, float OriginW, float OriginH)
{
	pGraphics->QuadsSetSubset(
		NormalizeMediaPreviewCoord(X, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y, OriginY, OriginH),
		NormalizeMediaPreviewCoord(X + W, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y + H, OriginY, OriginH));
}

static void QuadsSetSubsetFreeRelative(IGraphics *pGraphics,
	float X0, float Y0, float X1, float Y1, float X2, float Y2, float X3, float Y3,
	float OriginX, float OriginY, float OriginW, float OriginH)
{
	pGraphics->QuadsSetSubsetFree(
		NormalizeMediaPreviewCoord(X0, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y0, OriginY, OriginH),
		NormalizeMediaPreviewCoord(X1, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y1, OriginY, OriginH),
		NormalizeMediaPreviewCoord(X2, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y2, OriginY, OriginH),
		NormalizeMediaPreviewCoord(X3, OriginX, OriginW),
		NormalizeMediaPreviewCoord(Y3, OriginY, OriginH));
}

// Shared with CGifBubbles so the above-head bubble matches the chat preview's rounded style.
void DrawRoundedMediaPreview(IGraphics *pGraphics, const IGraphics::CTextureHandle &Texture, float X, float Y, float W, float H, float Rounding, float Alpha)
{
	if(!Texture.IsValid() || W <= 0.0f || H <= 0.0f)
		return;

	const float ClampedRounding = minimum(Rounding, minimum(W, H) / 2.0f);
	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);

	auto DrawQuad = [&](float QuadX, float QuadY, float QuadW, float QuadH) {
		if(QuadW <= 0.0f || QuadH <= 0.0f)
			return;

		QuadsSetSubsetRelative(pGraphics, QuadX, QuadY, QuadW, QuadH, X, Y, W, H);
		const IGraphics::CQuadItem QuadItem(QuadX, QuadY, QuadW, QuadH);
		pGraphics->QuadsDrawTL(&QuadItem, 1);
	};

	if(ClampedRounding <= 0.0f)
	{
		DrawQuad(X, Y, W, H);
	}
	else
	{
		constexpr int NumSegments = 8;
		const float SegmentAngle = pi / 2.0f / NumSegments;
		for(int i = 0; i < NumSegments; i += 2)
		{
			const float A1 = i * SegmentAngle;
			const float A2 = (i + 1) * SegmentAngle;
			const float A3 = (i + 2) * SegmentAngle;
			const float CosA1 = std::cos(A1);
			const float CosA2 = std::cos(A2);
			const float CosA3 = std::cos(A3);
			const float SinA1 = std::sin(A1);
			const float SinA2 = std::sin(A2);
			const float SinA3 = std::sin(A3);

			const IGraphics::CFreeformItem TopLeft(
				X + ClampedRounding, Y + ClampedRounding,
				X + (1.0f - CosA1) * ClampedRounding, Y + (1.0f - SinA1) * ClampedRounding,
				X + (1.0f - CosA3) * ClampedRounding, Y + (1.0f - SinA3) * ClampedRounding,
				X + (1.0f - CosA2) * ClampedRounding, Y + (1.0f - SinA2) * ClampedRounding);
			QuadsSetSubsetFreeRelative(pGraphics,
				TopLeft.m_X0, TopLeft.m_Y0, TopLeft.m_X1, TopLeft.m_Y1, TopLeft.m_X2, TopLeft.m_Y2, TopLeft.m_X3, TopLeft.m_Y3,
				X, Y, W, H);
			pGraphics->QuadsDrawFreeform(&TopLeft, 1);

			const IGraphics::CFreeformItem TopRight(
				X + W - ClampedRounding, Y + ClampedRounding,
				X + W - ClampedRounding + CosA1 * ClampedRounding, Y + (1.0f - SinA1) * ClampedRounding,
				X + W - ClampedRounding + CosA3 * ClampedRounding, Y + (1.0f - SinA3) * ClampedRounding,
				X + W - ClampedRounding + CosA2 * ClampedRounding, Y + (1.0f - SinA2) * ClampedRounding);
			QuadsSetSubsetFreeRelative(pGraphics,
				TopRight.m_X0, TopRight.m_Y0, TopRight.m_X1, TopRight.m_Y1, TopRight.m_X2, TopRight.m_Y2, TopRight.m_X3, TopRight.m_Y3,
				X, Y, W, H);
			pGraphics->QuadsDrawFreeform(&TopRight, 1);

			const IGraphics::CFreeformItem BottomLeft(
				X + ClampedRounding, Y + H - ClampedRounding,
				X + (1.0f - CosA1) * ClampedRounding, Y + H - ClampedRounding + SinA1 * ClampedRounding,
				X + (1.0f - CosA3) * ClampedRounding, Y + H - ClampedRounding + SinA3 * ClampedRounding,
				X + (1.0f - CosA2) * ClampedRounding, Y + H - ClampedRounding + SinA2 * ClampedRounding);
			QuadsSetSubsetFreeRelative(pGraphics,
				BottomLeft.m_X0, BottomLeft.m_Y0, BottomLeft.m_X1, BottomLeft.m_Y1, BottomLeft.m_X2, BottomLeft.m_Y2, BottomLeft.m_X3, BottomLeft.m_Y3,
				X, Y, W, H);
			pGraphics->QuadsDrawFreeform(&BottomLeft, 1);

			const IGraphics::CFreeformItem BottomRight(
				X + W - ClampedRounding, Y + H - ClampedRounding,
				X + W - ClampedRounding + CosA1 * ClampedRounding, Y + H - ClampedRounding + SinA1 * ClampedRounding,
				X + W - ClampedRounding + CosA3 * ClampedRounding, Y + H - ClampedRounding + SinA3 * ClampedRounding,
				X + W - ClampedRounding + CosA2 * ClampedRounding, Y + H - ClampedRounding + SinA2 * ClampedRounding);
			QuadsSetSubsetFreeRelative(pGraphics,
				BottomRight.m_X0, BottomRight.m_Y0, BottomRight.m_X1, BottomRight.m_Y1, BottomRight.m_X2, BottomRight.m_Y2, BottomRight.m_X3, BottomRight.m_Y3,
				X, Y, W, H);
			pGraphics->QuadsDrawFreeform(&BottomRight, 1);
		}

		DrawQuad(X + ClampedRounding, Y + ClampedRounding, W - ClampedRounding * 2.0f, H - ClampedRounding * 2.0f);
		DrawQuad(X + ClampedRounding, Y, W - ClampedRounding * 2.0f, ClampedRounding);
		DrawQuad(X + ClampedRounding, Y + H - ClampedRounding, W - ClampedRounding * 2.0f, ClampedRounding);
		DrawQuad(X, Y + ClampedRounding, ClampedRounding, H - ClampedRounding * 2.0f);
		DrawQuad(X + W - ClampedRounding, Y + ClampedRounding, ClampedRounding, H - ClampedRounding * 2.0f);
	}

	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
	pGraphics->TextureClear();
}

static bool ChatTypingAnimSupportsText(const char *pText)
{
	for(const char *pScan = pText; *pScan;)
	{
		const char *pBefore = pScan;
		const int Codepoint = str_utf8_decode(&pScan);
		if(Codepoint < 0)
			return false;
		if(pScan <= pBefore)
			return false;
	}
	return true;
}

static int WrongLayoutToLatinCodepoint(int Codepoint)
{
	switch(Codepoint)
	{
	case '.': return '/';
	case 0x0451: return '`'; // ё
	case 0x0401: return '~'; // Ё
	case 0x0439: return 'q'; // й
	case 0x0419: return 'Q'; // Й
	case 0x0446: return 'w'; // ц
	case 0x0426: return 'W'; // Ц
	case 0x0443: return 'e'; // у
	case 0x0423: return 'E'; // У
	case 0x043a: return 'r'; // к
	case 0x041a: return 'R'; // К
	case 0x0435: return 't'; // е
	case 0x0415: return 'T'; // Е
	case 0x043d: return 'y'; // н
	case 0x041d: return 'Y'; // Н
	case 0x0433: return 'u'; // г
	case 0x0413: return 'U'; // Г
	case 0x0448: return 'i'; // ш
	case 0x0428: return 'I'; // Ш
	case 0x0449: return 'o'; // щ
	case 0x0429: return 'O'; // Щ
	case 0x0437: return 'p'; // з
	case 0x0417: return 'P'; // З
	case 0x0445: return '['; // х
	case 0x0425: return '{'; // Х
	case 0x044a: return ']'; // ъ
	case 0x042a: return '}'; // Ъ
	case 0x0444: return 'a'; // ф
	case 0x0424: return 'A'; // Ф
	case 0x044b: return 's'; // ы
	case 0x042b: return 'S'; // Ы
	case 0x0432: return 'd'; // в
	case 0x0412: return 'D'; // В
	case 0x0430: return 'f'; // а
	case 0x0410: return 'F'; // А
	case 0x043f: return 'g'; // п
	case 0x041f: return 'G'; // П
	case 0x0440: return 'h'; // р
	case 0x0420: return 'H'; // Р
	case 0x043e: return 'j'; // о
	case 0x041e: return 'J'; // О
	case 0x043b: return 'k'; // л
	case 0x041b: return 'K'; // Л
	case 0x0434: return 'l'; // д
	case 0x0414: return 'L'; // Д
	case 0x0436: return ';'; // ж
	case 0x0416: return ':'; // Ж
	case 0x044d: return '\''; // э
	case 0x042d: return '"'; // Э
	case 0x044f: return 'z'; // я
	case 0x042f: return 'Z'; // Я
	case 0x0447: return 'x'; // ч
	case 0x0427: return 'X'; // Ч
	case 0x0441: return 'c'; // с
	case 0x0421: return 'C'; // С
	case 0x043c: return 'v'; // м
	case 0x041c: return 'V'; // М
	case 0x0438: return 'b'; // и
	case 0x0418: return 'B'; // И
	case 0x0442: return 'n'; // т
	case 0x0422: return 'N'; // Т
	case 0x044c: return 'm'; // ь
	case 0x042c: return 'M'; // Ь
	case 0x0431: return ','; // б
	case 0x0411: return '<'; // Б
	case 0x044e: return '.'; // ю
	case 0x042e: return '>'; // Ю
	default: return Codepoint;
	}
}

static bool IsLikelySlashCommandName(const char *pName)
{
	if(!pName || !pName[0] || !std::isalpha((unsigned char)pName[0]))
		return false;

	for(const char *pChar = pName; *pChar != '\0'; ++pChar)
	{
		if(!std::isalnum((unsigned char)*pChar) && *pChar != '_' && *pChar != '-')
			return false;
	}
	return true;
}

class CChat::CMediaDecodeJob : public IJob
{
	EMediaKind m_MediaKind;
	IGraphics *m_pGraphics;
	std::vector<unsigned char> m_vData;
	char m_aContextName[512];
	SMediaDecodedFrames m_DecodedFrames;
	bool m_Success = false;

protected:
	void Run() override
	{
		if(State() == IJob::STATE_ABORTED || m_vData.empty())
			return;

		auto DecodeSingleFrameFallback = [&]() -> bool {
			CImageInfo Image;
			if(!MediaDecoder::DecodeImageToRgba(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, Image))
				return false;

			m_DecodedFrames.Free();
			m_DecodedFrames.m_Width = (int)Image.m_Width;
			m_DecodedFrames.m_Height = (int)Image.m_Height;
			m_DecodedFrames.m_Animated = false;
			m_DecodedFrames.m_AnimationStart = time_get();

			SMediaRawFrame Frame;
			Frame.m_DurationMs = 100;
			Frame.m_Image = std::move(Image);
			m_DecodedFrames.m_vFrames.push_back(std::move(Frame));
			return !m_DecodedFrames.m_vFrames.empty();
		};

		SMediaDecodeLimits Limits;
		Limits.m_MaxDimension = CHAT_MEDIA_MAX_DIMENSION;
		Limits.m_MaxFrames = CHAT_MEDIA_MAX_GIF_FRAMES;
		Limits.m_MaxTotalBytes = CHAT_MEDIA_MAX_ANIMATED_MEMORY_BYTES;
		Limits.m_MaxAnimationDurationMs = CHAT_MEDIA_MAX_VIDEO_ANIMATION_MS;

		switch(m_MediaKind)
		{
		case EMediaKind::PHOTO:
			m_Success = MediaDecoder::DecodeStaticImageCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, CHAT_MEDIA_MAX_DIMENSION);
			if(!m_Success)
				m_Success = DecodeSingleFrameFallback();
			break;
		case EMediaKind::ANIMATED:
			// Animate previews for short animations within limits (frames/dimension/memory).
			// Long animations fall back to a single-frame thumbnail via m_MaxAnimationDurationMs.
			Limits.m_DecodeAllFrames = true;
			m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			if(!m_Success)
			{
				// Fallback for problematic GIF/animated WEBP payloads: decode single preview frame.
				Limits.m_DecodeAllFrames = false;
				m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			}
			if(!m_Success)
				m_Success = DecodeSingleFrameFallback();
			break;
		case EMediaKind::VIDEO:
			Limits.m_DecodeAllFrames = CHAT_MEDIA_ANIMATE_VIDEOS;
			m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			if(!m_Success)
			{
				// Fallback for videos where full animation decode fails: keep a static poster frame.
				Limits.m_DecodeAllFrames = false;
				m_Success = MediaDecoder::DecodeImageWithFfmpegCpu(m_pGraphics, m_vData.data(), m_vData.size(), m_aContextName, m_DecodedFrames, Limits);
			}
			if(!m_Success)
				m_Success = DecodeSingleFrameFallback();
			break;
		case EMediaKind::UNKNOWN:
		default:
			m_Success = false;
			break;
		}

		if(State() == IJob::STATE_ABORTED)
		{
			m_Success = false;
			m_DecodedFrames.Free();
		}
	}

public:
	CMediaDecodeJob(IGraphics *pGraphics, EMediaKind MediaKind, const unsigned char *pData, size_t DataSize, const char *pContextName) :
		m_MediaKind(MediaKind),
		m_pGraphics(pGraphics)
	{
		Abortable(true);
		if(pData != nullptr && DataSize > 0)
			m_vData.assign(pData, pData + DataSize);
		str_copy(m_aContextName, pContextName ? pContextName : "", sizeof(m_aContextName));
	}

	~CMediaDecodeJob() override
	{
		m_DecodedFrames.Free();
	}

	bool Success() const { return m_Success; }
	SMediaDecodedFrames &DecodedFrames() { return m_DecodedFrames; }
};

CChat::CLine::CLine()
{
	m_TextContainerIndex.Reset();
	m_QuadContainerIndex = -1;
	m_MediaState = EMediaState::NONE;
	m_MediaKind = EMediaKind::UNKNOWN;
	m_aMediaUrl[0] = '\0';
	m_MediaCandidateIndex = -1;
	m_MediaRetryCount = 0;
	m_MediaUploadIndex = 0;
	m_MediaTotalDurationMs = 0;
	m_MediaAnimated = false;
	m_MediaRevealed = false;
	m_MediaWidth = 0;
	m_MediaHeight = 0;
	m_MediaResolveDepth = 0;
	m_MediaAnimationStart = 0;
	m_aTextHeight[0] = 0.0f;
	m_aTextHeight[1] = 0.0f;
	m_aMediaPreviewWidth[0] = 0.0f;
	m_aMediaPreviewWidth[1] = 0.0f;
	m_aMediaPreviewHeight[0] = 0.0f;
	m_aMediaPreviewHeight[1] = 0.0f;
	m_SelectionStart = -1;
	m_SelectionEnd = -1;
	m_NameRectValid = false;
	m_TranslateRectValid = false;
	m_TranslateLanguageRectValid = false;
	m_MediaPreviewRectValid = false;
	m_MediaRetryRectValid = false;
	m_aMapUrl[0] = '\0';
	m_aMapFileName[0] = '\0';
	m_MapFileSize = -1;
	m_pMapSizeRequest = nullptr;
	m_aMapCardHeight[0] = 0.0f;
	m_aMapCardHeight[1] = 0.0f;
	m_MapCardRectValid = false;
	m_MapDownloadBtnRectValid = false;
	m_UClient = false;
	m_UClientFromCurrentServer = false;
	m_vLinkBounds.clear();
	m_vLinks.clear();
	m_vLinkFontSizes.clear();
	m_vLinkAlwaysConfirm.clear();
	m_vLinkGroups.clear();
	m_HasReply = false;
	m_ReplyToClientId = -1;
	m_ReplyMessageIndex = 0;
	m_aReplyToName[0] = '\0';
	m_aReplyPreview[0] = '\0';
	m_aReplyQuoteText[0] = '\0';
	m_aDisplayText[0] = '\0';
	m_ReplyQuoteRectValid = false;
	m_ReplyQuoteHeight = 0.0f;
	m_LineRectValid = false;
	m_ReplyButtonRectValid = false;
	m_ReplyButtonAnchorX = 0.0f;
	m_ReplyButtonAnchorY = 0.0f;
	m_ReplyButtonAnchorValid = false;
	m_MessageFullWidth = 0.0f;
	m_vReactions.clear();
	m_vReactionRects.clear();
	m_ReactionRectsValid = false;
	m_aReactionRowHeight[0] = 0.0f;
	m_aReactionRowHeight[1] = 0.0f;
	m_UClientMessageId = UUID_ZEROED;
	m_UClientSeq = 0;
	m_UClientMine = false;
	m_ReadLabelRectValid = false;
	m_ServerAnnouncement = false;
	m_UClientScope = -1;
	m_aUClientServerAddress[0] = '\0';
	m_aUClientRoomName[0] = '\0';
	m_aUClientRoomId[0] = '\0';
	m_ScopeHoverRectValid = false;
	m_aScopeNoteHeight[0] = 0.0f;
	m_aScopeNoteHeight[1] = 0.0f;
	m_HasServerJoinLink = false;
	m_aServerJoinAddress[0] = '\0';
	m_aServerJoinServerName[0] = '\0';
	m_ServerJoinBoundsValid = false;
	m_ServerJoinFontSize = 0.0f;
	m_ShowAboveHead = false;
	m_HasSettingsLink = false;
	m_SettingsLinkMissing = false;
	m_SettingsLinkPageOnly = false;
	m_aSettingsLinkUri[0] = '\0';
	m_SettingsLinkParsed = {};
	m_aSettingsLinkHeight[0] = 0.0f;
	m_aSettingsLinkHeight[1] = 0.0f;
	m_aSettingsLinkWidth[0] = 0.0f;
	m_aSettingsLinkWidth[1] = 0.0f;
	m_SettingsLinkRectValid = false;
	m_SettingsShortcutRectValid = false;
}

void CChat::CLine::Reset(CChat &This)
{
	This.TextRender()->DeleteTextContainer(m_TextContainerIndex);
	This.Graphics()->DeleteQuadContainer(m_QuadContainerIndex);
	if(This.m_MediaViewerOpen && This.ValidateMediaViewerLine() && &This.m_aLines[This.m_MediaViewerLineIndex] == this)
		This.CloseMediaViewer();
	This.ResetLineMedia(*this);
	m_Initialized = false;
	m_Time = 0;
	m_aText[0] = '\0';
	m_aName[0] = '\0';
	m_Friend = false;
	m_TimesRepeated = 0;
	m_pManagedTeeRenderInfo = nullptr;
	m_pTranslateResponse = nullptr;
	m_SelectionStart = -1;
	m_SelectionEnd = -1;
	m_NameRectValid = false;
	m_TranslateRectValid = false;
	m_TranslateLanguageRectValid = false;
	m_MediaPreviewRectValid = false;
	m_MediaRetryRectValid = false;
	m_aMapUrl[0] = '\0';
	m_aMapFileName[0] = '\0';
	m_MapFileSize = -1;
	if(m_pMapSizeRequest)
	{
		m_pMapSizeRequest->Abort();
		m_pMapSizeRequest = nullptr;
	}
	m_aMapCardHeight[0] = 0.0f;
	m_aMapCardHeight[1] = 0.0f;
	m_MapCardRectValid = false;
	m_MapDownloadBtnRectValid = false;
	m_UClient = false;
	m_UClientFromCurrentServer = false;
	m_vLinkBounds.clear();
	m_vLinks.clear();
	m_vLinkFontSizes.clear();
	m_vLinkAlwaysConfirm.clear();
	m_vLinkGroups.clear();
	m_HasReply = false;
	m_ReplyToClientId = -1;
	m_ReplyMessageIndex = 0;
	m_aReplyToName[0] = '\0';
	m_aReplyPreview[0] = '\0';
	m_aReplyQuoteText[0] = '\0';
	m_aDisplayText[0] = '\0';
	m_ReplyQuoteRectValid = false;
	m_ReplyQuoteHeight = 0.0f;
	m_LineRectValid = false;
	m_ReplyButtonRectValid = false;
	m_ReplyButtonAnchorX = 0.0f;
	m_ReplyButtonAnchorY = 0.0f;
	m_ReplyButtonAnchorValid = false;
	m_MessageFullWidth = 0.0f;
	m_vReactions.clear();
	m_vReactionRects.clear();
	m_ReactionRectsValid = false;
	m_aReactionRowHeight[0] = 0.0f;
	m_aReactionRowHeight[1] = 0.0f;
	m_UClientMessageId = UUID_ZEROED;
	m_UClientSeq = 0;
	m_UClientMine = false;
	m_ReadLabelRectValid = false;
	m_ServerAnnouncement = false;
	m_UClientScope = -1;
	m_aUClientServerAddress[0] = '\0';
	m_aUClientRoomName[0] = '\0';
	m_aUClientRoomId[0] = '\0';
	m_ScopeHoverRectValid = false;
	m_aScopeNoteHeight[0] = 0.0f;
	m_aScopeNoteHeight[1] = 0.0f;
	m_HasServerJoinLink = false;
	m_aServerJoinAddress[0] = '\0';
	m_aServerJoinServerName[0] = '\0';
	m_ServerJoinBoundsValid = false;
	m_ServerJoinFontSize = 0.0f;
	m_ShowAboveHead = false;
	m_HasSettingsLink = false;
	m_SettingsLinkMissing = false;
	m_SettingsLinkPageOnly = false;
	m_aSettingsLinkUri[0] = '\0';
	m_SettingsLinkParsed = {};
	m_aSettingsLinkHeight[0] = 0.0f;
	m_aSettingsLinkHeight[1] = 0.0f;
	m_aSettingsLinkWidth[0] = 0.0f;
	m_aSettingsLinkWidth[1] = 0.0f;
	m_SettingsLinkRectValid = false;
	m_SettingsShortcutRectValid = false;
}

CChat::CChat()
{
	m_Mode = MODE_NONE;
	m_BacklogCurLine = 0;
	m_ScrollbarDragging = false;
	m_ScrollbarDragOffset = 0.0f;
	m_MouseIsPress = false;
	m_MousePress = vec2(0.0f, 0.0f);
	m_MouseRelease = vec2(0.0f, 0.0f);
	m_HasSelection = false;
	m_WantsSelectionCopy = false;
	m_PrevHudLayoutX = -10000.0f;
	m_PrevHudLayoutY = -10000.0f;
	m_PrevHudLayoutScale = -1;
	m_PrevHudLayoutEnabled = true;
	m_PrevModeActive = false;
	m_PrevChatSelectionActive = false;
	m_TranslateButtonPressed = false;
	m_TranslateButtonRectValid = false;
	m_GiphyButtonPressed = false;
	m_GiphyButtonRectValid = false;
	m_RoomButtonPressed = false;
	m_RoomButtonRectValid = false;
	m_PendingReplyActive = false;
	m_PendingReplyClientId = -1;
	m_PendingReplySourceLineIndex = -1;
	m_aPendingReplyName[0] = '\0';
	m_aPendingReplyPreview[0] = '\0';
	m_ReplyCancelButtonRectValid = false;
	m_HoveredReplyLineIndex = -1;
	m_HoveredSettingsShortcutLineIndex = -1;
	m_LastOutgoingReplyTime = 0;
	m_aLastOutgoingReplyWire[0] = '\0';
	m_LastOutgoingReplyToClientId = -1;
	m_LastOutgoingReplyMessageIndex = 0;
	m_aLastOutgoingReplyToName[0] = '\0';
	m_aLastOutgoingReplyPreview[0] = '\0';
	m_aLastOutgoingReplyBody[0] = '\0';
	m_GiphySearching = false;
	m_GiphyLoadingMore = false;
	m_GiphyHasMoreResults = false;
	m_GiphyNextPageToLoad = 0;
	m_GiphyRequestedPage = 0;
	m_GiphyScrollOffset = vec2(0.0f, 0.0f);
	m_HideMediaByBind = false;
	m_MediaViewerOpen = false;
	m_MediaViewerLineIndex = -1;
	m_MediaViewerZoom = 1.0f;
	m_MediaViewerPan = vec2(0.0f, 0.0f);
	m_MediaViewerDragging = false;
	m_MediaViewerDragStartMouse = vec2(0.0f, 0.0f);
	m_MediaViewerPanStart = vec2(0.0f, 0.0f);
	m_MediaViewerLastClickTime = 0;
	m_MediaViewerFullTexture = IGraphics::CTextureHandle();
	m_MediaViewerFullTextureLine = -1;
	m_aPreviousDisplayedInputText[0] = '\0';
	m_ChatOpenAnimationStart = 0;
	m_vTypingGlyphAnims.clear();

	m_Input.SetClipboardLineCallback([this](const char *pStr) { SendChatQueued(pStr); });
	m_Input.SetClipboardImagePasteCallback([this]() { return m_UcChatPaste.TryPasteFromClipboard(this); });
	m_Input.SetCalculateOffsetCallback([this]() { return m_IsInputCensored; });
	m_Input.SetDisplayTextCallback([this](char *pStr, size_t NumChars) {
		m_IsInputCensored = false;
		(void)NumChars;
		if(GameClient()->m_BestClient.SanitizeSensitiveCommand(pStr, ms_aDisplayText, sizeof(ms_aDisplayText)))
		{
			m_IsInputCensored = true;
			return ms_aDisplayText;
		}
		return pStr;
	});
}

void CChat::RegisterCommand(const char *pName, const char *pParams, const char *pHelpText)
{
	// Don't allow duplicate commands.
	for(const auto &Command : m_vServerCommands)
		if(str_comp(Command.m_aName, pName) == 0)
			return;

	m_vServerCommands.emplace_back(pName, pParams, pHelpText);
	m_ServerCommandsNeedSorting = true;
}

void CChat::UnregisterCommand(const char *pName)
{
	m_vServerCommands.erase(std::remove_if(m_vServerCommands.begin(), m_vServerCommands.end(), [pName](const CCommand &Command) { return str_comp(Command.m_aName, pName) == 0; }), m_vServerCommands.end());
}

bool CChat::HasServerCommand(const char *pName) const
{
	for(const auto &Command : m_vServerCommands)
	{
		if(str_comp_nocase(Command.m_aName, pName) == 0)
			return true;
	}
	return false;
}

bool CChat::TryConvertWrongLayoutSlashCommand(const char *pLine, char *pOut, int OutSize) const
{
	if(!g_Config.m_BcChatAltCommandLayout || !pLine || !pOut || OutSize <= 0)
		return false;

	const char *pTokenStart = str_utf8_skip_whitespaces(pLine);
	if(*pTokenStart == '\0')
		return false;

	const char *pTokenEnd = pTokenStart;
	while(*pTokenEnd)
	{
		const char *pNext = pTokenEnd;
		const int Codepoint = str_utf8_decode(&pNext);
		if(Codepoint <= 0 || str_utf8_isspace(Codepoint))
			break;
		pTokenEnd = pNext;
	}

	char aConvertedToken[MAX_LINE_LENGTH];
	int ConvertedLen = 0;
	bool Changed = false;
	for(const char *pScan = pTokenStart; pScan < pTokenEnd;)
	{
		const char *pNext = pScan;
		const int Codepoint = str_utf8_decode(&pNext);
		const int ConvertedCodepoint = WrongLayoutToLatinCodepoint(Codepoint);
		Changed |= ConvertedCodepoint != Codepoint;

		char aEncoded[8];
		const int EncodedLen = str_utf8_encode(aEncoded, ConvertedCodepoint);
		if(ConvertedLen + EncodedLen >= (int)sizeof(aConvertedToken))
			return false;
		std::copy_n(aEncoded, EncodedLen, aConvertedToken + ConvertedLen);
		ConvertedLen += EncodedLen;
		pScan = pNext;
	}
	aConvertedToken[ConvertedLen] = '\0';

	if(!Changed || aConvertedToken[0] != '/')
		return false;

	const char *pCommandName = aConvertedToken + 1;
	if(!IsLikelySlashCommandName(pCommandName))
		return false;
	if(!m_vServerCommands.empty() && !HasServerCommand(pCommandName))
		return false;

	str_truncate(pOut, OutSize, pLine, pTokenStart - pLine);
	str_append(pOut, aConvertedToken, OutSize);
	str_append(pOut, pTokenEnd, OutSize);
	return str_comp(pOut, pLine) != 0;
}

void CChat::RebuildChat()
{
	for(auto &Line : m_aLines)
	{
		if(!Line.m_Initialized)
			continue;
		TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
		// recalculate sizes
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
		if(Line.m_MediaState == EMediaState::NONE && HasAllowedMediaCandidates(Line))
			QueueMediaDownload(Line);
	}
}

void CChat::ClearLines()
{
	for(auto &Line : m_aLines)
		Line.Reset(*this);
	m_PrevScoreBoardShowed = false;
	m_PrevShowChat = false;
	m_PrevModeActive = false;
	m_PrevChatSelectionActive = false;
	m_PrevHudLayoutX = -10000.0f;
	m_PrevHudLayoutY = -10000.0f;
	m_PrevHudLayoutScale = -1;
	m_PrevHudLayoutEnabled = true;
}

void CChat::OnWindowResize()
{
	RebuildChat();
}

void CChat::Reset()
{
	ClearLines();

	m_Show = false;
	m_CompletionUsed = false;
	m_CompletionChosen = -1;
	m_aCompletionBuffer[0] = 0;
	m_PlaceholderOffset = 0;
	m_PlaceholderLength = 0;
	m_pHistoryEntry = nullptr;
	m_vPendingChatQueue.clear();
	m_LastChatSend = 0;
	m_CurrentLine = 0;
	m_IsInputCensored = false;
	m_EditingNewLine = true;
	m_aSavedInputText[0] = '\0';
	m_SavedInputPending = false;
	m_aPreviousDisplayedInputText[0] = '\0';
	m_ChatOpenAnimationStart = 0;
	m_vTypingGlyphAnims.clear();
	m_ServerSupportsCommandInfo = false;
	m_ServerCommandsNeedSorting = false;
	m_aCurrentInputText[0] = '\0';
	m_BacklogCurLine = 0;
	m_ScrollbarDragging = false;
	m_ScrollbarDragOffset = 0.0f;
	m_LastMousePos = std::nullopt;
	m_TranslateButtonPressed = false;
	m_TranslateButtonRectValid = false;
	m_RoomButtonPressed = false;
	m_RoomButtonRectValid = false;
	m_HideMediaByBind = false;
	if(m_LinkPreflight.m_pRequest)
		m_LinkPreflight.m_pRequest->Abort();
	m_LinkPreflight = {};
	m_LinkPolicyCache.m_pRequest.reset();
	FreeMediaViewerFullTexture();
	m_MediaViewerOpen = false;
	m_MediaViewerLineIndex = -1;
	m_MediaViewerZoom = 1.0f;
	m_MediaViewerPan = vec2(0.0f, 0.0f);
	m_MediaViewerDragging = false;
	m_MediaViewerDragStartMouse = vec2(0.0f, 0.0f);
	m_MediaViewerPanStart = vec2(0.0f, 0.0f);
	m_MediaViewerLastClickTime = 0;
	m_UcChatPaste.Reset(this);
	DisableMode();
	m_vServerCommands.clear();

	for(int64_t &LastSoundPlayed : m_aLastSoundPlayed)
		LastSoundPlayed = 0;
}

void CChat::ResetTypingAnimation()
{
	m_vTypingGlyphAnims.clear();
}

void CChat::SyncTypingAnimationBaseline()
{
	ResetTypingAnimation();
	str_copy(m_aPreviousDisplayedInputText, m_Input.GetDisplayedString(), sizeof(m_aPreviousDisplayedInputText));
}

void CChat::RefreshTypingAnimation()
{
	if(m_Mode == MODE_NONE || !BCUiAnimations::Enabled() || g_Config.m_BcChatAnimation == 0 || g_Config.m_BcChatTypingAnimation == 0 || m_Input.HasSelection() || Input()->HasComposition())
	{
		SyncTypingAnimationBaseline();
		return;
	}

	const char *pCurrent = m_Input.GetDisplayedString();
	const size_t CurrentLen = str_length(pCurrent);
	const size_t PreviousLen = str_length(m_aPreviousDisplayedInputText);

	// Fall back only for invalid UTF-8, otherwise keep per-glyph animation for
	// normal multi-byte text such as Cyrillic.
	if(!ChatTypingAnimSupportsText(pCurrent) || !ChatTypingAnimSupportsText(m_aPreviousDisplayedInputText))
	{
		SyncTypingAnimationBaseline();
		return;
	}

	if(str_comp(pCurrent, m_aPreviousDisplayedInputText) == 0)
		return;

	if(CurrentLen == 0)
	{
		SyncTypingAnimationBaseline();
		return;
	}

	// Find edit boundaries aligned to UTF-8 codepoints (byte-wise diff breaks for multi-byte chars like Cyrillic).
	size_t PrefixBytes = 0;
	{
		const char *pCurScan = pCurrent;
		const char *pPrevScan = m_aPreviousDisplayedInputText;
		while(*pCurScan && *pPrevScan)
		{
			const char *pCurBefore = pCurScan;
			const char *pPrevBefore = pPrevScan;
			const int CurCp = str_utf8_decode(&pCurScan);
			const int PrevCp = str_utf8_decode(&pPrevScan);
			if(CurCp != PrevCp)
			{
				pCurScan = pCurBefore;
				pPrevScan = pPrevBefore;
				break;
			}
			PrefixBytes = (size_t)(pCurScan - pCurrent);
		}
	}

	size_t SuffixBytesCur = 0;
	size_t SuffixBytesPrev = 0;
	{
		int CurCursor = (int)CurrentLen;
		int PrevCursor = (int)PreviousLen;
		while(CurCursor > (int)PrefixBytes && PrevCursor > (int)PrefixBytes)
		{
			const int CurBefore = CurCursor;
			const int PrevBefore = PrevCursor;
			CurCursor = str_utf8_rewind(pCurrent, CurCursor);
			PrevCursor = str_utf8_rewind(m_aPreviousDisplayedInputText, PrevCursor);

			const char *pCurCpPtr = pCurrent + CurCursor;
			const char *pPrevCpPtr = m_aPreviousDisplayedInputText + PrevCursor;
			const int CurCp = str_utf8_decode(&pCurCpPtr);
			const int PrevCp = str_utf8_decode(&pPrevCpPtr);
			if(CurCp != PrevCp)
			{
				CurCursor = CurBefore;
				PrevCursor = PrevBefore;
				break;
			}

			SuffixBytesCur = CurrentLen - (size_t)CurCursor;
			SuffixBytesPrev = PreviousLen - (size_t)PrevCursor;
		}
	}

	const size_t RemovedBytes = PreviousLen - PrefixBytes - SuffixBytesPrev;
	const size_t InsertedBytes = CurrentLen - PrefixBytes - SuffixBytesCur;
	const int EditOldEndByte = (int)(PrefixBytes + RemovedBytes);
	const int DeltaBytes = (int)InsertedBytes - (int)RemovedBytes;

	for(auto It = m_vTypingGlyphAnims.begin(); It != m_vTypingGlyphAnims.end();)
	{
		const int AnimEndByte = It->m_ByteIndex + It->m_ByteLength;
		if(It->m_ByteIndex >= (int)PrefixBytes && AnimEndByte <= EditOldEndByte)
		{
			It = m_vTypingGlyphAnims.erase(It);
			continue;
		}
		if(It->m_ByteIndex >= EditOldEndByte)
			It->m_ByteIndex += DeltaBytes;

		if(It->m_ByteIndex < 0 || It->m_ByteLength <= 0 || It->m_ByteIndex + It->m_ByteLength > (int)CurrentLen)
		{
			It = m_vTypingGlyphAnims.erase(It);
			continue;
		}

		if(str_length(It->m_aText) != It->m_ByteLength ||
			str_comp_num(It->m_aText, pCurrent + It->m_ByteIndex, It->m_ByteLength) != 0)
		{
			It = m_vTypingGlyphAnims.erase(It);
			continue;
		}
		++It;
	}

	if(InsertedBytes > 0)
	{
		for(int ByteIndex = (int)PrefixBytes; ByteIndex < (int)(PrefixBytes + InsertedBytes);)
		{
			const int NextByteIndex = str_utf8_forward(pCurrent, ByteIndex);
			const int GlyphBytes = minimum(NextByteIndex - ByteIndex, CHAT_TYPING_ANIM_MAX_TEXT_BYTES - 1);
			if(GlyphBytes > 0)
			{
				STypingGlyphAnim Anim;
				Anim.m_StartTime = time_get();
				Anim.m_ByteIndex = ByteIndex;
				Anim.m_ByteLength = GlyphBytes;
				str_truncate(Anim.m_aText, sizeof(Anim.m_aText), pCurrent + ByteIndex, GlyphBytes);
				m_vTypingGlyphAnims.push_back(Anim);
			}
			ByteIndex = NextByteIndex;
		}
	}

	str_copy(m_aPreviousDisplayedInputText, pCurrent, sizeof(m_aPreviousDisplayedInputText));
}

void CChat::OnRelease()
{
	m_Show = false;
}

void CChat::OnStateChange(int NewState, int OldState)
{
	if(OldState <= IClient::STATE_CONNECTING)
		Reset();
}

void CChat::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SendChat(0, pResult->GetString(0));
}

void CChat::ConSayTeam(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SendChat(1, pResult->GetString(0));
}

void CChat::ConSayUClient(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->SayUClient(pResult->GetString(0));
}

// Shown in the chat window rather than the console so it is still visible when the command that
// hit it came from a bind. Tinted like any other UClient line.
void CChat::EchoUClientNotice(const char *pText)
{
	const int PrevShowClient = g_Config.m_TcShowChatClient;
	g_Config.m_TcShowChatClient = 1;
	Echo(pText);
	g_Config.m_TcShowChatClient = PrevShowClient;
	CLine &Line = m_aLines[m_CurrentLine];
	if(Line.m_Initialized && Line.m_ClientId == CLIENT_MSG)
		Line.m_CustomColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UcMessageColor));
}

void CChat::EchoUClientDisabled()
{
	EchoUClientNotice("UClient chat is disabled.");
}

void CChat::ConChat(IConsole::IResult *pResult, void *pUserData)
{
	CChat *pChat = (CChat *)pUserData;
	const char *pMode = pResult->GetString(0);
	if(str_comp(pMode, "all") == 0)
		pChat->EnableMode(0);
	else if(str_comp(pMode, "team") == 0)
		pChat->EnableMode(1);
	else if(str_comp(pMode, "uclient") == 0)
	{
		if(!g_Config.m_UcChat)
		{
			pChat->EchoUClientDisabled();
			return;
		}
		pChat->EnableMode(TEAM_UCLIENT);
	}
	else
	{
		pChat->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", "expected all, team or uclient as mode");
		return;
	}

	if(pResult->GetString(1)[0])
	{
		pChat->m_Input.Set(pResult->GetString(1));
	}
	else if(g_Config.m_ClChatReset || !g_Config.m_BcChatSaveDraft)
	{
		if(g_Config.m_BcChatSaveDraft && pChat->m_SavedInputPending)
			pChat->m_Input.Set(pChat->m_aSavedInputText);
		else
			pChat->m_Input.Clear();

		if(!g_Config.m_BcChatSaveDraft)
		{
			pChat->m_SavedInputPending = false;
			pChat->m_aSavedInputText[0] = '\0';
		}
	}
}

void CChat::ConShowChat(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->m_Show = pResult->GetInteger(0) != 0;
}

void CChat::ConEcho(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->Echo(pResult->GetString(0));
}

void CChat::ConClearChat(IConsole::IResult *pResult, void *pUserData)
{
	((CChat *)pUserData)->ClearLines();
}

void CChat::ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	((CChat *)pUserData)->RebuildChat();
}

void CChat::ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CChat *pChat = (CChat *)pUserData;
	pChat->EnsureCoherentWidth();
	pChat->RebuildChat();
}

void CChat::ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CChat *pChat = (CChat *)pUserData;
	pChat->EnsureCoherentFontSize();
	pChat->RebuildChat();
}

void CChat::Echo(const char *pString)
{
	AddLine(CLIENT_MSG, 0, pString);
}

void CChat::OnConsoleInit()
{
	Console()->Register("say", "r[message]", CFGFLAG_CLIENT, ConSay, this, "Say in chat");
	Console()->Register("say_team", "r[message]", CFGFLAG_CLIENT, ConSayTeam, this, "Say in team chat");
	Console()->Register("say_uclient", "r[message]", CFGFLAG_CLIENT, ConSayUClient, this, "Say in UClient chat");
	Console()->Register("chat", "s['team'|'all'|'uclient'] ?r[message]", CFGFLAG_CLIENT, ConChat, this, "Enable chat with all/team/uclient mode");
	Console()->Register("+show_chat", "", CFGFLAG_CLIENT, ConShowChat, this, "Show chat");
	Console()->Register("echo", "r[message]", CFGFLAG_CLIENT | CFGFLAG_STORE, ConEcho, this, "Echo the text in chat window");
	Console()->Register("clear_chat", "", CFGFLAG_CLIENT | CFGFLAG_STORE, ConClearChat, this, "Clear chat messages");
	Console()->Register("toggle_chat_media_hidden", "", CFGFLAG_CLIENT, ConToggleHideChatMedia, this, "Toggle hidden media mode in chat");
	Console()->Register("add_censor_list", "r[word]", CFGFLAG_CLIENT, ConAddCensorList, this, "Add a word to the chat filter regex");
	Console()->Register("add_white_list", "s[nickname]", CFGFLAG_CLIENT, ConAddWhiteList, this, "Add a player to the chat filter whitelist");
}

void CChat::OnInit()
{
	Reset();
	Console()->Chain("cl_chat_old", ConchainChatOld, this);
	Console()->Chain("cl_chat_size", ConchainChatFontSize, this);
	Console()->Chain("cl_chat_width", ConchainChatWidth, this);
	Console()->Chain("bc_regex_player_whitelist", ConchainRegexPlayerWhitelist, this);

	if(g_Config.m_BcRegexPlayerWhitelist[0])
	{
		auto Re = Regex(g_Config.m_BcRegexPlayerWhitelist);
		if(Re.error().empty())
			m_RegexPlayerWhitelist = std::move(Re);
	}
	if(g_Config.m_TcRegexChatIgnore[0])
	{
		auto Re = Regex(g_Config.m_TcRegexChatIgnore);
		if(Re.error().empty())
			GameClient()->m_TClient.m_RegexChatIgnore = std::move(Re);
	}
}

namespace
{
struct STranslateLanguageOption
{
	const char *m_pCode;
	const char *m_pLabel;
};

constexpr STranslateLanguageOption gs_aTranslateSourceOptions[] = {
	{"auto", "Auto"},
	{"ru", "Russian"},
	{"en", "English"},
	{"de", "German"},
	{"fr", "French"},
	{"es", "Spanish"},
	{"zh", "Chinese"},
	{"pt", "Brazilian"},
	{"tr", "Turkish"},
	{"ko", "Korean"},
	{"ja", "Japanese"},
};

constexpr STranslateLanguageOption gs_aTranslateTargetOptions[] = {
	{"ru", "Russian"},
	{"en", "English"},
	{"de", "German"},
	{"fr", "French"},
	{"es", "Spanish"},
	{"zh", "Chinese"},
	{"pt", "Brazilian"},
	{"tr", "Turkish"},
	{"ko", "Korean"},
	{"ja", "Japanese"},
};

template<size_t N>
int TranslateLanguageIndex(const char *pCode, const STranslateLanguageOption (&aOptions)[N])
{
	for(size_t i = 0; i < N; ++i)
	{
		if(str_comp_nocase(pCode, aOptions[i].m_pCode) == 0)
			return (int)i;
	}
	return 0;
}

template<size_t N>
void ApplyTranslateLanguage(char *pConfig, size_t ConfigSize, int Index, const STranslateLanguageOption (&aOptions)[N])
{
	Index = std::clamp(Index, 0, (int)N - 1);
	str_copy(pConfig, aOptions[Index].m_pCode, ConfigSize);
}
}

void CChat::OpenTranslateSettingsPopup(const CUIRect &ButtonRect)
{
	Ui()->DoPopupMenu(&m_TranslateSettingsPopupId, ButtonRect.x, ButtonRect.y, 300.0f, 306.0f, this, PopupTranslateSettings);
}

CUi::EPopupMenuFunctionResult CChat::PopupTranslateSettings(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;
	const float Spacing = 5.0f;
	const float RowHeight = 20.0f;
	const float FontSize = 11.0f;
	static CUi::SDropDownState s_IncomingSourceDropDown;
	static CUi::SDropDownState s_IncomingTargetDropDown;
	static CUi::SDropDownState s_OutgoingSourceDropDown;
	static CUi::SDropDownState s_OutgoingTargetDropDown;
	static CLineInput s_IncomingIgnoreLanguagesInput(g_Config.m_BcTranslateIncomingIgnoreLanguages, sizeof(g_Config.m_BcTranslateIncomingIgnoreLanguages));
	static CScrollRegion s_IncomingSourceScroll;
	static CScrollRegion s_IncomingTargetScroll;
	static CScrollRegion s_OutgoingSourceScroll;
	static CScrollRegion s_OutgoingTargetScroll;

	s_IncomingSourceDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_IncomingSourceScroll;
	s_IncomingTargetDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_IncomingTargetScroll;
	s_OutgoingSourceDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_OutgoingSourceScroll;
	s_OutgoingTargetDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_OutgoingTargetScroll;

	CUIRect Row;
	View.HSplitTop(14.0f, &Row, &View);
	pChat->Ui()->DoLabel(&Row, Localize("Chat translate"), 12.0f, TEXTALIGN_ML);

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsEnableButton, Localize("Auto translate others' messages"), g_Config.m_TcTranslateAutoIncoming, &Row))
		g_Config.m_TcTranslateAutoIncoming ^= 1;

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsEnableOutgoingButton, Localize("Auto translate your messages"), g_Config.m_TcTranslateAutoOutgoing, &Row))
		g_Config.m_TcTranslateAutoOutgoing ^= 1;

	const auto RenderLanguageField = [&](const char *pLabel, int CurrentIndex, const char **ppLabels, int LabelCount, CUi::SDropDownState &DropDownState) {
		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(RowHeight, &Row, &View);
		CUIRect Label, DropDown;
		Row.VSplitLeft(145.0f, &Label, &DropDown);
		pChat->Ui()->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
		return pChat->Ui()->DoDropDown(&DropDown, CurrentIndex, ppLabels, LabelCount, DropDownState);
	};

	static const char *s_apSourceLabels[] = {
		"Auto", "Russian", "English", "German", "French", "Spanish", "Chinese", "Brazilian", "Turkish", "Korean", "Japanese"};
	static const char *s_apTargetLabels[] = {
		"Russian", "English", "German", "French", "Spanish", "Chinese", "Brazilian", "Turkish", "Korean", "Japanese"};

	const int IncomingSourceIndex = TranslateLanguageIndex(g_Config.m_BcTranslateIncomingSource, gs_aTranslateSourceOptions);
	const int NewIncomingSourceIndex = RenderLanguageField(Localize("Incoming from"), IncomingSourceIndex, s_apSourceLabels, std::size(s_apSourceLabels), s_IncomingSourceDropDown);
	if(NewIncomingSourceIndex != IncomingSourceIndex)
		ApplyTranslateLanguage(g_Config.m_BcTranslateIncomingSource, sizeof(g_Config.m_BcTranslateIncomingSource), NewIncomingSourceIndex, gs_aTranslateSourceOptions);

	const int IncomingTargetIndex = TranslateLanguageIndex(g_Config.m_TcTranslateTarget, gs_aTranslateTargetOptions);
	const int NewIncomingTargetIndex = RenderLanguageField(Localize("Incoming to"), IncomingTargetIndex, s_apTargetLabels, std::size(s_apTargetLabels), s_IncomingTargetDropDown);
	if(NewIncomingTargetIndex != IncomingTargetIndex)
		ApplyTranslateLanguage(g_Config.m_TcTranslateTarget, sizeof(g_Config.m_TcTranslateTarget), NewIncomingTargetIndex, gs_aTranslateTargetOptions);

	const int OutgoingSourceIndex = TranslateLanguageIndex(g_Config.m_BcTranslateOutgoingSource, gs_aTranslateSourceOptions);
	const int NewOutgoingSourceIndex = RenderLanguageField(Localize("Your messages from"), OutgoingSourceIndex, s_apSourceLabels, std::size(s_apSourceLabels), s_OutgoingSourceDropDown);
	if(NewOutgoingSourceIndex != OutgoingSourceIndex)
		ApplyTranslateLanguage(g_Config.m_BcTranslateOutgoingSource, sizeof(g_Config.m_BcTranslateOutgoingSource), NewOutgoingSourceIndex, gs_aTranslateSourceOptions);

	const int OutgoingTargetIndex = TranslateLanguageIndex(g_Config.m_BcTranslateOutgoingTarget, gs_aTranslateTargetOptions);
	const int NewOutgoingTargetIndex = RenderLanguageField(Localize("Your messages to"), OutgoingTargetIndex, s_apTargetLabels, std::size(s_apTargetLabels), s_OutgoingTargetDropDown);
	if(NewOutgoingTargetIndex != OutgoingTargetIndex)
		ApplyTranslateLanguage(g_Config.m_BcTranslateOutgoingTarget, sizeof(g_Config.m_BcTranslateOutgoingTarget), NewOutgoingTargetIndex, gs_aTranslateTargetOptions);

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(RowHeight, &Row, &View);
	CUIRect IgnoreLabel, IgnoreEditBox;
	Row.VSplitLeft(145.0f, &IgnoreLabel, &IgnoreEditBox);
	pChat->Ui()->DoLabel(&IgnoreLabel, Localize("Don't translate from"), FontSize, TEXTALIGN_ML);
	s_IncomingIgnoreLanguagesInput.SetEmptyText("ru; en; zh");
	pChat->Ui()->DoClearableEditBox(&s_IncomingIgnoreLanguagesInput, &IgnoreEditBox, 14.0f);
	pChat->GameClient()->m_Tooltips.DoToolTip(&s_IncomingIgnoreLanguagesInput, &IgnoreEditBox, Localize("Semicolon-separated source languages to skip for auto-translation, for example: ru; en; zh"));

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	if(pChat->GameClient()->m_Menus.DoButton_CheckBox(&pChat->m_TranslateSettingsStripPunctuationButton, Localize("No commas or periods"), g_Config.m_BcTranslateOutgoingStripPunctuation, &Row))
		g_Config.m_BcTranslateOutgoingStripPunctuation ^= 1;

	View.HSplitTop(Spacing, nullptr, &View);
	static CButtonContainer s_TranslateKeyReader;
	static CButtonContainer s_TranslateKeyClear;
	pChat->GameClient()->m_Menus.DoLine_KeyReader(View, s_TranslateKeyReader, s_TranslateKeyClear, Localize("Toggle translate"), "toggle_translate");

	return CUi::POPUP_KEEP_OPEN;
}

void CChat::RenderTranslateSettingsButton(const CUIRect &ButtonRect)
{
	m_TranslateButtonRect.m_X = ButtonRect.x;
	m_TranslateButtonRect.m_Y = ButtonRect.y;
	m_TranslateButtonRect.m_W = ButtonRect.w;
	m_TranslateButtonRect.m_H = ButtonRect.h;
	m_TranslateButtonRectValid = true;

	const vec2 MousePos = ChatMousePos();
	const bool Hovered = MousePos.x >= ButtonRect.x && MousePos.x <= ButtonRect.x + ButtonRect.w &&
		MousePos.y >= ButtonRect.y && MousePos.y <= ButtonRect.y + ButtonRect.h;
	const bool IsOpen = Ui()->IsPopupOpen(&m_TranslateSettingsPopupId);
	const bool IsTranslateActive = g_Config.m_TcTranslateAutoIncoming || g_Config.m_TcTranslateAutoOutgoing;
	const ColorRGBA ButtonColor = IsOpen ? ColorRGBA(0.35f, 0.45f, 0.70f, 0.90f) :
		(IsTranslateActive ? (Hovered ? ColorRGBA(0.22f, 0.58f, 0.22f, 0.92f) : ColorRGBA(0.15f, 0.48f, 0.15f, 0.85f)) :
		(Hovered ? ColorRGBA(0.28f, 0.28f, 0.28f, 0.90f) : ColorRGBA(0.16f, 0.16f, 0.16f, 0.82f)));
	const float ButtonRounding = maximum(3.0f, ButtonRect.h * 0.28f);

	ButtonRect.Draw(ButtonColor, IGraphics::CORNER_ALL, ButtonRounding);

	CUIRect IconRect;
	ButtonRect.Margin(1.0f, &IconRect);
	const float IconSize = IconRect.h * CUi::ms_FontmodHeight;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
	Ui()->DoLabel(&IconRect, FontIcon::LANGUAGE, IconSize, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(Hovered)
		Ui()->SetHotItem(&m_TranslateSettingsButton);
	GameClient()->m_Tooltips.DoToolTip(&m_TranslateSettingsButton, &ButtonRect, Localize("Chat translate settings"));
}

void CChat::OpenRoomSelectPopup(const CUIRect &ButtonRect)
{
	const float ChatHeight = 300.0f;
	const float ChatWidth = ChatHeight * Graphics()->ScreenAspect();
	const CUIRect *pUiScreen = Ui()->Screen();
	const float ScaleX = pUiScreen->w / ChatWidth;
	const float ScaleY = pUiScreen->h / ChatHeight;
	const float PopupWidth = 330.0f;
	const float PopupHeight = 330.0f;
	const float Margin = 6.0f;
	const float UiButtonRight = (ButtonRect.x + ButtonRect.w) * ScaleX;
	const float UiButtonTop = ButtonRect.y * ScaleY;
	const float PopupX = std::clamp(UiButtonRight - PopupWidth, Margin, pUiScreen->w - PopupWidth - Margin);
	const float PopupY = std::clamp(UiButtonTop - PopupHeight - 6.0f, Margin, pUiScreen->h - PopupHeight - Margin);
	Ui()->DoPopupMenu(&m_RoomSelectPopupId, PopupX, PopupY, PopupWidth, PopupHeight, this, PopupRoomSelect);
}

CUi::EPopupMenuFunctionResult CChat::PopupRoomSelect(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;
	static int s_CurTab = 0;
	static CButtonContainer s_aTabs[2];
	CUIRect TabBar, TabButton;
	View.HSplitTop(22.0f, &TabBar, &View);
	const char *apTabNames[] = {Localize("UClient Chat"), Localize("Translation")};
	for(int Tab = 0; Tab < 2; ++Tab)
	{
		TabBar.VSplitLeft(TabBar.w / (2 - Tab), &TabButton, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : IGraphics::CORNER_R;
		if(pChat->GameClient()->m_Menus.DoButton_MenuTab(&s_aTabs[Tab], apTabNames[Tab], s_CurTab == Tab, &TabButton, Corners))
			s_CurTab = Tab;
	}
	View.HSplitTop(8.0f, nullptr, &View);
	if(s_CurTab == 1)
		return PopupTranslateSettings(pContext, View, Active);

	CUIRect Row, Label, DropDown;
	View.HSplitTop(20.0f, &Row, &View);
	Row.VSplitLeft(82.0f, &Label, &DropDown);
	DropDown.VSplitLeft(6.0f, nullptr, &DropDown);
	pChat->Ui()->DoLabel(&Label, Localize("Send target"), 11.0f, TEXTALIGN_ML);
	static CUi::SDropDownState s_SendTargetDropDownState;
	static CScrollRegion s_SendTargetDropDownScrollRegion;
	pChat->RenderUClientChatTargetDropDown(DropDown, s_SendTargetDropDownState, s_SendTargetDropDownScrollRegion);
	return CUi::POPUP_KEEP_OPEN;
}

void CChat::RenderUClientChatTargetDropDown(const CUIRect &Rect, CUi::SDropDownState &DropDownState, CScrollRegion &ScrollRegion)
{
	const auto &vRooms = GameClient()->m_UClientChatRooms.Rooms();
	std::vector<std::string> vLabels = {
		Localize("All UClient users"),
		Localize("Same server"),
	};
	vLabels.reserve(2 + vRooms.size());
	for(const auto &Room : vRooms)
		vLabels.emplace_back(Room.m_aName);
	std::vector<const char *> vpLabels;
	vpLabels.reserve(vLabels.size());
	for(const std::string &Label : vLabels)
		vpLabels.push_back(Label.c_str());

	int CurrentSelection = 0;
	if(g_Config.m_UcChatSendRoom[0])
	{
		for(size_t i = 0; i < vRooms.size(); ++i)
			if(!str_comp(g_Config.m_UcChatSendRoom, vRooms[i].m_aId))
				CurrentSelection = 2 + (int)i;
	}
	else if(g_Config.m_UcChatSendSameServerOnly)
		CurrentSelection = 1;
	DropDownState.m_SelectionPopupContext.m_pScrollRegion = &ScrollRegion;
	const int NewSelection = Ui()->DoDropDown(&Rect, CurrentSelection, vpLabels.data(), (int)vpLabels.size(), DropDownState);
	if(NewSelection != CurrentSelection)
	{
		if(NewSelection >= 2 && NewSelection < 2 + (int)vRooms.size())
			GameClient()->m_UClientChatRooms.SelectSendRoom(vRooms[NewSelection - 2].m_aId);
		else
		{
			GameClient()->m_UClientChatRooms.SelectSendRoom("");
			g_Config.m_UcChatSendSameServerOnly = NewSelection == 1;
			ConfigManager()->Save();
		}
	}
}

void CChat::RenderRoomSelectButton(const CUIRect &ButtonRect)
{
	m_RoomButtonRect = {ButtonRect.x, ButtonRect.y, ButtonRect.w, ButtonRect.h};
	m_RoomButtonRectValid = true;
	const vec2 MousePos = ChatMousePos();
	const bool Hovered = MousePos.x >= ButtonRect.x && MousePos.x <= ButtonRect.x + ButtonRect.w &&
		MousePos.y >= ButtonRect.y && MousePos.y <= ButtonRect.y + ButtonRect.h;
	const ColorRGBA ButtonColor = Hovered ? ColorRGBA(0.28f, 0.28f, 0.28f, 0.90f) :
		ColorRGBA(0.16f, 0.16f, 0.16f, 0.82f);
	ButtonRect.Draw(ButtonColor, IGraphics::CORNER_ALL, maximum(3.0f, ButtonRect.h * 0.28f));
	CUIRect IconRect;
	ButtonRect.Margin(1.0f, &IconRect);
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
	Ui()->DoLabel(&IconRect, FontIcon::GEAR, IconRect.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	if(Hovered)
		Ui()->SetHotItem(&m_RoomSelectButton);
	GameClient()->m_Tooltips.DoToolTip(&m_RoomSelectButton, &ButtonRect, Localize("UClient chat settings"));
}

namespace
{
static bool IsUrlStart(const char *pStr)
{
	return str_startswith(pStr, "http://") || str_startswith(pStr, "https://") || str_startswith(pStr, "settings://");
}

static bool IsTokenEnd(char c)
{
	return c == '\0' || c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static bool IsTrimmedUrlChar(char c)
{
	return c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' ||
		c == ')' || c == ']' || c == '}' || c == '"' || c == '\'' || c == '>';
}

static std::string ExtractUrlHostLower(const std::string &Url)
{
	const size_t SchemePos = Url.find("://");
	if(SchemePos == std::string::npos)
		return {};

	const size_t HostStart = SchemePos + 3;
	const size_t HostEnd = Url.find_first_of("/?#", HostStart);
	std::string HostPort = Url.substr(HostStart, HostEnd == std::string::npos ? std::string::npos : HostEnd - HostStart);

	// Strip userinfo (user:pass@host).
	const size_t AtPos = HostPort.rfind('@');
	if(AtPos != std::string::npos)
		HostPort = HostPort.substr(AtPos + 1);

	// Strip port (host:port) while supporting IPv6 literals in brackets.
	if(!HostPort.empty() && HostPort.front() == '[')
	{
		const size_t Close = HostPort.find(']');
		if(Close != std::string::npos)
			HostPort = HostPort.substr(1, Close - 1);
	}
	else
	{
		const size_t ColonPos = HostPort.find(':');
		if(ColonPos != std::string::npos)
			HostPort = HostPort.substr(0, ColonPos);
	}

	while(!HostPort.empty() && HostPort.back() == '.')
		HostPort.pop_back();

	std::transform(HostPort.begin(), HostPort.end(), HostPort.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return HostPort;
}

static bool HostIsOrEndsWith(const std::string &HostLower, const char *pDomainLower)
{
	const std::string Domain(pDomainLower);
	if(HostLower == Domain)
		return true;
	if(HostLower.size() <= Domain.size())
		return false;
	const size_t Start = HostLower.size() - Domain.size();
	return HostLower.compare(Start, Domain.size(), Domain) == 0 && HostLower[Start - 1] == '.';
}

static std::string TrimAsciiWhitespaceCopy(std::string Value)
{
	while(!Value.empty() && std::isspace((unsigned char)Value.front()))
		Value.erase(Value.begin());
	while(!Value.empty() && std::isspace((unsigned char)Value.back()))
		Value.pop_back();
	return Value;
}

static std::string NormalizeAllowedMediaDomain(std::string Domain)
{
	Domain = TrimAsciiWhitespaceCopy(std::move(Domain));
	std::transform(Domain.begin(), Domain.end(), Domain.begin(), [](unsigned char c) { return (char)std::tolower(c); });

	const size_t SchemePos = Domain.find("://");
	if(SchemePos != std::string::npos)
		Domain = Domain.substr(SchemePos + 3);

	const size_t AtPos = Domain.rfind('@');
	if(AtPos != std::string::npos)
		Domain = Domain.substr(AtPos + 1);

	if(!Domain.empty() && Domain.front() == '[')
	{
		const size_t ClosePos = Domain.find(']');
		if(ClosePos != std::string::npos)
			Domain = Domain.substr(1, ClosePos - 1);
	}
	else
	{
		const size_t SlashPos = Domain.find_first_of("/?#");
		if(SlashPos != std::string::npos)
			Domain.resize(SlashPos);
		const size_t ColonPos = Domain.find(':');
		if(ColonPos != std::string::npos)
			Domain.resize(ColonPos);
	}

	while(!Domain.empty() && (Domain.front() == '.' || std::isspace((unsigned char)Domain.front())))
		Domain.erase(Domain.begin());
	while(!Domain.empty() && (Domain.back() == '.' || std::isspace((unsigned char)Domain.back())))
		Domain.pop_back();

	return Domain;
}

static constexpr const char *s_pDefaultChatMediaAllowedDomains = "tenor.com; imgur.com; giphy.com";

static bool IsAllowedChatMediaHostByDomainList(const std::string &HostLower, const char *pList, bool &HasDomains)
{
	HasDomains = false;
	if(pList == nullptr || pList[0] == '\0')
		return false;

	const char *pTokenStart = pList;
	while(true)
	{
		const char *pSep = str_find(pTokenStart, ";");
		const size_t TokenLen = pSep ? (size_t)(pSep - pTokenStart) : str_length(pTokenStart);
		std::string Domain = NormalizeAllowedMediaDomain(std::string(pTokenStart, TokenLen));
		if(!Domain.empty())
		{
			HasDomains = true;
			if(HostLower == Domain)
				return true;
			if(HostLower.size() > Domain.size())
			{
				const size_t Start = HostLower.size() - Domain.size();
				if(HostLower.compare(Start, Domain.size(), Domain) == 0 && HostLower[Start - 1] == '.')
					return true;
			}
		}

		if(!pSep)
			break;
		pTokenStart = pSep + 1;
	}

	return false;
}

static bool IsAllowedChatMediaHost(const std::string &HostLower)
{
	if(!g_Config.m_BcChatMediaContentFilter)
		return true;
	if(HostLower.empty())
		return false;

	bool HasConfiguredDomains = false;
	if(IsAllowedChatMediaHostByDomainList(HostLower, g_Config.m_BcChatMediaAllowedDomains, HasConfiguredDomains))
		return true;
	if(HasConfiguredDomains)
		return false;

	bool HasDefaultDomains = false;
	return IsAllowedChatMediaHostByDomainList(HostLower, s_pDefaultChatMediaAllowedDomains, HasDefaultDomains);
}

static bool IsAllowedChatMediaUrl(const char *pUrl)
{
	if(!g_Config.m_BcChatMediaContentFilter)
		return true;
	if(pUrl == nullptr || pUrl[0] == '\0')
		return false;
	return IsAllowedChatMediaHost(ExtractUrlHostLower(pUrl));
}

// Separate, narrower allowlist than IsAllowedChatMediaUrl: only links from these domains
// pop the above-head gif bubble, so a random tenor/imgur link someone pastes doesn't spam it.
static bool IsGifBubbleUrl(const char *pUrl)
{
	if(!g_Config.m_BcGifBubbleAboveHead || pUrl == nullptr || pUrl[0] == '\0')
		return false;
	bool HasDomains = false;
	return IsAllowedChatMediaHostByDomainList(ExtractUrlHostLower(pUrl), g_Config.m_BcGifBubbleDomains, HasDomains);
}

static bool IsYouTubeUrl(const std::string &Url)
{
	const std::string HostLower = ExtractUrlHostLower(Url);
	if(HostLower.empty())
		return false;

	// Prevent media previews for YouTube links (the media preview fetcher may otherwise resolve
	// thumbnails/embeds from HTML/JSON-LD).
	return HostIsOrEndsWith(HostLower, "youtube.com") ||
		HostIsOrEndsWith(HostLower, "youtu.be") ||
		HostIsOrEndsWith(HostLower, "youtube-nocookie.com") ||
		HostIsOrEndsWith(HostLower, "ytimg.com") ||
		HostIsOrEndsWith(HostLower, "googlevideo.com");
}

static std::string ExtractUrlPath(const std::string &Url)
{
	const size_t SchemePos = Url.find("://");
	if(SchemePos == std::string::npos)
		return {};

	const size_t PathStart = Url.find('/', SchemePos + 3);
	if(PathStart == std::string::npos)
		return "/";

	const size_t PathEnd = Url.find_first_of("?#", PathStart);
	return Url.substr(PathStart, PathEnd == std::string::npos ? std::string::npos : PathEnd - PathStart);
}

static bool ExtractGiphyMediaId(const std::string &Url, std::string &OutMediaId)
{
	OutMediaId.clear();
	const std::string HostLower = ExtractUrlHostLower(Url);
	if(!HostIsOrEndsWith(HostLower, "giphy.com"))
		return false;

	const std::string Path = ExtractUrlPath(Url);
	if(Path.empty() || Path.find("/gifs/") == std::string::npos)
		return false;

	size_t SegmentStart = Path.find_last_of('/');
	if(SegmentStart == std::string::npos || SegmentStart + 1 >= Path.size())
		return false;

	std::string LastSegment = Path.substr(SegmentStart + 1);
	if(LastSegment.empty())
		return false;

	const size_t DashPos = LastSegment.find_last_of('-');
	if(DashPos != std::string::npos && DashPos + 1 < LastSegment.size())
		LastSegment = LastSegment.substr(DashPos + 1);

	if(LastSegment.size() < 6 || LastSegment.size() > 64)
		return false;

	for(char c : LastSegment)
	{
		if(!std::isalnum((unsigned char)c))
			return false;
	}

	OutMediaId = LastSegment;
	return true;
}

static void AddDirectGiphyCandidates(const std::string &Url, std::vector<std::string> &vOutCandidates)
{
	std::string MediaId;
	if(!ExtractGiphyMediaId(Url, MediaId))
		return;

	const char *apHosts[] = {"https://media.giphy.com/media/", "https://media1.giphy.com/media/"};
	const char *apFormats[] = {"giphy.mp4", "giphy.gif", "giphy.webp"};
	for(const char *pHost : apHosts)
	{
		for(const char *pFormat : apFormats)
		{
			std::string Candidate = std::string(pHost) + MediaId + "/" + pFormat;
			if((int)Candidate.size() <= CHAT_MEDIA_MAX_URL_LENGTH)
				vOutCandidates.push_back(std::move(Candidate));
		}
	}
}

static bool ExtractImgurMediaId(const std::string &Url, std::string &OutMediaId)
{
	OutMediaId.clear();
	const std::string HostLower = ExtractUrlHostLower(Url);
	if(!HostIsOrEndsWith(HostLower, "imgur.com"))
		return false;

	const std::string Path = ExtractUrlPath(Url);
	if(Path.empty() || Path == "/")
		return false;

	// Album/gallery/topic share links use post IDs that frequently do not map to a
	// direct i.imgur.com media ID. Let HTML extraction resolve the real media URL.
	const char *apPrefixes[] = {"/a/", "/gallery/", "/t/"};
	for(const char *pPrefix : apPrefixes)
	{
		if(str_startswith(Path.c_str(), pPrefix))
			return false;
	}

	size_t SegmentStart = Path.find_last_of('/');
	if(SegmentStart == std::string::npos || SegmentStart + 1 >= Path.size())
		return false;

	std::string LastSegment = Path.substr(SegmentStart + 1);
	const size_t DotPos = LastSegment.find('.');
	if(DotPos != std::string::npos)
		LastSegment.resize(DotPos);

	if(LastSegment.size() < 4 || LastSegment.size() > 16)
		return false;

	for(char c : LastSegment)
	{
		if(!std::isalnum((unsigned char)c))
			return false;
	}

	OutMediaId = LastSegment;
	return true;
}

static void AddDirectImgurCandidates(const std::string &Url, std::vector<std::string> &vOutCandidates)
{
	std::string MediaId;
	if(!ExtractImgurMediaId(Url, MediaId))
		return;

	const char *apFormats[] = {"mp4", "gif", "webm", "jpg", "jpeg", "png", "webp"};
	for(const char *pFormat : apFormats)
	{
		std::string Candidate = "https://i.imgur.com/" + MediaId + "." + pFormat;
		if((int)Candidate.size() <= CHAT_MEDIA_MAX_URL_LENGTH)
			vOutCandidates.push_back(std::move(Candidate));
	}
}

static bool IsGifSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 6 && (mem_comp(pData, "GIF87a", 6) == 0 || mem_comp(pData, "GIF89a", 6) == 0);
}

static std::string ExtractUrlExtensionLower(const std::string &Url)
{
	const size_t QueryPos = Url.find_first_of("?#");
	const std::string Path = Url.substr(0, QueryPos);
	const size_t SlashPos = Path.find_last_of('/');
	const size_t DotPos = Path.find_last_of('.');
	if(DotPos == std::string::npos || (SlashPos != std::string::npos && DotPos < SlashPos))
		return {};

	std::string Ext = Path.substr(DotPos + 1);
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return Ext;
}

static bool IsLikelyImageExtension(const std::string &Ext)
{
	return Ext == "png" || Ext == "jpg" || Ext == "jpeg" || Ext == "gif" || Ext == "webp" || Ext == "bmp" || Ext == "avif" || Ext == "apng";
}

static bool IsLikelyAnimatedImageExtension(const std::string &Ext)
{
	return Ext == "gif" || Ext == "webp" || Ext == "apng" || Ext == "avif";
}

static bool IsLikelyVideoExtension(const std::string &Ext)
{
	return Ext == "mp4" || Ext == "webm" || Ext == "mov" || Ext == "m4v" || Ext == "mkv" || Ext == "avi" ||
		Ext == "gifv" || Ext == "mpg" || Ext == "mpeg" || Ext == "ogv" || Ext == "3gp" || Ext == "3g2" ||
		Ext == "flv" || Ext == "wmv" || Ext == "asf" || Ext == "ts" || Ext == "m2ts" || Ext == "mts" || Ext == "f4v";
}

static bool IsBlockedMediaExtension(const std::string &Ext)
{
	return Ext == "svg" || Ext == "svgz" || Ext == "ico" || Ext == "css" || Ext == "js" || Ext == "json" || Ext == "txt" || Ext == "xml" || Ext == "pdf" || Ext == "html" || Ext == "htm";
}

static bool IsLikelyMediaExtension(const std::string &Ext)
{
	return IsLikelyImageExtension(Ext) || IsLikelyVideoExtension(Ext);
}

static bool IsPngSignature(const unsigned char *pData, size_t DataSize)
{
	static const unsigned char s_aPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	return DataSize >= 8 && mem_comp(pData, s_aPngSig, 8) == 0;
}

static bool IsJpegSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 3 && pData[0] == 0xff && pData[1] == 0xd8 && pData[2] == 0xff;
}

static bool IsWebpSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 12 && mem_comp(pData, "RIFF", 4) == 0 && mem_comp(pData + 8, "WEBP", 4) == 0;
}

static bool IsBmpSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 2 && pData[0] == 'B' && pData[1] == 'M';
}

static bool IsMp4LikeSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 12 && mem_comp(pData + 4, "ftyp", 4) == 0;
}

static bool IsWebmSignature(const unsigned char *pData, size_t DataSize)
{
	static const unsigned char s_aWebmSig[4] = {0x1a, 0x45, 0xdf, 0xa3};
	return DataSize >= 4 && mem_comp(pData, s_aWebmSig, 4) == 0;
}

static bool IsAviSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 12 && mem_comp(pData, "RIFF", 4) == 0 && mem_comp(pData + 8, "AVI ", 4) == 0;
}

static bool IsFlvSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 3 && mem_comp(pData, "FLV", 3) == 0;
}

static bool IsMpegProgramStreamSignature(const unsigned char *pData, size_t DataSize)
{
	static const unsigned char s_aMpegPsSig[4] = {0x00, 0x00, 0x01, 0xba};
	return DataSize >= 4 && mem_comp(pData, s_aMpegPsSig, 4) == 0;
}

static bool IsMpegTransportStreamSignature(const unsigned char *pData, size_t DataSize)
{
	// MPEG-TS packets are 188 bytes and start with sync byte 0x47.
	return DataSize >= 376 && pData[0] == 0x47 && pData[188] == 0x47;
}

static bool IsOggSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 4 && mem_comp(pData, "OggS", 4) == 0;
}

static bool IsImagePayloadSignature(const unsigned char *pData, size_t DataSize)
{
	return IsPngSignature(pData, DataSize) || IsJpegSignature(pData, DataSize) || IsGifSignature(pData, DataSize) || IsWebpSignature(pData, DataSize) || IsBmpSignature(pData, DataSize);
}

static bool IsVideoPayloadSignature(const unsigned char *pData, size_t DataSize)
{
	return IsMp4LikeSignature(pData, DataSize) || IsWebmSignature(pData, DataSize) || IsOggSignature(pData, DataSize) ||
		IsAviSignature(pData, DataSize) || IsFlvSignature(pData, DataSize) || IsMpegProgramStreamSignature(pData, DataSize) ||
		IsMpegTransportStreamSignature(pData, DataSize);
}

static std::string ToLowerAscii(std::string Value)
{
	std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return Value;
}

static void ReplaceAll(std::string &Value, const char *pFrom, const char *pTo)
{
	const std::string From(pFrom);
	const std::string To(pTo);
	size_t Pos = 0;
	while((Pos = Value.find(From, Pos)) != std::string::npos)
	{
		Value.replace(Pos, From.size(), To);
		Pos += To.size();
	}
}

static std::string DecodeHtmlUrl(std::string Value)
{
	ReplaceAll(Value, "&amp;", "&");
	ReplaceAll(Value, "&quot;", "\"");
	ReplaceAll(Value, "&#39;", "'");
	ReplaceAll(Value, "&lt;", "<");
	ReplaceAll(Value, "&gt;", ">");
	ReplaceAll(Value, "\\/", "/");
	return Value;
}

static void TrimAsciiWhitespace(std::string &Value)
{
	while(!Value.empty() && std::isspace((unsigned char)Value.front()))
		Value.erase(Value.begin());
	while(!Value.empty() && std::isspace((unsigned char)Value.back()))
		Value.pop_back();
}

static bool ExtractHtmlAttribute(const std::string &Tag, const std::string &TagLower, const char *pAttrName, std::string &OutValue)
{
	const std::string AttrName = ToLowerAscii(pAttrName);
	size_t Pos = 0;
	while((Pos = TagLower.find(AttrName, Pos)) != std::string::npos)
	{
		const bool LeftBoundary = Pos == 0 || std::isspace((unsigned char)TagLower[Pos - 1]) || TagLower[Pos - 1] == '<' || TagLower[Pos - 1] == '/';
		if(!LeftBoundary)
		{
			Pos += AttrName.size();
			continue;
		}

		size_t EqPos = Pos + AttrName.size();
		while(EqPos < TagLower.size() && std::isspace((unsigned char)TagLower[EqPos]))
			EqPos++;
		if(EqPos >= TagLower.size() || TagLower[EqPos] != '=')
		{
			Pos += AttrName.size();
			continue;
		}
		EqPos++;
		while(EqPos < Tag.size() && std::isspace((unsigned char)Tag[EqPos]))
			EqPos++;
		if(EqPos >= Tag.size())
			return false;

		size_t ValueBegin = EqPos;
		size_t ValueEnd = EqPos;
		if(Tag[EqPos] == '"' || Tag[EqPos] == '\'')
		{
			const char Quote = Tag[EqPos];
			ValueBegin = EqPos + 1;
			ValueEnd = Tag.find(Quote, ValueBegin);
			if(ValueEnd == std::string::npos)
				return false;
		}
		else
		{
			while(ValueEnd < Tag.size() && !std::isspace((unsigned char)Tag[ValueEnd]) && Tag[ValueEnd] != '>')
				ValueEnd++;
		}

		OutValue = DecodeHtmlUrl(Tag.substr(ValueBegin, ValueEnd - ValueBegin));
		TrimAsciiWhitespace(OutValue);
		return !OutValue.empty();
	}
	return false;
}

static bool ResolveRelativeUrl(const std::string &BaseUrl, const std::string &CandidateUrl, std::string &OutResolvedUrl)
{
	if(CandidateUrl.empty())
		return false;
	if(str_startswith(CandidateUrl.c_str(), "http://") || str_startswith(CandidateUrl.c_str(), "https://"))
	{
		OutResolvedUrl = CandidateUrl;
		return true;
	}
	if(str_startswith(CandidateUrl.c_str(), "//"))
	{
		const size_t SchemePos = BaseUrl.find("://");
		if(SchemePos == std::string::npos)
			return false;
		OutResolvedUrl = BaseUrl.substr(0, SchemePos) + ":" + CandidateUrl;
		return true;
	}
	if(CandidateUrl[0] == '#')
		return false;

	const size_t SchemePos = BaseUrl.find("://");
	if(SchemePos == std::string::npos)
		return false;
	const size_t HostStart = SchemePos + 3;
	const size_t PathStart = BaseUrl.find('/', HostStart);
	const std::string Origin = PathStart == std::string::npos ? BaseUrl : BaseUrl.substr(0, PathStart);

	if(CandidateUrl[0] == '/')
	{
		OutResolvedUrl = Origin + CandidateUrl;
		return true;
	}

	std::string BasePath = PathStart == std::string::npos ? "/" : BaseUrl.substr(PathStart);
	const size_t QueryPos = BasePath.find_first_of("?#");
	if(QueryPos != std::string::npos)
		BasePath.resize(QueryPos);
	size_t LastSlash = BasePath.find_last_of('/');
	if(LastSlash == std::string::npos)
		BasePath = "/";
	else
		BasePath.resize(LastSlash + 1);

	OutResolvedUrl = Origin + BasePath + CandidateUrl;
	return true;
}

static bool ResolveAndFilterCandidateUrl(const char *pBaseUrl, const std::string &RawCandidate, std::string &OutResolvedUrl, bool AllowUnknownExtensions)
{
	std::string Candidate = DecodeHtmlUrl(RawCandidate);
	TrimAsciiWhitespace(Candidate);
	if(Candidate.empty())
		return false;

	const std::string CandidateLower = ToLowerAscii(Candidate);
	if(str_startswith(CandidateLower.c_str(), "data:") || str_startswith(CandidateLower.c_str(), "blob:") ||
		str_startswith(CandidateLower.c_str(), "javascript:") || str_startswith(CandidateLower.c_str(), "mailto:") ||
		str_startswith(CandidateLower.c_str(), "about:"))
	{
		return false;
	}

	std::string Resolved;
	if(IsUrlStart(Candidate.c_str()))
		Resolved = Candidate;
	else
	{
		if(!pBaseUrl || !IsUrlStart(pBaseUrl) || !ResolveRelativeUrl(pBaseUrl, Candidate, Resolved))
			return false;
	}

	if(!IsUrlStart(Resolved.c_str()))
		return false;
	if((int)Resolved.size() > CHAT_MEDIA_MAX_URL_LENGTH)
		return false;
	for(char c : Resolved)
	{
		if((unsigned char)c < 32 || c == ' ' || c == '\t' || c == '\n' || c == '\r')
			return false;
	}

	const std::string LowerResolved = ToLowerAscii(Resolved);
	const std::string Ext = ExtractUrlExtensionLower(LowerResolved);
	if(!Ext.empty() && IsBlockedMediaExtension(Ext))
		return false;
	if(!AllowUnknownExtensions && !Ext.empty() && !IsLikelyMediaExtension(Ext))
		return false;

	OutResolvedUrl = Resolved;
	return true;
}

static bool IsLikelyHtmlDocument(const unsigned char *pData, size_t DataSize)
{
	if(!pData || DataSize == 0)
		return false;

	const size_t ScanSize = minimum(DataSize, (size_t)8192);
	std::string Prefix((const char *)pData, ScanSize);
	const std::string PrefixLower = ToLowerAscii(Prefix);
	return PrefixLower.find("<!doctype html") != std::string::npos ||
		PrefixLower.find("<html") != std::string::npos ||
		PrefixLower.find("<head") != std::string::npos ||
		PrefixLower.find("<meta") != std::string::npos;
}

static void FindMetaContentsByKey(const std::string &Html, const std::string &HtmlLower, const char *pKey, std::vector<std::string> &vOutValues)
{
	const std::string KeyLower = ToLowerAscii(pKey);
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<meta", Pos)) != std::string::npos)
	{
		const size_t EndPos = HtmlLower.find('>', Pos);
		if(EndPos == std::string::npos)
			break;
		if(EndPos - Pos > 3072)
		{
			Pos = EndPos + 1;
			continue;
		}

		const std::string Tag = Html.substr(Pos, EndPos - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, EndPos - Pos + 1);
		std::string NameOrProperty;
		const bool MatchesProperty = ExtractHtmlAttribute(Tag, TagLower, "property", NameOrProperty) && ToLowerAscii(NameOrProperty) == KeyLower;
		const bool MatchesName = ExtractHtmlAttribute(Tag, TagLower, "name", NameOrProperty) && ToLowerAscii(NameOrProperty) == KeyLower;
		if(MatchesProperty || MatchesName)
		{
			std::string Value;
			if(ExtractHtmlAttribute(Tag, TagLower, "content", Value) ||
				ExtractHtmlAttribute(Tag, TagLower, "src", Value) ||
				ExtractHtmlAttribute(Tag, TagLower, "href", Value))
			{
				vOutValues.push_back(Value);
			}
		}
		Pos = EndPos + 1;
	}
}

static void CollectLinkMediaHrefs(const std::string &Html, const std::string &HtmlLower, std::vector<std::string> &vOutValues)
{
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<link", Pos)) != std::string::npos)
	{
		const size_t EndPos = HtmlLower.find('>', Pos);
		if(EndPos == std::string::npos)
			break;

		const std::string Tag = Html.substr(Pos, EndPos - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, EndPos - Pos + 1);
		std::string Rel;
		if(ExtractHtmlAttribute(Tag, TagLower, "rel", Rel))
		{
			const std::string RelLower = ToLowerAscii(Rel);
			if(RelLower.find("image_src") != std::string::npos || RelLower.find("thumbnail") != std::string::npos ||
				RelLower.find("image") != std::string::npos || RelLower.find("video") != std::string::npos)
			{
				std::string Value;
				if(ExtractHtmlAttribute(Tag, TagLower, "href", Value))
					vOutValues.push_back(Value);
			}
		}

		Pos = EndPos + 1;
	}
}

static void CollectImageTagSources(const std::string &Html, const std::string &HtmlLower, std::vector<std::string> &vOutValues)
{
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<img", Pos)) != std::string::npos)
	{
		const size_t EndPos = HtmlLower.find('>', Pos);
		if(EndPos == std::string::npos)
			break;
		if(EndPos - Pos > 4096)
		{
			Pos = EndPos + 1;
			continue;
		}

		const std::string Tag = Html.substr(Pos, EndPos - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, EndPos - Pos + 1);
		std::string Value;
		if(ExtractHtmlAttribute(Tag, TagLower, "src", Value) ||
			ExtractHtmlAttribute(Tag, TagLower, "data-src", Value) ||
			ExtractHtmlAttribute(Tag, TagLower, "data-original", Value))
		{
			vOutValues.push_back(Value);
		}

		Pos = EndPos + 1;
	}
}

static void CollectVideoTagSources(const std::string &Html, const std::string &HtmlLower, std::vector<std::string> &vOutValues)
{
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<video", Pos)) != std::string::npos)
	{
		const size_t EndPos = HtmlLower.find('>', Pos);
		if(EndPos == std::string::npos)
			break;
		if(EndPos - Pos > 4096)
		{
			Pos = EndPos + 1;
			continue;
		}

		const std::string Tag = Html.substr(Pos, EndPos - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, EndPos - Pos + 1);
		std::string Value;
		if(ExtractHtmlAttribute(Tag, TagLower, "src", Value) ||
			ExtractHtmlAttribute(Tag, TagLower, "poster", Value) ||
			ExtractHtmlAttribute(Tag, TagLower, "data-src", Value))
		{
			vOutValues.push_back(Value);
		}

		Pos = EndPos + 1;
	}
}

static void CollectSourceTagSources(const std::string &Html, const std::string &HtmlLower, std::vector<std::string> &vOutValues)
{
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<source", Pos)) != std::string::npos)
	{
		const size_t EndPos = HtmlLower.find('>', Pos);
		if(EndPos == std::string::npos)
			break;
		if(EndPos - Pos > 4096)
		{
			Pos = EndPos + 1;
			continue;
		}

		const std::string Tag = Html.substr(Pos, EndPos - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, EndPos - Pos + 1);
		std::string Value;
		if(ExtractHtmlAttribute(Tag, TagLower, "src", Value))
			vOutValues.push_back(Value);

		Pos = EndPos + 1;
	}
}

static bool TryParseJsonQuotedValue(const std::string &Json, size_t QuotePos, std::string &OutValue, size_t &OutEndPos)
{
	if(QuotePos >= Json.size() || (Json[QuotePos] != '"' && Json[QuotePos] != '\''))
		return false;
	const char Quote = Json[QuotePos];
	std::string Value;
	size_t Pos = QuotePos + 1;
	while(Pos < Json.size())
	{
		const char c = Json[Pos++];
		if(c == '\\')
		{
			if(Pos >= Json.size())
				break;
			Value.push_back(Json[Pos++]);
			continue;
		}
		if(c == Quote)
		{
			OutValue = Value;
			OutEndPos = Pos;
			return true;
		}
		Value.push_back(c);
	}
	return false;
}

static void FindJsonValuesByKey(const std::string &Json, const std::string &JsonLower, const char *pKey, std::vector<std::string> &vOutValues)
{
	const std::string KeyPattern = "\"" + ToLowerAscii(pKey) + "\"";
	size_t Pos = 0;
	while((Pos = JsonLower.find(KeyPattern, Pos)) != std::string::npos)
	{
		const size_t ColonPos = JsonLower.find(':', Pos + KeyPattern.size());
		if(ColonPos == std::string::npos)
			break;

		size_t ValuePos = ColonPos + 1;
		while(ValuePos < Json.size() && std::isspace((unsigned char)Json[ValuePos]))
			ValuePos++;
		if(ValuePos >= Json.size())
			break;

		if(Json[ValuePos] == '"' || Json[ValuePos] == '\'')
		{
			std::string Value;
			size_t EndPos = ValuePos;
			if(TryParseJsonQuotedValue(Json, ValuePos, Value, EndPos))
			{
				vOutValues.push_back(Value);
				Pos = EndPos;
				continue;
			}
		}
		else
		{
			size_t EndPos = ValuePos;
			while(EndPos < Json.size() && Json[EndPos] != ',' && Json[EndPos] != '}' && Json[EndPos] != ']' && !std::isspace((unsigned char)Json[EndPos]))
				EndPos++;
			if(EndPos > ValuePos)
			{
				vOutValues.emplace_back(Json.substr(ValuePos, EndPos - ValuePos));
				Pos = EndPos;
				continue;
			}
		}

		Pos += KeyPattern.size();
	}
}

static void CollectJsonLdMediaCandidates(const std::string &Html, const std::string &HtmlLower, std::vector<std::string> &vOutValues)
{
	size_t Pos = 0;
	while((Pos = HtmlLower.find("<script", Pos)) != std::string::npos)
	{
		const size_t TagEnd = HtmlLower.find('>', Pos);
		if(TagEnd == std::string::npos)
			break;
		const size_t ClosePos = HtmlLower.find("</script>", TagEnd + 1);
		if(ClosePos == std::string::npos)
			break;

		const std::string Tag = Html.substr(Pos, TagEnd - Pos + 1);
		const std::string TagLower = HtmlLower.substr(Pos, TagEnd - Pos + 1);
		std::string TypeValue;
		if(!ExtractHtmlAttribute(Tag, TagLower, "type", TypeValue) || ToLowerAscii(TypeValue).find("ld+json") == std::string::npos)
		{
			Pos = ClosePos + 9;
			continue;
		}

		const std::string ScriptBody = Html.substr(TagEnd + 1, ClosePos - (TagEnd + 1));
		const std::string ScriptBodyLower = ToLowerAscii(ScriptBody);
		const char *apJsonKeys[] = {"contentUrl", "thumbnailUrl", "video", "embedUrl", "url", "mp4", "srcUrl"};
		for(const char *pKey : apJsonKeys)
			FindJsonValuesByKey(ScriptBody, ScriptBodyLower, pKey, vOutValues);

		Pos = ClosePos + 9;
	}
}

static void ExtractMediaUrlsFromHtmlDocument(const unsigned char *pData, size_t DataSize, const char *pBaseUrl, std::vector<std::string> &vOutUrls)
{
	vOutUrls.clear();
	if(!pData || DataSize == 0 || !pBaseUrl || !IsLikelyHtmlDocument(pData, DataSize))
		return;

	const size_t MaxHtmlParseSize = 256 * 1024;
	const size_t HtmlSize = minimum(DataSize, MaxHtmlParseSize);
	const std::string Html((const char *)pData, HtmlSize);
	const std::string HtmlLower = ToLowerAscii(Html);

	struct SPrioritizedCandidate
	{
		int m_Priority = 0;
		std::string m_Value;
	};

	std::vector<SPrioritizedCandidate> vRawCandidates;
	const auto AddCandidates = [&](int Priority, const std::vector<std::string> &vValues) {
		for(const std::string &Value : vValues)
		{
			vRawCandidates.push_back({Priority, Value});
			if((int)vRawCandidates.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES * 8)
				return;
		}
	};

	const char *apMetaVideoKeys[] = {"og:video", "og:video:url", "og:video:secure_url", "twitter:video", "twitter:video:src", "twitter:player:stream"};
	for(const char *pKey : apMetaVideoKeys)
	{
		std::vector<std::string> vValues;
		FindMetaContentsByKey(Html, HtmlLower, pKey, vValues);
		AddCandidates(0, vValues);
	}

	const char *apMetaImageKeys[] = {"og:image", "og:image:url", "og:image:secure_url", "twitter:image", "twitter:image:src"};
	for(const char *pKey : apMetaImageKeys)
	{
		std::vector<std::string> vValues;
		FindMetaContentsByKey(Html, HtmlLower, pKey, vValues);
		AddCandidates(1, vValues);
	}

	{
		std::vector<std::string> vValues;
		CollectVideoTagSources(Html, HtmlLower, vValues);
		AddCandidates(1, vValues);
	}
	{
		std::vector<std::string> vValues;
		CollectSourceTagSources(Html, HtmlLower, vValues);
		AddCandidates(1, vValues);
	}
	{
		std::vector<std::string> vValues;
		CollectLinkMediaHrefs(Html, HtmlLower, vValues);
		AddCandidates(2, vValues);
	}
	{
		std::vector<std::string> vValues;
		CollectJsonLdMediaCandidates(Html, HtmlLower, vValues);
		AddCandidates(2, vValues);
	}
	{
		std::vector<std::string> vValues;
		CollectImageTagSources(Html, HtmlLower, vValues);
		AddCandidates(3, vValues);
	}

	std::vector<std::pair<int, std::string>> vResolvedCandidates;
	for(const auto &Candidate : vRawCandidates)
	{
		if((int)vResolvedCandidates.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
			break;
		std::string Resolved;
		if(!ResolveAndFilterCandidateUrl(pBaseUrl, Candidate.m_Value, Resolved, true))
			continue;
		if(str_comp(Resolved.c_str(), pBaseUrl) == 0)
			continue;

		bool Exists = false;
		for(const auto &Entry : vResolvedCandidates)
		{
			if(str_comp(Entry.second.c_str(), Resolved.c_str()) == 0)
			{
				Exists = true;
				break;
			}
		}
		if(!Exists)
			vResolvedCandidates.emplace_back(Candidate.m_Priority, std::move(Resolved));
	}

	std::stable_sort(vResolvedCandidates.begin(), vResolvedCandidates.end(),
		[](const auto &A, const auto &B) { return A.first < B.first; });

	for(const auto &Entry : vResolvedCandidates)
	{
		if((int)vOutUrls.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
			break;
		vOutUrls.push_back(Entry.second);
	}
}
// ---- URL click / link safety helpers ----

struct SUrlMatch
{
	int m_Start;
	int m_Length;
	std::string m_TargetUrl;
};

struct SMarkdownLinkMatch
{
	int m_Start;
	int m_ConsumedLength;
	std::string m_DisplayText;
	std::string m_TargetUrl;
};

static ColorRGBA ChatLinkColor()
{
	return color_cast<ColorRGBA, ColorHSLA>(ColorHSLA(g_Config.m_ClMessageLinkColor));
}

static STextBoundingBox TightenLinkHoverBounds(STextBoundingBox Bounds, float FontSize)
{
	const float TargetHeight = maximum(1.0f, FontSize * 0.9f);
	if(Bounds.m_H > TargetHeight)
	{
		const float Trim = (Bounds.m_H - TargetHeight) / 2.0f;
		Bounds.m_Y += Trim;
		Bounds.m_H = TargetHeight;
	}
	return Bounds;
}

static bool IsUrlBoundaryCharacter(char c)
{
	return c == '\0' || std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == '[' || c == '{' || c == '<' || c == '"' || c == '\'';
}

static bool IsTrailingUrlPunctuation(char c)
{
	return c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':' || c == ')' || c == ']' || c == '}' || c == '"' || c == '\'';
}

static bool IsAsciiAlphaNumOrHyphen(char c)
{
	return std::isalnum(static_cast<unsigned char>(c)) || c == '-';
}

static bool IsValidHostLabel(std::string_view Label)
{
	if(Label.empty() || Label.size() > 63)
		return false;
	if(Label.front() == '-' || Label.back() == '-')
		return false;
	return std::all_of(Label.begin(), Label.end(), IsAsciiAlphaNumOrHyphen);
}

static bool IsValidUrlHost(std::string_view Host)
{
	if(Host.empty() || Host.find('.') == std::string_view::npos)
		return false;

	size_t LabelStart = 0;
	size_t LabelCount = 0;
	while(LabelStart < Host.size())
	{
		const size_t Dot = Host.find('.', LabelStart);
		const size_t LabelEnd = Dot == std::string_view::npos ? Host.size() : Dot;
		const std::string_view Label = Host.substr(LabelStart, LabelEnd - LabelStart);
		if(!IsValidHostLabel(Label))
			return false;
		++LabelCount;
		if(Dot == std::string_view::npos)
		{
			if(Label.size() < 2 || Label.size() > 24)
				return false;
			if(!std::all_of(Label.begin(), Label.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); }))
				return false;
			break;
		}
		LabelStart = Dot + 1;
	}
	return LabelCount >= 2;
}

static bool LinkDomainMatchesPolicy(std::string_view Host, const std::unordered_set<std::string> &vDomains)
{
	for(const auto &Domain : vDomains)
	{
		if(Host == Domain)
			return true;
		if(Host.size() > Domain.size() && Host.ends_with(Domain) && Host[Host.size() - Domain.size() - 1] == '.')
			return true;
	}
	return false;
}

static std::string NormalizeLinkDomain(std::string_view Domain)
{
	std::string Result;
	Result.reserve(Domain.size());
	for(char c : Domain)
		Result.push_back((char)std::tolower((unsigned char)c));
	if(str_startswith(Result.c_str(), "www."))
		Result.erase(0, 4);
	if(const size_t Colon = Result.rfind(':'); Colon != std::string::npos)
	{
		const std::string_view Port(Result.c_str() + Colon + 1, Result.size() - Colon - 1);
		if(!Port.empty() && std::all_of(Port.begin(), Port.end(), [](char c) { return std::isdigit((unsigned char)c); }))
			Result.erase(Colon);
	}
	return Result;
}

static bool ExtractNormalizedHostFromClickableUrl(std::string_view Url, std::string &NormalizedHost, bool &Https, bool &Http)
{
	Https = false;
	Http = false;
	std::string_view Remainder;
	if(Url.rfind("https://", 0) == 0)
	{
		Https = true;
		Remainder = Url.substr(8);
	}
	else if(Url.rfind("http://", 0) == 0)
	{
		Http = true;
		Remainder = Url.substr(7);
	}
	else
		return false;

	const size_t AuthorityEnd = Remainder.find_first_of("/?#");
	std::string_view Authority = AuthorityEnd == std::string_view::npos ? Remainder : Remainder.substr(0, AuthorityEnd);
	if(Authority.empty() || Authority.find('@') != std::string_view::npos)
		return false;

	std::string Host = NormalizeLinkDomain(Authority);
	if(!IsValidUrlHost(Host))
		return false;

	NormalizedHost = std::move(Host);
	return true;
}

static bool ExtractNormalizedHostFromBareClickableLink(std::string_view Link, std::string &NormalizedHost)
{
	const size_t AuthorityEnd = Link.find_first_of("/?#");
	std::string_view Authority = AuthorityEnd == std::string_view::npos ? Link : Link.substr(0, AuthorityEnd);
	if(Authority.empty() || Authority.find('@') != std::string_view::npos)
		return false;

	std::string Host = NormalizeLinkDomain(Authority);
	if(!IsValidUrlHost(Host))
		return false;

	NormalizedHost = std::move(Host);
	return true;
}

static bool NormalizeClickableLink(std::string_view Link, const std::unordered_set<std::string> &vSafeDomains, std::string &NormalizedUrl, std::string &NormalizedHost, bool &Https, bool &Http)
{
	if(ExtractNormalizedHostFromClickableUrl(Link, NormalizedHost, Https, Http))
	{
		NormalizedUrl.assign(Link.begin(), Link.end());
		return true;
	}

	Https = false;
	Http = false;
	if(!ExtractNormalizedHostFromBareClickableLink(Link, NormalizedHost))
		return false;
	if(!LinkDomainMatchesPolicy(NormalizedHost, vSafeDomains))
		return false;

	NormalizedUrl = "https://";
	NormalizedUrl.append(Link.begin(), Link.end());
	return true;
}

static std::string NormalizeLinkPolicyEntry(std::string_view Entry)
{
	while(!Entry.empty() && std::isspace((unsigned char)Entry.front()))
		Entry.remove_prefix(1);
	while(!Entry.empty() && std::isspace((unsigned char)Entry.back()))
		Entry.remove_suffix(1);

	std::string Host;
	bool Https = false;
	bool Http = false;
	if(ExtractNormalizedHostFromClickableUrl(Entry, Host, Https, Http))
		return Host;
	return NormalizeLinkDomain(Entry);
}

static void ParseLinkPolicyArray(const json_value *pValue, std::unordered_set<std::string> &vDomains)
{
	if(!pValue || pValue->type != json_array)
		return;

	const int Length = json_array_length(pValue);
	for(int i = 0; i < Length; ++i)
	{
		const json_value *pEntry = json_array_get(pValue, i);
		if(!pEntry || pEntry->type != json_string)
			continue;

		const std::string Domain = NormalizeLinkPolicyEntry(pEntry->u.string.ptr);
		if(IsValidUrlHost(Domain))
			vDomains.insert(Domain);
	}
}

static bool TryParseClickableUrlAt(const char *pText, int Index, const std::unordered_set<std::string> &vSafeDomains, SUrlMatch &Match)
{
	if(Index > 0 && !IsUrlBoundaryCharacter(pText[Index - 1]))
		return false;

	const int SettingsSchemeLength = str_startswith(pText + Index, "settings://") ? 11 : 0;
	const int SchemeLength = SettingsSchemeLength > 0 ? SettingsSchemeLength :
		(str_startswith(pText + Index, "https://") ? 8 : (str_startswith(pText + Index, "http://") ? 7 : 0));
	if(SchemeLength <= 0 && SettingsSchemeLength <= 0)
	{
		// Fall through to bare-host / safe-domain parsing via NormalizeClickableLink below.
	}

	int End = Index + maximum(SchemeLength, 0);
	if(SchemeLength <= 0)
		End = Index; // bare host candidates start at Index
	while(pText[End] != '\0' && !std::isspace(static_cast<unsigned char>(pText[End])))
		++End;
	while(End > Index + maximum(SchemeLength, 0) && IsTrailingUrlPunctuation(pText[End - 1]))
		--End;
	if(End <= Index + maximum(SchemeLength, 0))
		return false;

	const std::string_view Candidate(pText + Index, End - Index);
	if(SettingsSchemeLength > 0)
	{
		Match.m_Start = Index;
		Match.m_Length = End - Index;
		Match.m_TargetUrl = std::string(Candidate);
		return true;
	}

	std::string TargetUrl;
	std::string Host;
	bool Https = false;
	bool Http = false;
	if(!NormalizeClickableLink(Candidate, vSafeDomains, TargetUrl, Host, Https, Http))
		return false;

	Match.m_Start = Index;
	Match.m_Length = End - Index;
	Match.m_TargetUrl = std::move(TargetUrl);
	return true;
}

static std::vector<SUrlMatch> FindClickableUrlMatches(const char *pText, const std::unordered_set<std::string> &vSafeDomains)
{
	std::vector<SUrlMatch> vMatches;
	for(int i = 0; pText[i] != '\0'; ++i)
	{
		SUrlMatch Match;
		if(TryParseClickableUrlAt(pText, i, vSafeDomains, Match))
		{
			vMatches.push_back(Match);
			i += Match.m_Length - 1;
		}
	}
	return vMatches;
}

static bool TryParseClickableMarkdownLinkAt(const char *pText, int Index, const std::unordered_set<std::string> &vSafeDomains, SMarkdownLinkMatch &Match)
{
	if(pText[Index] != '[')
		return false;

	int LabelEnd = Index + 1;
	while(pText[LabelEnd] != '\0' && pText[LabelEnd] != ']')
		++LabelEnd;
	if(pText[LabelEnd] != ']' || LabelEnd == Index + 1)
		return false;
	if(pText[LabelEnd + 1] != '(')
		return false;

	int UrlStart = LabelEnd + 2;
	int UrlEnd = UrlStart;
	while(pText[UrlEnd] != '\0' && pText[UrlEnd] != ')')
		++UrlEnd;
	if(pText[UrlEnd] != ')' || UrlEnd == UrlStart)
		return false;

	std::string TargetUrl;
	std::string NormalizedHost;
	bool Https = false;
	bool Http = false;
	if(!NormalizeClickableLink(std::string_view(pText + UrlStart, UrlEnd - UrlStart), vSafeDomains, TargetUrl, NormalizedHost, Https, Http))
		return false;

	Match.m_Start = Index;
	Match.m_ConsumedLength = UrlEnd - Index + 1;
	Match.m_DisplayText.assign(pText + Index + 1, LabelEnd - Index - 1);
	Match.m_TargetUrl = std::move(TargetUrl);
	return true;
}

static int MaxFittingUrlChars(ITextRender *pTextRender, const CTextCursor &Cursor, const char *pText, int RemainingLength, float AvailableWidth)
{
	if(RemainingLength <= 0)
		return 0;
	if(AvailableWidth <= 0.0f)
		return 1;

	int Low = 1;
	int High = RemainingLength;
	int Best = 1;
	while(Low <= High)
	{
		const int Mid = (Low + High) / 2;
		const float Width = pTextRender->TextWidth(Cursor.m_FontSize, pText, Mid, -1.0f, Cursor.m_Flags);
		if(Width <= AvailableWidth)
		{
			Best = Mid;
			Low = Mid + 1;
		}
		else
			High = Mid - 1;
	}
	return Best;
}

static void MeasureLinkBounds(ITextRender *pTextRender, const CTextCursor &Cursor, const char *pDisplayText, int DisplayLength, const std::string &TargetUrl, bool AlwaysConfirm, std::vector<STextBoundingBox> &vBounds, std::vector<std::string> &vLinks, std::vector<float> &vFontSizes, std::vector<bool> &vAlwaysConfirm, std::vector<int> *pLinkGroups)
{
	const int LinkGroup = pLinkGroups == nullptr || pLinkGroups->empty() ? 0 : pLinkGroups->back() + 1;
	const auto AddLinkPart = [&](const STextBoundingBox &Bounds, float FontSize) {
		vBounds.push_back(Bounds);
		vLinks.push_back(TargetUrl);
		vFontSizes.push_back(FontSize);
		vAlwaysConfirm.push_back(AlwaysConfirm);
		if(pLinkGroups)
			pLinkGroups->push_back(LinkGroup);
	};
	CTextCursor LinkCursor = Cursor;
	LinkCursor.m_LongestLineWidth = 0.0f;
	LinkCursor.m_LineCount = 1;
	LinkCursor.m_GlyphCount = 0;
	LinkCursor.m_CharCount = 0;
	LinkCursor.m_MaxCharacterHeight = 0.0f;
	LinkCursor.m_vColorSplits.clear();
	pTextRender->TextEx(&LinkCursor, pDisplayText, DisplayLength);

	if(LinkCursor.m_LineCount <= 1 || Cursor.m_LineWidth <= 0.0f)
	{
		STextBoundingBox Bounds = LinkCursor.BoundingBox();
		Bounds.m_X = Cursor.m_X;
		Bounds.m_Y = Cursor.m_Y;
		Bounds.m_W = maximum(0.0f, LinkCursor.m_X - Cursor.m_X);
		Bounds.m_H = LinkCursor.m_FontSize;
		AddLinkPart(Bounds, LinkCursor.m_FontSize);
		return;
	}

	const float LineHeight = maximum(1.0f, LinkCursor.m_AlignedFontSize + LinkCursor.m_AlignedLineSpacing);
	int RemainingStart = 0;
	int RemainingLength = DisplayLength;
	float SegmentX = Cursor.m_X;
	float SegmentY = Cursor.m_Y;
	float AvailableWidth = Cursor.m_LineWidth - (Cursor.m_X - Cursor.m_StartX);

	while(RemainingLength > 0)
	{
		const int SegmentLength = MaxFittingUrlChars(pTextRender, LinkCursor, pDisplayText + RemainingStart, RemainingLength, AvailableWidth);
		const float SegmentWidth = pTextRender->TextWidth(LinkCursor.m_FontSize, pDisplayText + RemainingStart, SegmentLength, -1.0f, LinkCursor.m_Flags);
		AddLinkPart({SegmentX, SegmentY, SegmentWidth, LinkCursor.m_FontSize}, LinkCursor.m_FontSize);

		RemainingStart += SegmentLength;
		RemainingLength -= SegmentLength;
		SegmentX = Cursor.m_StartX;
		SegmentY += LineHeight;
		AvailableWidth = Cursor.m_LineWidth;
	}
}

static void AppendTextWithUrlColors(ITextRender *pTextRender, STextContainerIndex &TextContainerIndex, CTextCursor &Cursor, const char *pText, const std::unordered_set<std::string> &vSafeDomains, std::vector<STextBoundingBox> *pLinkBounds, std::vector<std::string> *pLinks, std::vector<float> *pFontSizes, std::vector<bool> *pAlwaysConfirm, std::vector<int> *pLinkGroups)
{
	int SegmentStart = 0;
	for(int i = 0; pText[i] != '\0';)
	{
		SMarkdownLinkMatch MarkdownMatch;
		SUrlMatch UrlMatch;
		const bool HasMarkdownLink = TryParseClickableMarkdownLinkAt(pText, i, vSafeDomains, MarkdownMatch);
		const bool HasRawUrl = TryParseClickableUrlAt(pText, i, vSafeDomains, UrlMatch);
		if(!HasMarkdownLink && !HasRawUrl)
		{
			++i;
			continue;
		}

		if(i > SegmentStart)
			pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, pText + SegmentStart, i - SegmentStart);

		std::string DisplayText;
		std::string TargetUrl;
		if(HasMarkdownLink)
		{
			DisplayText = MarkdownMatch.m_DisplayText;
			TargetUrl = MarkdownMatch.m_TargetUrl;
			i += MarkdownMatch.m_ConsumedLength;
		}
		else
		{
			DisplayText.assign(pText + i, UrlMatch.m_Length);
			TargetUrl = UrlMatch.m_TargetUrl;
			i += UrlMatch.m_Length;
		}

		if(!DisplayText.empty())
		{
			if(pLinkBounds != nullptr && pLinks != nullptr && pFontSizes != nullptr && pAlwaysConfirm != nullptr)
				MeasureLinkBounds(pTextRender, Cursor, DisplayText.c_str(), (int)DisplayText.size(), TargetUrl, HasMarkdownLink, *pLinkBounds, *pLinks, *pFontSizes, *pAlwaysConfirm, pLinkGroups);

			const auto SavedColorSplits = Cursor.m_vColorSplits;
			Cursor.m_vColorSplits.emplace_back(Cursor.m_CharCount, (int)DisplayText.size(), ChatLinkColor());
			pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, DisplayText.c_str(), (int)DisplayText.size());
			Cursor.m_vColorSplits = SavedColorSplits;
		}

		SegmentStart = i;
	}

	if(pText[SegmentStart] != '\0')
		pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, pText + SegmentStart);
}

// Lays out a server-join announcement body ("joined <ServerName>"): the leading prefix is drawn
// as normal text, and the trailing server-name span is colored like a link and its (single-line)
// bounds captured for hover/click. Mirrors the single-line branch of MeasureLinkBounds.
static void AppendServerJoinBody(ITextRender *pTextRender, STextContainerIndex &TextContainerIndex, CTextCursor &Cursor,
	const char *pBody, const char *pServerName, STextBoundingBox &OutBounds, bool &OutBoundsValid, float &OutFontSize)
{
	OutBoundsValid = false;
	const int BodyLen = str_length(pBody);
	const int NameLen = str_length(pServerName);
	// The server name is the trailing span of the body; locate it (last occurrence) so the
	// coloring/bounds stay correct even if the prefix text changed.
	int NameStart = maximum(0, BodyLen - NameLen);
	if(NameLen > 0 && (NameStart + NameLen > BodyLen || str_comp(pBody + NameStart, pServerName) != 0))
	{
		const char *pFound = nullptr;
		for(const char *pScan = pBody; (pScan = str_find(pScan, pServerName)) != nullptr; ++pScan)
			pFound = pScan;
		NameStart = pFound ? (int)(pFound - pBody) : BodyLen;
	}

	if(NameStart > 0)
		pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, pBody, NameStart);

	const char *pNameText = pBody + NameStart;
	const int NameTextLen = BodyLen - NameStart;
	if(NameTextLen <= 0)
		return;

	CTextCursor LinkCursor = Cursor;
	LinkCursor.m_LongestLineWidth = 0.0f;
	LinkCursor.m_LineCount = 1;
	LinkCursor.m_GlyphCount = 0;
	LinkCursor.m_CharCount = 0;
	LinkCursor.m_MaxCharacterHeight = 0.0f;
	LinkCursor.m_vColorSplits.clear();
	pTextRender->TextEx(&LinkCursor, pNameText, NameTextLen);

	OutBounds.m_X = Cursor.m_X;
	OutBounds.m_Y = Cursor.m_Y;
	OutBounds.m_W = maximum(0.0f, LinkCursor.m_X - Cursor.m_X);
	OutBounds.m_H = LinkCursor.m_FontSize;
	OutFontSize = LinkCursor.m_FontSize;
	OutBoundsValid = true;

	const auto SavedColorSplits = Cursor.m_vColorSplits;
	Cursor.m_vColorSplits.emplace_back(Cursor.m_CharCount, NameTextLen, ChatLinkColor());
	pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, pNameText, NameTextLen);
	Cursor.m_vColorSplits = SavedColorSplits;
}

static ColorRGBA UClientReplyQuotePreviewColor(const ColorRGBA &BodyColor)
{
	return ColorRGBA(0.72f, 0.72f, 0.72f, BodyColor.a);
}

static ColorRGBA UClientReplyQuoteMissingColor(const ColorRGBA &BodyColor)
{
	// Cooler blue-gray so the fallback is distinct from normal quote text.
	return ColorRGBA(0.55f, 0.62f, 0.82f, BodyColor.a * 0.92f);
}

static const char *ReplyQuoteMissingText()
{
	return "Could not load message.";
}

static ColorRGBA UClientReplyQuoteBarColor(float Blend)
{
	return ColorRGBA(0.64f, 0.64f, 0.64f, 0.48f * Blend);
}

static constexpr float REPLY_QUOTE_DASH_LENGTH_FACTOR = 1.35f;
static constexpr float REPLY_QUOTE_TEXT_GAP_FACTOR = 0.18f;
static constexpr float REPLY_QUOTE_BAR_THICKNESS_FACTOR = 0.05f;

static float ReplyQuoteDashLength(float FontSize)
{
	return FontSize * REPLY_QUOTE_DASH_LENGTH_FACTOR;
}

static float ReplyQuoteBarThickness(float FontSize)
{
	return std::clamp(FontSize * REPLY_QUOTE_BAR_THICKNESS_FACTOR, 1.0f, 1.4f);
}

static float ReplyQuoteTextStartOffset(float TeeSize, float FontSize)
{
	return TeeSize * 0.5f + ReplyQuoteDashLength(FontSize) + FontSize * REPLY_QUOTE_TEXT_GAP_FACTOR;
}

static void RenderReplyQuoteConnector(IGraphics *pGraphics, float TeeCenterX, float QuoteY, float QuoteLineH, float QuoteBlockH, float FontSize, float Blend)
{
	const float DashLength = ReplyQuoteDashLength(FontSize);
	const float Thickness = ReplyQuoteBarThickness(FontSize);
	const float Radius = Thickness * 0.5f;
	const float QuoteCenterY = QuoteY + QuoteLineH * 0.5f;
	const float BarTopY = QuoteCenterY - Thickness * 0.5f;
	const float LeftX = TeeCenterX - Thickness * 0.5f;
	// The horizontal arm points at the first quote line; the vertical stem must
	// reach down past every wrapped quote line so it does not look like it is
	// floating above a multi-line quote.
	const float SingleLineEndY = QuoteY + QuoteLineH * 0.88f;
	const float MultiLineEndY = QuoteY + QuoteBlockH - Thickness;
	const float VerticalEndY = maximum(SingleLineEndY, MultiLineEndY);
	const float StemHeight = maximum(0.0f, VerticalEndY - (BarTopY + Thickness));
	const float HorizontalWidth = maximum(Thickness, DashLength - Thickness * 0.5f);
	const ColorRGBA BarColor = UClientReplyQuoteBarColor(Blend);

	// Three abutting pieces (no overlap) so semi-transparent alpha stays even:
	//   [corner][======== horizontal ========]
	//   [stem ]
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(BarColor);
	pGraphics->DrawRectExt(LeftX, BarTopY, Thickness, Thickness, Radius, IGraphics::CORNER_TL);
	pGraphics->DrawRectExt(LeftX + Thickness, BarTopY, HorizontalWidth, Thickness, Radius, IGraphics::CORNER_R);
	if(StemHeight > 0.0f)
		pGraphics->DrawRectExt(LeftX, BarTopY + Thickness, Thickness, StemHeight, Radius, IGraphics::CORNER_B);
	pGraphics->QuadsEnd();
}

static void BuildReplyQuoteLine(ITextRender *pTextRender, float QuoteFontSize, float MaxWidth, const char *pReplyToName, const char *pReplyPreview, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(!pReplyToName || pReplyToName[0] == '\0')
		return;

	const char *pPreview = (pReplyPreview && pReplyPreview[0] != '\0') ? pReplyPreview : ReplyQuoteMissingText();

	char aFull[256];
	str_format(aFull, sizeof(aFull), "%s: %s", pReplyToName, pPreview);
	if(MaxWidth <= 0.0f || pTextRender->TextWidth(QuoteFontSize, aFull, -1, MaxWidth) <= MaxWidth)
	{
		str_copy(pOut, aFull, OutSize);
		return;
	}

	char aPrefix[128];
	str_format(aPrefix, sizeof(aPrefix), "%s: ", pReplyToName);
	const float PrefixWidth = pTextRender->TextWidth(QuoteFontSize, aPrefix, -1, -1.0f);
	const char aEllipsis[] = "...";
	const float EllipsisWidth = pTextRender->TextWidth(QuoteFontSize, aEllipsis, -1, -1.0f);
	const float AvailableWidth = maximum(0.0f, MaxWidth - PrefixWidth - EllipsisWidth);

	char aPreview[128];
	str_copy(aPreview, pPreview, sizeof(aPreview));
	while(aPreview[0] != '\0')
	{
		char aCandidate[256];
		str_format(aCandidate, sizeof(aCandidate), "%s%s", aPrefix, aPreview);
		if(pTextRender->TextWidth(QuoteFontSize, aCandidate, -1, -1.0f) <= PrefixWidth + AvailableWidth)
			break;

		const int PreviewLen = str_length(aPreview);
		if(PreviewLen <= 0)
			break;
		aPreview[PreviewLen - 1] = '\0';
		while(str_length(aPreview) > 0)
		{
			const int Len = str_length(aPreview);
			const unsigned char Last = (unsigned char)aPreview[Len - 1];
			if(Last < 0x80 || Last >= 0xC0)
				break;
			aPreview[Len - 1] = '\0';
		}
	}

	str_format(pOut, OutSize, "%s%s%s", aPrefix, aPreview, aEllipsis);
}

static void AppendTextWithUrlAndMentionColors(ITextRender *pTextRender, STextContainerIndex &TextContainerIndex, CTextCursor &Cursor, const char *pText, const std::unordered_set<std::string> &vSafeDomains, std::vector<STextBoundingBox> *pLinkBounds, std::vector<std::string> *pLinks, std::vector<float> *pFontSizes, std::vector<bool> *pAlwaysConfirm, std::vector<int> *pLinkGroups)
{
	int SegmentStart = 0;

	for(int i = SegmentStart; pText[i] != '\0';)
	{
		SMarkdownLinkMatch MarkdownMatch;
		SUrlMatch UrlMatch;
		const bool HasMarkdownLink = TryParseClickableMarkdownLinkAt(pText, i, vSafeDomains, MarkdownMatch);
		const bool HasRawUrl = TryParseClickableUrlAt(pText, i, vSafeDomains, UrlMatch);
		if(HasMarkdownLink || HasRawUrl)
		{
			if(i > SegmentStart)
				pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, pText + SegmentStart, i - SegmentStart);

			std::string DisplayText;
			std::string TargetUrl;
			if(HasMarkdownLink)
			{
				DisplayText = MarkdownMatch.m_DisplayText;
				TargetUrl = MarkdownMatch.m_TargetUrl;
				i += MarkdownMatch.m_ConsumedLength;
			}
			else
			{
				DisplayText.assign(pText + i, UrlMatch.m_Length);
				TargetUrl = UrlMatch.m_TargetUrl;
				i += UrlMatch.m_Length;
			}

			if(!DisplayText.empty())
			{
				if(pLinkBounds != nullptr && pLinks != nullptr && pFontSizes != nullptr && pAlwaysConfirm != nullptr)
					MeasureLinkBounds(pTextRender, Cursor, DisplayText.c_str(), (int)DisplayText.size(), TargetUrl, HasMarkdownLink, *pLinkBounds, *pLinks, *pFontSizes, *pAlwaysConfirm, pLinkGroups);

				const auto SavedColorSplits = Cursor.m_vColorSplits;
				Cursor.m_vColorSplits.emplace_back(Cursor.m_CharCount, (int)DisplayText.size(), ChatLinkColor());
				pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, DisplayText.c_str(), (int)DisplayText.size());
				Cursor.m_vColorSplits = SavedColorSplits;
			}

			SegmentStart = i;
			continue;
		}

		const char *pOld = pText + i;
		str_utf8_decode(&pOld);
		i = (int)(pOld - pText);
	}

	if(pText[SegmentStart] != '\0')
		pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, pText + SegmentStart);
}

static float AppendReplyQuoteToMeasure(ITextRender *pTextRender, CTextCursor &Cursor, bool HasReply, const char *pReplyToName, const char *pReplyPreview, float FontSize, float QuoteTextStartOffset, float MaxWidth)
{
	if(!HasReply || !pReplyToName || pReplyToName[0] == '\0')
		return 0.0f;

	const float QuoteFontSize = FontSize * 0.85f;
	const float SavedFontSize = Cursor.m_FontSize;
	const float SavedStartX = Cursor.m_StartX;
	const float SavedLineWidth = Cursor.m_LineWidth;
	const float SavedLongestLineWidth = Cursor.m_LongestLineWidth;
	const float StartY = Cursor.m_Y;
	Cursor.m_FontSize = QuoteFontSize;
	Cursor.m_X += QuoteTextStartOffset;

	char aQuoteLine[256];
	BuildReplyQuoteLine(pTextRender, QuoteFontSize, MaxWidth - QuoteTextStartOffset, pReplyToName, pReplyPreview, aQuoteLine, sizeof(aQuoteLine));

	char aNamePart[72];
	str_format(aNamePart, sizeof(aNamePart), "%s: ", pReplyToName);
	const int NamePartLen = str_length(aNamePart);

	// Render the "name: " prefix, then hang-indent the wrapped preview so that
	// continuation lines line up under the first character after "name: ".
	pTextRender->TextEx(&Cursor, aNamePart);
	if(Cursor.m_LineWidth > 0.0f)
	{
		const float ConsumedFromStart = Cursor.m_X - SavedStartX;
		Cursor.m_StartX = Cursor.m_X;
		Cursor.m_LineWidth = maximum(QuoteFontSize * 4.0f, SavedLineWidth - ConsumedFromStart);
	}
	if(NamePartLen < str_length(aQuoteLine))
		pTextRender->TextEx(&Cursor, aQuoteLine + NamePartLen);

	// Restore the wrap margins BEFORE the terminating newline, so the following
	// body line starts at the normal left column instead of the hang-indent X.
	Cursor.m_StartX = SavedStartX;
	Cursor.m_LineWidth = SavedLineWidth;
	pTextRender->TextEx(&Cursor, "\n");

	Cursor.m_FontSize = SavedFontSize;
	Cursor.m_LongestLineWidth = SavedLongestLineWidth;
	// Return the exact vertical advance of the quote block. The body text flows
	// from this same advance, so the tee/name (placed using this height) stay
	// vertically aligned with the body instead of sitting slightly lower.
	return maximum(0.0f, Cursor.m_Y - StartY);
}

static void AppendReplyQuoteToContainer(ITextRender *pTextRender, STextContainerIndex &TextContainerIndex, CTextCursor &Cursor, bool HasReply, const char *pReplyToName, const char *pReplyPreview, float FontSize, const ColorRGBA &BodyColor, float QuoteTextStartOffset, float MaxWidth, float &QuoteRectX, float &QuoteRectY, float &QuoteRectW, float &QuoteRectH, bool &QuoteRectValid)
{
	if(!HasReply || !pReplyToName || pReplyToName[0] == '\0')
		return;

	const float QuoteFontSize = FontSize * 0.85f;
	const float SavedFontSize = Cursor.m_FontSize;
	const float SavedStartX = Cursor.m_StartX;
	const float SavedLineWidth = Cursor.m_LineWidth;
	const float SavedLongestLineWidth = Cursor.m_LongestLineWidth;
	const bool MissingQuote = !pReplyPreview || pReplyPreview[0] == '\0';
	const char *pEffectivePreview = MissingQuote ? ReplyQuoteMissingText() : pReplyPreview;
	const ColorRGBA NameColor = UClientReplyQuotePreviewColor(BodyColor);
	const ColorRGBA PreviewColor = MissingQuote ? UClientReplyQuoteMissingColor(BodyColor) : NameColor;

	Cursor.m_FontSize = QuoteFontSize;
	Cursor.m_X += QuoteTextStartOffset;

	QuoteRectX = Cursor.m_X;
	QuoteRectY = Cursor.m_Y;
	QuoteRectValid = true;

	char aQuoteLine[256];
	BuildReplyQuoteLine(pTextRender, QuoteFontSize, MaxWidth - QuoteTextStartOffset, pReplyToName, pEffectivePreview, aQuoteLine, sizeof(aQuoteLine));

	char aNamePart[72];
	str_format(aNamePart, sizeof(aNamePart), "%s: ", pReplyToName);
	const int NamePartLen = str_length(aNamePart);

	pTextRender->TextColor(NameColor);
	pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, aNamePart);

	// Hang-indent the wrapped preview lines so continuation lines line up under
	// the first character after "name: " instead of the far-left tee column.
	if(Cursor.m_LineWidth > 0.0f)
	{
		const float ConsumedFromStart = Cursor.m_X - SavedStartX;
		Cursor.m_StartX = Cursor.m_X;
		Cursor.m_LineWidth = maximum(QuoteFontSize * 4.0f, SavedLineWidth - ConsumedFromStart);
	}

	if(NamePartLen < str_length(aQuoteLine))
	{
		pTextRender->TextColor(PreviewColor);
		pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, aQuoteLine + NamePartLen);
	}

	// Quote extent measured from the message start (QuoteRectX == SavedStartX + offset).
	// When the quote wraps it fills the whole line, so cap the width at the wrap edge.
	const float FullQuoteTextWidth = pTextRender->TextWidth(QuoteFontSize, aQuoteLine, -1, -1.0f);
	const float QuoteWidthFromStart = QuoteTextStartOffset + FullQuoteTextWidth;
	QuoteRectW = maximum(1.0f, SavedLineWidth > 0.0f ? minimum(QuoteWidthFromStart, SavedLineWidth) : QuoteWidthFromStart);
	QuoteRectH = QuoteFontSize;

	// Restore the wrap margins BEFORE the terminating newline, so the following
	// body line starts at the normal left column instead of the hang-indent X.
	Cursor.m_StartX = SavedStartX;
	Cursor.m_LineWidth = SavedLineWidth;
	pTextRender->CreateOrAppendTextContainer(TextContainerIndex, &Cursor, "\n");

	Cursor.m_FontSize = SavedFontSize;
	// Do not let the quote's accumulated width leak into the body's hanging-indent
	// calculation (which reads m_LongestLineWidth right after this returns).
	Cursor.m_LongestLineWidth = SavedLongestLineWidth;
	pTextRender->TextColor(BodyColor);
}

static bool ContainsCaseInsensitive(std::string_view Haystack, std::string_view Needle)
{
	if(Needle.empty())
		return true;

	auto It = std::search(Haystack.begin(), Haystack.end(), Needle.begin(), Needle.end(), [](char A, char B) {
		return std::tolower((unsigned char)A) == std::tolower((unsigned char)B);
	});
	return It != Haystack.end();
}

static bool IsDirectDownloadPath(std::string_view Url)
{
	std::string_view Remainder;
	if(Url.rfind("https://", 0) == 0)
		Remainder = Url.substr(8);
	else if(Url.rfind("http://", 0) == 0)
		Remainder = Url.substr(7);
	else
		return false;

	const size_t AuthorityEnd = Remainder.find_first_of("/?#");
	if(AuthorityEnd == std::string_view::npos)
		return false;

	std::string_view PathAndQuery = Remainder.substr(AuthorityEnd);
	std::string_view Host = Remainder.substr(0, AuthorityEnd);
	const size_t QueryPos = PathAndQuery.find('?');
	const std::string_view Path = QueryPos == std::string_view::npos ? PathAndQuery : PathAndQuery.substr(0, QueryPos);
	const std::string_view Query = QueryPos == std::string_view::npos ? std::string_view{} : PathAndQuery.substr(QueryPos + 1);

	const char *apDangerousPatterns[] = {
		"/releases/download/",
		"/archive/",
		"/raw/",
		"/files/download/",
		"/download/",
		"/latest/download/",
		"/dl/",
		"/get/",
	};
	for(const char *pPattern : apDangerousPatterns)
	{
		if(Path.find(pPattern) != std::string_view::npos)
			return true;
	}

	std::string PathString(Path);
	const char *apDangerousExtensions[] = {
		".zip", ".7z", ".rar", ".tar", ".gz", ".bz2", ".xz",
		".exe", ".msi", ".apk", ".dmg", ".pkg", ".appimage", ".deb", ".rpm",
		".iso", ".bin", ".dll", ".so", ".dylib",
	};
	for(const char *pExt : apDangerousExtensions)
	{
		if(str_endswith_nocase(PathString.c_str(), pExt) != nullptr)
			return true;
	}

	const char *apDangerousQueryFlags[] = {
		"download=1", "download=true", "dl=1",
		"attachment=1", "attachment=true",
		"response-content-disposition=attachment",
		"export=download",
	};
	for(const char *pFlag : apDangerousQueryFlags)
	{
		if(Query.find(pFlag) != std::string_view::npos)
			return true;
	}

	if(Host == "ddnet.under1111.com" && Path.rfind("/uclient/", 0) == 0)
		return true;

	const char *apDownloadDomains[] = {
		"sourceforge.net", "mediafire.com", "mega.nz",
		"dropbox.com", "1drv.ms", "gofile.io", "wetransfer.com",
	};
	for(const char *pDomain : apDownloadDomains)
	{
		if(Host.find(pDomain) != std::string_view::npos)
			return true;
	}

	return false;
}

// ---- end URL click helpers ----

} // namespace

// Remote UClient lines use CLIENT_MSG but still show a tee + "Name: text" like normal chat.
bool CChat::LineNeedsNameColon(const CLine &Line)
{
	return Line.m_aName[0] != '\0' && (Line.m_ClientId >= 0 || Line.m_UClient);
}

// Announcements read as a sentence ("Name joined X"), so the colon is dropped: with it they look
// like a chat message the user typed.
const char *CChat::LineNameSeparator(const CLine &Line)
{
	return Line.m_ServerAnnouncement ? " " : ": ";
}

// UClient chat can be addressed to everyone, to one game server, or to the sender's friends, and
// none of that is visible in the message itself. Hovering spells it out.
bool CChat::UClientScopeNoteText(const CLine &Line, char *pBuf, size_t BufSize)
{
	if(!Line.m_UClient || Line.m_ServerAnnouncement)
		return false;
	if(Line.m_aUClientRoomName[0])
	{
		str_format(pBuf, BufSize, Localize("This message was sent to the room \"%s\"."), Line.m_aUClientRoomName);
		return true;
	}
	if(Line.m_UClientScope < 0)
		return false;

	if(Line.m_UClientScope == UClientPresence::CHAT_SCOPE_FRIENDS)
	{
		if(Line.m_UClientMine)
			str_copy(pBuf, Localize("This message is only visible to your friends."), BufSize);
		else
			str_format(pBuf, BufSize, Localize("This message is only visible to %s's friends."), Line.m_aName);
	}
	else if(Line.m_UClientScope == UClientPresence::CHAT_SCOPE_SAME_SERVER)
	{
		str_copy(pBuf, Localize("This message is only visible to UClient users on the same server."), BufSize);
	}
	else
	{
		str_copy(pBuf, Localize("This message is visible to all UClient users."), BufSize);
	}
	return true;
}

float CChat::ScopeNoteFontSize() const
{
	return maximum(3.5f, FontSize() * 0.55f);
}

bool CChat::LineNeedsTeePadding(const CLine &Line)
{
	if(g_Config.m_ClChatOld || Line.m_aName[0] == '\0')
		return false;
	return Line.m_ClientId >= 0 || (Line.m_UClient && Line.m_pManagedTeeRenderInfo != nullptr);
}

bool CChat::IsDirectMediaUrl(const char *pUrl)
{
	if(!pUrl || !IsUrlStart(pUrl))
		return false;

	const std::string Ext = ExtractUrlExtensionLower(pUrl);
	return !Ext.empty() && (IsLikelyImageExtension(Ext) || IsLikelyVideoExtension(Ext));
}

CChat::EMediaKind CChat::MediaKindFromUrl(const char *pUrl)
{
	if(!pUrl)
		return EMediaKind::UNKNOWN;

	const std::string Ext = ExtractUrlExtensionLower(pUrl);
	if(IsLikelyVideoExtension(Ext))
		return EMediaKind::VIDEO;
	if(IsLikelyAnimatedImageExtension(Ext))
		return EMediaKind::ANIMATED;
	if(IsLikelyImageExtension(Ext))
		return EMediaKind::PHOTO;
	return EMediaKind::UNKNOWN;
}

void CChat::ExtractMediaUrlsFromText(const char *pText, std::vector<std::string> &vOutUrls)
{
	vOutUrls.clear();
	if(!pText)
		return;

	const char *pCur = pText;
	while(*pCur)
	{
		if(!IsUrlStart(pCur))
		{
			++pCur;
			continue;
		}

		const char *pEnd = pCur;
		while(!IsTokenEnd(*pEnd))
			++pEnd;

		std::string Url(pCur, pEnd - pCur);
		while(!Url.empty() && IsTrimmedUrlChar(Url.back()))
			Url.pop_back();

		if(IsYouTubeUrl(Url))
		{
			pCur = pEnd;
			continue;
		}

		std::vector<std::string> vExpandedUrls;
		AddDirectGiphyCandidates(Url, vExpandedUrls);
		AddDirectImgurCandidates(Url, vExpandedUrls);
		vExpandedUrls.push_back(Url);

		for(const std::string &ExpandedUrl : vExpandedUrls)
		{
			if(!IsUrlStart(ExpandedUrl.c_str()) || (int)ExpandedUrl.size() > CHAT_MEDIA_MAX_URL_LENGTH)
				continue;

			bool Exists = false;
			for(const auto &ExistingUrl : vOutUrls)
			{
				if(str_comp(ExistingUrl.c_str(), ExpandedUrl.c_str()) == 0)
				{
					Exists = true;
					break;
				}
			}
			if(!Exists)
			{
				vOutUrls.push_back(ExpandedUrl);
				if((int)vOutUrls.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
					return;
			}
		}

		pCur = pEnd;
	}
}

void CChat::ResetLineMedia(CLine &Line)
{
	if(Line.m_pMediaRequest)
	{
		Line.m_pMediaRequest->Abort();
		Line.m_pMediaRequest = nullptr;
	}
	if(Line.m_pMediaDecodeJob)
	{
		Line.m_pMediaDecodeJob->Abort();
		Line.m_pMediaDecodeJob = nullptr;
	}

	Line.m_OptMediaDecodedFrames.reset();
	Line.m_MediaUploadIndex = 0;
	Line.m_vMediaFrameEndMs.clear();
	Line.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
	Line.m_vMediaOriginalData.clear();
	Line.m_vMediaOriginalData.shrink_to_fit();
	if(m_MediaViewerFullTextureLine == (int)(&Line - m_aLines))
		FreeMediaViewerFullTexture();
	Line.m_MediaState = EMediaState::NONE;
	Line.m_MediaKind = EMediaKind::UNKNOWN;
	Line.m_aMediaUrl[0] = '\0';
	Line.m_vMediaCandidates.clear();
	Line.m_MediaCandidateIndex = -1;
	Line.m_MediaRetryCount = 0;
	Line.m_MediaAnimated = false;
	Line.m_MediaRevealed = false;
	Line.m_MediaWidth = 0;
	Line.m_MediaHeight = 0;
	Line.m_MediaResolveDepth = 0;
	Line.m_MediaAnimationStart = 0;
	Line.m_aMediaPreviewWidth[0] = 0.0f;
	Line.m_aMediaPreviewWidth[1] = 0.0f;
	Line.m_aMediaPreviewHeight[0] = 0.0f;
	Line.m_aMediaPreviewHeight[1] = 0.0f;
	Line.m_MediaPreviewRectValid = false;
	Line.m_MediaRetryRectValid = false;
}

void CChat::SetMediaCandidates(CLine &Line, const std::vector<std::string> &vCandidates)
{
	Line.m_vMediaCandidates.clear();
	for(const std::string &Candidate : vCandidates)
	{
		if(!IsUrlStart(Candidate.c_str()))
			continue;
		if((int)Candidate.size() > CHAT_MEDIA_MAX_URL_LENGTH)
			continue;
		if(ExtractUrlExtensionLower(Candidate) == "map")
			continue;

		bool Exists = false;
		for(const std::string &Existing : Line.m_vMediaCandidates)
		{
			if(str_comp(Existing.c_str(), Candidate.c_str()) == 0)
			{
				Exists = true;
				break;
			}
		}
		if(!Exists)
		{
			Line.m_vMediaCandidates.push_back(Candidate);
			if((int)Line.m_vMediaCandidates.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
				break;
		}
	}

	Line.m_MediaCandidateIndex = -1;
	Line.m_MediaKind = EMediaKind::UNKNOWN;
	Line.m_aMediaUrl[0] = '\0';
	if(!Line.m_vMediaCandidates.empty())
	{
		Line.m_MediaCandidateIndex = 0;
		str_copy(Line.m_aMediaUrl, Line.m_vMediaCandidates.front().c_str(), sizeof(Line.m_aMediaUrl));
		Line.m_MediaKind = MediaKindFromUrl(Line.m_aMediaUrl);
	}
}

void CChat::InsertMediaCandidates(CLine &Line, const std::vector<std::string> &vCandidates, int InsertIndex)
{
	if(vCandidates.empty())
		return;

	int InsertPos = std::clamp(InsertIndex, 0, (int)Line.m_vMediaCandidates.size());
	for(const std::string &Candidate : vCandidates)
	{
		if(!IsUrlStart(Candidate.c_str()))
			continue;
		if((int)Candidate.size() > CHAT_MEDIA_MAX_URL_LENGTH)
			continue;
		if(ExtractUrlExtensionLower(Candidate) == "map")
			continue;

		bool Exists = false;
		for(const std::string &Existing : Line.m_vMediaCandidates)
		{
			if(str_comp(Existing.c_str(), Candidate.c_str()) == 0)
			{
				Exists = true;
				break;
			}
		}
		if(Exists)
			continue;

		Line.m_vMediaCandidates.insert(Line.m_vMediaCandidates.begin() + InsertPos, Candidate);
		InsertPos++;
		if((int)Line.m_vMediaCandidates.size() >= CHAT_MEDIA_MAX_HTML_CANDIDATES)
			break;
	}
}

bool CChat::QueueNextMediaCandidate(CLine &Line, const char *pReason)
{
	Line.m_OptMediaDecodedFrames.reset();
	Line.m_MediaUploadIndex = 0;
	Line.m_vMediaFrameEndMs.clear();
	Line.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
	Line.m_MediaAnimated = false;
	Line.m_MediaWidth = 0;
	Line.m_MediaHeight = 0;

	const int NextIndex = Line.m_MediaCandidateIndex + 1;
	for(int CandidateIndex = maximum(0, NextIndex); CandidateIndex < (int)Line.m_vMediaCandidates.size(); ++CandidateIndex)
	{
		if(!IsUrlStart(Line.m_vMediaCandidates[CandidateIndex].c_str()))
			continue;
		if(!IsMediaUrlAllowed(Line.m_vMediaCandidates[CandidateIndex].c_str()))
			continue;

		Line.m_MediaCandidateIndex = CandidateIndex;
		str_copy(Line.m_aMediaUrl, Line.m_vMediaCandidates[CandidateIndex].c_str(), sizeof(Line.m_aMediaUrl));
		Line.m_MediaState = EMediaState::QUEUED;
		Line.m_MediaKind = MediaKindFromUrl(Line.m_aMediaUrl);
		Line.m_pMediaRequest = nullptr;
		Line.m_pMediaDecodeJob = nullptr;
		Line.m_MediaRevealed = false;
		Line.m_MediaPreviewRectValid = false;
		Line.m_MediaRetryRectValid = false;
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
		if(g_Config.m_Debug)
			log_debug("chat/media", "Trying fallback candidate (%d/%d): %s (%s)", CandidateIndex + 1, (int)Line.m_vMediaCandidates.size(), Line.m_aMediaUrl, pReason ? pReason : "unknown");
		return true;
	}

	if(g_Config.m_Debug)
		log_debug("chat/media", "No fallback media candidates left (%s)", pReason ? pReason : "unknown");
	return false;
}

bool CChat::RetryMediaLine(CLine &Line)
{
	if(Line.m_MediaState != EMediaState::FAILED)
		return false;

	if(Line.m_MediaRetryCount >= CHAT_MEDIA_MAX_RETRIES)
	{
		if(g_Config.m_Debug)
			log_debug("chat/media", "Retry limit reached for message media");
		return false;
	}

	if(Line.m_vMediaCandidates.empty())
	{
		if(g_Config.m_Debug)
			log_debug("chat/media", "Cannot retry media without candidates");
		return false;
	}

	if(Line.m_pMediaRequest)
	{
		Line.m_pMediaRequest->Abort();
		Line.m_pMediaRequest = nullptr;
	}
	if(Line.m_pMediaDecodeJob)
	{
		Line.m_pMediaDecodeJob->Abort();
		Line.m_pMediaDecodeJob = nullptr;
	}

	Line.m_OptMediaDecodedFrames.reset();
	Line.m_MediaUploadIndex = 0;
	Line.m_vMediaFrameEndMs.clear();
	Line.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);

	Line.m_MediaRetryCount++;
	Line.m_MediaCandidateIndex = 0;
	str_copy(Line.m_aMediaUrl, Line.m_vMediaCandidates.front().c_str(), sizeof(Line.m_aMediaUrl));
	Line.m_MediaState = EMediaState::QUEUED;
	Line.m_MediaKind = MediaKindFromUrl(Line.m_aMediaUrl);
	Line.m_MediaAnimated = false;
	Line.m_MediaRevealed = false;
	Line.m_MediaWidth = 0;
	Line.m_MediaHeight = 0;
	Line.m_MediaResolveDepth = 0;
	Line.m_MediaAnimationStart = 0;
	Line.m_MediaPreviewRectValid = false;
	Line.m_MediaRetryRectValid = false;
	Line.m_aYOffset[0] = -1.0f;
	Line.m_aYOffset[1] = -1.0f;
	if(g_Config.m_Debug)
		log_debug("chat/media", "Retrying media preview (%d/%d): %s", Line.m_MediaRetryCount, CHAT_MEDIA_MAX_RETRIES, Line.m_aMediaUrl);
	return true;
}

void CChat::QueueMediaDownload(CLine &Line)
{
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed() || Line.m_vMediaCandidates.empty())
		return;
	if(Line.m_MediaCandidateIndex < 0 || Line.m_MediaCandidateIndex >= (int)Line.m_vMediaCandidates.size())
	{
		if(!QueueNextMediaCandidate(Line, "initial candidate"))
		{
			Line.m_MediaState = EMediaState::NONE;
			return;
		}
	}
	if(Line.m_aMediaUrl[0] == '\0')
		return;
	if(!IsMediaUrlAllowed(Line.m_aMediaUrl))
	{
		if(!QueueNextMediaCandidate(Line, "media type disabled"))
			Line.m_MediaState = EMediaState::NONE;
		return;
	}
	Line.m_MediaState = EMediaState::QUEUED;
}

void CChat::StartMediaDownload(CLine &Line)
{
	if(Line.m_MediaState != EMediaState::QUEUED || Line.m_aMediaUrl[0] == '\0')
		return;
	if(!IsMediaUrlAllowed(Line.m_aMediaUrl))
	{
		if(!QueueNextMediaCandidate(Line, "media type disabled"))
			Line.m_MediaState = EMediaState::NONE;
		return;
	}
	if((int)str_length(Line.m_aMediaUrl) >= 255)
	{
		if(g_Config.m_Debug)
			log_debug("chat/media", "Skipping overlong URL (>255): %s", Line.m_aMediaUrl);
		if(!QueueNextMediaCandidate(Line, "overlong URL"))
			Line.m_MediaState = EMediaState::FAILED;
		return;
	}
	for(const char *p = Line.m_aMediaUrl; *p; ++p)
	{
		if((unsigned char)*p < 32 || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		{
			if(g_Config.m_Debug)
				log_debug("chat/media", "Skipping invalid URL characters: %s", Line.m_aMediaUrl);
			if(!QueueNextMediaCandidate(Line, "invalid URL characters"))
				Line.m_MediaState = EMediaState::FAILED;
			return;
		}
	}

	std::shared_ptr<CHttpRequest> pGet = HttpGet(Line.m_aMediaUrl);
	pGet->Timeout(CTimeout{8000, 0, 4096, 8});
	pGet->MaxResponseSize(CHAT_MEDIA_MAX_RESPONSE_SIZE);
	pGet->FailOnErrorStatus(false);
	pGet->LogProgress(HTTPLOG::NONE);
	Line.m_pMediaRequest = pGet;
	Line.m_MediaState = EMediaState::LOADING;
	Http()->Run(pGet);
}

bool CChat::StartMediaDecode(CLine &Line, EMediaKind MediaKind, const unsigned char *pData, size_t DataSize)
{
	if(!pData || DataSize == 0 || DataSize > (size_t)CHAT_MEDIA_MAX_RESPONSE_SIZE)
		return false;
	if(Line.m_pMediaDecodeJob)
	{
		Line.m_pMediaDecodeJob->Abort();
		Line.m_pMediaDecodeJob = nullptr;
	}

	Line.m_OptMediaDecodedFrames.reset();
	Line.m_MediaUploadIndex = 0;
	Line.m_vMediaFrameEndMs.clear();
	Line.m_MediaTotalDurationMs = 0;
	MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
	Line.m_MediaAnimated = false;
	Line.m_MediaWidth = 0;
	Line.m_MediaHeight = 0;
	Line.m_MediaAnimationStart = 0;

	Line.m_pMediaDecodeJob = std::make_shared<CMediaDecodeJob>(Graphics(), MediaKind, pData, DataSize, Line.m_aMediaUrl);
	Engine()->AddJob(Line.m_pMediaDecodeJob);
	Line.m_MediaState = EMediaState::DECODING;
	return true;
}

bool CChat::DecodeImageWithFfmpeg(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line, bool DecodeAllFrames, int MaxAnimationDurationMs)
{
	if(!pData || DataSize == 0 || DataSize > (size_t)CHAT_MEDIA_MAX_RESPONSE_SIZE)
		return false;

	return MediaDecoder::DecodeImageWithFfmpeg(Graphics(), pData, DataSize, pContextName, Line.m_vMediaFrames, Line.m_MediaAnimated, Line.m_MediaWidth, Line.m_MediaHeight, Line.m_MediaAnimationStart, DecodeAllFrames, MaxAnimationDurationMs);
}

bool CChat::DecodeStaticImage(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line)
{
	if(!pData || DataSize == 0 || DataSize > (size_t)CHAT_MEDIA_MAX_RESPONSE_SIZE)
		return false;
	return MediaDecoder::DecodeStaticImage(Graphics(), pData, DataSize, pContextName, Line.m_vMediaFrames, Line.m_MediaAnimated, Line.m_MediaWidth, Line.m_MediaHeight, Line.m_MediaAnimationStart);
}

bool CChat::DecodeAnimatedGif(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line)
{
	if(!pData || DataSize == 0 || DataSize > (size_t)CHAT_MEDIA_MAX_RESPONSE_SIZE)
		return false;
	if(!MediaDecoder::DecodeAnimatedImage(Graphics(), pData, DataSize, pContextName, Line.m_vMediaFrames, Line.m_MediaAnimated, Line.m_MediaWidth, Line.m_MediaHeight, Line.m_MediaAnimationStart, CHAT_MEDIA_MAX_VIDEO_ANIMATION_MS))
		return false;
	if(Line.m_vMediaFrames.empty())
		return false;
	Line.m_MediaAnimated = Line.m_vMediaFrames.size() > 1;
	return true;
}

bool CChat::AnyMediaAllowed() const
{
	return g_Config.m_BcChatMediaPhotos || g_Config.m_BcChatMediaGifs;
}

bool CChat::IsMediaKindAllowed(EMediaKind Kind) const
{
	switch(Kind)
	{
	case EMediaKind::PHOTO:
		return g_Config.m_BcChatMediaPhotos;
	case EMediaKind::ANIMATED:
	case EMediaKind::VIDEO:
		return g_Config.m_BcChatMediaGifs;
	case EMediaKind::UNKNOWN:
	default:
		return AnyMediaAllowed();
	}
}

bool CChat::IsMediaUrlAllowed(const char *pUrl) const
{
	return IsMediaKindAllowed(MediaKindFromUrl(pUrl)) && IsAllowedChatMediaUrl(pUrl);
}

bool CChat::HasAllowedMediaCandidates(const CLine &Line) const
{
	for(const std::string &Candidate : Line.m_vMediaCandidates)
	{
		if(IsMediaUrlAllowed(Candidate.c_str()))
			return true;
	}
	return Line.m_aMediaUrl[0] != '\0' && IsMediaUrlAllowed(Line.m_aMediaUrl);
}

bool CChat::ShouldDisplayMediaSlot(const CLine &Line) const
{
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
		return false;
	if(Line.m_MediaState == EMediaState::FAILED)
		return false;
	if((Line.m_MediaState == EMediaState::READY || Line.m_MediaState == EMediaState::LOADING || Line.m_MediaState == EMediaState::DECODING || Line.m_MediaState == EMediaState::QUEUED) && Line.m_MediaKind != EMediaKind::UNKNOWN)
		return IsMediaKindAllowed(Line.m_MediaKind);
	return HasAllowedMediaCandidates(Line);
}

bool CChat::ShouldHideMediaPreview(const CLine &Line) const
{
	return m_HideMediaByBind && !Line.m_MediaRevealed && ShouldDisplayMediaSlot(Line);
}

// Unlike ShouldHideMediaPreview (a spoiler toggle the user can click through), this is a hard
// block: if we know a link is nsfw and the browser's "show NSFW" setting is off, there is no
// click-to-reveal - the only way to see it is to enable that setting.
bool CChat::ShouldHideNsfwMedia(const CLine &Line) const
{
	if(g_Config.m_BcCherryGifsShowNsfw || Line.m_aMediaUrl[0] == '\0')
		return false;
	bool Nsfw = false;
	return GameClient()->m_CherryGifs.TryGetNsfw(Line.m_aMediaUrl, Nsfw) && Nsfw;
}

void CChat::ResetHiddenMediaReveals()
{
	for(auto &Line : m_aLines)
	{
		Line.m_MediaRevealed = false;
		Line.m_MediaPreviewRectValid = false;
		Line.m_MediaRetryRectValid = false;
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
	}
}

void CChat::UpdateMediaDownloads()
{
	if(!g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
	{
		if(m_MediaViewerOpen)
			CloseMediaViewer();
		return;
	}

	int ActiveDownloads = 0;
	for(auto &Line : m_aLines)
	{
		if(Line.m_MediaState == EMediaState::LOADING && Line.m_pMediaRequest && !Line.m_pMediaRequest->Done())
			ActiveDownloads++;
	}

	const auto FailLine = [this](CLine &Line, bool SuppressedBySettings) {
		if(SuppressedBySettings)
		{
			Line.m_OptMediaDecodedFrames.reset();
			Line.m_MediaUploadIndex = 0;
			Line.m_vMediaFrameEndMs.clear();
			Line.m_MediaTotalDurationMs = 0;
			MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
			Line.m_MediaState = EMediaState::NONE;
			Line.m_MediaAnimated = false;
			Line.m_MediaWidth = 0;
			Line.m_MediaHeight = 0;
			Line.m_MediaRetryRectValid = false;
			Line.m_MediaPreviewRectValid = false;
			Line.m_aYOffset[0] = -1.0f;
			Line.m_aYOffset[1] = -1.0f;
			return;
		}

		Line.m_OptMediaDecodedFrames.reset();
		Line.m_MediaUploadIndex = 0;
		Line.m_vMediaFrameEndMs.clear();
		Line.m_MediaTotalDurationMs = 0;
		MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
		Line.m_MediaState = EMediaState::FAILED;
		Line.m_MediaAnimated = false;
		Line.m_MediaWidth = 0;
		Line.m_MediaHeight = 0;
		Line.m_MediaRetryRectValid = false;
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
		if(g_Config.m_Debug)
		{
			if(Line.m_vMediaCandidates.empty())
				log_debug("chat/media", "Media failed: no candidates");
			else
				log_debug("chat/media", "Media failed after exhausting %d candidates", (int)Line.m_vMediaCandidates.size());
		}
	};

	int CompletedRequestsThisFrame = 0;
	for(auto &Line : m_aLines)
	{
		if(CompletedRequestsThisFrame >= CHAT_MEDIA_MAX_COMPLETED_DECODE_PER_FRAME)
			break;
		if(Line.m_MediaState != EMediaState::LOADING || !Line.m_pMediaRequest || !Line.m_pMediaRequest->Done())
			continue;

		bool StartedDecode = false;
		bool SuppressedBySettings = false;
		const char *pFailureReason = "download failed";
		const bool HttpDone = Line.m_pMediaRequest->State() == EHttpState::DONE;
		const int StatusCode = HttpDone ? Line.m_pMediaRequest->StatusCode() : -1;
		if(HttpDone && StatusCode >= 200 && StatusCode < 400)
		{
			unsigned char *pResult = nullptr;
			size_t ResultSize = 0;
			Line.m_pMediaRequest->Result(&pResult, &ResultSize);
			if(pResult && ResultSize > 0)
			{
				MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);

				if(Line.m_MediaResolveDepth < CHAT_MEDIA_MAX_RESOLVE_DEPTH)
				{
					std::vector<std::string> vExtractedUrls;
					ExtractMediaUrlsFromHtmlDocument(pResult, ResultSize, Line.m_aMediaUrl, vExtractedUrls);
					const int CandidateCountBefore = (int)Line.m_vMediaCandidates.size();
					InsertMediaCandidates(Line, vExtractedUrls, Line.m_MediaCandidateIndex + 1);
					if((int)Line.m_vMediaCandidates.size() > CandidateCountBefore)
					{
						Line.m_MediaResolveDepth++;
						if(g_Config.m_Debug)
							log_debug("chat/media", "Extracted %d fallback candidates from HTML: %s", (int)Line.m_vMediaCandidates.size() - CandidateCountBefore, Line.m_aMediaUrl);
					}
				}

				const bool IsHtmlResponse = IsLikelyHtmlDocument(pResult, ResultSize);
				if(IsHtmlResponse)
				{
					pFailureReason = "html response";
				}
				else
				{
					const std::string Ext = ExtractUrlExtensionLower(Line.m_aMediaUrl);
					const bool IsGif = IsGifSignature(pResult, ResultSize) || Ext == "gif";
					const bool IsVideoCandidate = IsLikelyVideoExtension(Ext) || IsVideoPayloadSignature(pResult, ResultSize);
					const bool IsImageCandidate = IsLikelyImageExtension(Ext) || IsImagePayloadSignature(pResult, ResultSize);
					const bool IsAnimatedImageCandidate = IsLikelyAnimatedImageExtension(Ext) && !IsVideoCandidate;
					EMediaKind MediaKind = EMediaKind::UNKNOWN;
					if(IsGif || IsAnimatedImageCandidate)
						MediaKind = EMediaKind::ANIMATED;
					else if(IsVideoCandidate || (!IsImageCandidate && Ext.empty()))
						MediaKind = EMediaKind::VIDEO;
					else if(IsImageCandidate)
						MediaKind = EMediaKind::PHOTO;

					// Allow unknown payload signatures to be decoded as a fallback.
					// Some CDNs return uncommon containers/codecs without filename extension.
					if(MediaKind == EMediaKind::UNKNOWN)
						MediaKind = EMediaKind::VIDEO;
					Line.m_MediaKind = MediaKind;

					if(!Ext.empty() && IsBlockedMediaExtension(Ext))
					{
						pFailureReason = "blocked extension";
					}
					else if(ResultSize < 16)
					{
						pFailureReason = "payload too small";
					}
					else
					{
						if(!IsMediaKindAllowed(MediaKind))
						{
							SuppressedBySettings = true;
							pFailureReason = "media type disabled";
						}
						else
						{
							StartedDecode = StartMediaDecode(Line, MediaKind, pResult, ResultSize);
							if(!StartedDecode)
								pFailureReason = "decode job failed";
							// Retain the original encoded bytes for static images so the fullscreen
							// viewer can decode them at full resolution on demand. Bounded by size.
							if(StartedDecode && MediaKind == EMediaKind::PHOTO && (int64_t)ResultSize <= CHAT_MEDIA_ORIGINAL_RETAIN_MAX_BYTES)
								Line.m_vMediaOriginalData.assign(pResult, pResult + ResultSize);
							else
							{
								Line.m_vMediaOriginalData.clear();
								Line.m_vMediaOriginalData.shrink_to_fit();
							}
						}
					}
				}
			}
			else if(g_Config.m_Debug)
			{
				pFailureReason = "empty response";
				log_debug("chat/media", "Empty HTTP response for media URL: %s", Line.m_aMediaUrl);
			}
		}
		else if(g_Config.m_Debug)
		{
			pFailureReason = "http failure";
			log_debug("chat/media", "HTTP request failed for media URL (state=%d, status=%d): %s", (int)Line.m_pMediaRequest->State(), StatusCode, Line.m_aMediaUrl);
		}

		Line.m_pMediaRequest = nullptr;
		ActiveDownloads = maximum(0, ActiveDownloads - 1);
		CompletedRequestsThisFrame++;

		if(StartedDecode)
			continue;

		if(QueueNextMediaCandidate(Line, pFailureReason))
			continue;

		FailLine(Line, SuppressedBySettings);
	}

	int CompletedUploadsThisFrame = 0;
	for(auto &Line : m_aLines)
	{
		if(CompletedUploadsThisFrame >= CHAT_MEDIA_MAX_COMPLETED_DECODE_PER_FRAME)
			break;
		if(Line.m_MediaState != EMediaState::DECODING || !Line.m_pMediaDecodeJob || !Line.m_pMediaDecodeJob->Done())
			continue;

		bool Success = false;
		const char *pFailureReason = "decode failed";
		if(Line.m_pMediaDecodeJob->State() == IJob::STATE_DONE && Line.m_pMediaDecodeJob->Success() && !Line.m_pMediaDecodeJob->DecodedFrames().Empty())
		{
			const int Width = Line.m_pMediaDecodeJob->DecodedFrames().m_Width;
			const int Height = Line.m_pMediaDecodeJob->DecodedFrames().m_Height;
			Line.m_OptMediaDecodedFrames.emplace(std::move(Line.m_pMediaDecodeJob->DecodedFrames()));
			Line.m_MediaUploadIndex = 0;
			Line.m_MediaWidth = Width;
			Line.m_MediaHeight = Height;
			Line.m_MediaAnimated = false;
			Line.m_MediaAnimationStart = 0;
			Success = true;
		}
		else if(g_Config.m_Debug)
		{
			log_debug("chat/media", "Media decode job failed: %s", Line.m_aMediaUrl);
		}

		Line.m_pMediaDecodeJob = nullptr;
		CompletedUploadsThisFrame++;

		if(Success)
			continue;

		Line.m_OptMediaDecodedFrames.reset();
		Line.m_MediaUploadIndex = 0;
		if(QueueNextMediaCandidate(Line, pFailureReason))
			continue;

		FailLine(Line, false);
	}

	const auto ClampFrameDurationMs = [](int DurationMs) -> int {
		constexpr int MediaFpsCap = 120;
		constexpr int MediaMinFrameMs = (1000 + MediaFpsCap - 1) / MediaFpsCap; // ceil(1000/fps)
		constexpr int MediaMaxFrameMs = 10000;
		return std::clamp(DurationMs, MediaMinFrameMs, MediaMaxFrameMs);
	};

	auto UploadDecodedFramesStep = [&](CLine &Line, int MaxFramesToUpload, int64_t TimeBudgetUs, int &UploadedFramesOut, bool &FinishedOut) -> bool {
		UploadedFramesOut = 0;
		FinishedOut = false;
		if(!Line.m_OptMediaDecodedFrames.has_value())
			return true;
		SMediaDecodedFrames &DecodedFrames = *Line.m_OptMediaDecodedFrames;
		if(DecodedFrames.m_vFrames.empty())
		{
			FinishedOut = true;
			return false;
		}

		const int64_t Start = time_get();
		while(Line.m_MediaUploadIndex < (int)DecodedFrames.m_vFrames.size())
		{
			if(UploadedFramesOut >= MaxFramesToUpload)
				break;
			if(TimeBudgetUs > 0)
			{
				const int64_t ElapsedUs = ((time_get() - Start) * 1000000) / time_freq();
				if(ElapsedUs >= TimeBudgetUs)
					break;
			}

			SMediaRawFrame &RawFrame = DecodedFrames.m_vFrames[Line.m_MediaUploadIndex];
			SMediaFrame Frame;
			Frame.m_DurationMs = RawFrame.m_DurationMs;
			Frame.m_Texture = Graphics()->LoadTextureRawMove(RawFrame.m_Image, 0, Line.m_aMediaUrl);
			if(!Frame.m_Texture.IsValid())
				return false;
			Line.m_vMediaFrames.push_back(Frame);
			Line.m_MediaUploadIndex++;
			UploadedFramesOut++;
		}

		FinishedOut = Line.m_MediaUploadIndex >= (int)DecodedFrames.m_vFrames.size();
		return true;
	};

	int UploadedTexturesThisFrame = 0;
	const int64_t UploadStart = time_get();
	for(auto &Line : m_aLines)
	{
		if(!Line.m_Initialized || !Line.m_OptMediaDecodedFrames.has_value())
			continue;
		if(UploadedTexturesThisFrame >= CHAT_MEDIA_MAX_TEXTURE_UPLOADS_PER_FRAME)
			break;

		const int64_t ElapsedUs = ((time_get() - UploadStart) * 1000000) / time_freq();
		const int64_t RemainingUs = CHAT_MEDIA_TEXTURE_UPLOAD_BUDGET_US - ElapsedUs;
		if(RemainingUs <= 0)
			break;

		const int FramesBudget = CHAT_MEDIA_MAX_TEXTURE_UPLOADS_PER_FRAME - UploadedTexturesThisFrame;
		int UploadedNow = 0;
		bool Finished = false;
		const bool Success = UploadDecodedFramesStep(Line, FramesBudget, RemainingUs, UploadedNow, Finished);
		UploadedTexturesThisFrame += UploadedNow;

		if(!Success)
		{
			Line.m_OptMediaDecodedFrames.reset();
			Line.m_MediaUploadIndex = 0;
			Line.m_vMediaFrameEndMs.clear();
			Line.m_MediaTotalDurationMs = 0;
			MediaDecoder::UnloadFrames(Graphics(), Line.m_vMediaFrames);
			if(QueueNextMediaCandidate(Line, "upload failed"))
				continue;
			FailLine(Line, false);
			continue;
		}

		if(!Line.m_vMediaFrames.empty() && Line.m_MediaState != EMediaState::READY)
		{
			Line.m_MediaState = EMediaState::READY;
			Line.m_MediaRetryRectValid = false;
			Line.m_MediaPreviewRectValid = false;
			Line.m_aYOffset[0] = -1.0f;
			Line.m_aYOffset[1] = -1.0f;
		}

		if(Finished)
		{
			Line.m_OptMediaDecodedFrames.reset();
			Line.m_MediaUploadIndex = 0;
			Line.m_vMediaFrameEndMs.clear();
			Line.m_MediaTotalDurationMs = 0;
			if(Line.m_vMediaFrames.size() > 1)
			{
				Line.m_vMediaFrameEndMs.reserve(Line.m_vMediaFrames.size());
				int TotalDuration = 0;
				for(const auto &Frame : Line.m_vMediaFrames)
				{
					TotalDuration += ClampFrameDurationMs(Frame.m_DurationMs);
					Line.m_vMediaFrameEndMs.push_back(TotalDuration);
				}
				Line.m_MediaTotalDurationMs = TotalDuration;
				Line.m_MediaAnimated = TotalDuration > 0;
				if(Line.m_MediaAnimated)
					Line.m_MediaAnimationStart = time_get();
			}
			else
			{
				Line.m_MediaAnimated = false;
				Line.m_MediaAnimationStart = 0;
			}
		}
	}

	for(auto &Line : m_aLines)
	{
		if(ActiveDownloads >= CHAT_MEDIA_MAX_CONCURRENT_DOWNLOADS)
			break;
		if(Line.m_MediaState == EMediaState::QUEUED)
		{
			StartMediaDownload(Line);
			if(Line.m_MediaState == EMediaState::LOADING)
				ActiveDownloads++;
		}
	}
}

bool CChat::ValidateMediaViewerLine() const
{
	if(!m_MediaViewerOpen)
		return false;
	if(m_MediaViewerLineIndex < 0 || m_MediaViewerLineIndex >= MAX_LINES)
		return false;
	const CLine &Line = m_aLines[m_MediaViewerLineIndex];
	return Line.m_Initialized && Line.m_MediaState == EMediaState::READY && !Line.m_vMediaFrames.empty();
}

void CChat::CloseMediaViewer()
{
	FreeMediaViewerFullTexture();
	m_MediaViewerOpen = false;
	m_MediaViewerLineIndex = -1;
	m_MediaViewerZoom = 1.0f;
	m_MediaViewerPan = vec2(0.0f, 0.0f);
	m_MediaViewerDragging = false;
}

void CChat::OpenMediaViewer(int LineIndex)
{
	if(!g_Config.m_BcChatMediaViewer)
		return;

	if(LineIndex < 0 || LineIndex >= MAX_LINES)
		return;
	CLine &Line = m_aLines[LineIndex];
	if(!Line.m_Initialized || Line.m_MediaState != EMediaState::READY || Line.m_vMediaFrames.empty())
		return;
	m_MediaViewerOpen = true;
	m_MediaViewerLineIndex = LineIndex;
	m_MediaViewerZoom = 1.0f;
	m_MediaViewerPan = vec2(0.0f, 0.0f);
	m_MediaViewerDragging = false;
	m_MediaViewerLastClickTime = 0;
	LoadMediaViewerFullTexture(Line);
}

void CChat::FreeMediaViewerFullTexture()
{
	if(m_MediaViewerFullTexture.IsValid())
		Graphics()->UnloadTexture(&m_MediaViewerFullTexture);
	m_MediaViewerFullTexture = IGraphics::CTextureHandle();
	m_MediaViewerFullTextureLine = -1;
}

void CChat::LoadMediaViewerFullTexture(CLine &Line)
{
	FreeMediaViewerFullTexture();

	// Only static images (photos) keep their original bytes; animated/video use the preview frames.
	if(Line.m_MediaKind != EMediaKind::PHOTO || Line.m_MediaAnimated || Line.m_vMediaOriginalData.empty())
		return;

	// Decode the original bytes at a much higher cap. The inline preview's m_MediaWidth/Height are
	// already clamped to CHAT_MEDIA_MAX_DIMENSION, so they can't tell us the original size; instead
	// we only keep the freshly decoded texture when it is actually higher-resolution than the preview.
	SMediaDecodedFrames FullFrames;
	if(MediaDecoder::DecodeStaticImageCpu(Graphics(), Line.m_vMediaOriginalData.data(), Line.m_vMediaOriginalData.size(),
		   Line.m_aMediaUrl, FullFrames, CHAT_MEDIA_VIEWER_MAX_DIMENSION) &&
		!FullFrames.m_vFrames.empty())
	{
		const bool LargerThanPreview = FullFrames.m_Width > Line.m_MediaWidth || FullFrames.m_Height > Line.m_MediaHeight;
		if(LargerThanPreview)
		{
			IGraphics::CTextureHandle Texture = Graphics()->LoadTextureRawMove(FullFrames.m_vFrames.front().m_Image, 0, Line.m_aMediaUrl);
			if(Texture.IsValid())
			{
				m_MediaViewerFullTexture = Texture;
				m_MediaViewerFullTextureLine = (int)(&Line - m_aLines);
			}
		}
	}
	FullFrames.Free();
}

bool CChat::GetMediaViewerTexture(CLine &Line, IGraphics::CTextureHandle &Texture) const
{
	// Prefer the full-resolution texture decoded on demand for the current viewer line.
	if(m_MediaViewerFullTexture.IsValid() && m_MediaViewerFullTextureLine == (int)(&Line - m_aLines))
	{
		Texture = m_MediaViewerFullTexture;
		return true;
	}
	return GetCurrentFrameTexture(Line, Texture);
}

bool CChat::GetCurrentFrameTexture(CLine &Line, IGraphics::CTextureHandle &Texture) const
{
	if(Line.m_vMediaFrames.empty())
		return false;
	if(!Line.m_MediaAnimated || Line.m_vMediaFrames.size() == 1)
	{
		Texture = Line.m_vMediaFrames.front().m_Texture;
		return Texture.IsValid();
	}

	if(Line.m_MediaTotalDurationMs <= 0 || (int)Line.m_vMediaFrameEndMs.size() != (int)Line.m_vMediaFrames.size())
	{
		// Fallback (should be rare, e.g. old state after config toggle).
		return MediaDecoder::GetCurrentFrameTexture(Line.m_vMediaFrames, Line.m_MediaAnimated, Line.m_MediaAnimationStart, Texture);
	}

	const int64_t ElapsedMs = ((time_get() - Line.m_MediaAnimationStart) * 1000) / time_freq();
	const int Offset = (int)(ElapsedMs % (int64_t)Line.m_MediaTotalDurationMs);
	const auto It = std::upper_bound(Line.m_vMediaFrameEndMs.begin(), Line.m_vMediaFrameEndMs.end(), Offset);
	const int Index = It == Line.m_vMediaFrameEndMs.end() ? 0 : (int)(It - Line.m_vMediaFrameEndMs.begin());
	Texture = Line.m_vMediaFrames[Index].m_Texture;
	return Texture.IsValid();
}

vec2 CChat::ChatMousePos() const
{
	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
	return UiMousePos * UiToChatScale;
}

std::string CChat::MediaPlaceholderText(const CLine &Line) const
{
	const char *pUrl = Line.m_aMediaUrl;
	if(pUrl[0] == '\0' && !Line.m_vMediaCandidates.empty())
		pUrl = Line.m_vMediaCandidates.front().c_str();

	const std::string Ext = ExtractUrlExtensionLower(pUrl);
	if(IsLikelyVideoExtension(Ext))
		return "Video";
	if(Line.m_MediaAnimated || IsLikelyAnimatedImageExtension(Ext))
		return "GIF";
	if(IsLikelyImageExtension(Ext))
		return "Photo";
	return "Media";
}

std::string CChat::BuildVisibleMessageText(const CLine &Line, bool UseMediaLabelWhenEmpty) const
{
	const char *pSource = GetLineDisplayText(Line);
	const bool HideMediaUrls = ShouldDisplayMediaSlot(Line);
	const bool HideMapUrls = HasMapAttachment(Line);
	if(!HideMediaUrls && !HideMapUrls)
		return pSource;

	std::string Result;
	bool RemovedUrl = false;
	for(const char *pCur = pSource; *pCur;)
	{
		if(IsUrlStart(pCur))
		{
			const char *pEnd = pCur;
			while(*pEnd && !IsTokenEnd(*pEnd))
				++pEnd;

			std::string Url(pCur, pEnd - pCur);
			while(!Url.empty() && IsTrimmedUrlChar(Url.back()))
				Url.pop_back();

			bool Strip = HideMediaUrls;
			if(!Strip && HideMapUrls)
			{
				Strip = str_comp(Url.c_str(), Line.m_aMapUrl) == 0 ||
					ExtractUrlExtensionLower(Url) == "map";
			}

			if(Strip)
			{
				RemovedUrl = true;
				pCur = pEnd;
				continue;
			}
		}

		Result.push_back(*pCur);
		++pCur;
	}

	std::string Compacted;
	Compacted.reserve(Result.size());
	bool PrevWhitespace = false;
	for(char c : Result)
	{
		if(std::isspace((unsigned char)c))
		{
			if(!PrevWhitespace)
				Compacted.push_back(' ');
			PrevWhitespace = true;
		}
		else
		{
			Compacted.push_back(c);
			PrevWhitespace = false;
		}
	}
	TrimAsciiWhitespace(Compacted);

	if(Compacted.empty() && RemovedUrl && UseMediaLabelWhenEmpty)
	{
		if(HideMediaUrls)
			return MediaPlaceholderText(Line);
		return "";
	}

	return Compacted;
}

void CChat::ConToggleHideChatMedia(IConsole::IResult *pResult, void *pUserData)
{
	CChat *pThis = static_cast<CChat *>(pUserData);
	(void)pResult;
	pThis->m_HideMediaByBind = !pThis->m_HideMediaByBind;
	pThis->CloseMediaViewer();
	pThis->ResetHiddenMediaReveals();
	pThis->RebuildChat();
	pThis->Echo(pThis->m_HideMediaByBind ? "Chat media hidden" : "Chat media visible");
}

std::vector<std::string> CChat::SplitWords(const char *pMessage)
{
	std::vector<std::string> Parts;
	if(!pMessage)
		return Parts;

	std::string Str(pMessage);
	size_t Start = 0;
	size_t End = 0;
	while((End = Str.find(' ', Start)) != std::string::npos)
	{
		Parts.push_back(Str.substr(Start, End - Start));
		Start = End + 1;
	}
	Parts.push_back(Str.substr(Start));
	return Parts;
}

void CChat::ConchainRegexPlayerWhitelist(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	if(pResult->NumArguments() == 1)
	{
		auto Re = Regex(pResult->GetString(0));
		if(!Re.error().empty())
		{
			log_error("chat", "Invalid whitelist regex: %s", Re.error().c_str());
			return;
		}
		static_cast<CChat *>(pUserData)->m_RegexPlayerWhitelist = std::move(Re);
	}
	pfnCallback(pResult, pCallbackUserData);
}

void CChat::ConAddWhiteList(IConsole::IResult *pResult, void *pUserData)
{
	CChat *pSelf = static_cast<CChat *>(pUserData);
	const char *pInput = pResult->GetString(0);
	char aInput[256];
	str_copy(aInput, pInput, sizeof(aInput));
	str_utf8_trim_right(aInput);
	char aBuf[256];
	if(!aInput[0])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", "No nickname given");
		return;
	}

	const bool HadExistingRegex = g_Config.m_BcRegexPlayerWhitelist[0] != '\0';
	char aOldRegex[sizeof(g_Config.m_BcRegexPlayerWhitelist)];
	str_copy(aOldRegex, g_Config.m_BcRegexPlayerWhitelist, sizeof(aOldRegex));
	const char *pNewRegex = aInput;
	char aNewRegex[sizeof(g_Config.m_BcRegexPlayerWhitelist)];
	if(HadExistingRegex)
	{
		str_format(aNewRegex, sizeof(aNewRegex), "%s|%s", g_Config.m_BcRegexPlayerWhitelist, aInput);
		pNewRegex = aNewRegex;
	}

	str_copy(g_Config.m_BcRegexPlayerWhitelist, pNewRegex, sizeof(g_Config.m_BcRegexPlayerWhitelist));

	auto Re = Regex(g_Config.m_BcRegexPlayerWhitelist);
	if(!Re.error().empty())
	{
		str_copy(g_Config.m_BcRegexPlayerWhitelist, aOldRegex, sizeof(g_Config.m_BcRegexPlayerWhitelist));
		str_format(aBuf, sizeof(aBuf), "Invalid regex, list not updated: %s", Re.error().c_str());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", aBuf);
		return;
	}

	pSelf->m_RegexPlayerWhitelist = std::move(Re);
	if(!HadExistingRegex)
		str_format(aBuf, sizeof(aBuf), "New regex added: %s", aInput);
	else
		str_format(aBuf, sizeof(aBuf), "Added to existing regex: %s", aInput);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", aBuf);
}

void CChat::ConAddCensorList(IConsole::IResult *pResult, void *pUserData)
{
	CChat *pSelf = static_cast<CChat *>(pUserData);
	const char *pInput = pResult->GetString(0);
	char aInput[256];
	str_copy(aInput, pInput, sizeof(aInput));
	str_utf8_trim_right(aInput);
	char aBuf[256];
	if(!aInput[0])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", "No word given");
		return;
	}

	const bool HadExistingRegex = g_Config.m_TcRegexChatIgnore[0] != '\0';
	char aOldRegex[sizeof(g_Config.m_TcRegexChatIgnore)];
	str_copy(aOldRegex, g_Config.m_TcRegexChatIgnore, sizeof(aOldRegex));
	const char *pNewRegex = aInput;
	char aNewRegex[sizeof(g_Config.m_TcRegexChatIgnore)];
	if(HadExistingRegex)
	{
		str_format(aNewRegex, sizeof(aNewRegex), "%s|%s", g_Config.m_TcRegexChatIgnore, aInput);
		pNewRegex = aNewRegex;
	}

	str_copy(g_Config.m_TcRegexChatIgnore, pNewRegex, sizeof(g_Config.m_TcRegexChatIgnore));

	auto Re = Regex(g_Config.m_TcRegexChatIgnore);
	if(!Re.error().empty())
	{
		str_copy(g_Config.m_TcRegexChatIgnore, aOldRegex, sizeof(g_Config.m_TcRegexChatIgnore));
		str_format(aBuf, sizeof(aBuf), "Invalid regex, list not updated: %s", Re.error().c_str());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", aBuf);
		return;
	}

	pSelf->GameClient()->m_TClient.m_RegexChatIgnore = std::move(Re);
	if(!HadExistingRegex)
		str_format(aBuf, sizeof(aBuf), "New regex added: %s", aInput);
	else
		str_format(aBuf, sizeof(aBuf), "Added to existing regex: %s", aInput);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex", aBuf);
}

const char *CChat::FilterText(const char *pMessage, int ClientId, bool IsChat)
{
	if(!pMessage || !g_Config.m_TcRegexChatIgnore[0] || !g_Config.m_BcEnableCensorList)
		return pMessage;

	static char s_aFilteredMessage[1024];
	s_aFilteredMessage[0] = '\0';
	if(g_Config.m_BcRegexPlayerWhitelist[0] && ClientId >= 0)
	{
		auto &RePlr = m_RegexPlayerWhitelist;
		if(RePlr.error().empty() && RePlr.test(GameClient()->m_aClients[ClientId].m_aName))
			return pMessage;
	}
	auto &Re = GameClient()->m_TClient.m_RegexChatIgnore;
	if(!Re.error().empty())
		return pMessage;
	if(!Re.test(pMessage))
		return pMessage;

	std::vector<std::string> BlockedWords;
	std::vector<std::string> SplitMsg = SplitWords(pMessage);

	if(g_Config.m_BcShowBlockedWordInConsole && IsChat)
	{
		Re.match(pMessage, true, [&BlockedWords](const std::string &Match, int /*MatchIndex*/, int Group) {
			if(Group == 0)
			{
				bool AlreadyBlocked = false;
				for(const auto &BlockedWord : BlockedWords)
				{
					if(BlockedWord == Match)
					{
						AlreadyBlocked = true;
						break;
					}
				}
				if(!AlreadyBlocked)
					BlockedWords.push_back(Match);
			}
		});
	}

	std::string FilteredMessage;
	if(g_Config.m_BcFilterChangeWholeWord == 0)
	{
		FilteredMessage = Re.replace(pMessage, true, [](const std::string &Match, int /*MatchIndex*/, int Group) -> std::string {
			if(Group != 0)
				return "";

			if(g_Config.m_BcMultipleReplacementChar)
			{
				size_t Size = 0, Count = 0;
				str_utf8_stats(Match.c_str(), Match.length() * 4, Match.length(), &Size, &Count);
				std::string Replacement;
				for(size_t i = 0; i < Count; i++)
					Replacement += g_Config.m_BcBlockedContentReplacementChar;
				return Replacement;
			}
			return g_Config.m_BcBlockedContentReplacementChar;
		});
		str_copy(s_aFilteredMessage, FilteredMessage.c_str(), sizeof(s_aFilteredMessage));
	}
	else if(g_Config.m_BcFilterChangeWholeWord == 1)
	{
		for(size_t w = 0; w < SplitMsg.size(); w++)
		{
			if(Re.error().empty() && Re.test(SplitMsg[w]))
			{
				if(g_Config.m_BcMultipleReplacementChar)
				{
					if(w > 0)
						str_append(s_aFilteredMessage, " ", sizeof(s_aFilteredMessage));
					size_t Size = 0, Count = 0;
					str_utf8_stats(SplitMsg[w].c_str(), SplitMsg[w].length() * 4, SplitMsg[w].length(), &Size, &Count);
					for(size_t i = 0; i < Count; i++)
						str_append(s_aFilteredMessage, g_Config.m_BcBlockedContentReplacementChar, sizeof(s_aFilteredMessage));
					if(w < SplitMsg.size())
						str_append(s_aFilteredMessage, " ", sizeof(s_aFilteredMessage));
				}
				else
				{
					str_append(s_aFilteredMessage, g_Config.m_BcBlockedContentReplacementChar, sizeof(s_aFilteredMessage));
				}
			}
			else
			{
				str_append(s_aFilteredMessage, SplitMsg[w].c_str(), sizeof(s_aFilteredMessage));
			}
		}
	}
	else if(g_Config.m_BcFilterChangeWholeWord == 2)
	{
		for(size_t w = 0; w < SplitMsg.size(); w++)
		{
			if(w > 0)
				str_append(s_aFilteredMessage, " ", sizeof(s_aFilteredMessage));

			bool IsExactMatch = false;
			if(Re.error().empty())
			{
				std::string LowerWord;
				LowerWord.resize(SplitMsg[w].size() * 4 + 1);
				str_utf8_tolower(SplitMsg[w].c_str(), LowerWord.data(), LowerWord.size());
				LowerWord.resize(std::strlen(LowerWord.c_str()));

				std::string MatchedWord;
				Re.match(LowerWord, false, [&MatchedWord, &LowerWord](const std::string &Match, int /*MatchIndex*/, int Group) {
					if(Group == 0 && Match == LowerWord)
						MatchedWord = Match;
				});
				IsExactMatch = !MatchedWord.empty();
			}

			if(IsExactMatch)
				str_append(s_aFilteredMessage, g_Config.m_BcBlockedContentReplacementChar, sizeof(s_aFilteredMessage));
			else
				str_append(s_aFilteredMessage, SplitMsg[w].c_str(), sizeof(s_aFilteredMessage));
		}

		FilteredMessage = Re.replace(s_aFilteredMessage, true, [](const std::string &Match, int /*MatchIndex*/, int Group) -> std::string {
			if(Group != 0)
				return "";

			if(g_Config.m_BcMultipleReplacementChar)
			{
				size_t Size = 0, Count = 0;
				str_utf8_stats(Match.c_str(), Match.length() * 4, Match.length(), &Size, &Count);
				std::string Replacement;
				for(size_t i = 0; i < Count; i++)
					Replacement += g_Config.m_BcBlockedContentPartialReplacementChar;
				return Replacement;
			}
			return g_Config.m_BcBlockedContentPartialReplacementChar;
		});
		str_copy(s_aFilteredMessage, FilteredMessage.c_str(), sizeof(s_aFilteredMessage));
	}

	if(g_Config.m_BcShowBlockedWordInConsole && IsChat && !BlockedWords.empty())
	{
		char aBlockedWordsStr[512];
		aBlockedWordsStr[0] = '\0';
		if(ClientId >= 0)
			str_format(aBlockedWordsStr, sizeof(aBlockedWordsStr), "%s said: ", GameClient()->m_aClients[ClientId].m_aName);
		else if(ClientId == SERVER_MSG)
			str_copy(aBlockedWordsStr, "Server said: ", sizeof(aBlockedWordsStr));
		for(size_t i = 0; i < BlockedWords.size(); i++)
		{
			if(i > 0)
				str_append(aBlockedWordsStr, ", ", sizeof(aBlockedWordsStr));
			str_append(aBlockedWordsStr, BlockedWords[i].c_str(), sizeof(aBlockedWordsStr));
		}
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Regex filter", aBlockedWordsStr, color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcBlockedWordConsoleColor)));
	}

	return s_aFilteredMessage;
}

bool CChat::ShouldHideLineFromStreamer(const CLine &Line) const
{
	return m_Mode == MODE_NONE && GameClient()->m_BestClient.HasStreamerFlag(CBestClient::STREAMER_HIDE_FRIEND_WHISPER) && Line.m_Whisper;
}

bool CChat::ShouldShowFriendMarker(const CLine &Line) const
{
	return Line.m_Friend && g_Config.m_ClMessageFriend && !(m_Mode == MODE_NONE && GameClient()->m_BestClient.HasStreamerFlag(CBestClient::STREAMER_HIDE_FRIEND_WHISPER));
}

std::string CChat::BuildPlayerSearchUrl(const char *pPlayerName) const
{
	if(!pPlayerName || pPlayerName[0] == '\0')
		return {};

	char aEscapedName[256];
	EscapeUrl(aEscapedName, sizeof(aEscapedName), pPlayerName);
	if(aEscapedName[0] == '\0')
		return {};

	char aUrl[512];
	if(g_Config.m_UcChatPlayerSearchEngine == 1)
		str_format(aUrl, sizeof(aUrl), "https://ddstats.tw/player/%s", aEscapedName);
	else
		str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/%s/", aEscapedName);
	return aUrl;
}

static constexpr const char *LINK_POLICY_URL = "https://ddnet.under1111.com/uclient/linkpolicy.json";

void CChat::UpdateLinkPolicy()
{
	if(m_LinkPolicyCache.m_pRequest == nullptr)
	{
		const int64_t Now = time_get();
		if(m_LinkPolicyCache.m_LastRefreshAttempt != 0 && Now < m_LinkPolicyCache.m_LastRefreshAttempt + time_freq() * 5)
			return;

		m_LinkPolicyCache.m_LastRefreshAttempt = Now;
		std::shared_ptr<CHttpRequest> pRequest = HttpGet(LINK_POLICY_URL);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->Timeout(CTimeout{2000, 4000, 500, 5});
		m_LinkPolicyCache.m_pRequest = pRequest;
		Http()->Run(pRequest);
		return;
	}

	if(!m_LinkPolicyCache.m_pRequest->Done())
		return;

	if(m_LinkPolicyCache.m_pRequest->State() == EHttpState::DONE)
	{
		unsigned char *pResult = nullptr;
		size_t ResultLength = 0;
		m_LinkPolicyCache.m_pRequest->Result(&pResult, &ResultLength);

		std::unordered_set<std::string> vSafeDomains;
		std::unordered_set<std::string> vDangerDomains;

		bool Parsed = false;
		json_settings JsonSettings{};
		char aError[256];
		json_value *pRoot = json_parse_ex(&JsonSettings, (json_char *)pResult, ResultLength, aError);
		if(pRoot != nullptr)
		{
			if(pRoot->type == json_object)
			{
				ParseLinkPolicyArray(json_object_get(pRoot, "safe"), vSafeDomains);
				ParseLinkPolicyArray(json_object_get(pRoot, "danger"), vDangerDomains);
				Parsed = true;
			}
			json_value_free(pRoot);
		}

		if(Parsed)
		{
			m_LinkPolicyCache.m_vSafeDomains = std::move(vSafeDomains);
			m_LinkPolicyCache.m_vDangerDomains = std::move(vDangerDomains);
		}
	}

	m_LinkPolicyCache.m_pRequest.reset();
}

CChat::ELinkSafety CChat::ClassifyLink(const std::string &Link) const
{
	if(IsDirectDownloadPath(Link))
		return ELinkSafety::DANGER;

	std::string NormalizedHost;
	bool Https = false;
	bool Http = false;
	if(!ExtractNormalizedHostFromClickableUrl(Link, NormalizedHost, Https, Http))
		return ELinkSafety::INVALID;
	if(Http)
		return ELinkSafety::DANGER;
	if(LinkDomainMatchesPolicy(NormalizedHost, m_LinkPolicyCache.m_vDangerDomains))
		return ELinkSafety::DANGER;
	if(LinkDomainMatchesPolicy(NormalizedHost, m_LinkPolicyCache.m_vSafeDomains))
		return ELinkSafety::SAFE;
	return Https ? ELinkSafety::WARNING : ELinkSafety::INVALID;
}

bool CChat::IsLikelyPreflightDownload(const CHttpRequest &Request) const
{
	// We don't have response header access in BestClient's http layer,
	// so we rely only on redirect URL inspection (from the final URL if available).
	(void)Request;
	return false;
}

void CChat::StartLinkPreflight(const std::string &Link, bool AlwaysConfirm)
{
	if(m_LinkPreflight.m_pRequest)
		m_LinkPreflight.m_pRequest->Abort();

	m_LinkPreflight = {};
	m_LinkPreflight.m_Link = Link;
	m_LinkPreflight.m_AlwaysConfirm = AlwaysConfirm;
	m_LinkPreflight.m_RequestType = ELinkPreflightRequestType::HEAD;

	std::shared_ptr<CHttpRequest> pRequest = HttpHead(Link.c_str());
	pRequest->FailOnErrorStatus(false);
	pRequest->LogProgress(HTTPLOG::FAILURE);
	pRequest->Timeout(CTimeout{2500, 5000, 500, 5});
	m_LinkPreflight.m_pRequest = pRequest;
	Http()->Run(pRequest);
}

void CChat::ShowLinkPrompt(const std::string &Link, bool AlwaysConfirm, ELinkSafety Safety, bool IsDownloadLink)
{
	if(AlwaysConfirm)
	{
		if(Safety == ELinkSafety::SAFE)
		{
			Client()->ViewLink(Link.c_str());
			return;
		}
		if(Safety == ELinkSafety::DANGER)
		{
			char aDangerMessage[512];
			if(IsDownloadLink)
			{
				str_format(aDangerMessage, sizeof(aDangerMessage), Localize("The link you are trying to access may trigger an unwanted file download. Please verify before proceeding.\n\n%s"), Link.c_str());
				GameClient()->m_Menus.PopupConfirmOpenLink(Localize("Download Warning"), aDangerMessage, Localize("Download"), Localize("Cancel"), Link.c_str(), true);
			}
			else
			{
				str_format(aDangerMessage, sizeof(aDangerMessage), Localize("This website was classified as dangerous because of inappropriate content such as hacks or cheats.\n\n%s"), Link.c_str());
				GameClient()->m_Menus.PopupConfirmOpenLink(Localize("Unsafe link"), aDangerMessage, Localize("Open anyway"), Localize("Do not open"), Link.c_str(), true);
			}
			return;
		}
		if(Safety == ELinkSafety::WARNING)
		{
			char aWarningMessage[512];
			str_format(aWarningMessage, sizeof(aWarningMessage), Localize("You are leaving DDNet.\n\n%s is not an official DDNet website."), Link.c_str());
			GameClient()->m_Menus.PopupConfirmOpenLink(Localize("External link"), aWarningMessage, Localize("Open"), Localize("Cancel"), Link.c_str(), false);
		}
		return;
	}

	switch(Safety)
	{
	case ELinkSafety::SAFE:
		Client()->ViewLink(Link.c_str());
		break;
	case ELinkSafety::DANGER:
	{
		char aMessage[512];
		if(IsDownloadLink)
		{
			str_format(aMessage, sizeof(aMessage), Localize("The link you are trying to access may trigger an unwanted file download. Please verify before proceeding.\n\n%s"), Link.c_str());
			GameClient()->m_Menus.PopupConfirmOpenLink(Localize("Download Warning"), aMessage, Localize("Download"), Localize("Cancel"), Link.c_str(), true);
		}
		else
		{
			str_format(aMessage, sizeof(aMessage), Localize("This website was classified as dangerous because of inappropriate content such as hacks or cheats.\n\n%s"), Link.c_str());
			GameClient()->m_Menus.PopupConfirmOpenLink(Localize("Unsafe link"), aMessage, Localize("Open anyway"), Localize("Do not open"), Link.c_str(), true);
		}
		break;
	}
	case ELinkSafety::WARNING:
	{
		char aMessage[512];
		str_format(aMessage, sizeof(aMessage), Localize("This link goes to the following website.\n\n%s"), Link.c_str());
		GameClient()->m_Menus.PopupConfirmOpenLink(Localize("Leaving DDNet..."), aMessage, Localize("Open"), Localize("Cancel"), Link.c_str(), false);
		break;
	}
	case ELinkSafety::INVALID:
	default:
		break;
	}
}

void CChat::UpdateLinkPreflight()
{
	if(!m_LinkPreflight.m_pRequest)
		return;
	if(!m_LinkPreflight.m_pRequest->Done())
		return;

	std::shared_ptr<CHttpRequest> pRequest = m_LinkPreflight.m_pRequest;
	m_LinkPreflight.m_pRequest.reset();

	const bool WantsGetFallback =
		m_LinkPreflight.m_RequestType == ELinkPreflightRequestType::HEAD &&
		pRequest->State() == EHttpState::DONE &&
		(pRequest->StatusCode() == 405 || pRequest->StatusCode() == 501);

	if(WantsGetFallback)
	{
		m_LinkPreflight.m_RequestType = ELinkPreflightRequestType::GET;
		std::shared_ptr<CHttpRequest> pFallback = HttpGet(m_LinkPreflight.m_Link.c_str());
		pFallback->FailOnErrorStatus(false);
		pFallback->Header("Range: bytes=0-1023");
		pFallback->MaxResponseSize(4096);
		pFallback->LogProgress(HTTPLOG::FAILURE);
		pFallback->Timeout(CTimeout{2500, 5000, 500, 5});
		m_LinkPreflight.m_pRequest = pFallback;
		Http()->Run(pFallback);
		return;
	}

	const std::string Link = m_LinkPreflight.m_Link;
	const bool AlwaysConfirm = m_LinkPreflight.m_AlwaysConfirm;
	m_LinkPreflight = {};

	ELinkSafety Safety = ClassifyLink(Link);
	const bool PreflightDownload = pRequest->State() == EHttpState::DONE && IsLikelyPreflightDownload(*pRequest);
	const bool IsDownloadLink = IsDirectDownloadPath(Link) || PreflightDownload;
	if(PreflightDownload && Safety == ELinkSafety::WARNING)
		Safety = ELinkSafety::DANGER;

	ShowLinkPrompt(Link, AlwaysConfirm, Safety, IsDownloadLink);
}

void CChat::HandleLinkActivation(const std::string &Link, bool AlwaysConfirm)
{
	const ELinkSafety Safety = ClassifyLink(Link);
	const bool IsDownloadLink = IsDirectDownloadPath(Link);

	if(Safety == ELinkSafety::WARNING && !IsDownloadLink)
	{
		StartLinkPreflight(Link, AlwaysConfirm);
		return;
	}

	ShowLinkPrompt(Link, AlwaysConfirm, Safety, IsDownloadLink);
}

std::string CChat::BuildPlainTextLine(const CLine &Line) const
{
	if(ShouldHideLineFromStreamer(Line))
		return "";

	char aClientId[16] = "";
	if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
	{
		GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
	}

	char aCount[12] = "";
	if(Line.m_TimesRepeated > 0)
	{
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);
	}

	bool TextHiddenByStreamer = false;
	std::string VisibleTextStorage;
	const char *pText = Line.m_aText;
	if(Config()->m_ClStreamerMode && Line.m_ClientId == SERVER_MSG)
	{
		if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
		{
			TextHiddenByStreamer = true;
			pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
		}
		else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
		{
			TextHiddenByStreamer = true;
			pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
		}
		else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
		{
			TextHiddenByStreamer = true;
			pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
		}
	}
	else
	{
		VisibleTextStorage = BuildVisibleMessageText(Line, true);
		pText = VisibleTextStorage.c_str();
	}

	const CColoredParts ColoredParts(pText, Line.m_ClientId == CLIENT_MSG);
	pText = ColoredParts.Text();

	const char *pTranslatedError = nullptr;
	const char *pTranslatedText = nullptr;
	const char *pTranslatedLanguage = nullptr;
	if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
	{
		if(TextHiddenByStreamer)
			pTranslatedError = TCLocalize("Translated text hidden due to streamer mode");
		else if(Line.m_pTranslateResponse->m_Error)
			pTranslatedError = Line.m_pTranslateResponse->m_Text;
		else
		{
			pTranslatedText = Line.m_pTranslateResponse->m_Text;
			if(Line.m_pTranslateResponse->m_Language[0] != '\0')
				pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
		}
	}

	std::string Result;
	if(Line.m_aUClientRoomName[0])
	{
		Result += "[";
		Result += Line.m_aUClientRoomName;
		Result += "] ";
	}
	if(LineNeedsNameColon(Line) && ShouldShowFriendMarker(Line))
		Result += "♥ ";
	Result += aClientId;
	Result += Line.m_aName;
	Result += aCount;
	if(LineNeedsNameColon(Line))
		Result += LineNameSeparator(Line);
	if(pTranslatedText)
	{
		Result += pTranslatedText;
		if(pTranslatedLanguage)
		{
			Result += " [";
			Result += pTranslatedLanguage;
			Result += "]";
		}
	}
	else if(pTranslatedError)
	{
		Result += pText;
		Result += "\n";
		Result += pTranslatedError;
	}
	else
	{
		Result += pText;
	}
	return Result;
}

void CChat::RenderTextLine(CLine &Line, float y, float FontSize, float LineWidth, float TextBegin, float RealMsgPaddingTee, float RealMsgPaddingY, bool IsScoreBoardOpen, float Blend, std::string *pSelectionString)
{
	char aClientId[16] = "";
	if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
	{
		GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
	}

	char aCount[12] = "";
	if(Line.m_TimesRepeated > 0)
	{
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);
	}

	bool TextHiddenByStreamer = false;
	std::string VisibleTextStorage;
	const char *pText = Line.m_aText;
	if(Config()->m_ClStreamerMode && Line.m_ClientId == SERVER_MSG)
	{
		if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
		{
			TextHiddenByStreamer = true;
			pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
		}
		else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
		{
			TextHiddenByStreamer = true;
			pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
		}
		else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
		{
			TextHiddenByStreamer = true;
			pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
		}
	}
	else
	{
		VisibleTextStorage = BuildVisibleMessageText(Line, false);
		pText = VisibleTextStorage.c_str();
	}

	const CColoredParts ColoredParts(pText, Line.m_ClientId == CLIENT_MSG);
	pText = ColoredParts.Text();

	std::optional<ColorRGBA> CustomColor;
	if(!ColoredParts.Colors().empty() && ColoredParts.Colors()[0].m_Index == 0)
		CustomColor = ColoredParts.Colors()[0].m_Color;

	const char *pTranslatedError = nullptr;
	const char *pTranslatedText = nullptr;
	const char *pTranslatedLanguage = nullptr;
	if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
	{
		if(TextHiddenByStreamer)
			pTranslatedError = TCLocalize("Translated text hidden due to streamer mode");
		else if(Line.m_pTranslateResponse->m_Error)
			pTranslatedError = Line.m_pTranslateResponse->m_Text;
		else
		{
			pTranslatedText = Line.m_pTranslateResponse->m_Text;
			if(Line.m_pTranslateResponse->m_Language[0] != '\0')
				pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
		}
	}

	CTextCursor LineCursor;
	LineCursor.SetPosition(vec2(TextBegin, y + RealMsgPaddingY / 2.0f));
	LineCursor.m_FontSize = FontSize;
	LineCursor.m_LineWidth = LineWidth;
	if(m_MouseIsPress || m_HasSelection || m_WantsSelectionCopy)
	{
		LineCursor.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_CALCULATE;
		LineCursor.m_PressMouse = m_MousePress;
		LineCursor.m_ReleaseMouse = m_MouseRelease;
	}

	if(LineNeedsTeePadding(Line))
	{
		LineCursor.m_X += RealMsgPaddingTee;
		if(ShouldShowFriendMarker(Line))
		{
			TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor)).WithAlpha(Blend));
			TextRender()->TextEx(&LineCursor, "♥ ");
		}
	}

	if(Line.m_aUClientRoomName[0])
	{
		char aRoomLabel[72];
		str_format(aRoomLabel, sizeof(aRoomLabel), "[%s] ", Line.m_aUClientRoomName);
		TextRender()->TextColor(ColorRGBA(0.55f, 0.78f, 1.0f, Blend));
		TextRender()->TextEx(&LineCursor, aRoomLabel);
	}

	ColorRGBA NameColor;
	if(CustomColor)
		NameColor = *CustomColor;
	else if(Line.m_ClientId == SERVER_MSG)
		NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
	else if(Line.m_UClient)
		NameColor = CalculateNameColor(ColorHSLA(g_Config.m_UcMessageColor));
	else if(Line.m_ClientId == CLIENT_MSG)
		NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
	else if(Line.m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListChat && GameClient()->m_WarList.GetAnyWar(Line.m_ClientId))
		NameColor = GameClient()->m_WarList.GetPriorityColor(Line.m_ClientId);
	else if(Line.m_Team)
		NameColor = CalculateNameColor(ColorHSLA(g_Config.m_ClMessageTeamColor));
	else if(Line.m_NameColor == TEAM_RED)
		NameColor = ColorRGBA(1.0f, 0.5f, 0.5f, Blend);
	else if(Line.m_NameColor == TEAM_BLUE)
		NameColor = ColorRGBA(0.7f, 0.7f, 1.0f, Blend);
	else if(Line.m_NameColor == TEAM_SPECTATORS)
		NameColor = ColorRGBA(0.75f, 0.5f, 0.75f, Blend);
	else if(Line.m_ClientId >= 0 && g_Config.m_ClChatTeamColors && GameClient()->m_Teams.Team(Line.m_ClientId))
		NameColor = GameClient()->GetDDTeamColor(GameClient()->m_Teams.Team(Line.m_ClientId), 0.75f);
	else
		NameColor = ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);
	NameColor.a *= Blend;

	TextRender()->TextColor(NameColor);
	TextRender()->TextEx(&LineCursor, aClientId);
	TextRender()->TextEx(&LineCursor, Line.m_aName);
	if(Line.m_TimesRepeated > 0)
	{
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f * Blend);
		TextRender()->TextEx(&LineCursor, aCount);
	}
	if(LineNeedsNameColon(Line))
	{
		TextRender()->TextColor(NameColor);
		TextRender()->TextEx(&LineCursor, LineNameSeparator(Line));
	}

	ColorRGBA Color;
	if(CustomColor)
		Color = *CustomColor;
	else if(Line.m_ClientId == SERVER_MSG)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
	else if(Line.m_Highlighted)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
	else if(Line.m_UClient)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UcMessageColor));
	else if(Line.m_ClientId == CLIENT_MSG)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
	else if(Line.m_Team)
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
	else
		Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
	Color.a *= Blend;
	TextRender()->TextColor(Color);

	const float PrefixWidth = LineCursor.m_LongestLineWidth;
	LineCursor.m_LongestLineWidth = 0.0f;
	if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
	{
		LineCursor.m_StartX = LineCursor.m_X;
		LineCursor.m_LineWidth -= PrefixWidth;
	}

	if(pTranslatedText)
	{
		TextRender()->TextEx(&LineCursor, pTranslatedText);
		if(pTranslatedLanguage)
		{
			ColorRGBA ColorLang = Color;
			ColorLang.r *= 0.8f;
			ColorLang.g *= 0.8f;
			ColorLang.b *= 0.8f;
			TextRender()->TextColor(ColorLang);
			TextRender()->TextEx(&LineCursor, " [");
			TextRender()->TextEx(&LineCursor, pTranslatedLanguage);
			TextRender()->TextEx(&LineCursor, "]");
		}
	}
	else if(pTranslatedError)
	{
		TextRender()->TextColor(Color);
		TextRender()->TextEx(&LineCursor, pText);
		ColorRGBA ColorSub = Color;
		ColorSub.r = 0.7f;
		ColorSub.g = 0.6f;
		ColorSub.b = 0.6f;
		TextRender()->TextColor(ColorSub);
		TextRender()->TextEx(&LineCursor, "\n");
		LineCursor.m_FontSize *= 0.8f;
		TextRender()->TextEx(&LineCursor, pTranslatedError);
		LineCursor.m_FontSize /= 0.8f;
	}
	else
	{
		LineCursor.m_vColorSplits = {};
		ColoredParts.AddSplitsToCursor(LineCursor);
		TextRender()->TextEx(&LineCursor, pText);
		LineCursor.m_vColorSplits.clear();
	}

	if((m_MouseIsPress || m_HasSelection || m_WantsSelectionCopy) && LineCursor.m_SelectionStart >= 0 && LineCursor.m_SelectionEnd >= 0 && LineCursor.m_SelectionStart != LineCursor.m_SelectionEnd)
	{
		m_HasSelection = true;
		if(pSelectionString != nullptr)
		{
			const std::string PlainText = BuildPlainTextLine(Line);
			const int SelectionMin = minimum(LineCursor.m_SelectionStart, LineCursor.m_SelectionEnd);
			const int SelectionMax = maximum(LineCursor.m_SelectionStart, LineCursor.m_SelectionEnd);
			const size_t OffUTF8Start = str_utf8_offset_chars_to_bytes(PlainText.c_str(), SelectionMin);
			const size_t OffUTF8End = str_utf8_offset_chars_to_bytes(PlainText.c_str(), SelectionMax);
			const bool HasNewLine = !pSelectionString->empty();
			pSelectionString->insert(0, PlainText.substr(OffUTF8Start, OffUTF8End - OffUTF8Start) + (HasNewLine ? "\n" : ""));
		}
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

bool CChat::GetMediaViewerRect(const CLine &Line, float ScreenWidth, float ScreenHeight, float &x, float &y, float &w, float &h) const
{
	if(Line.m_MediaWidth <= 0 || Line.m_MediaHeight <= 0)
		return false;

	const float Margin = FontSize() * 2.0f;
	const float MaxW = maximum(16.0f, ScreenWidth - Margin * 2.0f);
	const float MaxH = maximum(16.0f, ScreenHeight - Margin * 2.0f);
	const float FitScale = minimum(MaxW / (float)Line.m_MediaWidth, MaxH / (float)Line.m_MediaHeight);
	const float BaseW = (float)Line.m_MediaWidth * FitScale;
	const float BaseH = (float)Line.m_MediaHeight * FitScale;

	w = BaseW * m_MediaViewerZoom;
	h = BaseH * m_MediaViewerZoom;
	x = (ScreenWidth - w) / 2.0f + m_MediaViewerPan.x;
	y = (ScreenHeight - h) / 2.0f + m_MediaViewerPan.y;
	return true;
}

void CChat::ClampMediaViewerPan(const CLine &Line, float ScreenWidth, float ScreenHeight)
{
	float x = 0.0f;
	float y = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
	if(!GetMediaViewerRect(Line, ScreenWidth, ScreenHeight, x, y, w, h))
		return;

	float ClampedX = x;
	float ClampedY = y;
	if(w <= ScreenWidth)
		ClampedX = (ScreenWidth - w) / 2.0f;
	else
		ClampedX = maximum(ScreenWidth - w, minimum(0.0f, ClampedX));
	if(h <= ScreenHeight)
		ClampedY = (ScreenHeight - h) / 2.0f;
	else
		ClampedY = maximum(ScreenHeight - h, minimum(0.0f, ClampedY));

	m_MediaViewerPan.x += ClampedX - x;
	m_MediaViewerPan.y += ClampedY - y;
}

bool CChat::OnInput(const IInput::CEvent &Event)
{
	// uclient: chat paste image
	if(m_UcChatPaste.OnInput(this, Event))
		return true;

	const bool ChatInputActive = m_Mode != MODE_NONE;
	const bool ChatInteractionActive = ChatInputActive || m_Show;

	// UClient: when a chat-owned modal popup (media context / save-as / reaction picker) is open,
	// route input to the popup and consume mouse events so the chat behind it is not interacted
	// with (reply buttons, player-name links, text selection, etc.). This must run before the
	// reply/link handlers below, otherwise clicks "pass through" the popup to the chat behind.
	const bool ChatModalPopupOpen = Ui()->IsPopupOpen(&m_MediaContextPopupId) || Ui()->IsPopupOpen(&m_MediaSaveAssetPopupId) ||
		Ui()->IsPopupOpen(&m_MediaSaveSkinPopupId) || Ui()->IsPopupOpen(&m_ReactionPickerPopupId);
	if(ChatInteractionActive && ChatModalPopupOpen)
	{
		// Let the popup handle text input / active items first.
		if(Ui()->OnInput(Event))
			return true;
		// Consume remaining mouse events so the chat behind the popup does not react. Popup
		// buttons and click-outside-to-close are handled by RenderPopupMenus using the polled
		// mouse state, so this does not break the popup itself. Keyboard events fall through so
		// escape-to-close still works.
		if(Event.m_Key == KEY_MOUSE_1 || Event.m_Key == KEY_MOUSE_2 || Event.m_Key == KEY_MOUSE_3 ||
			Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
			return true;
	}

	if(ChatInputActive && Input()->ModifierIsPressed())
	{
		const bool PasteKey = (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_V;
		const bool PasteText = (Event.m_Flags & IInput::FLAG_TEXT) != 0;
		if(PasteKey || PasteText)
		{
			if(m_UcChatPaste.TryPasteFromClipboard(this))
				return true;
		}
	}

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 &&
		m_HoveredServerJoinLineIndex >= 0 && m_HoveredServerJoinLineIndex < MAX_LINES)
	{
		const CLine &Line = m_aLines[m_HoveredServerJoinLineIndex];
		if(Line.m_HasServerJoinLink && Line.m_aServerJoinAddress[0] != '\0')
		{
			GameClient()->m_Menus.RequestUClientServerJoin(Line.m_aServerJoinAddress, Line.m_aServerJoinServerName);
			return true;
		}
	}

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && !m_HoveredLink.empty())
	{
		HandleLinkActivation(m_HoveredLink, m_HoveredLinkAlwaysConfirm);
		return true;
	}

	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && m_ReplyCancelButtonRectValid)
	{
		const vec2 MousePos = ChatMousePos();
		const SRenderRect &Rect = m_ReplyCancelButtonRect;
		if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
			MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
		{
			ClearPendingReply();
			return true;
		}
	}

	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1)
	{
		const vec2 MousePos = ChatMousePos();
		// Prefer toggling the inline settings card before other chat click handlers.
		for(int i = 0; i < MAX_LINES; ++i)
		{
			CLine &Line = m_aLines[i];
			if(!Line.m_SettingsLinkRectValid)
				continue;
			if(TryHandleSettingsLinkClick(Line, MousePos, FontSize()))
			{
				m_MouseIsPress = false;
				m_HasSelection = false;
				return true;
			}
		}

		for(int i = 0; i < MAX_LINES; ++i)
		{
			CLine &Line = m_aLines[i];
			if(!Line.m_SettingsShortcutRectValid)
				continue;
			const SRenderRect &Rect = Line.m_SettingsShortcutRect;
			if(MousePos.x < Rect.m_X || MousePos.x > Rect.m_X + Rect.m_W ||
				MousePos.y < Rect.m_Y || MousePos.y > Rect.m_Y + Rect.m_H)
				continue;
			CUClientSettingsLink::SNavigateRequest Nav;
			if(CUClientSettingsLink::BuildNavigateRequest(Line.m_SettingsLinkParsed, Nav))
			{
				DisableMode();
				// +show_chat (expand-only) steals mouse wheel; clear it so settings can scroll.
				m_Show = false;
				GameClient()->m_Menus.NavigateToSettingsLink(Nav);
			}
			return true;
		}
	}

	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && m_HoveredReplyLineIndex >= 0)
	{
		CLine &Line = m_aLines[m_HoveredReplyLineIndex];
		if(Line.m_ReplyButtonRectValid)
		{
			const vec2 MousePos = ChatMousePos();
			const SRenderRect &Rect = Line.m_ReplyButtonRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				const char *pReplyName = Line.m_aName;
				char aSanitizedName[64];
				if(Line.m_ClientId >= 0 && Line.m_ClientId < MAX_CLIENTS && GameClient()->m_aClients[Line.m_ClientId].m_Active)
				{
					GameClient()->m_BestClient.SanitizePlayerName(GameClient()->m_aClients[Line.m_ClientId].m_aName, aSanitizedName, sizeof(aSanitizedName), Line.m_ClientId);
					pReplyName = aSanitizedName;
				}
				if(Line.m_UClient && g_Config.m_UcChat && g_Config.m_UcChatSendSameServerOnly && !Line.m_UClientFromCurrentServer)
				{
					StashUcReplySendScopePrompt(Line.m_ClientId, pReplyName, m_HoveredReplyLineIndex, GetLineDisplayText(Line));
					GameClient()->m_Menus.OfferDisableUcChatSendSameServerForReply();
					return true;
				}
				SetPendingReply(Line.m_ClientId, pReplyName, m_HoveredReplyLineIndex, GetLineDisplayText(Line));
				// Match input mode to the message being replied to (same as UClient reply).
				if(Line.m_UClient && g_Config.m_UcChat)
					m_Mode = MODE_UCLIENT;
				else if(Line.m_Team)
					m_Mode = MODE_TEAM;
				else
					m_Mode = MODE_ALL;
				m_Input.Activate(EInputPriority::CHAT);
				return true;
			}
		}
	}

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && !m_HoveredPlayerName.empty())
	{
		const std::string Url = BuildPlayerSearchUrl(m_HoveredPlayerName.c_str());
		if(!Url.empty())
			os_open_link(Url.c_str());
		return true;
	}

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && g_Config.m_BcChatMediaPreview &&
		(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK))
	{
		bool HasRetryTargets = false;
		for(const auto &Line : m_aLines)
		{
			if(Line.m_MediaRetryRectValid && Line.m_MediaState == EMediaState::FAILED)
			{
				HasRetryTargets = true;
				break;
			}
		}
		if(HasRetryTargets)
		{
			const vec2 MousePos = ChatMousePos();
			for(auto &Line : m_aLines)
			{
				if(!Line.m_MediaRetryRectValid || Line.m_MediaState != EMediaState::FAILED)
					continue;

				const SRenderRect &Rect = Line.m_MediaRetryRect;
				if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
					MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
				{
					if(RetryMediaLine(Line))
						return true;
				}
			}
		}
	}

	if(!ChatInteractionActive)
		return false;

	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE && Ui()->IsPopupOpen())
	{
		Ui()->ClosePopupMenus();
		return true;
	}

	if(ChatInputActive && Ui()->IsPopupOpen(&m_TranslateSettingsPopupId) && Ui()->OnInput(Event))
		return true;
	if(ChatInputActive && Ui()->IsPopupOpen(&m_RoomSelectPopupId) && Ui()->OnInput(Event))
		return true;

	if(ChatInputActive && Ui()->IsPopupOpen(&m_GiphyPopupId) && Ui()->OnInput(Event))
		return true;

	if(ChatInputActive && Event.m_Key == KEY_MOUSE_1 && m_GiphyButtonRectValid)
	{
		const vec2 MousePos = ChatMousePos();
		const bool InsideGiphyButton =
			MousePos.x >= m_GiphyButtonRect.m_X && MousePos.x <= m_GiphyButtonRect.m_X + m_GiphyButtonRect.m_W &&
			MousePos.y >= m_GiphyButtonRect.m_Y && MousePos.y <= m_GiphyButtonRect.m_Y + m_GiphyButtonRect.m_H;

		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			m_GiphyButtonPressed = InsideGiphyButton;
			if(InsideGiphyButton)
			{
				m_MouseIsPress = false;
				m_HasSelection = false;
				return true;
			}
		}
		else if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool ActivateButton = m_GiphyButtonPressed && InsideGiphyButton;
			m_GiphyButtonPressed = false;
			if(ActivateButton)
			{
				CUIRect ButtonRect = {m_GiphyButtonRect.m_X, m_GiphyButtonRect.m_Y, m_GiphyButtonRect.m_W, m_GiphyButtonRect.m_H};
				if(Ui()->IsPopupOpen(&m_GiphyPopupId))
				{
					Ui()->ClosePopupMenu(&m_GiphyPopupId);
					m_GiphySearchInput.Deactivate();
					Ui()->SetActiveItem(nullptr);
				}
				else
					OpenGiphyPopup(ButtonRect);
				return true;
			}
		}
	}

	if(ChatInputActive && Event.m_Key == KEY_MOUSE_1 && m_TranslateButtonRectValid)
	{
		const vec2 MousePos = ChatMousePos();
		const bool InsideTranslateButton =
			MousePos.x >= m_TranslateButtonRect.m_X && MousePos.x <= m_TranslateButtonRect.m_X + m_TranslateButtonRect.m_W &&
			MousePos.y >= m_TranslateButtonRect.m_Y && MousePos.y <= m_TranslateButtonRect.m_Y + m_TranslateButtonRect.m_H;

		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			m_TranslateButtonPressed = InsideTranslateButton;
			if(InsideTranslateButton)
			{
				m_MouseIsPress = false;
				m_HasSelection = false;
				return true;
			}
		}
		else if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool ActivateButton = m_TranslateButtonPressed && InsideTranslateButton;
			m_TranslateButtonPressed = false;
			if(ActivateButton)
			{
				CUIRect ButtonRect = {m_TranslateButtonRect.m_X, m_TranslateButtonRect.m_Y, m_TranslateButtonRect.m_W, m_TranslateButtonRect.m_H};
				if(Ui()->IsPopupOpen(&m_TranslateSettingsPopupId))
					Ui()->ClosePopupMenu(&m_TranslateSettingsPopupId);
				else
					OpenTranslateSettingsPopup(ButtonRect);
				return true;
			}
		}
	}

	if(ChatInputActive && Event.m_Key == KEY_MOUSE_1 && m_RoomButtonRectValid)
	{
		const vec2 MousePos = ChatMousePos();
		const bool InsideRoomButton =
			MousePos.x >= m_RoomButtonRect.m_X && MousePos.x <= m_RoomButtonRect.m_X + m_RoomButtonRect.m_W &&
			MousePos.y >= m_RoomButtonRect.m_Y && MousePos.y <= m_RoomButtonRect.m_Y + m_RoomButtonRect.m_H;
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			m_RoomButtonPressed = InsideRoomButton;
			if(InsideRoomButton)
			{
				m_MouseIsPress = false;
				m_HasSelection = false;
				return true;
			}
		}
		else if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool ActivateButton = m_RoomButtonPressed && InsideRoomButton;
			m_RoomButtonPressed = false;
			if(ActivateButton)
			{
				CUIRect ButtonRect = {m_RoomButtonRect.m_X, m_RoomButtonRect.m_Y, m_RoomButtonRect.m_W, m_RoomButtonRect.m_H};
				if(Ui()->IsPopupOpen(&m_RoomSelectPopupId))
					Ui()->ClosePopupMenu(&m_RoomSelectPopupId);
				else
					OpenRoomSelectPopup(ButtonRect);
				return true;
			}
		}
	}

	if(m_MediaViewerOpen && (!g_Config.m_BcChatMediaPreview || !g_Config.m_BcChatMediaViewer))
		CloseMediaViewer();

	if(m_MediaViewerOpen && !ValidateMediaViewerLine())
		CloseMediaViewer();

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && m_HideMediaByBind && !m_MediaViewerOpen)
	{
		const vec2 MousePos = ChatMousePos();
		for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
		{
			const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
			CLine &Line = m_aLines[LineIndex];
			if(!Line.m_Initialized)
				break;
			if(!Line.m_MediaPreviewRectValid || !ShouldHideMediaPreview(Line) || ShouldHideNsfwMedia(Line))
				continue;
			const SRenderRect &Rect = Line.m_MediaPreviewRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				Line.m_MediaRevealed = true;
				Line.m_aYOffset[0] = -1.0f;
				Line.m_aYOffset[1] = -1.0f;
				RebuildChat();
				return true;
			}
		}
	}

	if(m_MediaViewerOpen && ValidateMediaViewerLine())
	{
		CLine &ViewerLine = m_aLines[m_MediaViewerLineIndex];
		const float ScreenHeight = 300.0f;
		const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
		const vec2 MousePos = ChatMousePos();
		float ViewerX = 0.0f;
		float ViewerY = 0.0f;
		float ViewerW = 0.0f;
		float ViewerH = 0.0f;
		GetMediaViewerRect(ViewerLine, ScreenWidth, ScreenHeight, ViewerX, ViewerY, ViewerW, ViewerH);

		if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
		{
			CloseMediaViewer();
			return true;
		}

		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			const float MaxZoom = maximum(1.0f, g_Config.m_BcChatMediaViewerMaxZoom / 100.0f);
			const float ZoomStep = 1.12f;
			if(Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
			{
				const float OldZoom = m_MediaViewerZoom;
				float NewZoom = OldZoom;
				if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
					NewZoom = minimum(MaxZoom, OldZoom * ZoomStep);
				else
					NewZoom = maximum(1.0f, OldZoom / ZoomStep);

				if(NewZoom != OldZoom && ViewerW > 0.0f && ViewerH > 0.0f)
				{
					const float RelX = (MousePos.x - ViewerX) / ViewerW;
					const float RelY = (MousePos.y - ViewerY) / ViewerH;
					m_MediaViewerZoom = NewZoom;
					float NewX = 0.0f;
					float NewY = 0.0f;
					float NewW = 0.0f;
					float NewH = 0.0f;
					GetMediaViewerRect(ViewerLine, ScreenWidth, ScreenHeight, NewX, NewY, NewW, NewH);
					const float TargetX = MousePos.x - RelX * NewW;
					const float TargetY = MousePos.y - RelY * NewH;
					m_MediaViewerPan.x += TargetX - NewX;
					m_MediaViewerPan.y += TargetY - NewY;
					ClampMediaViewerPan(ViewerLine, ScreenWidth, ScreenHeight);
				}
				return true;
			}

			if(Event.m_Key == KEY_MOUSE_1)
			{
				const bool InsideMedia = MousePos.x >= ViewerX && MousePos.x <= ViewerX + ViewerW &&
					MousePos.y >= ViewerY && MousePos.y <= ViewerY + ViewerH;
				if(!InsideMedia)
				{
					CloseMediaViewer();
					return true;
				}

				const int64_t Now = time_get();
				if(m_MediaViewerLastClickTime > 0 &&
					(Now - m_MediaViewerLastClickTime) * 1000 / time_freq() <= CHAT_MEDIA_DOUBLE_CLICK_MS)
				{
					m_MediaViewerZoom = 1.0f;
					m_MediaViewerPan = vec2(0.0f, 0.0f);
					m_MediaViewerDragging = false;
					m_MediaViewerLastClickTime = 0;
					return true;
				}

				m_MediaViewerLastClickTime = Now;
				m_MediaViewerDragging = true;
				m_MediaViewerDragStartMouse = MousePos;
				m_MediaViewerPanStart = m_MediaViewerPan;
				return true;
			}
		}

		if((Event.m_Flags & IInput::FLAG_RELEASE) && Event.m_Key == KEY_MOUSE_1)
		{
			m_MediaViewerDragging = false;
			return true;
		}
	}

	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_2 && g_Config.m_BcChatMediaPreview && !m_MediaViewerOpen && !m_pMediaSaveRequest)
	{
		const vec2 MousePos = ChatMousePos();
		for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
		{
			const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
			CLine &Line = m_aLines[LineIndex];
			if(!Line.m_Initialized)
				break;
			if(!Line.m_MediaPreviewRectValid || Line.m_MediaState != EMediaState::READY)
				continue;
			const SRenderRect &Rect = Line.m_MediaPreviewRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
				const vec2 UiPos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
				OpenMediaContextMenu(LineIndex, UiPos.x, UiPos.y);
				return true;
			}
		}
	}

	// UClient: right-click a chat message (not over a media preview) to open the reaction picker.
	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_2 && !m_MediaViewerOpen && !m_pMediaSaveRequest)
	{
		const vec2 MousePos = ChatMousePos();
		for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
		{
			const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
			CLine &Line = m_aLines[LineIndex];
			if(!Line.m_Initialized)
				break;
			if(!Line.m_LineRectValid || !CanReactToLine(Line))
				continue;
			const SRenderRect &Rect = Line.m_LineRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
				const vec2 UiPos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
				OpenReactionPicker(LineIndex, UiPos.x, UiPos.y);
				return true;
			}
		}
	}

	// UClient: left-click an existing reaction pill to toggle your own reaction.
	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && !m_MediaViewerOpen)
	{
		const vec2 MousePos = ChatMousePos();
		for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
		{
			const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
			CLine &Line = m_aLines[LineIndex];
			if(!Line.m_Initialized)
				break;
			if(!Line.m_ReactionRectsValid)
				continue;
			const size_t Count = minimum(Line.m_vReactionRects.size(), Line.m_vReactions.size());
			for(size_t r = 0; r < Count; ++r)
			{
				const SRenderRect &Rect = Line.m_vReactionRects[r];
				if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
					MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
				{
					char aEmoji[16];
					str_copy(aEmoji, Line.m_vReactions[r].m_aEmoji);
					ToggleLocalReaction(LineIndex, aEmoji);
					return true;
				}
			}
		}
	}

	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && !m_MediaViewerOpen && !m_pMapSaveRequest)
	{
		const vec2 MousePos = ChatMousePos();
		for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
		{
			const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
			CLine &Line = m_aLines[LineIndex];
			if(!Line.m_Initialized)
				break;
			if(!Line.m_MapDownloadBtnRectValid)
				continue;
			const SRenderRect &Rect = Line.m_MapDownloadBtnRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				OpenMapContextMenu(LineIndex);
				return true;
			}
		}
	}

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && g_Config.m_BcChatMediaViewer && !m_MediaViewerOpen)
	{
		const vec2 MousePos = ChatMousePos();
		for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
		{
			const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
			CLine &Line = m_aLines[LineIndex];
			if(!Line.m_Initialized)
				break;
			// NSFW content is a hard block (see ShouldHideNsfwMedia): never let a click open the
			// full media viewer for it, even if the preview rect is otherwise valid.
			if(!Line.m_MediaPreviewRectValid || ShouldHideNsfwMedia(Line))
				continue;
			const SRenderRect &Rect = Line.m_MediaPreviewRect;
			if(MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
				MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H)
			{
				OpenMediaViewer(LineIndex);
				return m_MediaViewerOpen;
			}
		}
	}

	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(Input()->ModifierIsPressed() && Event.m_Key == KEY_C && !m_Input.HasSelection() && m_HasSelection)
		{
			m_WantsSelectionCopy = true;
			return true;
		}

		if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
		{
			m_BacklogCurLine = minimum(m_BacklogCurLine + 1, MAX_LINES - 1);
			m_HasSelection = false;
			return true;
		}
		if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
		{
			m_BacklogCurLine = maximum(m_BacklogCurLine - 1, 0);
			m_HasSelection = false;
			return true;
		}
	}

	if(!ChatInputActive)
	{
		if(Event.m_Key == KEY_MOUSE_1 && (Event.m_Flags & (IInput::FLAG_PRESS | IInput::FLAG_RELEASE)))
			return true;
		return false;
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		const bool SaveDraft = g_Config.m_BcChatSaveDraft != 0;
		DisableMode();
		GameClient()->OnRelease();
		if(g_Config.m_ClChatReset)
		{
			if(SaveDraft)
			{
				if(m_Input.GetString()[0] != '\0')
				{
					str_copy(m_aSavedInputText, m_Input.GetString(), sizeof(m_aSavedInputText));
					m_SavedInputPending = true;
				}
				else
				{
					m_SavedInputPending = false;
					m_aSavedInputText[0] = '\0';
				}
			}
			else
			{
				m_SavedInputPending = false;
				m_aSavedInputText[0] = '\0';
			}
			m_Input.Clear();
			m_pHistoryEntry = nullptr;
		}
		else if(!SaveDraft)
		{
			m_Input.Clear();
			m_SavedInputPending = false;
			m_aSavedInputText[0] = '\0';
			m_pHistoryEntry = nullptr;
		}
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
	}
	else if(Event.m_Flags & IInput::FLAG_PRESS && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER))
	{
		// uclient: chat paste image
		if(m_UcChatPaste.TrySendOnEnter(this, m_Input.GetString()))
		{
			m_HasSelection = false;
			m_WantsSelectionCopy = false;
			return true;
		}

		if(m_ServerCommandsNeedSorting)
		{
			std::sort(m_vServerCommands.begin(), m_vServerCommands.end());
			m_ServerCommandsNeedSorting = false;
		}

		if(GameClient()->m_BindChat.ChatDoBinds(m_Input.GetString()))
			; // Do nothing as bindchat was executed
		else if(GameClient()->m_TClient.ChatDoSpecId(m_Input.GetString()))
			; // Do nothing as specid was executed
		else
			SendChatQueued(m_Input.GetString());
		m_SavedInputPending = false;
		m_aSavedInputText[0] = '\0';
		m_pHistoryEntry = nullptr;
		DisableMode();
		GameClient()->OnRelease();
		m_Input.Clear();
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
	}
	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_TAB)
	{
		const bool ShiftPressed = Input()->ShiftIsPressed();
		const bool CtrlPressed = Input()->KeyIsPressed(KEY_LCTRL) || Input()->KeyIsPressed(KEY_RCTRL);

		// fill the completion buffer
		if(!m_CompletionUsed)
		{
			const char *pCursor = m_Input.GetString() + m_Input.GetCursorOffset();
			for(size_t Count = 0; Count < m_Input.GetCursorOffset() && *(pCursor - 1) != ' '; --pCursor, ++Count)
				;
			m_PlaceholderOffset = pCursor - m_Input.GetString();

			for(m_PlaceholderLength = 0; *pCursor && *pCursor != ' '; ++pCursor)
				++m_PlaceholderLength;

			str_truncate(m_aCompletionBuffer, sizeof(m_aCompletionBuffer), m_Input.GetString() + m_PlaceholderOffset, m_PlaceholderLength);
		}

		if(!m_CompletionUsed && m_aCompletionBuffer[0] != '/' && m_aCompletionBuffer[0] != '!')
		{
			m_PlayerCompletionListLength = 0;
			m_vUClientCompletionNames.clear();
			if(CtrlPressed)
			{
				if(m_Mode == MODE_UCLIENT)
				{
					// Ctrl+Tab always uses players on the current game server.
					for(auto &PlayerInfo : GameClient()->m_Snap.m_apInfoByName)
					{
						if(!PlayerInfo)
							continue;
						const char *pName = GameClient()->m_aClients[PlayerInfo->m_ClientId].m_aName;
						const char *pFound = str_utf8_find_nocase(pName, m_aCompletionBuffer);
						if(pFound)
							m_aPlayerCompletionList[m_PlayerCompletionListLength++] = {PlayerInfo->m_ClientId, (int)(pFound - pName)};
					}
				}
				else
				{
					// Existing Ctrl+Tab behavior outside UClient chat: nearby players.
					CUClientChatNearbyTab::SEntry aNearby[MAX_CLIENTS];
					int NearbyLength = 0;
					CUClientChatNearbyTab::BuildCompletionList(GameClient(), aNearby, NearbyLength, MAX_CLIENTS);
					for(int i = 0; i < NearbyLength; ++i)
					{
						m_aPlayerCompletionList[i].m_ClientId = aNearby[i].m_ClientId;
						m_aPlayerCompletionList[i].m_Score = aNearby[i].m_Score;
					}
					m_PlayerCompletionListLength = NearbyLength;
				}
			}
			else if(m_Mode == MODE_UCLIENT)
			{
				const char *pRoomId = GameClient()->m_UClientChatRooms.SelectedSendRoomId();
				if(pRoomId[0])
				{
					for(const auto &Room : GameClient()->m_UClientChatRooms.Rooms())
					{
						if(str_comp(Room.m_aId, pRoomId) != 0)
							continue;
						for(const auto &Member : Room.m_vMembers)
							if(Member.m_aDisplayName[0] && str_utf8_find_nocase(Member.m_aDisplayName, m_aCompletionBuffer))
								m_vUClientCompletionNames.emplace_back(Member.m_aDisplayName);
						break;
					}
				}
				else if(g_Config.m_UcChatSendSameServerOnly)
				{
					for(auto &PlayerInfo : GameClient()->m_Snap.m_apInfoByName)
					{
						if(!PlayerInfo || !GameClient()->m_ClientIndicator.IsPlayerUClient(PlayerInfo->m_ClientId))
							continue;
						const char *pName = GameClient()->m_aClients[PlayerInfo->m_ClientId].m_aName;
						if(str_utf8_find_nocase(pName, m_aCompletionBuffer))
							m_vUClientCompletionNames.emplace_back(pName);
					}
				}
				else
				GameClient()->m_ClientIndicator.CollectOnlineUClientNames(m_vUClientCompletionNames);
				std::erase_if(m_vUClientCompletionNames, [&](const std::string &Name) {
					return str_utf8_find_nocase(Name.c_str(), m_aCompletionBuffer) == nullptr;
				});
				std::stable_sort(m_vUClientCompletionNames.begin(), m_vUClientCompletionNames.end(), [&](const std::string &Left, const std::string &Right) {
					const char *pLeft = str_utf8_find_nocase(Left.c_str(), m_aCompletionBuffer);
					const char *pRight = str_utf8_find_nocase(Right.c_str(), m_aCompletionBuffer);
					const int LeftScore = pLeft ? (int)(pLeft - Left.c_str()) : 0;
					const int RightScore = pRight ? (int)(pRight - Right.c_str()) : 0;
					return LeftScore == RightScore ? str_comp_nocase(Left.c_str(), Right.c_str()) < 0 : LeftScore < RightScore;
				});
			}
			else
			{
				// Create the completion list of player names through which the player can iterate
				const char *PlayerName, *FoundInput;
				for(auto &PlayerInfo : GameClient()->m_Snap.m_apInfoByName)
				{
					if(PlayerInfo)
					{
						PlayerName = GameClient()->m_aClients[PlayerInfo->m_ClientId].m_aName;
						FoundInput = str_utf8_find_nocase(PlayerName, m_aCompletionBuffer);
						if(FoundInput != nullptr)
						{
							m_aPlayerCompletionList[m_PlayerCompletionListLength].m_ClientId = PlayerInfo->m_ClientId;
							// The score for suggesting a player name is determined by the distance of the search input to the beginning of the player name
							m_aPlayerCompletionList[m_PlayerCompletionListLength].m_Score = (int)(FoundInput - PlayerName);
							m_PlayerCompletionListLength++;
						}
					}
				}
			}
			std::stable_sort(m_aPlayerCompletionList, m_aPlayerCompletionList + m_PlayerCompletionListLength,
				[](const CRateablePlayer &Player1, const CRateablePlayer &Player2) -> bool {
					return Player1.m_Score < Player2.m_Score;
				});
		}

		auto DoVoiceAutocomplete = [&]() -> bool {
			const char *pInput = m_Input.GetString();
			if(!pInput || pInput[0] != '!')
				return false;

			const int InputLen = str_length(pInput);
			int aTokenStarts[8];
			int aTokenEnds[8];
			int NumTokens = 0;
			bool InToken = false;
			int TokenStart = 0;
			for(int i = 0; i <= InputLen && NumTokens < 8; ++i)
			{
				const char c = pInput[i];
				const bool IsSpace = c == '\0' || std::isspace((unsigned char)c);
				if(!InToken && !IsSpace)
				{
					InToken = true;
					TokenStart = i;
				}
				else if(InToken && IsSpace)
				{
					InToken = false;
					aTokenStarts[NumTokens] = TokenStart;
					aTokenEnds[NumTokens] = i;
					NumTokens++;
				}
			}
			if(NumTokens <= 0)
				return false;

			int PlaceholderToken = -1;
			for(int t = 0; t < NumTokens; ++t)
			{
				if(aTokenStarts[t] == m_PlaceholderOffset)
				{
					PlaceholderToken = t;
					break;
				}
				if(m_PlaceholderOffset >= aTokenStarts[t] && m_PlaceholderOffset < aTokenEnds[t])
					PlaceholderToken = t;
			}
			if(PlaceholderToken < 0 && m_PlaceholderLength == 0)
			{
				// Cursor is on whitespace (e.g. "!voice "): treat as completing the next token.
				if(m_PlaceholderOffset >= 0 && (m_PlaceholderOffset == InputLen || std::isspace((unsigned char)pInput[m_PlaceholderOffset])))
					PlaceholderToken = NumTokens;
			}
			if(PlaceholderToken < 0)
				return false;

			char aToken0[64];
			str_truncate(aToken0, sizeof(aToken0), pInput + aTokenStarts[0], aTokenEnds[0] - aTokenStarts[0]);
			char aToken1[64] = {};
			if(NumTokens > 1)
				str_truncate(aToken1, sizeof(aToken1), pInput + aTokenStarts[1], aTokenEnds[1] - aTokenStarts[1]);

			const char *apSuggestions[24];
			int NumSuggestions = 0;

			if(PlaceholderToken == 0)
			{
				apSuggestions[NumSuggestions++] = "!voice";
			}
			else
			{
				if(str_comp_nocase(aToken0, "!voice") == 0)
				{
					if(PlaceholderToken == 1)
					{
						apSuggestions[NumSuggestions++] = "mute";
						apSuggestions[NumSuggestions++] = "unmute";
						apSuggestions[NumSuggestions++] = "volume";
						apSuggestions[NumSuggestions++] = "radius";
					}
					else if(PlaceholderToken == 2 && str_comp_nocase(aToken1, "radius") == 0)
					{
						apSuggestions[NumSuggestions++] = "on";
						apSuggestions[NumSuggestions++] = "off";
					}
					else
					{
						return false;
					}
				}
				else
				{
					return false;
				}
			}

			if(NumSuggestions <= 0)
				return false;

			const char *pCompletion = nullptr;
			if(ShiftPressed && m_CompletionUsed)
				m_CompletionChosen--;
			else if(!ShiftPressed)
				m_CompletionChosen++;
			m_CompletionChosen = (m_CompletionChosen % NumSuggestions + NumSuggestions) % NumSuggestions;
			m_CompletionUsed = true;

			for(int i = 0; i < NumSuggestions; ++i)
			{
				const int Index = (m_CompletionChosen + (ShiftPressed ? -i : i) + NumSuggestions) % NumSuggestions;
				const char *pCandidate = apSuggestions[Index];
				if(str_startswith_nocase(pCandidate, m_aCompletionBuffer))
				{
					pCompletion = pCandidate;
					m_CompletionChosen = Index;
					break;
				}
			}
			if(!pCompletion)
				return false;

			char aBuf[MAX_LINE_LENGTH];
			str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);
			str_append(aBuf, pCompletion);

			const char *pAfter = m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength;
			const char *pSeparator = *pAfter == '\0' ? " " : (*pAfter != ' ' ? " " : "");
			if(*pSeparator)
				str_append(aBuf, pSeparator);

			str_append(aBuf, pAfter);

			m_PlaceholderLength = str_length(pCompletion) + str_length(pSeparator);
			m_Input.Set(aBuf);
			m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			return true;
		};

		if(GameClient()->m_BindChat.ChatDoAutocomplete(ShiftPressed))
		{
		}
		else if(m_aCompletionBuffer[0] == '/' && !m_vServerCommands.empty())
		{
			CCommand *pCompletionCommand = nullptr;

			const size_t NumCommands = m_vServerCommands.size();

			if(ShiftPressed && m_CompletionUsed)
				m_CompletionChosen--;
			else if(!ShiftPressed)
				m_CompletionChosen++;
			m_CompletionChosen = (m_CompletionChosen + 2 * NumCommands) % (2 * NumCommands);

			m_CompletionUsed = true;

			const char *pCommandStart = m_aCompletionBuffer + 1;
			for(size_t i = 0; i < 2 * NumCommands; ++i)
			{
				int SearchType;
				int Index;

				if(ShiftPressed)
				{
					SearchType = ((m_CompletionChosen - i + 2 * NumCommands) % (2 * NumCommands)) / NumCommands;
					Index = (m_CompletionChosen - i + NumCommands) % NumCommands;
				}
				else
				{
					SearchType = ((m_CompletionChosen + i) % (2 * NumCommands)) / NumCommands;
					Index = (m_CompletionChosen + i) % NumCommands;
				}

				auto &Command = m_vServerCommands[Index];

				if(str_startswith_nocase(Command.m_aName, pCommandStart))
				{
					pCompletionCommand = &Command;
					m_CompletionChosen = Index + SearchType * NumCommands;
					break;
				}
			}

			// insert the command
			if(pCompletionCommand)
			{
				char aBuf[MAX_LINE_LENGTH];
				// add part before the name
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// add the command
				str_append(aBuf, "/");
				str_append(aBuf, pCompletionCommand->m_aName);

				// add separator
				const char *pSeparator = pCompletionCommand->m_aParams[0] == '\0' ? "" : " ";
				str_append(aBuf, pSeparator);

				// add part after the name
				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				m_PlaceholderLength = str_length(pSeparator) + str_length(pCompletionCommand->m_aName) + 1;
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
		else if(DoVoiceAutocomplete())
		{
		}
		else
		{
			// find next possible name
			const char *pCompletionString = nullptr;
			if(!m_vUClientCompletionNames.empty())
			{
				if(ShiftPressed && m_CompletionUsed)
					--m_CompletionChosen;
				else if(!ShiftPressed)
					++m_CompletionChosen;
				const int Count = (int)m_vUClientCompletionNames.size();
				m_CompletionChosen = (m_CompletionChosen % Count + Count) % Count;
				m_CompletionUsed = true;
				pCompletionString = m_vUClientCompletionNames[m_CompletionChosen].c_str();
			}
			else if(m_PlayerCompletionListLength > 0)
			{
				// We do this in a loop, if a player left the game during the repeated pressing of Tab, they are skipped
				CGameClient::CClientData *pCompletionClientData;
				for(int i = 0; i < m_PlayerCompletionListLength; ++i)
				{
					if(ShiftPressed && m_CompletionUsed)
					{
						m_CompletionChosen--;
					}
					else if(!ShiftPressed)
					{
						m_CompletionChosen++;
					}
					if(m_CompletionChosen < 0)
					{
						m_CompletionChosen += m_PlayerCompletionListLength;
					}
					m_CompletionChosen %= m_PlayerCompletionListLength;
					m_CompletionUsed = true;

					pCompletionClientData = &GameClient()->m_aClients[m_aPlayerCompletionList[m_CompletionChosen].m_ClientId];
					if(!pCompletionClientData->m_Active)
					{
						continue;
					}

					pCompletionString = pCompletionClientData->m_aName;
					break;
				}
			}

			// insert the name
			if(pCompletionString)
			{
				char aBuf[MAX_LINE_LENGTH];
				// add part before the name
				str_truncate(aBuf, sizeof(aBuf), m_Input.GetString(), m_PlaceholderOffset);

				// quote the name
				char aQuoted[128];
				if((m_Input.GetString()[0] == '/' || m_Input.GetString()[0] == '!' || GameClient()->m_BindChat.CheckBindChat(m_Input.GetString())) && (str_find(pCompletionString, " ") || str_find(pCompletionString, "\"")))
				{
					// escape the name
					str_copy(aQuoted, "\"");
					char *pDst = aQuoted + str_length(aQuoted);
					str_escape(&pDst, pCompletionString, aQuoted + sizeof(aQuoted));
					str_append(aQuoted, "\"");

					pCompletionString = aQuoted;
				}

				// add the name
				str_append(aBuf, pCompletionString);

				// add separator
				const char *pSeparator = "";
				if(*(m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength) != ' ')
					pSeparator = m_PlaceholderOffset == 0 ? ": " : " ";
				else if(m_PlaceholderOffset == 0)
					pSeparator = ":";
				if(*pSeparator)
					str_append(aBuf, pSeparator);

				// add part after the name
				str_append(aBuf, m_Input.GetString() + m_PlaceholderOffset + m_PlaceholderLength);

				m_PlaceholderLength = str_length(pSeparator) + str_length(pCompletionString);
				m_Input.Set(aBuf);
				m_Input.SetCursorOffset(m_PlaceholderOffset + m_PlaceholderLength);
			}
		}
	}
	else
	{
		// reset name completion process
		if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key != KEY_TAB && Event.m_Key != KEY_LSHIFT && Event.m_Key != KEY_RSHIFT)
		{
			m_CompletionChosen = -1;
			m_CompletionUsed = false;
		}

		if(!Ui()->IsPopupOpen(&m_GiphyPopupId) && !Ui()->IsPopupOpen(&m_TranslateSettingsPopupId) &&
			!Ui()->IsPopupOpen(&m_MediaContextPopupId) && !Ui()->IsPopupOpen(&m_MediaSaveAssetPopupId) && !Ui()->IsPopupOpen(&m_MediaSaveSkinPopupId) &&
			!Ui()->IsPopupOpen(&m_ReactionPickerPopupId))
			m_Input.ProcessInput(Event);
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_UP)
	{
		if(m_EditingNewLine)
		{
			str_copy(m_aCurrentInputText, m_Input.GetString());
			m_EditingNewLine = false;
		}

		if(m_pHistoryEntry)
		{
			CHistoryEntry *pTest = m_History.Prev(m_pHistoryEntry);

			if(pTest)
				m_pHistoryEntry = pTest;
		}
		else
			m_pHistoryEntry = m_History.Last();

		if(m_pHistoryEntry)
			m_Input.Set(m_pHistoryEntry->m_aText);
	}
	else if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_DOWN)
	{
		if(m_pHistoryEntry)
			m_pHistoryEntry = m_History.Next(m_pHistoryEntry);

		if(m_pHistoryEntry)
		{
			m_Input.Set(m_pHistoryEntry->m_aText);
		}
		else if(!m_EditingNewLine)
		{
			m_Input.Set(m_aCurrentInputText);
			m_EditingNewLine = true;
		}
	}

	RefreshTypingAnimation();
	return true;
}

bool CChat::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	// BestClient: holding the expand-only bind (+show_chat) must not grab the cursor.
	// Only typing (m_Mode) or an open media viewer routes mouse movement into the UI;
	// while only m_Show is set the crosshair keeps moving and no cursor appears.
	if(m_Mode == MODE_NONE && !m_MediaViewerOpen)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	if(m_MediaViewerOpen && m_MediaViewerDragging && ValidateMediaViewerLine())
	{
		const float Height = 300.0f;
		const float Width = Height * Graphics()->ScreenAspect();
		const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
		m_MediaViewerPan += vec2(x * UiToChatScale.x, y * UiToChatScale.y);
		ClampMediaViewerPan(m_aLines[m_MediaViewerLineIndex], Width, Height);
	}
	Ui()->OnCursorMove(x, y);
	return true;
}

void CChat::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Ui()->Screen();
	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

bool CChat::IsReadingChat() const
{
	// WindowActive() lives on IEngineGraphics (the game-facing IGraphics doesn't expose it).
	IEngineGraphics *pEngineGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	if(!pEngineGraphics || !pEngineGraphics->WindowActive())
		return false;

	// The editor takes over the whole screen.
	if(g_Config.m_ClEditor)
		return false;

	// Of the ESC-menu tabs only "Game" leaves the chat visible; the rest (settings, demos,
	// clans, server info, ...) draw a full-height panel over it. The chat still renders
	// underneath, so nothing here can be assumed to have been seen.
	if(GameClient()->m_Menus.IsActive() && !GameClient()->m_Menus.IsIngameGamePage())
		return false;

	// Being flagged AFK by the server means nobody has touched the controls in a while, so a
	// focused window alone proves nothing: the player may have walked away from a lit screen.
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	const bool Afk = LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_Afk;
	if(!Afk)
		return true;

	// Pulling the chat up by hand still counts. +show_chat deliberately sends no input, so it
	// never clears the server-side AFK flag even though the player is clearly at the keyboard.
	return m_Show || m_Mode != MODE_NONE;
}

bool CChat::WasChatAutoHidden() const
{
	if(g_Config.m_ClShowChat == 0 || g_Config.m_ClShowChat == 2 || m_Mode != MODE_NONE)
		return false;

	const int64_t Now = time();
	bool HadAnyLines = false;
	for(int i = 0; i < MAX_LINES; i++)
	{
		const CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized)
			break;

		HadAnyLines = true;
		if(Now <= Line.m_Time + 16 * time_freq())
			return false;
	}

	return HadAnyLines;
}

void CChat::EnableMode(int Team)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(m_Mode == MODE_NONE)
	{
		const bool AnimateWholeChatOpen = WasChatAutoHidden();
		if(Team == TEAM_UCLIENT)
			m_Mode = MODE_UCLIENT;
		else if(Team)
			m_Mode = MODE_TEAM;
		else
			m_Mode = MODE_ALL;

		Input()->Clear();
		m_CompletionChosen = -1;
		m_CompletionUsed = false;
		m_BacklogCurLine = 0;
		m_ScrollbarDragging = false;
		m_ScrollbarDragOffset = 0.0f;
		m_MouseIsPress = false;
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
		m_ChatOpenAnimationStart = AnimateWholeChatOpen ? time_get() : 0;
		ResetTypingAnimation();
		const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
		m_LastMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
		SetUiMousePos(Ui()->Screen()->Center());
		m_Input.Activate(EInputPriority::CHAT);
		SyncTypingAnimationBaseline();
	}
}

void CChat::DisableMode()
{
	if(m_Mode != MODE_NONE)
	{
		CloseMediaViewer();
		Ui()->ClosePopupMenus();
		m_UcChatPaste.Reset(this);
		ClearPendingReply();
		m_Mode = MODE_NONE;
		m_BacklogCurLine = 0;
		m_ScrollbarDragging = false;
		m_MouseIsPress = false;
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
		m_ChatOpenAnimationStart = 0;
		ResetTypingAnimation();
		m_aPreviousDisplayedInputText[0] = '\0';
		ResetHiddenMediaReveals();
		if(m_LastMousePos.has_value())
			SetUiMousePos(m_LastMousePos.value());
		const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
		m_LastMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
		m_Input.Deactivate();
		m_GiphySearchInput.Deactivate();
		m_GiphyButtonPressed = false;
		m_RoomButtonPressed = false;

		}
	}

void CChat::OnMessage(int MsgType, void *pRawMsg)
{
	if(GameClient()->m_SuppressEvents)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		if(pMsg->m_ClientId == SERVER_MSG)
			GameClient()->m_Scoreboard.OnServerSwapMessage(pMsg->m_pMessage);

		// Silently consume the response to our automatic `/settings timeout` query.
		if(GameClient()->m_TimeoutReconnect.TryConsumeTimeoutSettingsMessage(pMsg->m_ClientId, pMsg->m_pMessage))
			return;

		if(g_Config.m_TcRegexChatIgnore[0] && g_Config.m_BcEnableCensorList)
		{
			auto &Re = GameClient()->m_TClient.m_RegexChatIgnore;
			if(Re.error().empty() && Re.test(pMsg->m_pMessage))
			{
				const char *pFilteredMSG = FilterText(pMsg->m_pMessage, pMsg->m_ClientId, true);
				AddLine(pMsg->m_ClientId, pMsg->m_Team, pFilteredMSG);
				return;
			}
		}
		else
		{
			auto &Re = GameClient()->m_TClient.m_RegexChatIgnore;
			if(Re.error().empty() && Re.test(pMsg->m_pMessage))
				return;
		}

		/*
		if(g_Config.m_ClCensorChat)
		{
			char aMessage[MAX_LINE_LENGTH];
			str_copy(aMessage, pMsg->m_pMessage);
			GameClient()->m_Censor.CensorMessage(aMessage);
			AddLine(pMsg->m_ClientId, pMsg->m_Team, aMessage);
		}
		else
			AddLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);
		*/

		AddLine(pMsg->m_ClientId, pMsg->m_Team, pMsg->m_pMessage);

		if(Client()->State() != IClient::STATE_DEMOPLAYBACK &&
			pMsg->m_ClientId == SERVER_MSG)
		{
			StoreSave(pMsg->m_pMessage);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_COMMANDINFO)
	{
		CNetMsg_Sv_CommandInfo *pMsg = (CNetMsg_Sv_CommandInfo *)pRawMsg;
		if(!m_ServerSupportsCommandInfo)
		{
			m_vServerCommands.clear();
			m_ServerSupportsCommandInfo = true;
		}
		RegisterCommand(pMsg->m_pName, pMsg->m_pArgsFormat, pMsg->m_pHelpText);
	}
	else if(MsgType == NETMSGTYPE_SV_COMMANDINFOREMOVE)
	{
		CNetMsg_Sv_CommandInfoRemove *pMsg = (CNetMsg_Sv_CommandInfoRemove *)pRawMsg;
		UnregisterCommand(pMsg->m_pName);
	}
}

bool CChat::LineShouldHighlight(const char *pLine, const char *pName)
{
	if(!pName || pName[0] == '\0')
		return false;

	char aReplyTag[MAX_NAME_LENGTH + 2];
	str_format(aReplyTag, sizeof(aReplyTag), "%s:", pName);
	if(str_startswith_nocase(pLine, aReplyTag))
		return true;

	const char *pHit = str_utf8_find_nocase(pLine, pName);

	while(pHit)
	{
		int Length = str_length(pName);

		if(Length > 0 && (pLine == pHit || pHit[-1] == ' ') && (pHit[Length] == 0 || pHit[Length] == ' ' || pHit[Length] == '.' || pHit[Length] == '!' || pHit[Length] == ',' || pHit[Length] == '?' || pHit[Length] == ':'))
			return true;

		pHit = str_utf8_find_nocase(pHit + 1, pName);
	}

	return false;
}

const char *CChat::GetLineDisplayText(const CLine &Line) const
{
	return Line.m_aDisplayText[0] != '\0' ? Line.m_aDisplayText : Line.m_aText;
}

static bool ChatLineNameMatches(const char *pLineName, const char *pName)
{
	if(!pName || pName[0] == '\0' || !pLineName || pLineName[0] == '\0')
		return false;

	char aLineName[64];
	str_copy(aLineName, pLineName, sizeof(aLineName));
	const int NameLen = str_length(aLineName);
	if(NameLen > 0 && aLineName[NameLen - 1] == ' ')
		aLineName[NameLen - 1] = '\0';
	return str_comp_nocase(aLineName, pName) == 0;
}

int CChat::ComputeSenderRecentIndex(int SourceLineIndex, const char *pName, int ClientId) const
{
	if(SourceLineIndex < 0 || !pName || pName[0] == '\0')
		return 0;

	// When we know the sender's client id, count messages by client id so the
	// index survives display-name spoofing (multiple players with the same name).
	// The receiver counts the same way, keeping the index consistent.
	const bool UseClientId = ClientId >= 0 && ClientId < MAX_CLIENTS;

	int Count = 0;
	for(int Step = 0; Step < MAX_LINES; ++Step)
	{
		const int Index = (m_CurrentLine - Step + MAX_LINES) % MAX_LINES;
		const CLine &Candidate = m_aLines[Index];
		if(!Candidate.m_Initialized)
			break;
		if(UseClientId)
		{
			if(Candidate.m_ClientId != ClientId)
				continue;
		}
		else if(!ChatLineNameMatches(Candidate.m_aName, pName))
			continue;
		++Count;
		if(Index == SourceLineIndex)
			return Count;
	}
	return 0;
}

bool CChat::TryResolveReplyQuoteByIndex(int ReplyLineIndex, const char *pReplyToName, int MessageIndex, char *pOut, int OutSize, int ReplyToClientId) const
{
	if(!pOut || OutSize <= 0 || ReplyLineIndex < 0 || MessageIndex <= 0 || !pReplyToName || pReplyToName[0] == '\0')
		return false;
	pOut[0] = '\0';

	// Prefer matching by the sender's client id when it was transmitted, so the
	// quote resolves to the right player even if another player uses the same
	// display name. The sender computed MessageIndex on the same basis.
	const bool UseClientId = ReplyToClientId >= 0 && ReplyToClientId < MAX_CLIENTS;

	int Count = 0;
	for(int Step = 1; Step < MAX_LINES; ++Step)
	{
		const int Index = (ReplyLineIndex - Step + MAX_LINES) % MAX_LINES;
		const CLine &Candidate = m_aLines[Index];
		if(!Candidate.m_Initialized)
			continue;
		if(UseClientId)
		{
			if(Candidate.m_ClientId != ReplyToClientId)
				continue;
		}
		else if(!ChatLineNameMatches(Candidate.m_aName, pReplyToName))
			continue;
		++Count;
		if(Count == MessageIndex)
		{
			str_copy(pOut, GetLineDisplayText(Candidate), OutSize);
			return true;
		}
	}
	return false;
}

bool CChat::TryResolveReplyQuoteText(int ReplyClientId, const char *pReplyToName, const char *pWirePreview, char *pOut, int OutSize, int SkipLineIndex) const
{
	if(!pOut || OutSize <= 0 || !pWirePreview || pWirePreview[0] == '\0')
		return false;
	pOut[0] = '\0';

	for(int i = 0; i < MAX_LINES; ++i)
	{
		const int Index = (m_CurrentLine - i + MAX_LINES) % MAX_LINES;
		if(Index == SkipLineIndex)
			continue;
		const CLine &Candidate = m_aLines[Index];
		if(!Candidate.m_Initialized)
			continue;
		if(ReplyClientId >= 0 && Candidate.m_ClientId != ReplyClientId)
			continue;
		if(pReplyToName && pReplyToName[0] != '\0' && !ChatLineNameMatches(Candidate.m_aName, pReplyToName))
			continue;

		const char *pDisplay = GetLineDisplayText(Candidate);
		if(!CUClientChatReply::TextMatchesWirePreview(pWirePreview, pDisplay))
			continue;

		str_copy(pOut, pDisplay, OutSize);
		return true;
	}
	return false;
}

const char *CChat::GetLineReplyQuoteText(const CLine &Line) const
{
	return Line.m_aReplyQuoteText;
}

void CChat::SetPendingReply(int ClientId, const char *pName, int SourceLineIndex, const char *pQuoteText)
{
	// ClientId may be < 0 for remote UClient chat lines (CLIENT_MSG).
	if(SourceLineIndex < 0 || !pName || pName[0] == '\0')
		return;
	m_PendingReplyActive = true;
	m_PendingReplyClientId = ClientId;
	m_PendingReplySourceLineIndex = SourceLineIndex;
	str_copy(m_aPendingReplyName, pName, sizeof(m_aPendingReplyName));
	str_copy(m_aPendingReplyPreview, pQuoteText ? pQuoteText : "", sizeof(m_aPendingReplyPreview));
}

void CChat::ClearPendingReply()
{
	m_PendingReplyActive = false;
	m_PendingReplyClientId = -1;
	m_PendingReplySourceLineIndex = -1;
	m_aPendingReplyName[0] = '\0';
	m_aPendingReplyPreview[0] = '\0';
	m_ReplyCancelButtonRectValid = false;
}

void CChat::StashUcReplySendScopePrompt(int ClientId, const char *pName, int SourceLineIndex, const char *pQuoteText)
{
	m_UcReplySendScopePromptPending = true;
	m_UcReplySendScopePromptClientId = ClientId;
	m_UcReplySendScopePromptSourceLineIndex = SourceLineIndex;
	str_copy(m_aUcReplySendScopePromptName, pName ? pName : "", sizeof(m_aUcReplySendScopePromptName));
	str_copy(m_aUcReplySendScopePromptPreview, pQuoteText ? pQuoteText : "", sizeof(m_aUcReplySendScopePromptPreview));
}

void CChat::ClearStashedUcReplySendScopePrompt()
{
	m_UcReplySendScopePromptPending = false;
	m_UcReplySendScopePromptClientId = -1;
	m_UcReplySendScopePromptSourceLineIndex = -1;
	m_aUcReplySendScopePromptName[0] = '\0';
	m_aUcReplySendScopePromptPreview[0] = '\0';
}

void CChat::ApplyStashedUcReplyAfterSendScopePrompt()
{
	if(!m_UcReplySendScopePromptPending)
		return;

	const int ClientId = m_UcReplySendScopePromptClientId;
	const int SourceLineIndex = m_UcReplySendScopePromptSourceLineIndex;
	char aName[sizeof(m_aUcReplySendScopePromptName)];
	char aPreview[sizeof(m_aUcReplySendScopePromptPreview)];
	str_copy(aName, m_aUcReplySendScopePromptName, sizeof(aName));
	str_copy(aPreview, m_aUcReplySendScopePromptPreview, sizeof(aPreview));
	ClearStashedUcReplySendScopePrompt();

	SetPendingReply(ClientId, aName, SourceLineIndex, aPreview);
	if(g_Config.m_UcChat)
		m_Mode = MODE_UCLIENT;
	m_Input.Activate(EInputPriority::CHAT);
}

bool CChat::CanShowReplyButton(const CLine &Line) const
{
	if(!CUClientChatReply::IsReplyFeatureEnabled() || m_Mode == MODE_NONE || Line.m_Whisper || Line.m_aName[0] == '\0')
		return false;
	if(Line.m_UClient)
		return g_Config.m_UcChat != 0;
	return Line.m_ClientId >= 0;
}

bool CChat::CanReactToLine(const CLine &Line) const
{
	if(!Line.m_Initialized || Line.m_aText[0] == '\0')
		return false;
	if(Line.m_UClient)
		return Line.m_UClientMessageId != UUID_ZEROED;
	return Line.m_ClientId >= 0 || Line.m_ClientId == SERVER_MSG;
}

void CChat::ApplySettingsLinkToLine(CLine &Line, const char *pSourceText)
{
	Line.m_HasSettingsLink = false;
	Line.m_SettingsLinkMissing = false;
	Line.m_SettingsLinkPageOnly = false;
	Line.m_aSettingsLinkUri[0] = '\0';
	Line.m_SettingsLinkParsed = {};
	Line.m_aSettingsLinkHeight[0] = 0.0f;
	Line.m_aSettingsLinkHeight[1] = 0.0f;
	Line.m_aSettingsLinkWidth[0] = 0.0f;
	Line.m_aSettingsLinkWidth[1] = 0.0f;
	Line.m_SettingsLinkRectValid = false;
	Line.m_SettingsShortcutRectValid = false;

	int UriStart = -1, UriLen = 0;
	char aUri[CUClientSettingsLink::MAX_URI_LENGTH];
	if(!CUClientSettingsLink::FindUriInText(pSourceText, UriStart, UriLen, aUri, sizeof(aUri)))
		return;
	CUClientSettingsLink::SParsed Parsed;
	if(!CUClientSettingsLink::TryParse(aUri, Parsed))
		return;

	str_copy(Line.m_aSettingsLinkUri, aUri, sizeof(Line.m_aSettingsLinkUri));
	Line.m_SettingsLinkParsed = Parsed;
	Line.m_HasSettingsLink = true;

	if(Parsed.m_Kind == CUClientSettingsLink::EKind::PAGE)
	{
		Line.m_SettingsLinkPageOnly = true;
		// A tab/page link to a page or tab this client doesn't have renders red like a
		// missing setting, and gets no shortcut button (CanShowSettingsShortcut checks this).
		Line.m_SettingsLinkMissing = !CUClientSettingsLink::IsPageLinkValid(Parsed);
	}
	else if(Parsed.m_Kind == CUClientSettingsLink::EKind::VAR)
	{
		const SConfigVariable *pVar = CUClientSettingsLink::FindVariable(ConfigManager(), Parsed.m_aScriptName);
		Line.m_SettingsLinkMissing = pVar == nullptr;

		// Prefer local parent chain (gated UI) over URI; fills parents when the sender
		// omitted them but this client has already visited the settings page.
		CUClientSettingsLink::SVarLocation Loc;
		if(CUClientSettingsLink::LookupVarLocation(Parsed.m_aScriptName, Loc) && Loc.m_NumParents > 0)
		{
			Line.m_SettingsLinkParsed.m_NumParents = Loc.m_NumParents;
			for(int i = 0; i < Loc.m_NumParents; ++i)
			{
				str_copy(Line.m_SettingsLinkParsed.m_aaParents[i], Loc.m_aaParents[i], sizeof(Line.m_SettingsLinkParsed.m_aaParents[i]));
				Line.m_SettingsLinkParsed.m_aaParentLabels[i][0] = '\0';
				CUClientSettingsLink::SVarLocation ParentLoc;
				if(CUClientSettingsLink::LookupVarLocation(CUClientSettingsLink::ParentScriptName(Loc.m_aaParents[i]), ParentLoc) && ParentLoc.m_aLabel[0] != '\0')
					str_copy(Line.m_SettingsLinkParsed.m_aaParentLabels[i], ParentLoc.m_aLabel, sizeof(Line.m_SettingsLinkParsed.m_aaParentLabels[i]));
			}
		}
	}

	char aStripped[CHAT_LINE_LENGTH];
	CUClientSettingsLink::StripUriFromDisplay(pSourceText, UriStart, UriLen, aStripped, sizeof(aStripped));
	if(Line.m_aDisplayText[0] != '\0')
		str_copy(Line.m_aDisplayText, aStripped, sizeof(Line.m_aDisplayText));
	else
		str_copy(Line.m_aDisplayText, aStripped, sizeof(Line.m_aDisplayText));
}

float CChat::MeasureSettingsLinkHeight(const CLine &Line, float FontSize, float CardWidth) const
{
	if(!Line.m_HasSettingsLink)
		return 0.0f;
	return GameClient()->m_Menus.MeasureSettingsLinkInlineHeight(Line.m_SettingsLinkParsed, Line.m_SettingsLinkMissing, Line.m_SettingsLinkPageOnly, FontSize, CardWidth);
}

float CChat::MeasureSettingsLinkWidth(const CLine &Line, float FontSize) const
{
	if(!Line.m_HasSettingsLink)
		return 0.0f;
	return GameClient()->m_Menus.MeasureSettingsLinkInlineWidth(Line.m_SettingsLinkParsed, Line.m_SettingsLinkMissing, Line.m_SettingsLinkPageOnly, FontSize);
}

float CChat::SettingsCardRenderWidth(const CLine &Line, float FontSize, bool ScoreboardOpen, float AvailWidth, float RealMsgPaddingX) const
{
	// Mirror the clamp used when rendering the bubble (see RenderSettingsLinkBubble /
	// the OnRender call site) so the cached height matches the on-screen width.
	const float MaxWidth = ScoreboardOpen ? AvailWidth : maximum(AvailWidth, ChatWidth() - RealMsgPaddingX);
	return minimum(MaxWidth, maximum(MeasureSettingsLinkWidth(Line, FontSize), FontSize * 8.0f));
}

bool CChat::CanShowSettingsShortcut(const CLine &Line) const
{
	// While chat is open; the shortcut button itself stays visible (not hover-gated).
	return m_Mode != MODE_NONE && Line.m_HasSettingsLink && !Line.m_SettingsLinkMissing;
}

void CChat::RenderSettingsLinkBubble(CLine &Line, float X, float Y, float MaxWidth, float FontSize, float Blend)
{
	if(!Line.m_HasSettingsLink)
		return;

	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->SetRenderFlags(0);
	const float CardW = minimum(MaxWidth, maximum(MeasureSettingsLinkWidth(Line, FontSize), FontSize * 8.0f));
	const float CardH = MeasureSettingsLinkHeight(Line, FontSize, CardW);
	Line.m_SettingsLinkRect.m_X = X;
	Line.m_SettingsLinkRect.m_Y = Y;
	Line.m_SettingsLinkRect.m_W = CardW;
	Line.m_SettingsLinkRect.m_H = CardH;
	Line.m_SettingsLinkRectValid = true;

	CUIRect Card(X, Y, CardW, CardH);
	GameClient()->m_Menus.RenderSettingsLinkInline(Line.m_SettingsLinkParsed, Line.m_SettingsLinkMissing, Line.m_SettingsLinkPageOnly, Card, FontSize, Blend, ChatMousePos());
}

bool CChat::TryHandleSettingsLinkClick(CLine &Line, vec2 MousePos, float FontSize)
{
	if(!Line.m_SettingsLinkRectValid || !Line.m_HasSettingsLink)
		return false;
	const CUIRect Card(Line.m_SettingsLinkRect.m_X, Line.m_SettingsLinkRect.m_Y, Line.m_SettingsLinkRect.m_W, Line.m_SettingsLinkRect.m_H);
	return GameClient()->m_Menus.TryClickSettingsLinkInline(Line.m_SettingsLinkParsed, Line.m_SettingsLinkMissing, Line.m_SettingsLinkPageOnly, Card, FontSize, MousePos);
}

float CChat::ReplyBannerHeight(float ScaledFontSize) const
{
	if(!m_PendingReplyActive)
		return 0.0f;
	return ScaledFontSize * 0.72f + 4.0f;
}

void CChat::RenderReplyBanner(float x, float InputY, float ScaledFontSize)
{
	if(!m_PendingReplyActive)
		return;

	const float LabelFontSize = ScaledFontSize * 0.72f;
	const float LabelY = InputY - LabelFontSize - 3.0f;

	char aBannerText[128];
	str_format(aBannerText, sizeof(aBannerText), "Replying to %s", m_aPendingReplyName);

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
	CTextCursor LabelCursor;
	LabelCursor.SetPosition(vec2(x, LabelY));
	LabelCursor.m_FontSize = LabelFontSize;
	TextRender()->TextEx(&LabelCursor, aBannerText);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	// Place the cancel icon just after the rendered text advance (not TextWidth),
	// so the gap stays consistent across fonts and CJK names.
	const float CancelSize = maximum(8.0f, LabelFontSize * 0.62f);
	const float CancelGap = LabelFontSize * 0.28f;
	const float CancelX = LabelCursor.m_X + CancelGap;
	const float CancelY = LabelY + maximum(0.0f, (LabelFontSize - CancelSize) * 0.5f);

	const vec2 MousePos = ChatMousePos();
	const bool HoveredCancel = MousePos.x >= CancelX && MousePos.x <= CancelX + CancelSize &&
		MousePos.y >= CancelY && MousePos.y <= CancelY + CancelSize;

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, HoveredCancel ? 1.0f : 0.85f);
	CUIRect CancelButton(CancelX, CancelY, CancelSize, CancelSize);
	Ui()->DoLabel(&CancelButton, FontIcon::XMARK, CancelSize * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	m_ReplyCancelButtonRect.m_X = CancelX;
	m_ReplyCancelButtonRect.m_Y = CancelY;
	m_ReplyCancelButtonRect.m_W = CancelSize;
	m_ReplyCancelButtonRect.m_H = CancelSize;
	m_ReplyCancelButtonRectValid = true;

	if(HoveredCancel)
		Ui()->SetHotItem(&m_ReplyCancelButton);
}

static constexpr const char *SAVES_HEADER[] = {
	"Time",
	"Player",
	"Map",
	"Code",
};

// TODO: remove this in a few releases (in 2027 or later)
//       it got deprecated by CGameClient::StoreSave
void CChat::StoreSave(const char *pText)
{
	const char *pStart = str_find(pText, "Team successfully saved by ");
	const char *pMid = str_find(pText, ". Use '/load ");
	const char *pOn = str_find(pText, "' on ");
	const char *pEnd = str_find(pText, pOn ? " to continue" : "' to continue");

	if(!pStart || !pMid || !pEnd || pMid < pStart || pEnd < pMid || (pOn && (pOn < pMid || pEnd < pOn)))
		return;

	char aName[16];
	str_truncate(aName, sizeof(aName), pStart + 27, pMid - pStart - 27);

	char aSaveCode[64];

	str_truncate(aSaveCode, sizeof(aSaveCode), pMid + 13, (pOn ? pOn : pEnd) - pMid - 13);

	char aTimestamp[20];
	str_timestamp_format(aTimestamp, sizeof(aTimestamp), TimestampFormat::SPACE);

	const bool SavesFileExists = Storage()->FileExists(SAVES_FILE, IStorage::TYPE_SAVE);
	IOHANDLE File = Storage()->OpenFile(SAVES_FILE, IOFLAG_APPEND, IStorage::TYPE_SAVE);
	if(!File)
		return;

	const char *apColumns[4] = {
		aTimestamp,
		aName,
		GameClient()->Map()->BaseName(),
		aSaveCode,
	};

	if(!SavesFileExists)
	{
		CsvWrite(File, 4, SAVES_HEADER);
	}
	CsvWrite(File, 4, apColumns);
	io_close(File);
}

void CChat::AddLine(int ClientId, int Team, const char *pLine)
{
	if(*pLine == 0 ||
		(ClientId == SERVER_MSG && !g_Config.m_ClShowChatSystem) ||
		(ClientId >= 0 && (GameClient()->m_aClients[ClientId].m_aName[0] == '\0' || // unknown client
					  GameClient()->m_aClients[ClientId].m_ChatIgnore ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_ClShowChatFriends && !GameClient()->m_aClients[ClientId].m_Friend) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && g_Config.m_ClShowChatTeamMembersOnly && GameClient()->IsOtherTeam(ClientId) && GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId) != TEAM_FLOCK) ||
					  (GameClient()->m_Snap.m_LocalClientId != ClientId && GameClient()->m_aClients[ClientId].m_Foe))))
		return;

	// BestClient
	if(ClientId == CLIENT_MSG && !g_Config.m_TcShowChatClient)
		return;

	// trim right and set maximum length to 256 utf8-characters
	int Length = 0;
	const char *pStr = pLine;
	const char *pEnd = nullptr;
	while(*pStr)
	{
		const char *pStrOld = pStr;
		int Code = str_utf8_decode(&pStr);

		// check if unicode is not empty
		if(!str_utf8_isspace(Code))
		{
			pEnd = nullptr;
		}
		else if(pEnd == nullptr)
			pEnd = pStrOld;

		if(++Length >= MAX_LINE_LENGTH)
		{
			*(const_cast<char *>(pStr)) = '\0';
			break;
		}
	}
	if(pEnd != nullptr)
		*(const_cast<char *>(pEnd)) = '\0';

	char aWireLine[CHAT_LINE_LENGTH];
	str_copy(aWireLine, pLine, sizeof(aWireLine));

	char aSanitizedText[1024];
	GameClient()->m_BestClient.SanitizeText(pLine, aSanitizedText, sizeof(aSanitizedText));
	pLine = aSanitizedText;

	if(*pLine == 0 && aWireLine[0] == '\0')
		return;

	const char *pParseSource = pLine;
	if(CUClientChatReply::IsReplyFeatureEnabled())
	{
		if(str_startswith(aWireLine, "[UCR:") || aWireLine[0] == '\x1E' || str_startswith(aWireLine, "UCR "))
			pParseSource = aWireLine;
	}

	bool Highlighted = false;

	auto &&FChatMsgCheckAndPrint = [this](const CLine &Line) {
		char aBuf[1024];
		str_format(aBuf, sizeof(aBuf), "%s%s%s", Line.m_aName, LineNeedsNameColon(Line) ? LineNameSeparator(Line) : "", Line.m_aText);

		ColorRGBA ChatLogColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		if(Line.m_Highlighted)
		{
			ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		}
		else
		{
			if(ShouldShowFriendMarker(Line))
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor));
			else if(Line.m_UClient)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UcMessageColor));
			else if(Line.m_Team)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
			else if(Line.m_ClientId == SERVER_MSG)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
			else if(Line.m_ClientId == CLIENT_MSG)
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
			else // regular message
				ChatLogColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));
		}

		const char *pFrom;
		if(Line.m_Whisper)
			pFrom = "chat/whisper";
		else if(Line.m_UClient)
			pFrom = "chat/uclient";
		else if(Line.m_Team)
			pFrom = "chat/team";
		else if(Line.m_ClientId == SERVER_MSG)
			pFrom = "chat/server";
		else if(Line.m_ClientId == CLIENT_MSG)
			pFrom = "chat/client";
		else
			pFrom = "chat/all";

		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, pFrom, aBuf, ChatLogColor);
	};

	// Custom color for new line
	std::optional<ColorRGBA> CustomColor = std::nullopt;
	if(ClientId == CLIENT_MSG && Team != TEAM_UCLIENT)
		CustomColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));

	CLine &PreviousLine = m_aLines[m_CurrentLine];

	// Team Number:
	// 0 = global; 1 = team; 2 = sending whisper; 3 = receiving whisper

	// If it's a client message, m_aText will have ": " prepended so we have to work around it.
	// Remote UClient lines all share CLIENT_MSG, so skip merge there (names differ).
	if(PreviousLine.m_Initialized &&
		!(Team == TEAM_UCLIENT && ClientId == CLIENT_MSG) &&
		PreviousLine.m_TeamNumber == Team &&
		PreviousLine.m_ClientId == ClientId &&
		str_comp(PreviousLine.m_aText, pLine) == 0 &&
		PreviousLine.m_CustomColor == CustomColor)
	{
		PreviousLine.m_TimesRepeated++;
		TextRender()->DeleteTextContainer(PreviousLine.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(PreviousLine.m_QuadContainerIndex);
		PreviousLine.m_Time = time();
		PreviousLine.m_aYOffset[0] = -1.0f;
		PreviousLine.m_aYOffset[1] = -1.0f;

		FChatMsgCheckAndPrint(PreviousLine);
		return;
	}

	m_CurrentLine = (m_CurrentLine + 1) % MAX_LINES;
	// The previous line switches from "latest" to backlog now. In compact chat
	// media-related layout (including expanded compact area) may change
	// immediately, so invalidate cached heights and force relayout next frame.
	PreviousLine.m_aYOffset[0] = -1.0f;
	PreviousLine.m_aYOffset[1] = -1.0f;
	if(m_BacklogCurLine > 0)
		m_BacklogCurLine = minimum(m_BacklogCurLine + 1, MAX_LINES - 1);

	CLine &CurrentLine = m_aLines[m_CurrentLine];
	CurrentLine.Reset(*this);
	CurrentLine.m_Initialized = true;
	CurrentLine.m_Time = time();
	CurrentLine.m_aYOffset[0] = -1.0f;
	CurrentLine.m_aYOffset[1] = -1.0f;
	CurrentLine.m_ClientId = ClientId;
	CurrentLine.m_TeamNumber = Team;
	CurrentLine.m_Team = Team == 1;
	CurrentLine.m_UClient = Team == TEAM_UCLIENT;
	CurrentLine.m_Whisper = Team >= 2 && Team != TEAM_UCLIENT;
	CurrentLine.m_NameColor = -2;
	CurrentLine.m_CustomColor = CustomColor;

	const char *pHighlightText = pLine;
	CUClientChatReply::SReplyMeta PreReplyMeta;
	char aPreDisplayBody[CHAT_LINE_LENGTH];
	aPreDisplayBody[0] = '\0';
	bool UsedOutgoingReplyCache = false;
	if(CUClientChatReply::IsReplyFeatureEnabled() &&
		CUClientChatReply::TryParseReply(pParseSource, PreReplyMeta, aPreDisplayBody, sizeof(aPreDisplayBody)))
	{
		pHighlightText = aPreDisplayBody;
	}

	if(!PreReplyMeta.m_Valid && CUClientChatReply::IsReplyFeatureEnabled() && m_LastOutgoingReplyTime > 0 &&
		time() <= m_LastOutgoingReplyTime + time_freq())
	{
		bool IsLocalSender = false;
		for(int LocalId : GameClient()->m_aLocalIds)
		{
			if(ClientId == LocalId)
			{
				IsLocalSender = true;
				break;
			}
		}
		if(IsLocalSender && str_comp(aWireLine, m_aLastOutgoingReplyWire) == 0)
		{
			PreReplyMeta.m_Valid = true;
			PreReplyMeta.m_ReplyToClientId = -1;
			PreReplyMeta.m_ReplyMessageIndex = m_LastOutgoingReplyMessageIndex;
			str_copy(PreReplyMeta.m_aReplyToName, m_aLastOutgoingReplyToName, sizeof(PreReplyMeta.m_aReplyToName));
			PreReplyMeta.m_aReplyPreview[0] = '\0';
			str_copy(aPreDisplayBody, m_aLastOutgoingReplyBody, sizeof(aPreDisplayBody));
			pHighlightText = aPreDisplayBody;
			UsedOutgoingReplyCache = true;
		}
	}

	// check for highlighted name
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && ClientId != GameClient()->m_aLocalIds[1])
		{
			for(int LocalId : GameClient()->m_aLocalIds)
			{
				Highlighted |= LocalId >= 0 && LineShouldHighlight(pHighlightText, GameClient()->m_aClients[LocalId].m_aName);
			}
		}
		else if(Team == TEAM_UCLIENT && ClientId == CLIENT_MSG)
		{
			// Remote UClient senders have no snap client id, so the check above skips
			// them. Highlight by local name(s) when mentioned, but ignore our own
			// echoed line (sender name equals ours).
			for(int LocalId : GameClient()->m_aLocalIds)
			{
				if(LocalId < 0)
					continue;
				const char *pLocalName = GameClient()->m_aClients[LocalId].m_aName;
				if(m_aPendingUClientName[0] != '\0' && str_comp(m_aPendingUClientName, pLocalName) == 0)
					continue;
				Highlighted |= LineShouldHighlight(pHighlightText, pLocalName);
			}
		}
	}
	else
	{
		// on demo playback use local id from snap directly,
		// since m_aLocalIds isn't valid there
		Highlighted |= GameClient()->m_Snap.m_LocalClientId >= 0 && LineShouldHighlight(pHighlightText, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName);
	}

	if(PreReplyMeta.m_Valid)
	{
		for(int LocalId : GameClient()->m_aLocalIds)
		{
			if(LocalId >= 0 && str_comp(PreReplyMeta.m_aReplyToName, GameClient()->m_aClients[LocalId].m_aName) == 0)
				Highlighted = true;
		}
		if(Client()->State() == IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_LocalClientId >= 0 &&
			str_comp(PreReplyMeta.m_aReplyToName, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName) == 0)
			Highlighted = true;
	}
	CurrentLine.m_Highlighted = Highlighted;

	str_copy(CurrentLine.m_aText, aWireLine[0] != '\0' ? aWireLine : pLine);
	CurrentLine.m_HasReply = false;
	CurrentLine.m_aDisplayText[0] = '\0';
	CurrentLine.m_aReplyToName[0] = '\0';
	CurrentLine.m_aReplyPreview[0] = '\0';
	CurrentLine.m_aReplyQuoteText[0] = '\0';
	CurrentLine.m_ReplyToClientId = -1;
	CurrentLine.m_ReplyMessageIndex = 0;
	if(PreReplyMeta.m_Valid)
	{
		CurrentLine.m_HasReply = true;
		CurrentLine.m_ReplyToClientId = PreReplyMeta.m_ReplyToClientId;
		CurrentLine.m_ReplyMessageIndex = PreReplyMeta.m_ReplyMessageIndex;
		str_copy(CurrentLine.m_aReplyToName, PreReplyMeta.m_aReplyToName, sizeof(CurrentLine.m_aReplyToName));
		str_copy(CurrentLine.m_aReplyPreview, PreReplyMeta.m_aReplyPreview, sizeof(CurrentLine.m_aReplyPreview));
		str_copy(CurrentLine.m_aDisplayText, aPreDisplayBody, sizeof(CurrentLine.m_aDisplayText));
		if(UsedOutgoingReplyCache)
			str_copy(CurrentLine.m_aReplyQuoteText, m_aLastOutgoingReplyPreview, sizeof(CurrentLine.m_aReplyQuoteText));
		else if(CurrentLine.m_ReplyMessageIndex > 0)
			TryResolveReplyQuoteByIndex(m_CurrentLine, CurrentLine.m_aReplyToName, CurrentLine.m_ReplyMessageIndex, CurrentLine.m_aReplyQuoteText, sizeof(CurrentLine.m_aReplyQuoteText), CurrentLine.m_ReplyToClientId);
		else
			TryResolveReplyQuoteText(CurrentLine.m_ReplyToClientId, CurrentLine.m_aReplyToName, CurrentLine.m_aReplyPreview, CurrentLine.m_aReplyQuoteText, sizeof(CurrentLine.m_aReplyQuoteText), m_CurrentLine);
	}
	if(g_Config.m_BcChatMediaPreview && AnyMediaAllowed())
	{
		std::vector<std::string> vMediaUrls;
		ExtractMediaUrlsFromText(CurrentLine.m_aText, vMediaUrls);
		SetMediaCandidates(CurrentLine, vMediaUrls);
		if(!CurrentLine.m_vMediaCandidates.empty())
		{
			QueueMediaDownload(CurrentLine);
		}
		else if(g_Config.m_Debug && str_find(CurrentLine.m_aText, "http"))
		{
			log_debug("chat/media", "No usable media candidates in message: %s", CurrentLine.m_aText);
		}
	}
	{
		char aMapUrl[512];
		char aMapName[128];
		if(ExtractMapUrlFromText(CurrentLine.m_aText, aMapUrl, sizeof(aMapUrl), aMapName, sizeof(aMapName)))
			SetMapAttachment(CurrentLine, aMapUrl, aMapName);
	}

	ApplySettingsLinkToLine(CurrentLine, CurrentLine.m_aDisplayText[0] != '\0' ? CurrentLine.m_aDisplayText : CurrentLine.m_aText);

	// The whole message is a single recognized link from a gif-bubble domain (e.g. sent via
	// the gif wheel, or pasted by hand) -> pop a bubble above the sender's head too.
	CurrentLine.m_ShowAboveHead = ClientId >= 0 &&
				       CurrentLine.m_vMediaCandidates.size() == 1 &&
				       str_comp(CurrentLine.m_aText, CurrentLine.m_vMediaCandidates.front().c_str()) == 0 &&
				       IsGifBubbleUrl(CurrentLine.m_aText);

	if(CurrentLine.m_ClientId == SERVER_MSG)
	{
		str_copy(CurrentLine.m_aName, "*** ");
	}
	else if(CurrentLine.m_ClientId == CLIENT_MSG)
	{
		// Remote UClient lines carry the sender's presence name via m_aPendingUClientName so
		// the console log (printed just below) shows "Name: text" instead of the "— " dash.
		if(Team == TEAM_UCLIENT && m_aPendingUClientName[0] != '\0')
			str_copy(CurrentLine.m_aName, m_aPendingUClientName);
		else
			str_copy(CurrentLine.m_aName, "— ");
	}
	else
	{
		const auto &LineAuthor = GameClient()->m_aClients[CurrentLine.m_ClientId];
		// v2.0: censor-list/word filter is applied to the author's name before the UClient
		// streamer-mode sanitizer, so both features union cleanly.
		const char *pFilteredLineAuthor = FilterText(LineAuthor.m_aName);

		if(LineAuthor.m_Active)
		{
			if(LineAuthor.m_Team == TEAM_SPECTATORS)
				CurrentLine.m_NameColor = TEAM_SPECTATORS;

			if(GameClient()->IsTeamPlay())
			{
				if(LineAuthor.m_Team == TEAM_RED)
					CurrentLine.m_NameColor = TEAM_RED;
				else if(LineAuthor.m_Team == TEAM_BLUE)
					CurrentLine.m_NameColor = TEAM_BLUE;
			}
		}

		if(Team == TEAM_WHISPER_SEND)
		{
			str_copy(CurrentLine.m_aName, "→");
			if(LineAuthor.m_Active)
			{
				char aSanitizedName[64];
				GameClient()->m_BestClient.SanitizePlayerName(pFilteredLineAuthor, aSanitizedName, sizeof(aSanitizedName), CurrentLine.m_ClientId);
				str_append(CurrentLine.m_aName, " ");
				str_append(CurrentLine.m_aName, aSanitizedName);
			}
			CurrentLine.m_NameColor = TEAM_BLUE;
			CurrentLine.m_Highlighted = false;
			Highlighted = false;
		}
		else if(Team == TEAM_WHISPER_RECV)
		{
			str_copy(CurrentLine.m_aName, "←");
			if(LineAuthor.m_Active)
			{
				char aSanitizedName[64];
				GameClient()->m_BestClient.SanitizePlayerName(pFilteredLineAuthor, aSanitizedName, sizeof(aSanitizedName), CurrentLine.m_ClientId);
				str_append(CurrentLine.m_aName, " ");
				str_append(CurrentLine.m_aName, aSanitizedName);
			}
			CurrentLine.m_NameColor = TEAM_RED;
			CurrentLine.m_Highlighted = true;
			Highlighted = true;
		}
		else
		{
			GameClient()->m_BestClient.SanitizePlayerName(pFilteredLineAuthor, CurrentLine.m_aName, sizeof(CurrentLine.m_aName), CurrentLine.m_ClientId);
		}

		if(LineAuthor.m_Active)
		{
			CurrentLine.m_Friend = LineAuthor.m_Friend;
			CurrentLine.m_pManagedTeeRenderInfo = GameClient()->CreateManagedTeeRenderInfo(LineAuthor);
		}
	}

	FChatMsgCheckAndPrint(CurrentLine);

	// play sound
	int64_t Now = time();
	if(ClientId == SERVER_MSG)
	{
		if(Now - m_aLastSoundPlayed[CHAT_SERVER] >= time_freq() * 3 / 10)
		{
			if(g_Config.m_SndServerMessage)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_SERVER, 1.0f);
				m_aLastSoundPlayed[CHAT_SERVER] = Now;
			}
		}
	}
	else if(ClientId == CLIENT_MSG && !(CurrentLine.m_UClient && Highlighted))
	{
		// No sound yet
	}
	else if(Highlighted && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(Now - m_aLastSoundPlayed[CHAT_HIGHLIGHT] >= time_freq() * 3 / 10)
		{
			char aBuf[1024];
			const std::string VisibleText = BuildVisibleMessageText(CurrentLine, true);
			str_format(aBuf, sizeof(aBuf), "%s: %s", CurrentLine.m_aName, VisibleText.c_str());
			Client()->Notify("DDNet Chat", aBuf);
			if(g_Config.m_SndHighlight)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_HIGHLIGHT, 1.0f);
				m_aLastSoundPlayed[CHAT_HIGHLIGHT] = Now;
			}

			if(g_Config.m_ClEditor)
			{
				GameClient()->Editor()->UpdateMentions();
			}
		}
	}
	else if(Team != TEAM_WHISPER_SEND)
	{
		if(Now - m_aLastSoundPlayed[CHAT_CLIENT] >= time_freq() * 3 / 10)
		{
			bool PlaySound = CurrentLine.m_Team ? g_Config.m_SndTeamChat : g_Config.m_SndChat;
#if defined(CONF_VIDEORECORDER)
			if(IVideo::Current())
			{
				PlaySound &= (bool)g_Config.m_ClVideoShowChat;
			}
#endif
			if(PlaySound)
			{
				GameClient()->m_Sounds.Play(CSounds::CHN_GUI, SOUND_CHAT_CLIENT, 1.0f);
				m_aLastSoundPlayed[CHAT_CLIENT] = Now;
			}
		}
	}

	// BestClient
	GameClient()->m_Translate.AutoTranslate(CurrentLine);
}

void CChat::AddUClientChatLine(const char *pName, int SuggestedClientId, const char *pLine, const char *pServerAddress,
	const CUuid &MessageId, bool Mine,
	const char *pSkinName, int UseCustomColor, int ColorBody, int ColorFeet, int Scope, const char *pRoomName, const char *pRoomId)
{
	if(!g_Config.m_UcChat || !pName || !pLine || pName[0] == '\0' || pLine[0] == '\0')
		return;

	int ClientId = CLIENT_MSG;
	const bool SameServer = pServerAddress && pServerAddress[0] != '\0' &&
		GameClient()->m_ClientIndicator.UcPeerAppliesToCurrentServer(pServerAddress);
	if(SameServer)
	{
		if(SuggestedClientId >= 0 && SuggestedClientId < MAX_CLIENTS &&
			GameClient()->m_aClients[SuggestedClientId].m_Active &&
			str_comp(GameClient()->m_aClients[SuggestedClientId].m_aName, pName) == 0)
		{
			ClientId = SuggestedClientId;
		}
		else
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!GameClient()->m_aClients[i].m_Active)
					continue;
				if(str_comp(GameClient()->m_aClients[i].m_aName, pName) == 0)
				{
					ClientId = i;
					break;
				}
			}
		}
	}

	// Provide the sender's display name to AddLine so its console log prints "Name: text"
	// (remote lines use CLIENT_MSG, which would otherwise log the "— " dash prefix).
	if(ClientId == CLIENT_MSG)
	{
		const char *pFilteredName = FilterText(pName);
		GameClient()->m_BestClient.SanitizePlayerName(pFilteredName, m_aPendingUClientName, sizeof(m_aPendingUClientName), -1);
	}

	const int PrevShowClient = g_Config.m_TcShowChatClient;
	if(ClientId == CLIENT_MSG)
		g_Config.m_TcShowChatClient = 1;
	AddLine(ClientId, TEAM_UCLIENT, pLine);
	if(ClientId == CLIENT_MSG)
		g_Config.m_TcShowChatClient = PrevShowClient;
	m_aPendingUClientName[0] = '\0';

	CLine &Line = m_aLines[m_CurrentLine];
	if(!Line.m_Initialized || !Line.m_UClient)
		return;

	Line.m_UClientFromCurrentServer = SameServer;
	Line.m_UClientMessageId = MessageId;
	Line.m_UClientMine = Mine;
	Line.m_UClientScope = Scope;
	str_copy(Line.m_aUClientServerAddress, pServerAddress ? pServerAddress : "", sizeof(Line.m_aUClientServerAddress));
	str_copy(Line.m_aUClientRoomName, pRoomName ? pRoomName : "", sizeof(Line.m_aUClientRoomName));
	str_copy(Line.m_aUClientRoomId, pRoomId ? pRoomId : "", sizeof(Line.m_aUClientRoomId));
	// Monotonic order used as the read high-water mark (newer lines sort later).
	Line.m_UClientSeq = ++m_UcSeqCounter;

	// Remote / unmatched senders: show the presence name instead of the client-msg dash prefix.
	if(ClientId == CLIENT_MSG)
	{
		const char *pFiltered = FilterText(pName);
		GameClient()->m_BestClient.SanitizePlayerName(pFiltered, Line.m_aName, sizeof(Line.m_aName), -1);
		Line.m_CustomColor = std::nullopt;
		Line.m_pManagedTeeRenderInfo.reset();
		if(pSkinName && pSkinName[0] != '\0')
		{
			CSkinDescriptor SkinDescriptor;
			SkinDescriptor.m_Flags = CSkinDescriptor::FLAG_SIX;
			str_copy(SkinDescriptor.m_aSkinName, pSkinName, sizeof(SkinDescriptor.m_aSkinName));
			if(!CSkin::IsValidName(SkinDescriptor.m_aSkinName))
				str_copy(SkinDescriptor.m_aSkinName, "default", sizeof(SkinDescriptor.m_aSkinName));

			CTeeRenderInfo TeeRenderInfo;
			TeeRenderInfo.ApplyColors(UseCustomColor, ColorBody, ColorFeet);
			TeeRenderInfo.m_Size = 64.0f;
			Line.m_pManagedTeeRenderInfo = GameClient()->CreateManagedTeeRenderInfo(TeeRenderInfo, SkinDescriptor);
		}
		TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
	}

	// Same-server lines already inherit m_Friend from the snap client. Remote UClient
	// senders use CLIENT_MSG and have no snap entry — resolve friends by player name
	// so the ♥ marker matches normal chat (clan-only entries are ignored).
	if(ClientId == CLIENT_MSG && Line.m_aName[0] != '\0')
		Line.m_Friend = IsUClientFriendName(Line.m_aName) || IsUClientFriendName(pName);
}

bool CChat::IsUClientFriendName(const char *pName) const
{
	if(!pName || pName[0] == '\0')
		return false;
	IFriends *pFriends = GameClient()->Friends();
	if(!pFriends)
		return false;
	// UClient only knows the player name, so match by name and ignore clan-only entries.
	for(int i = 0; i < pFriends->NumFriends(); ++i)
	{
		const CFriendInfo *pFriend = pFriends->GetFriend(i);
		if(pFriend->m_aName[0] == '\0')
			continue;
		if(!str_comp(pFriend->m_aName, pName))
			return true;
	}
	return false;
}

void CChat::AddServerLeaveLine(const char *pLeaverName, const char *pServerAddress,
	const char *pSkinName, int UseCustomColor, int ColorBody, int ColorFeet, const char *pRoomName, const char *pRoomId)
{
	if(!g_Config.m_UcChat || !pLeaverName || pLeaverName[0] == '\0')
		return;

	// Plain announcement: no server name and nothing clickable.
	AddUClientChatLine(pLeaverName, -1, Localize("left the server."), pServerAddress, UUID_ZEROED, false,
		pSkinName, UseCustomColor, ColorBody, ColorFeet, -1, pRoomName, pRoomId);

	CLine &Line = m_aLines[m_CurrentLine];
	if(!Line.m_Initialized || !Line.m_UClient)
		return;
	Line.m_ServerAnnouncement = true;
	// Force a fresh layout so the name separator change is picked up.
	TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
	Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
	Line.m_aYOffset[0] = -1.0f;
	Line.m_aYOffset[1] = -1.0f;
}

void CChat::AddServerJoinLine(const char *pJoinerName, const char *pServerAddress, const char *pServerName,
	const char *pSkinName, int UseCustomColor, int ColorBody, int ColorFeet, bool Moved, const char *pRoomName, const char *pRoomId)
{
	if(!g_Config.m_UcChat || !pJoinerName || !pServerAddress || pJoinerName[0] == '\0' || pServerAddress[0] == '\0')
		return;

	const char *pName = (pServerName && pServerName[0] != '\0') ? pServerName : pServerAddress;

	// Truncate very long server names with a trailing ellipsis so the announcement stays short.
	char aServerName[128];
	const int MaxNameBytes = 40;
	if(str_length(pName) > MaxNameBytes)
	{
		char aTruncated[128];
		str_utf8_truncate(aTruncated, sizeof(aTruncated), pName, MaxNameBytes - 3);
		str_format(aServerName, sizeof(aServerName), "%s...", aTruncated);
	}
	else
	{
		str_copy(aServerName, pName, sizeof(aServerName));
	}

	// Body is "joined <ServerName>" / "moved to <ServerName>"; only the server-name suffix
	// becomes the clickable link.
	char aBody[CHAT_LINE_LENGTH];
	str_format(aBody, sizeof(aBody), "%s%s", Moved ? Localize("moved to ") : Localize("joined "), aServerName);

	// Reuse the remote-UClient line path (tee/name/friend-heart/UClient color). No message id so
	// it never participates in read receipts. Not mine.
	AddUClientChatLine(pJoinerName, -1, aBody, pServerAddress, UUID_ZEROED, false,
		pSkinName, UseCustomColor, ColorBody, ColorFeet, -1, pRoomName, pRoomId);

	CLine &Line = m_aLines[m_CurrentLine];
	if(!Line.m_Initialized || !Line.m_UClient)
		return;
	Line.m_ServerAnnouncement = true;
	Line.m_HasServerJoinLink = true;
	str_copy(Line.m_aServerJoinAddress, pServerAddress, sizeof(Line.m_aServerJoinAddress));
	str_copy(Line.m_aServerJoinServerName, aServerName, sizeof(Line.m_aServerJoinServerName));
	Line.m_ServerJoinBoundsValid = false;
	// Force a fresh layout so the dedicated server-join append runs.
	TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
	Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
	Line.m_aYOffset[0] = -1.0f;
	Line.m_aYOffset[1] = -1.0f;
}

void CChat::OnPrepareLines(float y, int StartLine, int HoveredTranslateLineIndex, int HoveredScopeLineIndex)
{
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	const float Height = HudLayout::CANVAS_HEIGHT;
	const float Width = Height * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_CHAT, Width, Height);
	const bool LayoutEnabled = HudLayout::IsEnabled(HudLayout::MODULE_CHAT);
	const float LayoutScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	float x = Layout.m_X;
	float FontSize = this->FontSize();

	// Always compact chat while the scoreboard is open so wide settings cards / media
	// previews do not run underneath the scoreboard (previously gated on aspect > 1.7).
	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsActive();
	const bool ShowLargeArea = m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	// BestClient: mouse interaction (cursor, selection, scrollbar, media clicks) belongs to
	// typing only. The expand-only bind (m_Show) still shows the large area and wheel-scrolls,
	// but must not engage the mouse, so it is intentionally excluded here.
	const bool ChatInteractionActive = m_Mode != MODE_NONE;
	const bool ModeActive = m_Mode != MODE_NONE;
	const bool ChatSelectionActive = m_HasSelection && !m_MouseIsPress && !m_WantsSelectionCopy && !m_Input.HasSelection();
	const bool ForceSelectionRefresh = m_MouseIsPress || m_WantsSelectionCopy || ChatSelectionActive != m_PrevChatSelectionActive;
	const bool LayoutChanged = Layout.m_X != m_PrevHudLayoutX || Layout.m_Y != m_PrevHudLayoutY || Layout.m_Scale != m_PrevHudLayoutScale || LayoutEnabled != m_PrevHudLayoutEnabled;
	const bool ForceRecreate = IsScoreBoardOpen != m_PrevScoreBoardShowed || ShowLargeArea != m_PrevShowChat || ModeActive != m_PrevModeActive || ForceSelectionRefresh || HoveredTranslateLineIndex != m_HoveredTranslateLineIndex || LayoutChanged;
	const bool KeepLinesAlive = m_MediaViewerOpen && ValidateMediaViewerLine();
	m_PrevScoreBoardShowed = IsScoreBoardOpen;
	m_PrevShowChat = ShowLargeArea;
	m_PrevModeActive = ModeActive;
	m_PrevChatSelectionActive = ChatSelectionActive;
	m_PrevHudLayoutX = Layout.m_X;
	m_PrevHudLayoutY = Layout.m_Y;
	m_PrevHudLayoutScale = Layout.m_Scale;
	m_PrevHudLayoutEnabled = LayoutEnabled;
	m_HoveredTranslateLineIndex = HoveredTranslateLineIndex;

	const int TeeSize = MessageTeeSize();
	float RealMsgPaddingX = MessagePaddingX();
	float RealMsgPaddingY = MessagePaddingY();
	float RealMsgPaddingTee = TeeSize + MESSAGE_TEE_PADDING_RIGHT;

	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
		RealMsgPaddingTee = 0;
	}

	int64_t Now = time();
	float LineWidth = (IsScoreBoardOpen ? maximum(85.0f * LayoutScale, (FontSize * 85.0f / 6.0f)) : ChatWidth()) - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;

	const auto ShouldExpandCompactAreaForMedia = [&]() {
		if(IsScoreBoardOpen || ShowLargeArea || !g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
			return false;
		for(int i = 0; i < 3; ++i)
		{
			const CLine &RecentLine = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
			if(!RecentLine.m_Initialized)
				break;
			if(ShouldHideLineFromStreamer(RecentLine))
				continue;
			if(ShouldDisplayMediaSlot(RecentLine))
				return true;
		}
		return false;
	};
	const bool ExpandCompactAreaForMedia = ShouldExpandCompactAreaForMedia();
	const float VisibleHeight = IsScoreBoardOpen ? 93.0f * LayoutScale : (ShowLargeArea ? 223.0f * LayoutScale : (ExpandCompactAreaForMedia ? CHAT_MEDIA_COMPACT_EXPANDED_HEIGHT * LayoutScale : 73.0f * LayoutScale));
	float HeightLimit = y - VisibleHeight;
	float Begin = x;
	float TextBegin = Begin + RealMsgPaddingX / 2.0f;
	int OffsetType = IsScoreBoardOpen ? 1 : 0;
	const float MaxPreviewHeight = (IsScoreBoardOpen ? CHAT_MEDIA_MAX_PREVIEW_HEIGHT_SCOREBOARD : CHAT_MEDIA_MAX_PREVIEW_HEIGHT) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;

	for(int i = StartLine; i < MAX_LINES; i++)
	{
		const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		CLine &Line = m_aLines[LineIndex];
		if(!Line.m_Initialized)
			break;
		if(ShouldHideLineFromStreamer(Line))
			continue;
		if(Now > Line.m_Time + 16 * time_freq() && !m_PrevShowChat && !KeepLinesAlive)
			break;

		// Rebuild when settings-card layout metrics change (size / checkbox proportions).
		const bool SettingsLinkLayoutStale = Line.m_HasSettingsLink &&
			(Line.m_aSettingsLinkWidth[OffsetType] <= 0.0f ||
				absolute(Line.m_aSettingsLinkHeight[OffsetType] - MeasureSettingsLinkHeight(Line, FontSize, SettingsCardRenderWidth(Line, FontSize, IsScoreBoardOpen, LineWidth, RealMsgPaddingX))) > 0.5f ||
				absolute(Line.m_aSettingsLinkWidth[OffsetType] - MeasureSettingsLinkWidth(Line, FontSize)) > 0.5f);

		// UClient: the hovered message gets an extra row for its audience note. Deriving the
		// wanted height here (rather than folding the hover into ForceRecreate) keeps the rebuild
		// to the two lines whose height actually changes, and repairs any row left reserved on a
		// line that scrolled out of view while it was hovered.
		float WantScopeNoteHeight = 0.0f;
		if(LineIndex == HoveredScopeLineIndex)
		{
			char aScopeNote[192];
			if(UClientScopeNoteText(Line, aScopeNote, sizeof(aScopeNote)))
				WantScopeNoteHeight = ScopeNoteFontSize() * 1.3f;
		}
		const bool ScopeNoteStale = absolute(Line.m_aScopeNoteHeight[OffsetType] - WantScopeNoteHeight) > 0.01f;

		if(Line.m_TextContainerIndex.Valid() && Line.m_aYOffset[OffsetType] >= 0.0f && !ForceRecreate && !SettingsLinkLayoutStale && !ScopeNoteStale)
			continue;

		TextRender()->DeleteTextContainer(Line.m_TextContainerIndex);
		Graphics()->DeleteQuadContainer(Line.m_QuadContainerIndex);
		Line.m_vLinkBounds.clear();
		Line.m_vLinks.clear();
		Line.m_vLinkFontSizes.clear();
		Line.m_vLinkAlwaysConfirm.clear();
		Line.m_vLinkGroups.clear();
		Line.m_aYOffset[OffsetType] = -1.0f;
		Line.m_aTextHeight[OffsetType] = -1.0f;
		Line.m_ReplyQuoteRectValid = false;
		Line.m_ReplyQuoteHeight = 0.0f;
		Line.m_ReplyButtonAnchorValid = false;

		char aClientId[16] = "";
		if(g_Config.m_ClShowIds && Line.m_ClientId >= 0 && Line.m_aName[0] != '\0')
		{
			GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
		}
		char aRoomLabel[72] = "";
		if(Line.m_aUClientRoomName[0])
			str_format(aRoomLabel, sizeof(aRoomLabel), "[%s] ", Line.m_aUClientRoomName);

		char aCount[12];
		if(Line.m_ClientId < 0)
			str_format(aCount, sizeof(aCount), "[%d] ", Line.m_TimesRepeated + 1);
		else
			str_format(aCount, sizeof(aCount), " [%d]", Line.m_TimesRepeated + 1);

		bool TextHiddenByStreamer = false;
		std::string VisibleTextStorage;
		const char *pText = Line.m_aText;
		if(Config()->m_ClStreamerMode && Line.m_ClientId == SERVER_MSG)
		{
			if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load ") && str_endswith(Line.m_aText, "'"))
			{
				TextHiddenByStreamer = true;
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***'";
			}
			else if(str_startswith(Line.m_aText, "Team save in progress. You'll be able to load with '/load") && str_endswith(Line.m_aText, "if it fails"))
			{
				TextHiddenByStreamer = true;
				pText = "Team save in progress. You'll be able to load with '/load *** *** ***' if save is successful or with '/load *** *** ***' if it fails";
			}
			else if(str_startswith(Line.m_aText, "Team successfully saved by ") && str_endswith(Line.m_aText, " to continue"))
			{
				TextHiddenByStreamer = true;
				pText = "Team successfully saved by ***. Use '/load *** *** ***' to continue";
			}
		}
		else
		{
			VisibleTextStorage = BuildVisibleMessageText(Line, false);
			if(Line.m_HasReply && Line.m_aReplyToName[0] != '\0')
			{
				char aReplyPrefix[72];
				CUClientChatReply::BuildReplyBodyPrefix(Line.m_aReplyToName, aReplyPrefix, sizeof(aReplyPrefix));
				if(aReplyPrefix[0] != '\0' && str_startswith_nocase(VisibleTextStorage.c_str(), aReplyPrefix))
					VisibleTextStorage.erase(0, str_length(aReplyPrefix));
			}
			if(Line.m_HasSettingsLink)
			{
				int UriStart = -1, UriLen = 0;
				char aUri[CUClientSettingsLink::MAX_URI_LENGTH];
				if(CUClientSettingsLink::FindUriInText(VisibleTextStorage.c_str(), UriStart, UriLen, aUri, sizeof(aUri)))
				{
					char aStripped[CHAT_LINE_LENGTH];
					CUClientSettingsLink::StripUriFromDisplay(VisibleTextStorage.c_str(), UriStart, UriLen, aStripped, sizeof(aStripped));
					VisibleTextStorage = aStripped;
				}
			}
			pText = VisibleTextStorage.c_str();
		}

		const CColoredParts ColoredParts(pText, Line.m_ClientId == CLIENT_MSG);
		if(!ColoredParts.Colors().empty() && ColoredParts.Colors()[0].m_Index == 0)
			Line.m_CustomColor = ColoredParts.Colors()[0].m_Color;
		pText = ColoredParts.Text();

		const char *pMessageText = pText;

		const char *pTranslatedError = nullptr;
		const char *pTranslatedText = nullptr;
		const char *pTranslatedLanguage = nullptr;
		if(Line.m_pTranslateResponse != nullptr && Line.m_pTranslateResponse->m_Text[0])
		{
			// If hidden and there is translated text
			if(TextHiddenByStreamer)
			{
				pTranslatedError = TCLocalize("Translated text hidden due to streamer mode");
			}
			else if(Line.m_pTranslateResponse->m_Error)
			{
				pTranslatedError = Line.m_pTranslateResponse->m_Text;
			}
			else
			{
				pTranslatedText = Line.m_pTranslateResponse->m_Text;
				if(Line.m_pTranslateResponse->m_Language[0] != '\0')
					pTranslatedLanguage = Line.m_pTranslateResponse->m_Language;
			}
		}
		const char *pDisplayedTranslatedText = pTranslatedText;
		const char *pDisplayedTranslatedLanguage = pTranslatedLanguage;
		const bool ShowOriginalOnHover = pTranslatedText != nullptr && HoveredTranslateLineIndex == LineIndex;
		if(ShowOriginalOnHover)
		{
			pDisplayedTranslatedText = pText;
			pDisplayedTranslatedLanguage = nullptr;
		}

		Line.m_SelectionStart = -1;
		Line.m_SelectionEnd = -1;

		if(Line.m_HasReply && Line.m_aReplyQuoteText[0] == '\0')
		{
			if(Line.m_ReplyMessageIndex > 0)
				TryResolveReplyQuoteByIndex(LineIndex, Line.m_aReplyToName, Line.m_ReplyMessageIndex, Line.m_aReplyQuoteText, sizeof(Line.m_aReplyQuoteText), Line.m_ReplyToClientId);
			else
				TryResolveReplyQuoteText(Line.m_ReplyToClientId, Line.m_aReplyToName, Line.m_aReplyPreview, Line.m_aReplyQuoteText, sizeof(Line.m_aReplyQuoteText), LineIndex);
		}
		const char *pReplyQuoteText = GetLineReplyQuoteText(Line);

		// get the y offset (calculate it if we haven't done that yet)
		if(Line.m_aYOffset[OffsetType] < 0.0f)
		{
			CTextCursor MeasureCursor;
			MeasureCursor.SetPosition(vec2(TextBegin, 0.0f));
			MeasureCursor.m_FontSize = FontSize;
			MeasureCursor.m_Flags = 0;
			MeasureCursor.m_LineWidth = LineWidth;

			const float QuoteTextStart = ReplyQuoteTextStartOffset(TeeSize, FontSize);
			const float QuoteMaxWidth = LineWidth + RealMsgPaddingTee;
			if(Line.m_HasReply && !pDisplayedTranslatedText && !pTranslatedError)
				Line.m_ReplyQuoteHeight = AppendReplyQuoteToMeasure(TextRender(), MeasureCursor, true, Line.m_aReplyToName, pReplyQuoteText, FontSize, QuoteTextStart, QuoteMaxWidth);

			if(LineNeedsTeePadding(Line))
			{
				MeasureCursor.m_X += RealMsgPaddingTee;

				if(ShouldShowFriendMarker(Line))
				{
					TextRender()->TextEx(&MeasureCursor, "♥ ");
				}
			}

			TextRender()->TextEx(&MeasureCursor, aRoomLabel);
			TextRender()->TextEx(&MeasureCursor, aClientId);
			TextRender()->TextEx(&MeasureCursor, Line.m_aName);
			if(Line.m_TimesRepeated > 0)
				TextRender()->TextEx(&MeasureCursor, aCount);

			if(LineNeedsNameColon(Line))
			{
				TextRender()->TextEx(&MeasureCursor, LineNameSeparator(Line));
			}

			const float PrefixWidth = MeasureCursor.m_LongestLineWidth;
			MeasureCursor.m_LongestLineWidth = 0.0f;
			if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
			{
				MeasureCursor.m_StartX = MeasureCursor.m_X;
				MeasureCursor.m_LineWidth -= PrefixWidth;
			}

			if(pDisplayedTranslatedText)
			{
				TextRender()->TextEx(&MeasureCursor, pDisplayedTranslatedText);
				if(pDisplayedTranslatedLanguage)
				{
					TextRender()->TextEx(&MeasureCursor, " [");
					TextRender()->TextEx(&MeasureCursor, pDisplayedTranslatedLanguage);
					TextRender()->TextEx(&MeasureCursor, "]");
				}
			}
			else if(pTranslatedError)
			{
				TextRender()->TextEx(&MeasureCursor, pMessageText);
				TextRender()->TextEx(&MeasureCursor, "\n");
				MeasureCursor.m_FontSize *= 0.8f;
				TextRender()->TextEx(&MeasureCursor, pTranslatedError);
				MeasureCursor.m_FontSize /= 0.8f;
			}
			else
			{
				TextRender()->TextEx(&MeasureCursor, pMessageText);
			}

			Line.m_aTextHeight[OffsetType] = MeasureCursor.Height();
			Line.m_aMediaPreviewWidth[OffsetType] = 0.0f;
			Line.m_aMediaPreviewHeight[OffsetType] = 0.0f;
			float TotalHeight = Line.m_aTextHeight[OffsetType] + RealMsgPaddingY;
			const bool ShowMediaSlot = ShouldDisplayMediaSlot(Line);
			const bool HideMediaPreview = ShouldHideMediaPreview(Line);
			const bool HideNsfwMedia = ShouldHideNsfwMedia(Line);
			if(ShowMediaSlot && (HideMediaPreview || HideNsfwMedia || (Line.m_MediaState == EMediaState::READY && Line.m_MediaWidth > 0 && Line.m_MediaHeight > 0 && !Line.m_vMediaFrames.empty())))
			{
				const float MaxPreviewWidth = minimum(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
				if(MaxPreviewWidth > 0.0f && MaxPreviewHeight > 0.0f)
				{
					if(Line.m_MediaState == EMediaState::READY && Line.m_MediaWidth > 0 && Line.m_MediaHeight > 0 && !Line.m_vMediaFrames.empty())
					{
						const float ScaleByWidth = MaxPreviewWidth / (float)Line.m_MediaWidth;
						const float ScaleByHeight = MaxPreviewHeight / (float)Line.m_MediaHeight;
						float Scale = minimum(1.0f, minimum(ScaleByWidth, ScaleByHeight));
						float PreviewW = maximum(1.0f, (float)Line.m_MediaWidth * Scale);
						float PreviewH = maximum(1.0f, (float)Line.m_MediaHeight * Scale);
						if(PreviewW < CHAT_MEDIA_MIN_PREVIEW_SIDE || PreviewH < CHAT_MEDIA_MIN_PREVIEW_SIDE)
						{
							const float UpscaleByW = CHAT_MEDIA_MIN_PREVIEW_SIDE / PreviewW;
							const float UpscaleByH = CHAT_MEDIA_MIN_PREVIEW_SIDE / PreviewH;
							const float Upscale = maximum(UpscaleByW, UpscaleByH);
							const float MaxUpscale = minimum(MaxPreviewWidth / PreviewW, MaxPreviewHeight / PreviewH);
							if(MaxUpscale > 1.0f)
							{
								const float UseUpscale = minimum(Upscale, MaxUpscale);
								PreviewW *= UseUpscale;
								PreviewH *= UseUpscale;
							}
						}
						Line.m_aMediaPreviewWidth[OffsetType] = maximum(1.0f, PreviewW);
						Line.m_aMediaPreviewHeight[OffsetType] = maximum(1.0f, PreviewH);
					}
					else
					{
						Line.m_aMediaPreviewWidth[OffsetType] = MaxPreviewWidth;
						Line.m_aMediaPreviewHeight[OffsetType] = maximum(FontSize * 1.6f, 18.0f) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
					}
					TotalHeight += FontSize * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
				}
			}
			else if(ShowMediaSlot && (Line.m_MediaState == EMediaState::QUEUED || Line.m_MediaState == EMediaState::LOADING || Line.m_MediaState == EMediaState::DECODING))
			{
				Line.m_aMediaPreviewWidth[OffsetType] = minimum(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
				Line.m_aMediaPreviewHeight[OffsetType] = maximum(FontSize * 1.2f, 12.0f) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
				TotalHeight += FontSize * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
			}
			else if(ShowMediaSlot && Line.m_MediaState == EMediaState::FAILED)
			{
				Line.m_aMediaPreviewWidth[OffsetType] = minimum(LineWidth, (float)g_Config.m_BcChatMediaPreviewMaxWidth) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
				Line.m_aMediaPreviewHeight[OffsetType] = maximum(FontSize * 2.1f, 18.0f) * CHAT_MEDIA_PREVIEW_SIZE_SCALE;
				TotalHeight += FontSize * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
			}

			// UClient: reserve space for shared .map attachment card.
			Line.m_aMapCardHeight[OffsetType] = 0.0f;
			if(HasMapAttachment(Line))
			{
				Line.m_aMapCardHeight[OffsetType] = maximum(18.0f, FontSize * 1.85f);
				TotalHeight += FontSize * 0.25f + Line.m_aMapCardHeight[OffsetType];
			}

			Line.m_aSettingsLinkHeight[OffsetType] = 0.0f;
			Line.m_aSettingsLinkWidth[OffsetType] = 0.0f;
			if(Line.m_HasSettingsLink)
			{
				Line.m_aSettingsLinkWidth[OffsetType] = MeasureSettingsLinkWidth(Line, FontSize);
				const float SettingsCardW = SettingsCardRenderWidth(Line, FontSize, IsScoreBoardOpen, LineWidth, RealMsgPaddingX);
				Line.m_aSettingsLinkHeight[OffsetType] = MeasureSettingsLinkHeight(Line, FontSize, SettingsCardW);
				TotalHeight += FontSize * 0.25f + Line.m_aSettingsLinkHeight[OffsetType];
			}

			// UClient: reserve space for the emoji reaction row below the message/media.
			Line.m_aReactionRowHeight[OffsetType] = LayoutReactionRow(Line, FontSize, LineWidth, 0.0f, 0.0f, nullptr);
			TotalHeight += Line.m_aReactionRowHeight[OffsetType];

			// UClient: the audience note row. The bubble extends upwards from a fixed bottom, so
			// the pointer stays inside the message it just grew and the note cannot flicker.
			Line.m_aScopeNoteHeight[OffsetType] = WantScopeNoteHeight;
			TotalHeight += Line.m_aScopeNoteHeight[OffsetType];

			Line.m_aYOffset[OffsetType] = TotalHeight;
		}

		y -= Line.m_aYOffset[OffsetType];

		// cut off if msgs waste too much space
		if(y < HeightLimit && i != StartLine)
			break;

		// the position the text was created
		Line.m_TextYOffset = y + RealMsgPaddingY / 2.0f;

		int CurRenderFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(CurRenderFlags | ETextRenderFlags::TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD);

		// reset the cursor
		CTextCursor LineCursor;
		LineCursor.SetPosition(vec2(TextBegin, Line.m_TextYOffset));
		LineCursor.m_FontSize = FontSize;
		LineCursor.m_LineWidth = LineWidth;
		if(ChatInteractionActive && !m_Input.HasSelection() && (m_MouseIsPress || m_HasSelection || m_WantsSelectionCopy))
		{
			LineCursor.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_CALCULATE;
			LineCursor.m_PressMouse = m_MousePress;
			LineCursor.m_ReleaseMouse = m_MouseRelease;
		}

		ColorRGBA Color;
		if(Line.m_CustomColor)
			Color = *Line.m_CustomColor;
		else if(Line.m_ClientId == SERVER_MSG)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		else if(Line.m_Highlighted)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor));
		else if(Line.m_UClient)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UcMessageColor));
		else if(Line.m_ClientId == CLIENT_MSG)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		else if(Line.m_Team)
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageTeamColor));
		else // regular message
			Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageColor));

		const float QuoteTextStart = ReplyQuoteTextStartOffset(TeeSize, FontSize);
		const float QuoteMaxWidth = LineWidth + RealMsgPaddingTee;
		if(Line.m_HasReply && !pDisplayedTranslatedText && !pTranslatedError)
		{
			AppendReplyQuoteToContainer(TextRender(), Line.m_TextContainerIndex, LineCursor, true, Line.m_aReplyToName, pReplyQuoteText, FontSize, Color, QuoteTextStart, QuoteMaxWidth, Line.m_ReplyQuoteRect.m_X, Line.m_ReplyQuoteRect.m_Y, Line.m_ReplyQuoteRect.m_W, Line.m_ReplyQuoteRect.m_H, Line.m_ReplyQuoteRectValid);
		}

		// Message is from a player, or a remote UClient sender with a tee/name.
		if(LineNeedsTeePadding(Line))
		{
			LineCursor.m_X += RealMsgPaddingTee;

			if(ShouldShowFriendMarker(Line))
			{
				TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageFriendColor)).WithAlpha(1.0f));
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, "♥ ");
			}
		}

		if(aRoomLabel[0])
		{
			TextRender()->TextColor(ColorRGBA(0.55f, 0.78f, 1.0f, 1.0f));
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aRoomLabel);
		}

		// render name
		ColorRGBA NameColor;
		if(Line.m_CustomColor)
			NameColor = *Line.m_CustomColor;
		else if(Line.m_ClientId == SERVER_MSG)
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageSystemColor));
		else if(Line.m_UClient)
			NameColor = CalculateNameColor(ColorHSLA(g_Config.m_UcMessageColor));
		else if(Line.m_ClientId == CLIENT_MSG)
			NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageClientColor));
		else if(Line.m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListChat && GameClient()->m_WarList.GetAnyWar(Line.m_ClientId)) // BestClient
			NameColor = GameClient()->m_WarList.GetPriorityColor(Line.m_ClientId);
		else if(Line.m_Team)
			NameColor = CalculateNameColor(ColorHSLA(g_Config.m_ClMessageTeamColor));
		else if(Line.m_NameColor == TEAM_RED)
			NameColor = ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
		else if(Line.m_NameColor == TEAM_BLUE)
			NameColor = ColorRGBA(0.7f, 0.7f, 1.0f, 1.0f);
		else if(Line.m_NameColor == TEAM_SPECTATORS)
			NameColor = ColorRGBA(0.75f, 0.5f, 0.75f, 1.0f);
		else if(Line.m_ClientId >= 0 && g_Config.m_ClChatTeamColors && GameClient()->m_Teams.Team(Line.m_ClientId))
			NameColor = GameClient()->GetDDTeamColor(GameClient()->m_Teams.Team(Line.m_ClientId), 0.75f);
		else
			NameColor = ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f);

		TextRender()->TextColor(NameColor);
		TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aClientId);
		TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, Line.m_aName);

		if(Line.m_TimesRepeated > 0)
		{
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.3f);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, aCount);
		}

		if(LineNeedsNameColon(Line))
		{
			TextRender()->TextColor(NameColor);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, LineNameSeparator(Line));
		}

		TextRender()->TextColor(Color);

		const float PrefixWidth = LineCursor.m_LongestLineWidth;
		LineCursor.m_LongestLineWidth = 0.0f;
		if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
		{
			LineCursor.m_StartX = LineCursor.m_X;
			LineCursor.m_LineWidth -= PrefixWidth;
		}

		if(pDisplayedTranslatedText)
		{
			const float TranslateRectX = LineCursor.m_X;
			const float TranslateRectY = LineCursor.m_Y;
			const float TextLineWidth = maximum(1.0f, LineCursor.m_LineWidth);
			const STextBoundingBox DisplayedBoundingBox = TextRender()->TextBoundingBox(FontSize, pDisplayedTranslatedText, -1, TextLineWidth);
			float HoverRectWidth = DisplayedBoundingBox.m_W;
			float HoverRectHeight = DisplayedBoundingBox.m_H;
			if(pTranslatedText != nullptr && pText != nullptr)
			{
				const STextBoundingBox TranslatedBoundingBox = TextRender()->TextBoundingBox(FontSize, pTranslatedText, -1, TextLineWidth);
				const STextBoundingBox OriginalBoundingBox = TextRender()->TextBoundingBox(FontSize, pText, -1, TextLineWidth);
				HoverRectWidth = maximum(TranslatedBoundingBox.m_W, OriginalBoundingBox.m_W);
				HoverRectHeight = maximum(TranslatedBoundingBox.m_H, OriginalBoundingBox.m_H);
			}
			Line.m_TranslateRect.m_X = TranslateRectX;
			Line.m_TranslateRect.m_Y = TranslateRectY;
			Line.m_TranslateRect.m_W = maximum(1.0f, HoverRectWidth);
			Line.m_TranslateRect.m_H = maximum(FontSize, HoverRectHeight);
			Line.m_TranslateRectValid = true;
			Line.m_TranslateLanguageRectValid = false;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pDisplayedTranslatedText);
			if(pDisplayedTranslatedLanguage)
			{
				ColorRGBA ColorLang = Color;
				ColorLang.r *= 0.8f;
				ColorLang.g *= 0.8f;
				ColorLang.b *= 0.8f;
				TextRender()->TextColor(ColorLang);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, " [");
				const float RectX = LineCursor.m_X;
				const float RectY = LineCursor.m_Y;
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pDisplayedTranslatedLanguage);
				Line.m_TranslateLanguageRect.m_X = RectX;
				Line.m_TranslateLanguageRect.m_Y = RectY;
				Line.m_TranslateLanguageRect.m_W = maximum(1.0f, LineCursor.m_X - RectX);
				Line.m_TranslateLanguageRect.m_H = FontSize;
				Line.m_TranslateLanguageRectValid = true;
				const float TranslateRight = maximum(Line.m_TranslateRect.m_X + Line.m_TranslateRect.m_W, Line.m_TranslateLanguageRect.m_X + Line.m_TranslateLanguageRect.m_W);
				const float TranslateBottom = maximum(Line.m_TranslateRect.m_Y + Line.m_TranslateRect.m_H, Line.m_TranslateLanguageRect.m_Y + Line.m_TranslateLanguageRect.m_H);
				Line.m_TranslateRect.m_W = maximum(1.0f, TranslateRight - Line.m_TranslateRect.m_X);
				Line.m_TranslateRect.m_H = maximum(FontSize, TranslateBottom - Line.m_TranslateRect.m_Y);
				TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, "]");
			}
			TextRender()->TextColor(Color);
		}
		else if(pTranslatedError)
		{
			Line.m_TranslateRectValid = false;
			Line.m_TranslateLanguageRectValid = false;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pMessageText);
			ColorRGBA ColorSub = Color;
			ColorSub.r = 0.7f;
			ColorSub.g = 0.6f;
			ColorSub.b = 0.6f;
			TextRender()->TextColor(ColorSub);
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, "\n");
			LineCursor.m_FontSize *= 0.8f;
			TextRender()->CreateOrAppendTextContainer(Line.m_TextContainerIndex, &LineCursor, pTranslatedError);
			LineCursor.m_FontSize /= 0.8f;
			TextRender()->TextColor(Color);
		}
		else if(Line.m_HasServerJoinLink)
		{
			// Server-join announcement: draw "joined " normally and the server name as a link.
			// Skip generic URL/mention detection (server names commonly contain dots).
			Line.m_TranslateRectValid = false;
			Line.m_TranslateLanguageRectValid = false;
			ColoredParts.AddSplitsToCursor(LineCursor);
			AppendServerJoinBody(TextRender(), Line.m_TextContainerIndex, LineCursor, pMessageText,
				Line.m_aServerJoinServerName, Line.m_ServerJoinBounds, Line.m_ServerJoinBoundsValid, Line.m_ServerJoinFontSize);
			LineCursor.m_vColorSplits.clear();
		}
		else
		{
			Line.m_TranslateRectValid = false;
			Line.m_TranslateLanguageRectValid = false;
			ColoredParts.AddSplitsToCursor(LineCursor);
			AppendTextWithUrlAndMentionColors(TextRender(), Line.m_TextContainerIndex, LineCursor, pMessageText,
				m_LinkPolicyCache.m_vSafeDomains,
				&Line.m_vLinkBounds, &Line.m_vLinks, &Line.m_vLinkFontSizes, &Line.m_vLinkAlwaysConfirm, &Line.m_vLinkGroups);
			LineCursor.m_vColorSplits.clear();
		}

		Line.m_SelectionStart = LineCursor.m_SelectionStart;
		Line.m_SelectionEnd = LineCursor.m_SelectionEnd;

		Line.m_ReplyButtonAnchorX = LineCursor.m_X;
		Line.m_ReplyButtonAnchorY = LineCursor.m_Y;
		Line.m_ReplyButtonAnchorValid = CanShowReplyButton(Line) || CanShowSettingsShortcut(Line);

		if(!g_Config.m_ClChatOld && (Line.m_aText[0] != '\0' || Line.m_aName[0] != '\0'))
		{
			float FullWidth = RealMsgPaddingX * 1.5f;
			if(!IsScoreBoardOpen && !g_Config.m_ClChatOld)
			{
				FullWidth += PrefixWidth + LineCursor.m_LongestLineWidth;
			}
			else
			{
				FullWidth += maximum(PrefixWidth, LineCursor.m_LongestLineWidth);
			}
			// The reply button floats to the right of the message on hover; it must
			// NOT widen the message bubble, otherwise the background box and the
			// reply highlight stretch past the actual text.
			if(Line.m_aMediaPreviewWidth[OffsetType] > 0.0f)
			{
				const float PreviewWidth = Line.m_aMediaPreviewWidth[OffsetType] + (TextBegin - Begin) + RealMsgPaddingX;
				FullWidth = maximum(FullWidth, PreviewWidth);
			}
			// A reply quote can be wider than the body (e.g. a long URL that wraps),
			// so make sure the message background box still covers it.
			if(Line.m_HasReply && Line.m_ReplyQuoteRectValid)
			{
				const float QuoteWidth = Line.m_ReplyQuoteRect.m_W + (TextBegin - Begin) + RealMsgPaddingX;
				FullWidth = maximum(FullWidth, QuoteWidth);
			}
			if(Line.m_aSettingsLinkWidth[OffsetType] > 0.0f)
			{
				const float SettingsWidth = Line.m_aSettingsLinkWidth[OffsetType] + (TextBegin - Begin) + RealMsgPaddingX;
				FullWidth = maximum(FullWidth, SettingsWidth);
			}
			// When the scoreboard shrinks the chat column, clamp the bubble to that
			// column so the background doesn't outrun the clamped settings/media card.
			// With scoreboard off, allow the bubble to grow up to ChatWidth() so a
			// page-only settings card can show its full hint line.
			const float MaxBubbleWidth = IsScoreBoardOpen ?
				(LineWidth + (RealMsgPaddingX * 1.5f) + RealMsgPaddingTee) :
				ChatWidth();
			FullWidth = minimum(FullWidth, MaxBubbleWidth);
			Graphics()->SetColor(1, 1, 1, 1);
			Line.m_QuadContainerIndex = Graphics()->CreateRectQuadContainer(Begin, y, FullWidth, Line.m_aYOffset[OffsetType], MessageRounding(), IGraphics::CORNER_ALL);
			Line.m_MessageFullWidth = FullWidth;
		}

		TextRender()->SetRenderFlags(CurRenderFlags);
		if(Line.m_TextContainerIndex.Valid())
			TextRender()->UploadTextContainer(Line.m_TextContainerIndex);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CChat::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(GameClient()->m_BestClient.HasStreamerFlag(CBestClient::STREAMER_HIDE_CHAT) && m_Mode == MODE_NONE)
		return;

	// send pending chat messages
	if(!m_vPendingChatQueue.empty() && m_LastChatSend + time_freq() < time())
	{
		const CPendingChatEntry Entry = m_vPendingChatQueue.front();
		m_vPendingChatQueue.erase(m_vPendingChatQueue.begin());
		SendChat(Entry.m_Team, Entry.m_aText);
	}

	UpdateMediaDownloads();
	UpdateMediaSave();
	UpdateMapSave();
	UpdateMapSizeRequests();
	UpdateGiphySearch();
	UpdateGiphyPreviewCache();
	m_UcChatPaste.OnUpdate(this);
	// uclient: chat paste image
	if(m_UcChatPaste.OnRenderEditor(this))
		return;
	UpdateLinkPolicy();
	UpdateLinkPreflight();
	if(m_MediaViewerOpen && (!g_Config.m_BcChatMediaPreview || !g_Config.m_BcChatMediaViewer))
		CloseMediaViewer();

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	const bool BcChatMessageAnimEnabled = BCUiAnimations::Enabled() && g_Config.m_BcChatAnimation != 0;
	const auto Layout = HudLayout::Get(HudLayout::MODULE_CHAT, Width, Height);
	if(!HudLayout::IsEnabled(HudLayout::MODULE_CHAT))
		return;
	const float LayoutScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	float x = Layout.m_X;
	const vec2 WindowSize(maximum(1.0f, (float)Graphics()->WindowWidth()), maximum(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	const vec2 UiToChatScale(Width / Ui()->Screen()->w, Height / Ui()->Screen()->h);
	const vec2 MousePos = UiMousePos * UiToChatScale;
	// When a chat-owned modal popup is open, do not let the polled mouse state start a text
	// selection / drag on the chat behind the popup.
	const bool ChatModalPopupOpen = Ui()->IsPopupOpen(&m_MediaContextPopupId) || Ui()->IsPopupOpen(&m_MediaSaveAssetPopupId) ||
		Ui()->IsPopupOpen(&m_MediaSaveSkinPopupId) || Ui()->IsPopupOpen(&m_ReactionPickerPopupId);
	const bool MouseDown = Input()->KeyIsPressed(KEY_MOUSE_1) && !ChatModalPopupOpen;
	int HoveredTranslateLineIndex = -1;
	m_HoveredPlayerName.clear();
	m_HoveredLink.clear();
	m_HoveredLinkAlwaysConfirm = false;
	m_HoveredReplyLineIndex = -1;
	m_HoveredSettingsShortcutLineIndex = -1;
	m_HoveredReactionLineIndex = -1;
	m_HoveredReactionIndex = -1;
	m_HoveredReadLineIndex = -1;
	m_HoveredServerJoinLineIndex = -1;
	m_ReplyCancelButtonRectValid = false;
	// UClient audience note: resolved before OnPrepareLines because it changes the line's height.
	// The bubble rects come from the previous frame, like the translate hover above.
	int HoveredScopeLineIndex = -1;
	if(m_Mode != MODE_NONE)
	{
		for(int LineIndex = 0; LineIndex < MAX_LINES; ++LineIndex)
		{
			const CLine &Line = m_aLines[LineIndex];
			if(!Line.m_ScopeHoverRectValid)
				continue;
			if(MousePos.x >= Line.m_ScopeHoverRect.m_X && MousePos.x <= Line.m_ScopeHoverRect.m_X + Line.m_ScopeHoverRect.m_W &&
				MousePos.y >= Line.m_ScopeHoverRect.m_Y && MousePos.y <= Line.m_ScopeHoverRect.m_Y + Line.m_ScopeHoverRect.m_H)
			{
				HoveredScopeLineIndex = LineIndex;
				break;
			}
		}
	}
	for(int LineIndex = 0; LineIndex < MAX_LINES; ++LineIndex)
	{
		const CLine &Line = m_aLines[LineIndex];
		if(!Line.m_TranslateRectValid && !Line.m_TranslateLanguageRectValid)
			continue;
		const bool HoveredTranslatedText = Line.m_TranslateRectValid &&
			MousePos.x >= Line.m_TranslateRect.m_X && MousePos.x <= Line.m_TranslateRect.m_X + Line.m_TranslateRect.m_W &&
			MousePos.y >= Line.m_TranslateRect.m_Y && MousePos.y <= Line.m_TranslateRect.m_Y + Line.m_TranslateRect.m_H;
		const bool HoveredLanguage = Line.m_TranslateLanguageRectValid &&
			MousePos.x >= Line.m_TranslateLanguageRect.m_X && MousePos.x <= Line.m_TranslateLanguageRect.m_X + Line.m_TranslateLanguageRect.m_W &&
			MousePos.y >= Line.m_TranslateLanguageRect.m_Y && MousePos.y <= Line.m_TranslateLanguageRect.m_Y + Line.m_TranslateLanguageRect.m_H;
		if(HoveredTranslatedText || HoveredLanguage)
		{
			HoveredTranslateLineIndex = LineIndex;
			break;
		}
	}
	for(auto &Line : m_aLines)
	{
		Line.m_NameRectValid = false;
		Line.m_MediaPreviewRectValid = false;
		Line.m_MediaRetryRectValid = false;
		Line.m_LineRectValid = false;
		Line.m_ReplyButtonRectValid = false;
		Line.m_SettingsShortcutRectValid = false;
		Line.m_ReactionRectsValid = false;
		Line.m_ReadLabelRectValid = false;
		Line.m_ScopeHoverRectValid = false;
	}
	m_TranslateButtonRectValid = false;
	m_GiphyButtonRectValid = false;
	// BestClient
	float y = Layout.m_Y;
	// float y = 300.0f - 20.0f * FontSize() / 6.0f;
	float ScaledFontSize = FontSize() * (8.0f / 6.0f);
	float PendingPreviewReserve = 0.0f;
	const bool BcChatOpenAnimEnabled = BcChatMessageAnimEnabled && g_Config.m_BcChatOpenAnimation != 0 && g_Config.m_BcChatOpenAnimationMs > 0;
	const bool BcChatTypingAnimEnabled = BcChatMessageAnimEnabled && g_Config.m_BcChatTypingAnimation != 0 && g_Config.m_BcChatTypingAnimationMs > 0;
	float ChatOpenOffsetX = 0.0f;
	if(m_Mode != MODE_NONE && BcChatOpenAnimEnabled && m_ChatOpenAnimationStart > 0)
	{
		const float Dur = BCUiAnimations::MsToSeconds(g_Config.m_BcChatOpenAnimationMs);
		const float Age = (time_get() - m_ChatOpenAnimationStart) / (float)time_freq();
		const float Progress = Dur > 0.0f ? std::clamp(Age / Dur, 0.0f, 1.0f) : 1.0f;
		const float ChatOpenEase = BCUiAnimations::EaseInOutQuart(Progress);
		ChatOpenOffsetX = -(x + maximum(Width - 190.0f, 190.0f) + 24.0f) * (1.0f - ChatOpenEase);
	}
	// BestClient: mouse interaction (cursor, selection, scrollbar, media clicks) belongs to
	// typing only. The expand-only bind (m_Show) still shows the large area and wheel-scrolls,
	// but must not engage the mouse, so it is intentionally excluded here.
	const bool ChatInteractionActive = m_Mode != MODE_NONE;
	if(m_MediaViewerOpen && !ChatInteractionActive)
		CloseMediaViewer();
	if(!ChatInteractionActive)
	{
		m_MouseIsPress = false;
		m_HasSelection = false;
		m_WantsSelectionCopy = false;
	}
	if(ChatInteractionActive)
	{
		if(!m_MediaViewerOpen && !m_ScrollbarDragging)
		{
			bool OverSettingsLink = false;
			for(int i = 0; i < MAX_LINES; ++i)
			{
				const CLine &Line = m_aLines[i];
				if(!Line.m_SettingsLinkRectValid)
					continue;
				const SRenderRect &R = Line.m_SettingsLinkRect;
				if(MousePos.x >= R.m_X && MousePos.x <= R.m_X + R.m_W &&
					MousePos.y >= R.m_Y && MousePos.y <= R.m_Y + R.m_H)
				{
					OverSettingsLink = true;
					break;
				}
			}
			if(!m_MouseIsPress && MouseDown && !OverSettingsLink)
			{
				m_MouseIsPress = true;
				m_MousePress = MousePos;
				m_MouseRelease = MousePos;
				m_HasSelection = false;
			}
			else if(m_MouseIsPress && !MouseDown)
			{
				m_MouseIsPress = false;
			}
			if(m_MouseIsPress)
				m_MouseRelease = MousePos;
		}
		else
		{
			m_MouseIsPress = false;
		}

		if(m_Mode != MODE_NONE)
		{
			const float InputAreaWidth = maximum(190.0f * LayoutScale, ChatWidth());
			float StackAboveInput = 0.0f;

			const float ReplyBannerReserve = ReplyBannerHeight(ScaledFontSize);
			if(ReplyBannerReserve > 0.0f)
			{
				RenderReplyBanner(x + ChatOpenOffsetX, y, ScaledFontSize);
				StackAboveInput += ReplyBannerReserve;
			}

			// uclient: chat paste image preview above input
			const float PreviewH = m_UcChatPaste.PreviewHeight(this, InputAreaWidth, ScaledFontSize);
			if(PreviewH > 0.0f)
			{
				StackAboveInput += PreviewH + maximum(6.0f, FontSize() * 0.45f);
				m_UcChatPaste.RenderPreview(this, x + ChatOpenOffsetX, y - StackAboveInput, InputAreaWidth, Height, ScaledFontSize);
			}
			PendingPreviewReserve = StackAboveInput;

			// render chat input
			CTextCursor InputCursor;
			InputCursor.SetPosition(vec2(x + ChatOpenOffsetX, y));
			InputCursor.m_FontSize = ScaledFontSize;
			InputCursor.m_LineWidth = ChatWidth() - 190.0f * LayoutScale;

		// BestClient
		InputCursor.m_LineWidth = std::max(InputCursor.m_LineWidth, 190.0f * LayoutScale);

		if(m_Mode == MODE_ALL)
			TextRender()->TextEx(&InputCursor, Localize("All"));
		else if(m_Mode == MODE_TEAM)
			TextRender()->TextEx(&InputCursor, Localize("Team"));
		else if(m_Mode == MODE_UCLIENT)
			TextRender()->TextEx(&InputCursor, "UClient");
		else
			TextRender()->TextEx(&InputCursor, Localize("Chat"));

		TextRender()->TextEx(&InputCursor, ": ");

		const float TranslateButtonSize = maximum(16.0f, ScaledFontSize * 1.35f);
		const float TranslateButtonGap = 4.0f;
		const float MessageMaxWidth = maximum(40.0f, InputCursor.m_LineWidth - (InputCursor.m_X - InputCursor.m_StartX) - 2.0f * (TranslateButtonSize + TranslateButtonGap));
		const CUIRect ClippingRect = {InputCursor.m_X, InputCursor.m_Y, MessageMaxWidth, 2.25f * InputCursor.m_FontSize};
		const float TypingTravel = 30.0f;
		const CUIRect ChatInputClipRect = {0.0f, ClippingRect.y - TypingTravel, Width, ClippingRect.h + TypingTravel};
		const float XScale = Graphics()->ScreenWidth() / Width;
		const float YScale = Graphics()->ScreenHeight() / Height;
		Graphics()->ClipEnable((int)(ChatInputClipRect.x * XScale), (int)(ChatInputClipRect.y * YScale), (int)(ChatInputClipRect.w * XScale), (int)(ChatInputClipRect.h * YScale));

		float ScrollOffset = m_Input.GetScrollOffset();
		float ScrollOffsetChange = m_Input.GetScrollOffsetChange();
		CLineInput::SMouseSelection *pMouseSelection = m_Input.GetMouseSelection();
		const bool InputInside = MousePos.x >= ClippingRect.x && MousePos.x <= ClippingRect.x + ClippingRect.w &&
			MousePos.y >= ClippingRect.y && MousePos.y <= ClippingRect.y + ClippingRect.h;
		if(InputInside && m_MouseIsPress)
		{
			pMouseSelection->m_Selecting = true;
			pMouseSelection->m_PressMouse = m_MousePress;
			pMouseSelection->m_ReleaseMouse = m_MouseRelease;
			pMouseSelection->m_Offset.y = ScrollOffset;
			m_HasSelection = false;
		}
		else if(!m_MouseIsPress)
		{
			pMouseSelection->m_Selecting = false;
		}
		if(ScrollOffset != pMouseSelection->m_Offset.y)
		{
			pMouseSelection->m_PressMouse.y -= ScrollOffset - pMouseSelection->m_Offset.y;
			pMouseSelection->m_Offset.y = ScrollOffset;
		}

			const bool PopupInputActive = Ui()->IsPopupOpen(&m_GiphyPopupId) || Ui()->IsPopupOpen(&m_TranslateSettingsPopupId) ||
						      Ui()->IsPopupOpen(&m_MediaContextPopupId) || Ui()->IsPopupOpen(&m_MediaSaveAssetPopupId) || Ui()->IsPopupOpen(&m_MediaSaveSkinPopupId) ||
						      Ui()->IsPopupOpen(&m_ReactionPickerPopupId);
			if(!PopupInputActive)
				m_Input.Activate(EInputPriority::CHAT); // Ensure that the input is active
			const CUIRect InputCursorRect = {InputCursor.m_X, InputCursor.m_Y - ScrollOffset, 0.0f, 0.0f};
			const bool WasChanged = m_Input.WasChanged();
			const bool WasCursorChanged = m_Input.WasCursorChanged();
			const bool Changed = WasChanged || WasCursorChanged;

			char aDisplayedInputText[MAX_LINE_LENGTH];
			str_copy(aDisplayedInputText, m_Input.GetDisplayedString(), sizeof(aDisplayedInputText));
			const float TypingAnimDuration = BCUiAnimations::MsToSeconds(g_Config.m_BcChatTypingAnimationMs);
			std::vector<STextColorSplit> vTypingColorSplits;
			if(aDisplayedInputText[0] != '\0')
			{
				const std::vector<SUrlMatch> vInputLinks = FindClickableUrlMatches(aDisplayedInputText, m_LinkPolicyCache.m_vSafeDomains);
				for(const SUrlMatch &Link : vInputLinks)
					vTypingColorSplits.emplace_back(Link.m_Start, Link.m_Length, ChatLinkColor());
			}
			std::vector<CChat::STypingGlyphAnim> vActiveTypingGlyphAnims;
			if(BcChatTypingAnimEnabled && TypingAnimDuration > 0.0f && aDisplayedInputText[0] != '\0' && ChatTypingAnimSupportsText(aDisplayedInputText))
			{
				for(auto It = m_vTypingGlyphAnims.begin(); It != m_vTypingGlyphAnims.end();)
				{
					const float TypingAnimAge = (time_get() - It->m_StartTime) / (float)time_freq();
					const int StartByte = It->m_ByteIndex;
					const int GlyphBytes = It->m_ByteLength;
					const int StoredGlyphBytes = str_length(It->m_aText);
					const bool Valid =
						TypingAnimAge < TypingAnimDuration &&
						It->m_ByteIndex >= 0 &&
						GlyphBytes > 0 &&
						StartByte + GlyphBytes <= str_length(aDisplayedInputText) &&
						StoredGlyphBytes == GlyphBytes &&
						str_comp_num(It->m_aText, aDisplayedInputText + StartByte, GlyphBytes) == 0;
					if(!Valid)
					{
						It = m_vTypingGlyphAnims.erase(It);
						continue;
					}

					vActiveTypingGlyphAnims.push_back(*It);
					vTypingColorSplits.emplace_back(It->m_ByteIndex, It->m_ByteLength, ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
					++It;
				}
			}

			// Typing anims hide fill via transparent color splits; outline would still show those glyphs.
			// Only disable outline for the base pass when glyph overlays are active (not for URL tinting alone).
			const bool DisableBaseOutline = !vActiveTypingGlyphAnims.empty();
			if(DisableBaseOutline)
				TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
			const STextBoundingBox BoundingBox = m_Input.Render(&InputCursorRect, InputCursor.m_FontSize, TEXTALIGN_TL, Changed, MessageMaxWidth, 0.0f, vTypingColorSplits);
			if(DisableBaseOutline)
				TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

			for(const auto &TypingGlyphAnim : vActiveTypingGlyphAnims)
			{
				const float TypingAnimAge = (time_get() - TypingGlyphAnim.m_StartTime) / (float)time_freq();
				const float Progress = std::clamp(TypingAnimAge / TypingAnimDuration, 0.0f, 1.0f);
				const float Ease = BCUiAnimations::EaseInOutQuart(Progress);
				const float OverlayYOffset = -4.5f * (1.0f - Ease);
				const int PrefixBytes = TypingGlyphAnim.m_ByteIndex;
				char aPrefixText[MAX_LINE_LENGTH] = "";
				if(PrefixBytes < 0 || PrefixBytes > str_length(aDisplayedInputText))
					continue;
				str_truncate(aPrefixText, sizeof(aPrefixText), aDisplayedInputText, PrefixBytes);

				CTextCursor MeasureCursor;
				MeasureCursor.SetPosition(vec2(InputCursorRect.x, InputCursorRect.y));
				MeasureCursor.m_FontSize = InputCursor.m_FontSize;
				MeasureCursor.m_LineWidth = MessageMaxWidth;
				TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
				TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
				TextRender()->TextEx(&MeasureCursor, aPrefixText);

				CTextCursor OverlayCursor;
				OverlayCursor.SetPosition(vec2(MeasureCursor.m_X, MeasureCursor.m_Y + OverlayYOffset));
				OverlayCursor.m_FontSize = InputCursor.m_FontSize;
				OverlayCursor.m_LineWidth = MessageMaxWidth;
				TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f + 0.25f * Ease));
				TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(0.75f + 0.25f * Ease));
				TextRender()->TextEx(&OverlayCursor, TypingGlyphAnim.m_aText);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

		Graphics()->ClipDisable();

		CUIRect GiphyButtonRect = {ClippingRect.x + ClippingRect.w + TranslateButtonGap, ClippingRect.y, TranslateButtonSize, maximum(InputCursor.m_FontSize + 4.0f, 16.0f)};
		RenderGiphyButton(GiphyButtonRect);
		CUIRect RoomButtonRect = {GiphyButtonRect.x + GiphyButtonRect.w + TranslateButtonGap, ClippingRect.y, TranslateButtonSize, maximum(InputCursor.m_FontSize + 4.0f, 16.0f)};
		RenderRoomSelectButton(RoomButtonRect);
		if(Ui()->HotItem() == &m_GiphyButton || Ui()->HotItem() == &m_RoomSelectButton ||
			m_GiphyButtonPressed || m_RoomButtonPressed)
		{
			m_MouseIsPress = false;
			m_HasSelection = false;
		}

		// Scroll up or down to keep the caret inside the clipping rect
		const float CaretPositionY = m_Input.GetCaretPosition().y - ScrollOffsetChange;
		if(CaretPositionY < ClippingRect.y)
			ScrollOffsetChange -= ClippingRect.y - CaretPositionY;
		else if(CaretPositionY + InputCursor.m_FontSize > ClippingRect.y + ClippingRect.h)
			ScrollOffsetChange += CaretPositionY + InputCursor.m_FontSize - (ClippingRect.y + ClippingRect.h);

		Ui()->DoSmoothScrollLogic(&ScrollOffset, &ScrollOffsetChange, ClippingRect.h, BoundingBox.m_H);

		m_Input.SetScrollOffset(ScrollOffset);
		m_Input.SetScrollOffsetChange(ScrollOffsetChange);
		if(m_Input.HasSelection())
			m_HasSelection = false;

		// Autocompletion hint
		if(m_Input.GetString()[0] == '/' && m_Input.GetString()[1] != '\0' && !m_vServerCommands.empty())
		{
			for(const auto &Command : m_vServerCommands)
			{
				if(str_startswith_nocase(Command.m_aName, m_Input.GetString() + 1))
				{
					InputCursor.m_X = InputCursor.m_X + TextRender()->TextWidth(InputCursor.m_FontSize, m_Input.GetString(), -1, InputCursor.m_LineWidth);
					InputCursor.m_Y = m_Input.GetCaretPosition().y;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
					TextRender()->TextEx(&InputCursor, Command.m_aName + str_length(m_Input.GetString() + 1));
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					break;
				}
			}
		}
		else if(m_Input.GetString()[0] == '!' && m_Input.GetString()[1] != '\0')
		{
			const char *pIn = m_Input.GetString();
			bool HasSpace = false;
			for(const char *pScan = pIn; *pScan; ++pScan)
			{
				if(std::isspace((unsigned char)*pScan))
				{
					HasSpace = true;
					break;
				}
			}
			if(!HasSpace)
			{
				const char *apCmds[] = {"!voice"};
				const char *pCandidate = nullptr;
				for(const char *pCmd : apCmds)
				{
					if(str_startswith_nocase(pCmd, pIn))
					{
						pCandidate = pCmd;
						break;
					}
				}
				if(pCandidate && str_length(pCandidate) > str_length(pIn))
				{
					InputCursor.m_X = InputCursor.m_X + TextRender()->TextWidth(InputCursor.m_FontSize, pIn, -1, InputCursor.m_LineWidth);
					InputCursor.m_Y = m_Input.GetCaretPosition().y;
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.5f);
					TextRender()->TextEx(&InputCursor, pCandidate + str_length(pIn));
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}
		}

		if(m_Mode == MODE_UCLIENT)
		{
			const char *pTarget = Localize("All UClient users");
			if(g_Config.m_UcChatSendRoom[0])
			{
				if(const char *pRoomName = GameClient()->m_UClientChatRooms.RoomNameById(g_Config.m_UcChatSendRoom))
					pTarget = pRoomName;
			}
			else if(g_Config.m_UcChatSendSameServerOnly)
				pTarget = Localize("Same server");
			char aTargetHint[160];
			str_format(aTargetHint, sizeof(aTargetHint), Localize("Messages will be sent to '%s'."), pTarget);
			CTextCursor HintCursor;
			HintCursor.SetPosition(vec2(x + ChatOpenOffsetX, y + ScaledFontSize * 1.35f));
			HintCursor.m_FontSize = maximum(7.0f, ScaledFontSize * 0.72f);
			HintCursor.m_LineWidth = InputAreaWidth;
			TextRender()->TextColor(0.68f, 0.68f, 0.68f, 0.9f);
			TextRender()->TextEx(&HintCursor, aTargetHint);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
	}

#if defined(CONF_VIDEORECORDER)
	if(!((g_Config.m_ClShowChat && !IVideo::Current()) || (g_Config.m_ClVideoShowChat && IVideo::Current())))
#else
	if(!g_Config.m_ClShowChat)
#endif
		return;

	// Check focus mode settings
	if(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideChat)
		return;

	y -= PendingPreviewReserve;
	y -= ScaledFontSize;
	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsActive();
	const bool ShowLargeArea = m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const bool KeepLinesAlive = m_MediaViewerOpen && ValidateMediaViewerLine();

	int64_t Now = time();
	const auto ShouldExpandCompactAreaForMedia = [&]() {
		if(IsScoreBoardOpen || ShowLargeArea || !g_Config.m_BcChatMediaPreview || !AnyMediaAllowed())
			return false;
		for(int i = 0; i < 3; ++i)
		{
			const CLine &RecentLine = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
			if(!RecentLine.m_Initialized)
				break;
			if(ShouldHideLineFromStreamer(RecentLine))
				continue;
			if(ShouldDisplayMediaSlot(RecentLine))
				return true;
		}
		return false;
	};
	const bool ExpandCompactAreaForMedia = ShouldExpandCompactAreaForMedia();
	const float VisibleHeight = IsScoreBoardOpen ? 93.0f * LayoutScale : (ShowLargeArea ? 223.0f * LayoutScale : (ExpandCompactAreaForMedia ? CHAT_MEDIA_COMPACT_EXPANDED_HEIGHT * LayoutScale : 73.0f * LayoutScale));
	float HeightLimit = y - VisibleHeight;
	int OffsetType = IsScoreBoardOpen ? 1 : 0;

	float RealMsgPaddingX = MessagePaddingX();
	float RealMsgPaddingY = MessagePaddingY();
	float RealMsgPaddingTee = MessageTeeSize() + MESSAGE_TEE_PADDING_RIGHT;

	if(g_Config.m_ClChatOld)
	{
		RealMsgPaddingX = 0;
		RealMsgPaddingY = 0;
		RealMsgPaddingTee = 0;
	}

	// Same message content width as OnPrepareLines, used to lay out the reaction row.
	const float ReactionAvailWidth = (IsScoreBoardOpen ? maximum(85.0f * LayoutScale, (FontSize() * 85.0f / 6.0f)) : ChatWidth()) - (RealMsgPaddingX * 1.5f) - RealMsgPaddingTee;

	int TotalLines = 0;
	for(int i = 0; i < MAX_LINES; i++)
	{
		CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized)
			break;
		if(Now > Line.m_Time + 16 * time_freq() && !ShowLargeArea && !KeepLinesAlive)
			break;
		++TotalLines;
	}

	const auto CountVisibleLines = [&](int StartLine) {
		int VisibleLines = 0;
		float TmpY = y;
		for(int i = StartLine; i < TotalLines; i++)
		{
			CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
			if(ShouldHideLineFromStreamer(Line))
				continue;
			const float LineHeight = Line.m_aYOffset[OffsetType] > 0.0f ? Line.m_aYOffset[OffsetType] : (FontSize() + RealMsgPaddingY);
			TmpY -= LineHeight;
			if(TmpY < HeightLimit)
			{
				if(VisibleLines == 0)
					++VisibleLines;
				break;
			}
			++VisibleLines;
		}
		return maximum(1, VisibleLines);
	};

	m_BacklogCurLine = maximum(0, minimum(m_BacklogCurLine, maximum(0, TotalLines - 1)));
	const int VisibleLines = CountVisibleLines(m_BacklogCurLine);
	const int MaxScroll = maximum(0, TotalLines - VisibleLines);
	m_BacklogCurLine = maximum(0, minimum(m_BacklogCurLine, MaxScroll));

	if(ChatInteractionActive && MaxScroll > 0)
	{
		const float LogTop = HeightLimit;
		const float LogBottom = y;
		const float LogHeight = maximum(0.0f, LogBottom - LogTop);
		const float RailMargin = 1.0f;
		const float RailWidth = maximum(0.0f, CHAT_SCROLLBAR_WIDTH - 2.0f * RailMargin);
		const float MinRailHeight = RailWidth * 3.0f;
		const float MinScrollbarHeight = MinRailHeight + 2.0f * RailMargin;
		if(LogHeight >= MinScrollbarHeight && RailWidth > 0.0f)
		{
			const float Current = 1.0f - (float)m_BacklogCurLine / (float)MaxScroll;
			CUIRect ScrollbarRect;
			ScrollbarRect.x = x + ChatOpenOffsetX - CHAT_SCROLLBAR_WIDTH - CHAT_SCROLLBAR_MARGIN;
			ScrollbarRect.y = LogTop;
			ScrollbarRect.w = CHAT_SCROLLBAR_WIDTH;
			ScrollbarRect.h = LogHeight;

			CUIRect Rail;
			ScrollbarRect.Margin(RailMargin, &Rail);
			CUIRect Handle;
			const float HandleHeight = maximum(Rail.w, minimum(24.0f, Rail.h / 3.0f));
			Rail.HSplitTop(HandleHeight, &Handle, nullptr);
			Handle.y = Rail.y + (Rail.h - Handle.h) * Current;

			const auto InsideRect = [&](const CUIRect &Rect) {
				return MousePos.x >= Rect.x && MousePos.x <= Rect.x + Rect.w && MousePos.y >= Rect.y && MousePos.y <= Rect.y + Rect.h;
			};

			if(!MouseDown)
			{
				m_ScrollbarDragging = false;
			}
			else if(!m_ScrollbarDragging && InsideRect(Rail))
			{
				if(InsideRect(Handle))
					m_ScrollbarDragOffset = MousePos.y - Handle.y;
				else
					m_ScrollbarDragOffset = Handle.h / 2.0f;
				m_ScrollbarDragging = true;
				m_MouseIsPress = false;
				m_HasSelection = false;
			}

			float NewValue = Current;
			if(m_ScrollbarDragging)
			{
				const float ScrollableHeight = Rail.h - Handle.h;
				if(ScrollableHeight > 0.0f)
				{
					const float Cur = MousePos.y - m_ScrollbarDragOffset;
					NewValue = maximum(0.0f, minimum((Cur - Rail.y) / ScrollableHeight, 1.0f));
				}
			}

			const int NewLine = maximum(0, minimum((int)((1.0f - NewValue) * MaxScroll + 0.5f), MaxScroll));
			m_BacklogCurLine = NewLine;

			Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, Rail.w / 2.0f);
			const ColorRGBA HandleColor = m_ScrollbarDragging ? ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f) : ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f);
			Handle.Draw(HandleColor, IGraphics::CORNER_ALL, Handle.w / 2.0f);
		}
	}
	else
	{
		m_ScrollbarDragging = false;
	}

	OnPrepareLines(y, m_BacklogCurLine, HoveredTranslateLineIndex, HoveredScopeLineIndex);
	std::string SelectionString;
	bool HasChatSelection = false;

	// UClient read receipts: newest other-authored UClient message that is actually visible
	// (and not faded out) this frame. Used after the loop to advance our own read marker.
	int MaxVisibleReadSeq = -1;
	CUuid MaxVisibleReadMsgId = UUID_ZEROED;

	for(int i = m_BacklogCurLine; i < MAX_LINES; i++)
	{
		const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		CLine &Line = m_aLines[LineIndex];
		if(!Line.m_Initialized)
			break;
		if(ShouldHideLineFromStreamer(Line))
			continue;
		if(Now > Line.m_Time + 16 * time_freq() && !m_PrevShowChat && !KeepLinesAlive)
			break;

		y -= Line.m_aYOffset[OffsetType];

		// cut off if msgs waste too much space
		if(y < HeightLimit && i != m_BacklogCurLine)
			break;

		float Blend = Now > Line.m_Time + 14 * time_freq() && !m_PrevShowChat ? 1.0f - (Now - Line.m_Time - 14 * time_freq()) / (2.0f * time_freq()) : 1.0f;
		if(KeepLinesAlive && LineIndex == m_MediaViewerLineIndex)
			Blend = 1.0f;

		// A visibly-rendered message from someone else counts as "read": remember the newest one
		// so we can move our read marker forward once (see IsReadingChat for when that applies).
		if(Line.m_UClient && !Line.m_UClientMine && Line.m_UClientMessageId != UUID_ZEROED &&
			Blend > 0.5f && Line.m_UClientSeq > MaxVisibleReadSeq)
		{
			MaxVisibleReadSeq = Line.m_UClientSeq;
			MaxVisibleReadMsgId = Line.m_UClientMessageId;
		}

		// BestClient: lift newly received messages from the bottom.
		float BcLineXOffset = 0.0f;
		float BcLineYOffset = 0.0f;
		if(BcChatMessageAnimEnabled && g_Config.m_BcChatAnimationMs > 0 && Line.m_Time > 0)
		{
			const float Dur = BCUiAnimations::MsToSeconds(g_Config.m_BcChatAnimationMs);
			const float Age = (Now - Line.m_Time) / (float)time_freq();
			const float Progress = Dur > 0.0f ? std::clamp(Age / Dur, 0.0f, 1.0f) : 1.0f;
			const float Ease = BCUiAnimations::EaseInOutQuad(Progress);
			BcLineYOffset = 42.0f * (1.0f - Ease);
		}

		const float LineRenderX = x + ChatOpenOffsetX + BcLineXOffset;
		const float LineRenderY = y + BcLineYOffset;

		const bool IsPendingReplyTarget = m_PendingReplyActive && LineIndex == m_PendingReplySourceLineIndex;
		const float LineH = maximum(Line.m_aYOffset[OffsetType], FontSize() + RealMsgPaddingY);

		// Draw backgrounds for messages in one batch
		if(!g_Config.m_ClChatOld)
		{
			Graphics()->TextureClear();
			if(Line.m_QuadContainerIndex != -1 && !IsPendingReplyTarget)
			{
				Graphics()->SetColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClChatBackgroundColor, true)).WithMultipliedAlpha(Blend));
				Graphics()->RenderQuadContainerEx(Line.m_QuadContainerIndex, 0, -1, ChatOpenOffsetX + BcLineXOffset, ((LineRenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset));
			}
		}

		if(IsPendingReplyTarget)
		{
			float HighlightW = maximum(1.0f, ChatWidth());
			if(Line.m_MessageFullWidth > 0.0f)
			{
				// Cover the message bubble, but trim a bit of the right-side
				// padding so the solid blue highlight doesn't overshoot the text.
				HighlightW = maximum(1.0f, Line.m_MessageFullWidth - FontSize() * 0.55f);
			}
			else if(Line.m_ReplyButtonAnchorValid)
			{
				const float TextEndX = Line.m_ReplyButtonAnchorX + ChatOpenOffsetX + BcLineXOffset;
				HighlightW = maximum(1.0f, (TextEndX - LineRenderX) + FontSize() * 0.12f);
			}
			// Map attachment cards stretch toward the chat edge; keep the reply
			// highlight at least as wide so the blue backdrop reaches the card.
			if(HasMapAttachment(Line))
				HighlightW = maximum(HighlightW, RealMsgPaddingX / 2.0f + ReactionAvailWidth);
			if(Line.m_aSettingsLinkWidth[OffsetType] > 0.0f)
				HighlightW = maximum(HighlightW, RealMsgPaddingX / 2.0f + Line.m_aSettingsLinkWidth[OffsetType] + RealMsgPaddingX / 2.0f);
			Graphics()->DrawRect(LineRenderX, LineRenderY, HighlightW, LineH, ColorRGBA(0.18f, 0.32f, 0.58f, 0.42f * Blend), IGraphics::CORNER_L, MessageRounding());

			const float AccentW = maximum(0.8f, FontSize() * 0.06f);
			Graphics()->DrawRect(LineRenderX, LineRenderY, AccentW, LineH, ColorRGBA(0.35f, 0.62f, 1.0f, 0.92f * Blend), IGraphics::CORNER_NONE, 0.0f);
		}

		if(Line.m_TextContainerIndex.Valid())
		{
			const ColorRGBA TextColor = TextRender()->DefaultTextColor().WithMultipliedAlpha(Blend);
			const ColorRGBA TextOutlineColor = TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(Blend);
			const float TextOffsetY = (LineRenderY + RealMsgPaddingY / 2.0f) - Line.m_TextYOffset;

			if(!g_Config.m_ClChatOld && Line.m_pManagedTeeRenderInfo != nullptr)
			{
				CTeeRenderInfo &TeeRenderInfo = Line.m_pManagedTeeRenderInfo->TeeRenderInfo();
				const int TeeSize = MessageTeeSize();
				TeeRenderInfo.m_Size = TeeSize;

				const CAnimState *pIdleState = CAnimState::GetIdle();
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeRenderInfo, OffsetToMid);
				const float MainLineCenterY = Line.m_TextYOffset + TextOffsetY + Line.m_ReplyQuoteHeight + FontSize() * 0.5f;
				vec2 TeeRenderPos(LineRenderX + (RealMsgPaddingX + TeeSize) / 2.0f, MainLineCenterY + OffsetToMid.y);
				RenderTools()->RenderTee(pIdleState, &TeeRenderInfo, EMOTE_NORMAL, vec2(1, 0.1f), TeeRenderPos, Blend);
			}

			bool HoveredName = false;
			if(LineNeedsNameColon(Line))
			{
				char aClientId[16] = "";
				if(g_Config.m_ClShowIds && Line.m_ClientId >= 0)
					GameClient()->FormatClientId(Line.m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);

				float NameRectX = LineRenderX + RealMsgPaddingX / 2.0f;
				if(LineNeedsTeePadding(Line))
					NameRectX += RealMsgPaddingTee;
				if(ShouldShowFriendMarker(Line))
					NameRectX += TextRender()->TextWidth(FontSize(), "♥ ");
				if(Line.m_aUClientRoomName[0])
				{
					char aRoomLabel[72];
					str_format(aRoomLabel, sizeof(aRoomLabel), "[%s] ", Line.m_aUClientRoomName);
					NameRectX += TextRender()->TextWidth(FontSize(), aRoomLabel);
				}
				NameRectX += TextRender()->TextWidth(FontSize(), aClientId);
				Line.m_NameRect.m_X = NameRectX;
				Line.m_NameRect.m_Y = Line.m_TextYOffset + TextOffsetY + Line.m_ReplyQuoteHeight;
				Line.m_NameRect.m_W = maximum(1.0f, TextRender()->TextWidth(FontSize(), Line.m_aName));
				Line.m_NameRect.m_H = FontSize();
				Line.m_NameRectValid = true;

				if((m_Mode != MODE_NONE || m_Show) && Line.m_aName[0] != '\0')
				{
					const SRenderRect &Nr = Line.m_NameRect;
					HoveredName =
						MousePos.x >= Nr.m_X && MousePos.x <= Nr.m_X + Nr.m_W &&
						MousePos.y >= Nr.m_Y && MousePos.y <= Nr.m_Y + Nr.m_H;
					if(HoveredName)
					{
						m_HoveredPlayerName = Line.m_aName;
						const float UnderlineY = Nr.m_Y + Nr.m_H + 0.35f;
						Graphics()->TextureClear();
						Graphics()->SetColor(ColorRGBA(0.5f, 0.75f, 1.0f, Blend));
						Graphics()->LinesBegin();
						const IGraphics::CLineItem Underline(Nr.m_X, UnderlineY, Nr.m_X + Nr.m_W, UnderlineY);
						Graphics()->LinesDraw(&Underline, 1);
						Graphics()->LinesEnd();
					}
				}
			}
			TextRender()->RenderTextContainer(Line.m_TextContainerIndex, TextColor, TextOutlineColor, ChatOpenOffsetX + BcLineXOffset, TextOffsetY);

			if(Line.m_HasReply && Line.m_ReplyQuoteHeight > 0.0f && Blend > 0.0f)
			{
				const float QuoteLineH = FontSize() * 0.85f;
				const int TeeSize = MessageTeeSize();
				const float TeeCenterX = LineRenderX + (RealMsgPaddingX + TeeSize) / 2.0f;

				float QuoteY = Line.m_TextYOffset + TextOffsetY;
				if(Line.m_ReplyQuoteRectValid)
					QuoteY = Line.m_ReplyQuoteRect.m_Y + TextOffsetY;

				RenderReplyQuoteConnector(Graphics(), TeeCenterX, QuoteY, QuoteLineH, Line.m_ReplyQuoteHeight, FontSize(), Blend);
			}

			// UClient read receipts: build the "<name> read" label for this line (if it is the
			// read marker of one or more peers), so we can reserve room for it before the
			// reply/settings buttons and draw it right after the message text.
			std::string ReadLabelText;
			int ReadCount = 0;
			if(Line.m_UClient && Line.m_UClientMessageId != UUID_ZEROED && !m_UcReadMarkers.empty())
			{
				std::string Representative;
				int BestOrder = -1;
				for(const auto &Entry : m_UcReadMarkers)
				{
					const SReadMarker &Marker = Entry.second;
					if(Marker.m_MessageId != Line.m_UClientMessageId)
						continue;
					++ReadCount;
					if(Marker.m_Order > BestOrder)
					{
						BestOrder = Marker.m_Order;
						Representative = Marker.m_Name;
					}
				}
				if(ReadCount == 1)
				{
					ReadLabelText = Representative + " read";
				}
				else if(ReadCount >= 2)
				{
					const int Others = ReadCount - 1;
					char aReadLabel[128];
					str_format(aReadLabel, sizeof(aReadLabel), "%s and %d %s read", Representative.c_str(), Others, Others == 1 ? "other" : "others");
					ReadLabelText = aReadLabel;
				}
			}
			const float ReadLabelFont = FontSize() * 0.6f;
			const float ReadLabelW = ReadCount > 0 ? TextRender()->TextWidth(ReadLabelFont, ReadLabelText.c_str()) : 0.0f;
			const float ReadLabelSpacing = FontSize() * 0.5f;

			const bool ShowReplyActions = CanShowReplyButton(Line) && Line.m_ReplyButtonAnchorValid && !IsPendingReplyTarget;
			const bool ShowSettingsShortcut = CanShowSettingsShortcut(Line) && Line.m_ReplyButtonAnchorValid;

			// The line rect doubles as the right-click target for the reaction picker, so it has to
			// exist on lines that show no buttons at all: server messages carry no name, which rules
			// out the reply button.
			if(ShowReplyActions || ShowSettingsShortcut || CanReactToLine(Line))
			{
				Line.m_LineRect.m_X = LineRenderX;
				Line.m_LineRect.m_Y = LineRenderY;
				Line.m_LineRect.m_W = maximum(1.0f, ChatWidth());
				Line.m_LineRect.m_H = maximum(Line.m_aYOffset[OffsetType], FontSize() + RealMsgPaddingY);
				Line.m_LineRectValid = true;
			}

			if(ShowReplyActions || ShowSettingsShortcut)
			{
				const bool HoveredLine = MousePos.x >= Line.m_LineRect.m_X && MousePos.x <= Line.m_LineRect.m_X + Line.m_LineRect.m_W &&
					MousePos.y >= Line.m_LineRect.m_Y && MousePos.y <= Line.m_LineRect.m_Y + Line.m_LineRect.m_H;
				const float BtnSize = maximum(6.5f, FontSize() * 0.34f);
				const float BtnGap = FontSize() * 0.6f;
				const float BtnSpacing = FontSize() * 0.18f;
				// Keep the read label (drawn separately below) leftmost; buttons follow it.
				float NextX = Line.m_ReplyButtonAnchorX + ChatOpenOffsetX + BcLineXOffset + BtnGap +
					(ReadCount > 0 ? ReadLabelW + ReadLabelSpacing : 0.0f);
				const float BtnY = Line.m_ReplyButtonAnchorY + TextOffsetY + maximum(0.0f, (FontSize() - BtnSize) * 0.5f) - FontSize() * 0.08f;

				// Settings shortcut stays visible whenever chat is open (hint text refers to it).
				if(ShowSettingsShortcut)
				{
					const float ShortcutX = NextX;
					Line.m_SettingsShortcutRect.m_X = ShortcutX;
					Line.m_SettingsShortcutRect.m_Y = BtnY;
					Line.m_SettingsShortcutRect.m_W = BtnSize;
					Line.m_SettingsShortcutRect.m_H = BtnSize;
					Line.m_SettingsShortcutRectValid = true;
					m_HoveredSettingsShortcutLineIndex = LineIndex;
					const bool HoveredShortcut = MousePos.x >= ShortcutX && MousePos.x <= ShortcutX + BtnSize &&
						MousePos.y >= BtnY && MousePos.y <= BtnY + BtnSize;
					const ColorRGBA ShortcutColor = HoveredShortcut ? ColorRGBA(0.22f, 0.48f, 0.42f, 0.90f * Blend) : ColorRGBA(0.14f, 0.14f, 0.14f, 0.72f * Blend);
					CUIRect ShortcutRect(ShortcutX, BtnY, BtnSize, BtnSize);
					ShortcutRect.Draw(ShortcutColor, IGraphics::CORNER_ALL, maximum(2.0f, BtnSize * 0.24f));
					CUIRect ShortcutIcon;
					ShortcutRect.Margin(0.5f, &ShortcutIcon);
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
					TextRender()->TextColor(0.92f, 0.92f, 0.92f, Blend);
					Ui()->DoLabel(&ShortcutIcon, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, ShortcutIcon.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
					TextRender()->SetRenderFlags(0);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
					NextX += BtnSize + BtnSpacing;
				}

				// Reply button remains hover-only.
				if(ShowReplyActions && (HoveredLine || HoveredName))
				{
					const float BtnX = NextX;
					Line.m_ReplyButtonRect.m_X = BtnX;
					Line.m_ReplyButtonRect.m_Y = BtnY;
					Line.m_ReplyButtonRect.m_W = BtnSize;
					Line.m_ReplyButtonRect.m_H = BtnSize;
					Line.m_ReplyButtonRectValid = true;
					m_HoveredReplyLineIndex = LineIndex;

					const bool HoveredBtn = MousePos.x >= BtnX && MousePos.x <= BtnX + BtnSize &&
						MousePos.y >= BtnY && MousePos.y <= BtnY + BtnSize;
					const ColorRGBA BtnColor = HoveredBtn ? ColorRGBA(0.30f, 0.38f, 0.58f, 0.90f * Blend) : ColorRGBA(0.14f, 0.14f, 0.14f, 0.72f * Blend);
					CUIRect BtnRect(BtnX, BtnY, BtnSize, BtnSize);
					BtnRect.Draw(BtnColor, IGraphics::CORNER_ALL, maximum(2.0f, BtnSize * 0.24f));

					CUIRect IconRect;
					BtnRect.Margin(0.5f, &IconRect);
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
					TextRender()->TextColor(0.92f, 0.92f, 0.92f, Blend);
					Ui()->DoLabel(&IconRect, FontIcon::REPLY, IconRect.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
					TextRender()->SetRenderFlags(0);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}

			// UClient: publish the message bubble so next frame's hover test can decide whether to
			// open a row for the audience note, and draw that note once the row exists. Restricted
			// to the bubble rather than the full chat column so the note only follows the message
			// the pointer is actually on.
			{
				char aScopeNote[192];
				if(UClientScopeNoteText(Line, aScopeNote, sizeof(aScopeNote)))
				{
					Line.m_ScopeHoverRect.m_X = LineRenderX;
					Line.m_ScopeHoverRect.m_Y = LineRenderY;
					Line.m_ScopeHoverRect.m_W = maximum(1.0f, Line.m_MessageFullWidth > 0.0f ? Line.m_MessageFullWidth : ChatWidth());
					Line.m_ScopeHoverRect.m_H = LineH;
					Line.m_ScopeHoverRectValid = true;

					if(Line.m_aScopeNoteHeight[OffsetType] > 0.0f)
					{
						const float NoteFont = ScopeNoteFontSize();
						const float NoteX = LineRenderX + RealMsgPaddingX / 2.0f + (LineNeedsTeePadding(Line) ? RealMsgPaddingTee : 0.0f);
						// Bottom of the bubble, inside the padding the layout already leaves there.
						const float NoteY = LineRenderY + LineH - RealMsgPaddingY / 2.0f - Line.m_aScopeNoteHeight[OffsetType];
						CTextCursor NoteCursor;
						NoteCursor.SetPosition(vec2(NoteX, NoteY));
						NoteCursor.m_FontSize = NoteFont;
						TextRender()->TextColor(0.62f, 0.66f, 0.72f, Blend);
						TextRender()->TextEx(&NoteCursor, aScopeNote);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
					}
				}
			}

			// UClient read receipts: draw the "<name> read" label at the end of the message.
			// It sits just past the text (leftmost of the action buttons) and is hoverable to
			// reveal the full list of readers in a tooltip below.
			if(ReadCount > 0)
			{
				const float LabelX = Line.m_ReplyButtonAnchorX + ChatOpenOffsetX + BcLineXOffset + FontSize() * 0.6f;
				// Bottom-align the small read label to the message line (sits low, not centered).
				const float LabelY = Line.m_ReplyButtonAnchorY + TextOffsetY + maximum(0.0f, FontSize() - ReadLabelFont);
				Line.m_ReadLabelRect.m_X = LabelX;
				Line.m_ReadLabelRect.m_Y = LabelY;
				Line.m_ReadLabelRect.m_W = maximum(1.0f, ReadLabelW);
				Line.m_ReadLabelRect.m_H = ReadLabelFont;
				Line.m_ReadLabelRectValid = true;

				const bool HoveredRead = ChatInteractionActive &&
					MousePos.x >= LabelX && MousePos.x <= LabelX + ReadLabelW &&
					MousePos.y >= LabelY && MousePos.y <= LabelY + ReadLabelFont;
				if(HoveredRead)
				{
					m_HoveredReadLineIndex = LineIndex;
					m_HoveredReadRect = Line.m_ReadLabelRect;
				}

				CTextCursor ReadCursor;
				ReadCursor.SetPosition(vec2(LabelX, LabelY));
				ReadCursor.m_FontSize = ReadLabelFont;
				TextRender()->TextColor(0.62f, 0.66f, 0.72f, Blend);
				TextRender()->TextEx(&ReadCursor, ReadLabelText.c_str());
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}

			if((m_Mode != MODE_NONE || m_Show) && !Line.m_vLinks.empty())
			{
				const float OffX = ChatOpenOffsetX + BcLineXOffset;
				const float OffY = TextOffsetY;
				int HoveredLinkPart = -1;
				for(size_t Li = 0; Li < Line.m_vLinkBounds.size(); ++Li)
				{
					const STextBoundingBox Bounds = TightenLinkHoverBounds(
						{Line.m_vLinkBounds[Li].m_X + OffX, Line.m_vLinkBounds[Li].m_Y + OffY,
						Line.m_vLinkBounds[Li].m_W, Line.m_vLinkBounds[Li].m_H},
						Li < Line.m_vLinkFontSizes.size() ? Line.m_vLinkFontSizes[Li] : FontSize());
					if(MousePos.x >= Bounds.m_X && MousePos.x <= Bounds.m_X + Bounds.m_W &&
						MousePos.y >= Bounds.m_Y && MousePos.y <= Bounds.m_Y + Bounds.m_H)
					{
						HoveredLinkPart = (int)Li;
						break;
					}
				}
				if(HoveredLinkPart >= 0)
				{
					m_HoveredLink = Line.m_vLinks[HoveredLinkPart];
					m_HoveredLinkAlwaysConfirm = HoveredLinkPart < (int)Line.m_vLinkAlwaysConfirm.size() && Line.m_vLinkAlwaysConfirm[HoveredLinkPart];
					const int HoveredGroup = HoveredLinkPart < (int)Line.m_vLinkGroups.size() ?
						Line.m_vLinkGroups[HoveredLinkPart] : HoveredLinkPart;
					Graphics()->TextureClear();
					Graphics()->SetColor(ChatLinkColor().WithMultipliedAlpha(Blend));
					Graphics()->LinesBegin();
					for(size_t Li = 0; Li < Line.m_vLinkBounds.size(); ++Li)
					{
						const int LinkGroup = Li < Line.m_vLinkGroups.size() ? Line.m_vLinkGroups[Li] : (int)Li;
						if(LinkGroup != HoveredGroup)
							continue;
						const STextBoundingBox Bounds = TightenLinkHoverBounds(
							{Line.m_vLinkBounds[Li].m_X + OffX, Line.m_vLinkBounds[Li].m_Y + OffY,
								Line.m_vLinkBounds[Li].m_W, Line.m_vLinkBounds[Li].m_H},
							Li < Line.m_vLinkFontSizes.size() ? Line.m_vLinkFontSizes[Li] : FontSize());
						const float UnderlineY = Bounds.m_Y + Bounds.m_H + 0.35f;
						const IGraphics::CLineItem Underline(Bounds.m_X, UnderlineY, Bounds.m_X + Bounds.m_W, UnderlineY);
						Graphics()->LinesDraw(&Underline, 1);
					}
					Graphics()->LinesEnd();
				}
			}

			// UClient server-join: the server-name span is a clickable link. Draw the hover
			// underline and record the hovered rect for the click handler + hint tooltip.
			if(Line.m_HasServerJoinLink && Line.m_ServerJoinBoundsValid)
			{
				const float OffX = ChatOpenOffsetX + BcLineXOffset;
				const float OffY = TextOffsetY;
				const SRenderRect JoinRect = {
					Line.m_ServerJoinBounds.m_X + OffX, Line.m_ServerJoinBounds.m_Y + OffY,
					Line.m_ServerJoinBounds.m_W, Line.m_ServerJoinBounds.m_H};
				const bool HoveredJoin = (m_Mode != MODE_NONE || m_Show) &&
					MousePos.x >= JoinRect.m_X && MousePos.x <= JoinRect.m_X + JoinRect.m_W &&
					MousePos.y >= JoinRect.m_Y && MousePos.y <= JoinRect.m_Y + JoinRect.m_H;
				if(HoveredJoin)
				{
					m_HoveredServerJoinLineIndex = LineIndex;
					m_HoveredServerJoinRect = JoinRect;
					const float UnderlineY = JoinRect.m_Y + JoinRect.m_H + 0.35f;
					Graphics()->TextureClear();
					Graphics()->SetColor(ChatLinkColor().WithMultipliedAlpha(Blend));
					Graphics()->LinesBegin();
					const IGraphics::CLineItem Underline(JoinRect.m_X, UnderlineY, JoinRect.m_X + JoinRect.m_W, UnderlineY);
					Graphics()->LinesDraw(&Underline, 1);
					Graphics()->LinesEnd();
				}
			}

			if(Line.m_TranslateRectValid || Line.m_TranslateLanguageRectValid)
			{
				const SRenderRect ActualTranslateRect = {
					Line.m_TranslateRect.m_X + ChatOpenOffsetX + BcLineXOffset,
					Line.m_TranslateRect.m_Y + TextOffsetY,
					Line.m_TranslateRect.m_W,
					Line.m_TranslateRect.m_H};
				Line.m_TranslateRect = ActualTranslateRect;
				CUIRect TranslateRectUi = {
					ActualTranslateRect.m_X / UiToChatScale.x,
					ActualTranslateRect.m_Y / UiToChatScale.y,
					ActualTranslateRect.m_W / UiToChatScale.x,
					ActualTranslateRect.m_H / UiToChatScale.y};
				CUIRect LanguageRectUi = {0.0f, 0.0f, 0.0f, 0.0f};
				const bool HoveredTranslatedMessage = Line.m_TranslateRectValid &&
					(Ui()->MouseHovered(&TranslateRectUi) ||
						(MousePos.x >= ActualTranslateRect.m_X && MousePos.x <= ActualTranslateRect.m_X + ActualTranslateRect.m_W &&
							MousePos.y >= ActualTranslateRect.m_Y && MousePos.y <= ActualTranslateRect.m_Y + ActualTranslateRect.m_H));

				bool HoveredLanguageTag = false;
				if(Line.m_TranslateLanguageRectValid)
				{
					const SRenderRect ActualLanguageRect = {
						Line.m_TranslateLanguageRect.m_X + ChatOpenOffsetX + BcLineXOffset,
						Line.m_TranslateLanguageRect.m_Y + TextOffsetY,
						Line.m_TranslateLanguageRect.m_W,
						Line.m_TranslateLanguageRect.m_H};
					Line.m_TranslateLanguageRect = ActualLanguageRect;
					LanguageRectUi = {
						ActualLanguageRect.m_X / UiToChatScale.x,
						ActualLanguageRect.m_Y / UiToChatScale.y,
						ActualLanguageRect.m_W / UiToChatScale.x,
						ActualLanguageRect.m_H / UiToChatScale.y};
					HoveredLanguageTag = Ui()->MouseHovered(&LanguageRectUi) ||
						(MousePos.x >= ActualLanguageRect.m_X && MousePos.x <= ActualLanguageRect.m_X + ActualLanguageRect.m_W &&
							MousePos.y >= ActualLanguageRect.m_Y && MousePos.y <= ActualLanguageRect.m_Y + ActualLanguageRect.m_H);
				}

				if(HoveredTranslatedMessage || HoveredLanguageTag)
					Ui()->SetHotItem((const void *)&Line.m_TranslateRect);

				if(HoveredTranslatedMessage || HoveredLanguageTag)
					HoveredTranslateLineIndex = LineIndex;
			}

			if(Line.m_SelectionStart >= 0 && Line.m_SelectionEnd >= 0 && Line.m_SelectionStart != Line.m_SelectionEnd)
			{
				HasChatSelection = true;
				if(m_WantsSelectionCopy)
				{
					const std::string PlainText = BuildPlainTextLine(Line);
					const int SelectionMin = minimum(Line.m_SelectionStart, Line.m_SelectionEnd);
					const int SelectionMax = maximum(Line.m_SelectionStart, Line.m_SelectionEnd);
					const size_t OffUTF8Start = str_utf8_offset_chars_to_bytes(PlainText.c_str(), SelectionMin);
					const size_t OffUTF8End = str_utf8_offset_chars_to_bytes(PlainText.c_str(), SelectionMax);
					const bool HasNewLine = !SelectionString.empty();
					SelectionString.insert(0, PlainText.substr(OffUTF8Start, OffUTF8End - OffUTF8Start) + (HasNewLine ? "\n" : ""));
				}
			}

				// UClient: draw emoji reaction pills below the message (and any media preview).
				if(!Line.m_vReactions.empty())
				{
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					const bool HasReactionMedia = ShouldDisplayMediaSlot(Line) && Line.m_aMediaPreviewWidth[OffsetType] > 0.0f && Line.m_aMediaPreviewHeight[OffsetType] > 0.0f;
					float ReactionOriginY = Line.m_TextYOffset + TextOffsetY + Line.m_aTextHeight[OffsetType];
					if(HasReactionMedia)
						ReactionOriginY += FontSize() * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
					if(HasMapAttachment(Line) && Line.m_aMapCardHeight[OffsetType] > 0.0f)
						ReactionOriginY += FontSize() * 0.25f + Line.m_aMapCardHeight[OffsetType];
					if(Line.m_HasSettingsLink && Line.m_aSettingsLinkHeight[OffsetType] > 0.0f)
						ReactionOriginY += FontSize() * 0.25f + Line.m_aSettingsLinkHeight[OffsetType];
					// Align with the message text, which is only inset by the tee when there is one
					// (server messages have no name and therefore no tee).
					const float ReactionOriginX = LineRenderX + RealMsgPaddingX / 2.0f + (LineNeedsTeePadding(Line) ? RealMsgPaddingTee : 0.0f);

					Line.m_vReactionRects.clear();
					LayoutReactionRow(Line, FontSize(), ReactionAvailWidth, ReactionOriginX, ReactionOriginY, &Line.m_vReactionRects);
					Line.m_ReactionRectsValid = ChatInteractionActive;

					const int LocalId = GameClient()->m_Snap.m_LocalClientId;
					const float PillFont = FontSize() * 0.85f;
					const float PadX = FontSize() * 0.34f;
					const size_t ReactionCount = minimum(Line.m_vReactionRects.size(), Line.m_vReactions.size());
					for(size_t r = 0; r < ReactionCount; ++r)
					{
						const CLine::SReaction &Reaction = Line.m_vReactions[r];
						const SRenderRect &PillR = Line.m_vReactionRects[r];
						bool Mine = false;
						for(int Id : Reaction.m_vReactorClientIds)
						{
							if(Id == LocalId)
							{
								Mine = true;
								break;
							}
						}
						const bool HoveredPill = ChatInteractionActive &&
							MousePos.x >= PillR.m_X && MousePos.x <= PillR.m_X + PillR.m_W &&
							MousePos.y >= PillR.m_Y && MousePos.y <= PillR.m_Y + PillR.m_H;
						if(HoveredPill)
						{
							m_HoveredReactionLineIndex = LineIndex;
							m_HoveredReactionIndex = (int)r;
							m_HoveredReactionRect = PillR;
						}
						ColorRGBA Fill;
						if(Mine)
							Fill = HoveredPill ? ColorRGBA(0.30f, 0.46f, 0.74f, 0.95f * Blend) : ColorRGBA(0.22f, 0.36f, 0.62f, 0.88f * Blend);
						else
							Fill = HoveredPill ? ColorRGBA(0.24f, 0.24f, 0.28f, 0.9f * Blend) : ColorRGBA(0.16f, 0.16f, 0.18f, 0.8f * Blend);

						CUIRect PillRect(PillR.m_X, PillR.m_Y, PillR.m_W, PillR.m_H);
						PillRect.Draw(Fill, IGraphics::CORNER_ALL, PillR.m_H * 0.35f);

						char aReactionCount[12];
						str_format(aReactionCount, sizeof(aReactionCount), "%d", (int)Reaction.m_vReactorClientIds.size());
						CTextCursor PillCursor;
						PillCursor.SetPosition(vec2(PillR.m_X + PadX, PillR.m_Y + (PillR.m_H - PillFont) / 2.0f));
						PillCursor.m_FontSize = PillFont;
						TextRender()->TextColor(1.0f, 1.0f, 1.0f, Blend);
						TextRender()->TextEx(&PillCursor, Reaction.m_aEmoji);
						PillCursor.m_X += FontSize() * 0.22f;
						TextRender()->TextEx(&PillCursor, aReactionCount);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
					}
				}

				const bool ShowMediaSlot = ShouldDisplayMediaSlot(Line);
				const bool HideMediaPreview = ShouldHideMediaPreview(Line);
				const bool HideNsfwMedia = ShouldHideNsfwMedia(Line);
				const bool HasMediaPreview = Line.m_aMediaPreviewWidth[OffsetType] > 0.0f && Line.m_aMediaPreviewHeight[OffsetType] > 0.0f;
				const float PreviewX = LineRenderX + RealMsgPaddingX / 2.0f;
				const float PreviewY = Line.m_TextYOffset + TextOffsetY + Line.m_aTextHeight[OffsetType] + FontSize() * 0.4f;
				const float PreviewW = Line.m_aMediaPreviewWidth[OffsetType];
				const float PreviewH = Line.m_aMediaPreviewHeight[OffsetType];
				Line.m_MediaPreviewRectValid = false;
				if(ShowMediaSlot && HasMediaPreview)
				{
					auto DrawMediaPreviewFrame = [&](ColorRGBA FillColor, float &InnerPreviewX, float &InnerPreviewY, float &InnerPreviewW, float &InnerPreviewH, float &InnerPreviewRounding) {
					const float PreviewBorder = maximum(0.35f, FontSize() * 0.025f);
						const float PreviewRounding = minimum(minimum(PreviewW, PreviewH) / 2.0f, maximum(4.0f, FontSize() * 0.55f));
						Graphics()->DrawRect(PreviewX, PreviewY, PreviewW, PreviewH, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * Blend), IGraphics::CORNER_ALL, PreviewRounding);
						InnerPreviewX = PreviewX + PreviewBorder;
						InnerPreviewY = PreviewY + PreviewBorder;
						InnerPreviewW = maximum(1.0f, PreviewW - PreviewBorder * 2.0f);
						InnerPreviewH = maximum(1.0f, PreviewH - PreviewBorder * 2.0f);
						InnerPreviewRounding = maximum(0.0f, PreviewRounding - PreviewBorder);
						Graphics()->DrawRect(InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, FillColor, IGraphics::CORNER_ALL, InnerPreviewRounding);
					};

					float InnerPreviewX = PreviewX;
					float InnerPreviewY = PreviewY;
					float InnerPreviewW = PreviewW;
					float InnerPreviewH = PreviewH;
					float InnerPreviewRounding = 0.0f;

					if(HideMediaPreview || HideNsfwMedia)
					{
						const char *pHiddenLabel = HideNsfwMedia ? "NSFW content hidden" : "hidden media";
						DrawMediaPreviewFrame(ColorRGBA(0.10f, 0.10f, 0.10f, 0.82f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);

						CTextCursor HiddenCursor;
						const float HiddenFontSize = FontSize() * 0.72f;
						const float HiddenLabelWidth = TextRender()->TextWidth(HiddenFontSize, pHiddenLabel);
						HiddenCursor.SetPosition(vec2(InnerPreviewX + maximum(FontSize() * 0.35f, (InnerPreviewW - HiddenLabelWidth) / 2.0f), InnerPreviewY + maximum(FontSize() * 0.25f, (InnerPreviewH - HiddenFontSize) / 2.0f)));
						HiddenCursor.m_FontSize = HiddenFontSize;
						TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f * Blend);
						TextRender()->TextEx(&HiddenCursor, pHiddenLabel);
						TextRender()->TextColor(TextRender()->DefaultTextColor());

						Line.m_MediaRetryRectValid = false;
						if(ChatInteractionActive)
						{
							Line.m_MediaPreviewRect.m_X = PreviewX;
							Line.m_MediaPreviewRect.m_Y = PreviewY;
							Line.m_MediaPreviewRect.m_W = PreviewW;
							Line.m_MediaPreviewRect.m_H = PreviewH;
							Line.m_MediaPreviewRectValid = true;
						}
					}
					else if(Line.m_MediaState == EMediaState::READY)
					{
						IGraphics::CTextureHandle MediaTexture;
						if(GetCurrentFrameTexture(Line, MediaTexture))
						{
							DrawMediaPreviewFrame(ColorRGBA(0.05f, 0.05f, 0.05f, 0.18f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);
							DrawRoundedMediaPreview(Graphics(), MediaTexture, InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding, Blend);

							Line.m_MediaRetryRectValid = false;
							if(ChatInteractionActive && g_Config.m_BcChatMediaViewer)
							{
								Line.m_MediaPreviewRect.m_X = PreviewX;
								Line.m_MediaPreviewRect.m_Y = PreviewY;
								Line.m_MediaPreviewRect.m_W = PreviewW;
								Line.m_MediaPreviewRect.m_H = PreviewH;
								Line.m_MediaPreviewRectValid = true;
							}
						}
					}
					else if(Line.m_MediaState == EMediaState::QUEUED || Line.m_MediaState == EMediaState::LOADING || Line.m_MediaState == EMediaState::DECODING)
					{
						DrawMediaPreviewFrame(ColorRGBA(0.12f, 0.12f, 0.12f, 0.75f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);

						CTextCursor LoadingCursor;
						LoadingCursor.SetPosition(vec2(InnerPreviewX + FontSize() * 0.35f, InnerPreviewY + InnerPreviewH * 0.15f));
						LoadingCursor.m_FontSize = FontSize() * 0.75f;
						TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.8f * Blend);
						TextRender()->TextEx(&LoadingCursor, "Loading media...");
						TextRender()->TextColor(TextRender()->DefaultTextColor());
						Line.m_MediaRetryRectValid = false;
					}
					else if(Line.m_MediaState == EMediaState::FAILED)
					{
						const bool CanRetry = Line.m_MediaRetryCount < CHAT_MEDIA_MAX_RETRIES && !Line.m_vMediaCandidates.empty();
						DrawMediaPreviewFrame(ColorRGBA(0.23f, 0.10f, 0.10f, 0.82f * Blend), InnerPreviewX, InnerPreviewY, InnerPreviewW, InnerPreviewH, InnerPreviewRounding);

						CTextCursor FailedCursor;
						FailedCursor.SetPosition(vec2(InnerPreviewX + FontSize() * 0.35f, InnerPreviewY + FontSize() * 0.25f));
						FailedCursor.m_FontSize = FontSize() * 0.70f;
						TextRender()->TextColor(1.0f, 0.85f, 0.85f, 0.95f * Blend);
						TextRender()->TextEx(&FailedCursor, CanRetry ? "Media preview unavailable" : "Media preview unavailable (retry limit reached)");

						const char *pRetryLabel = CanRetry ? "Retry" : "Retry limit reached";
						const float RetryFont = FontSize() * 0.66f;
						const float RetryLabelWidth = TextRender()->TextWidth(RetryFont, pRetryLabel);
						const float RetryW = maximum(FontSize() * 4.2f, RetryLabelWidth + FontSize() * 0.8f);
						const float RetryH = maximum(FontSize() * 0.95f, 12.0f);
						const float RetryX = InnerPreviewX + InnerPreviewW - RetryW - FontSize() * 0.25f;
						const float RetryY = InnerPreviewY + InnerPreviewH - RetryH - FontSize() * 0.25f;

						Graphics()->DrawRect(RetryX, RetryY, RetryW, RetryH, CanRetry ? ColorRGBA(0.86f, 0.28f, 0.28f, 0.95f * Blend) : ColorRGBA(0.35f, 0.35f, 0.35f, 0.75f * Blend), IGraphics::CORNER_ALL, maximum(2.0f, RetryH * 0.3f));

						CTextCursor RetryCursor;
						RetryCursor.SetPosition(vec2(RetryX + (RetryW - RetryLabelWidth) / 2.0f, RetryY + (RetryH - RetryFont) / 2.0f));
						RetryCursor.m_FontSize = RetryFont;
						TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f * Blend);
						TextRender()->TextEx(&RetryCursor, pRetryLabel);
						TextRender()->TextColor(TextRender()->DefaultTextColor());

						Line.m_MediaRetryRectValid = CanRetry;
						if(CanRetry)
						{
							Line.m_MediaRetryRect.m_X = RetryX;
							Line.m_MediaRetryRect.m_Y = RetryY;
							Line.m_MediaRetryRect.m_W = RetryW;
							Line.m_MediaRetryRect.m_H = RetryH;
						}
					}
				}

				// UClient: Discord-style .map attachment card
				Line.m_MapCardRectValid = false;
				Line.m_MapDownloadBtnRectValid = false;
				if(HasMapAttachment(Line) && Line.m_aMapCardHeight[OffsetType] > 0.0f)
				{
					const bool HasMediaAbove = ShowMediaSlot && HasMediaPreview;
					float CardY = Line.m_TextYOffset + TextOffsetY + Line.m_aTextHeight[OffsetType] + FontSize() * 0.25f;
					if(HasMediaAbove)
						CardY += FontSize() * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
					const float CardX = LineRenderX + RealMsgPaddingX / 2.0f;
					const float CardH = Line.m_aMapCardHeight[OffsetType];
					const float NameFont = FontSize() * 0.78f;
					const float SizeFont = FontSize() * 0.66f;
					const char *pFileName = Line.m_aMapFileName[0] != '\0' ? Line.m_aMapFileName : "map.map";

					char aSizeLabel[64];
					if(Line.m_MapFileSize > 0)
					{
						const double Mb = (double)Line.m_MapFileSize / (1024.0 * 1024.0);
						if(Mb >= 1.0)
							str_format(aSizeLabel, sizeof(aSizeLabel), "%.1f MB", Mb);
						else
							str_format(aSizeLabel, sizeof(aSizeLabel), "%.0f KB", (double)Line.m_MapFileSize / 1024.0);
					}
					else
						str_copy(aSizeLabel, "Map file");

					const float IconPad = FontSize() * 0.28f;
					const float IconSize = FontSize() * 0.95f;
					const float TextGap = FontSize() * 0.28f;
					const float BtnReserve = FontSize() * 1.05f;
					const float NameW = TextRender()->TextWidth(NameFont, pFileName);
					const float SizeW = TextRender()->TextWidth(SizeFont, aSizeLabel);
					const float ContentW = IconPad + IconSize + TextGap + maximum(NameW, SizeW) + BtnReserve;
					const float CardW = minimum(ReactionAvailWidth, maximum(ContentW, ReactionAvailWidth * 0.92f));
					const float CardRounding = maximum(3.0f, FontSize() * 0.3f);

					const ColorRGBA CardBg = IsPendingReplyTarget ?
						ColorRGBA(0.16f, 0.30f, 0.52f, 0.92f * Blend) :
						ColorRGBA(0.13f, 0.14f, 0.16f, 0.95f * Blend);
					Graphics()->DrawRect(CardX, CardY, CardW, CardH, CardBg, IGraphics::CORNER_ALL, CardRounding);
					if(IsPendingReplyTarget)
					{
						Graphics()->DrawRect(CardX, CardY, CardW, CardH, ColorRGBA(0.35f, 0.62f, 1.0f, 0.18f * Blend), IGraphics::CORNER_ALL, CardRounding);
					}

					CUIRect IconRect(CardX + IconPad, CardY + (CardH - IconSize) / 2.0f, IconSize, IconSize);
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					TextRender()->TextColor(0.75f, 0.78f, 0.85f, Blend);
					Ui()->DoLabel(&IconRect, FontIcon::FILE, IconSize * CUi::ms_FontmodHeight, TEXTALIGN_MC);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

					const float TextX = CardX + IconPad + IconSize + TextGap;
					const float TextMaxW = maximum(8.0f, CardW - (TextX - CardX) - BtnReserve * 0.85f);
					CTextCursor NameCursor;
					NameCursor.SetPosition(vec2(TextX, CardY + CardH * 0.12f));
					NameCursor.m_FontSize = NameFont;
					NameCursor.m_LineWidth = TextMaxW;
					TextRender()->TextColor(0.35f, 0.62f, 0.95f, Blend);
					TextRender()->TextEx(&NameCursor, pFileName);

					CTextCursor SizeCursor;
					SizeCursor.SetPosition(vec2(TextX, CardY + CardH * 0.52f));
					SizeCursor.m_FontSize = SizeFont;
					SizeCursor.m_LineWidth = TextMaxW;
					TextRender()->TextColor(0.55f, 0.57f, 0.62f, Blend);
					TextRender()->TextEx(&SizeCursor, aSizeLabel);
					TextRender()->TextColor(TextRender()->DefaultTextColor());

					const bool HoverCard = ChatInteractionActive &&
						MousePos.x >= CardX && MousePos.x <= CardX + CardW &&
						MousePos.y >= CardY && MousePos.y <= CardY + CardH;
					if(HoverCard)
					{
						const float Btn = maximum(8.0f, FontSize() * 0.72f);
						// Upper-right of the card, inset enough to clear the rounded corner.
						const float CornerInset = maximum(3.5f, FontSize() * 0.38f);
						const float BtnX = CardX + CardW - Btn - CornerInset;
						const float BtnY = CardY + CornerInset;
						const bool HoverBtn = MousePos.x >= BtnX && MousePos.x <= BtnX + Btn &&
							MousePos.y >= BtnY && MousePos.y <= BtnY + Btn;
						Graphics()->DrawRect(BtnX, BtnY, Btn, Btn,
							HoverBtn ? ColorRGBA(0.25f, 0.28f, 0.34f, 0.95f * Blend) : ColorRGBA(0.18f, 0.20f, 0.24f, 0.9f * Blend),
							IGraphics::CORNER_ALL, Btn * 0.22f);
						CUIRect DlIcon(BtnX, BtnY, Btn, Btn);
						TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
						TextRender()->TextColor(0.9f, 0.92f, 0.96f, Blend);
						Ui()->DoLabel(&DlIcon, FontIcon::ANGLE_DOWN, Btn * 0.65f * CUi::ms_FontmodHeight, TEXTALIGN_MC);
						TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
						Line.m_MapDownloadBtnRect.m_X = BtnX;
						Line.m_MapDownloadBtnRect.m_Y = BtnY;
						Line.m_MapDownloadBtnRect.m_W = Btn;
						Line.m_MapDownloadBtnRect.m_H = Btn;
						Line.m_MapDownloadBtnRectValid = true;
					}

					if(ChatInteractionActive)
					{
						Line.m_MapCardRect.m_X = CardX;
						Line.m_MapCardRect.m_Y = CardY;
						Line.m_MapCardRect.m_W = CardW;
						Line.m_MapCardRect.m_H = CardH;
						Line.m_MapCardRectValid = true;
					}
				}

				Line.m_SettingsLinkRectValid = false;
				if(Line.m_HasSettingsLink && Line.m_aSettingsLinkHeight[OffsetType] > 0.0f)
				{
					float CardY = Line.m_TextYOffset + TextOffsetY + Line.m_aTextHeight[OffsetType] + FontSize() * 0.25f;
					if(ShouldDisplayMediaSlot(Line) && Line.m_aMediaPreviewHeight[OffsetType] > 0.0f)
						CardY += FontSize() * 0.4f + Line.m_aMediaPreviewHeight[OffsetType];
					if(HasMapAttachment(Line) && Line.m_aMapCardHeight[OffsetType] > 0.0f)
						CardY += FontSize() * 0.25f + Line.m_aMapCardHeight[OffsetType];
					const float CardX = LineRenderX + RealMsgPaddingX / 2.0f;
					// Scoreboard keeps the compact column; otherwise let page/settings
					// cards use the full chat width (minus side padding) so hints like
					// "Press the shortcut button to go to settings." are not ellipsized.
					const float SettingsMaxWidth = IsScoreBoardOpen ?
						ReactionAvailWidth :
						maximum(ReactionAvailWidth, ChatWidth() - RealMsgPaddingX);
					RenderSettingsLinkBubble(Line, CardX, CardY, SettingsMaxWidth, FontSize(), Blend);
				}
		}
	}

	// UClient read receipts: advance our own read marker to the newest other-authored UClient
	// message that was visible this frame. Broadcast once whenever the marker actually moves
	// forward so peers can update their "read" labels.
	if(MaxVisibleReadSeq > m_UcLocalReadMarkerSeq && MaxVisibleReadMsgId != UUID_ZEROED && IsReadingChat())
	{
		m_UcLocalReadMarkerSeq = MaxVisibleReadSeq;
		m_UcLocalReadMarkerMsgId = MaxVisibleReadMsgId;
		GameClient()->m_ClientIndicator.SendChatReadMarker(MaxVisibleReadMsgId);
	}

	// UClient read receipts: tooltip listing everyone who has read up to the hovered message.
	if(m_HoveredReadLineIndex >= 0 && m_HoveredReadLineIndex < MAX_LINES)
	{
		const CLine &Line = m_aLines[m_HoveredReadLineIndex];
		std::vector<std::string> vReaderNames;
		if(Line.m_UClientMessageId != UUID_ZEROED)
		{
			for(const auto &Entry : m_UcReadMarkers)
			{
				if(Entry.second.m_MessageId == Line.m_UClientMessageId)
					vReaderNames.push_back(Entry.second.m_Name);
			}
		}
		if(!vReaderNames.empty())
		{
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			TextRender()->SetRenderFlags(0);

			const float TipFont = maximum(4.5f, FontSize() * 0.85f);
			const float PadX = TipFont * 0.6f;
			const float PadY = TipFont * 0.45f;
			const float LineH = TipFont * 1.32f;
			const float HeaderGap = TipFont * 0.35f;

			const size_t Total = vReaderNames.size();
			const size_t MaxShown = 14;
			const size_t Shown = minimum(Total, MaxShown);
			char aMore[32] = "";
			if(Total > MaxShown)
				str_format(aMore, sizeof(aMore), "and %d more…", (int)(Total - MaxShown));

			char aHeader[32];
			str_format(aHeader, sizeof(aHeader), "%d read", (int)Total);

			float MaxTextW = TextRender()->TextWidth(TipFont, aHeader);
			for(size_t i = 0; i < Shown; ++i)
				MaxTextW = maximum(MaxTextW, TextRender()->TextWidth(TipFont, vReaderNames[i].c_str()));
			if(aMore[0] != '\0')
				MaxTextW = maximum(MaxTextW, TextRender()->TextWidth(TipFont, aMore));

			const int TotalTipLines = 1 + (int)Shown + (aMore[0] != '\0' ? 1 : 0);
			const float BoxW = MaxTextW + PadX * 2.0f;
			const float BoxH = TotalTipLines * LineH + PadY * 2.0f + HeaderGap;

			const SRenderRect &Label = m_HoveredReadRect;
			float BoxX = Label.m_X + Label.m_W * 0.5f - BoxW * 0.5f;
			float BoxY = Label.m_Y - BoxH - TipFont * 0.4f;
			BoxX = std::clamp(BoxX, 1.0f, maximum(1.0f, Width - BoxW - 1.0f));
			if(BoxY < 1.0f)
				BoxY = Label.m_Y + Label.m_H + TipFont * 0.4f; // flip below if no room above

			CUIRect Box(BoxX, BoxY, BoxW, BoxH);
			Box.Draw(ColorRGBA(0.055f, 0.06f, 0.07f, 0.98f), IGraphics::CORNER_ALL, maximum(3.0f, TipFont * 0.4f));

			float TextY = BoxY + PadY;
			CTextCursor HeadCursor;
			HeadCursor.SetPosition(vec2(BoxX + PadX, TextY));
			HeadCursor.m_FontSize = TipFont;
			TextRender()->TextColor(0.78f, 0.80f, 0.86f, 1.0f);
			TextRender()->TextEx(&HeadCursor, aHeader);
			TextY += LineH + HeaderGap;

			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			for(size_t i = 0; i < Shown; ++i)
			{
				CTextCursor NameCursor;
				NameCursor.SetPosition(vec2(BoxX + PadX, TextY));
				NameCursor.m_FontSize = TipFont;
				TextRender()->TextEx(&NameCursor, vReaderNames[i].c_str());
				TextY += LineH;
			}
			if(aMore[0] != '\0')
			{
				CTextCursor MoreCursor;
				MoreCursor.SetPosition(vec2(BoxX + PadX, TextY));
				MoreCursor.m_FontSize = TipFont;
				TextRender()->TextColor(0.65f, 0.67f, 0.72f, 1.0f);
				TextRender()->TextEx(&MoreCursor, aMore);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}

	// UClient server-join: hint tooltip inviting the user to click the server name to connect.
	if(m_HoveredServerJoinLineIndex >= 0 && m_HoveredServerJoinLineIndex < MAX_LINES)
	{
		const CLine &Line = m_aLines[m_HoveredServerJoinLineIndex];
		if(Line.m_HasServerJoinLink)
		{
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			TextRender()->SetRenderFlags(0);

			const char *pHint = Localize("Click to join the server your friend is on.");
			const float TipFont = maximum(4.5f, FontSize() * 0.85f);
			const float PadX = TipFont * 0.6f;
			const float PadY = TipFont * 0.45f;

			const float TextW = TextRender()->TextWidth(TipFont, pHint);
			const float BoxW = TextW + PadX * 2.0f;
			const float BoxH = TipFont * 1.32f + PadY * 2.0f;

			const SRenderRect &Anchor = m_HoveredServerJoinRect;
			float BoxX = Anchor.m_X + Anchor.m_W * 0.5f - BoxW * 0.5f;
			float BoxY = Anchor.m_Y - BoxH - TipFont * 0.4f;
			BoxX = std::clamp(BoxX, 1.0f, maximum(1.0f, Width - BoxW - 1.0f));
			if(BoxY < 1.0f)
				BoxY = Anchor.m_Y + Anchor.m_H + TipFont * 0.4f; // flip below if no room above

			CUIRect Box(BoxX, BoxY, BoxW, BoxH);
			Box.Draw(ColorRGBA(0.055f, 0.06f, 0.07f, 0.98f), IGraphics::CORNER_ALL, maximum(3.0f, TipFont * 0.4f));

			CTextCursor HintCursor;
			HintCursor.SetPosition(vec2(BoxX + PadX, BoxY + PadY));
			HintCursor.m_FontSize = TipFont;
			TextRender()->TextColor(0.90f, 0.92f, 0.96f, 1.0f);
			TextRender()->TextEx(&HintCursor, pHint);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}

	// UClient: Discord-style tooltip listing who reacted, drawn on top of everything.
	if(m_HoveredReactionLineIndex >= 0 && m_HoveredReactionIndex >= 0)
	{
		const CLine &Line = m_aLines[m_HoveredReactionLineIndex];
		if(m_HoveredReactionIndex < (int)Line.m_vReactions.size())
		{
			const CLine::SReaction &Reaction = Line.m_vReactions[m_HoveredReactionIndex];
			const size_t Total = Reaction.m_vReactorNames.size();
			if(Total > 0)
			{
				TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				TextRender()->SetRenderFlags(0);

				const float TipFont = maximum(4.5f, FontSize() * 0.85f);
				const float PadX = TipFont * 0.6f;
				const float PadY = TipFont * 0.45f;
				const float LineH = TipFont * 1.32f;
				const float EmojiGap = TipFont * 0.35f;

				const size_t MaxShown = 14;
				const size_t Shown = minimum(Total, MaxShown);
				char aMore[32] = "";
				if(Total > MaxShown)
					str_format(aMore, sizeof(aMore), "and %d more…", (int)(Total - MaxShown));

				// Header line: "<emoji>  N reacted" — the emoji is drawn with the count row.
				char aHeader[48];
				str_format(aHeader, sizeof(aHeader), "%s  %d reacted", Reaction.m_aEmoji, (int)Total);

				float MaxTextW = TextRender()->TextWidth(TipFont, aHeader);
				for(size_t i = 0; i < Shown; ++i)
					MaxTextW = maximum(MaxTextW, TextRender()->TextWidth(TipFont, Reaction.m_vReactorNames[i].c_str()));
				if(aMore[0] != '\0')
					MaxTextW = maximum(MaxTextW, TextRender()->TextWidth(TipFont, aMore));

				const int TotalLines = 1 + (int)Shown + (aMore[0] != '\0' ? 1 : 0);
				const float BoxW = MaxTextW + PadX * 2.0f;
				const float BoxH = TotalLines * LineH + PadY * 2.0f + EmojiGap;

				const SRenderRect &Pill = m_HoveredReactionRect;
				float BoxX = Pill.m_X + Pill.m_W * 0.5f - BoxW * 0.5f;
				float BoxY = Pill.m_Y - BoxH - TipFont * 0.4f;
				BoxX = std::clamp(BoxX, 1.0f, maximum(1.0f, Width - BoxW - 1.0f));
				if(BoxY < 1.0f)
					BoxY = Pill.m_Y + Pill.m_H + TipFont * 0.4f; // flip below if no room above

				CUIRect Box(BoxX, BoxY, BoxW, BoxH);
				Box.Draw(ColorRGBA(0.055f, 0.06f, 0.07f, 0.98f), IGraphics::CORNER_ALL, maximum(3.0f, TipFont * 0.4f));

				float TextY = BoxY + PadY;
				// Header (emoji + count) in a slightly brighter tone.
				CTextCursor HeadCursor;
				HeadCursor.SetPosition(vec2(BoxX + PadX, TextY));
				HeadCursor.m_FontSize = TipFont;
				TextRender()->TextColor(0.78f, 0.80f, 0.86f, 1.0f);
				TextRender()->TextEx(&HeadCursor, aHeader);
				TextY += LineH + EmojiGap;

				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				for(size_t i = 0; i < Shown; ++i)
				{
					CTextCursor NameCursor;
					NameCursor.SetPosition(vec2(BoxX + PadX, TextY));
					NameCursor.m_FontSize = TipFont;
					TextRender()->TextEx(&NameCursor, Reaction.m_vReactorNames[i].c_str());
					TextY += LineH;
				}
				if(aMore[0] != '\0')
				{
					CTextCursor MoreCursor;
					MoreCursor.SetPosition(vec2(BoxX + PadX, TextY));
					MoreCursor.m_FontSize = TipFont;
					TextRender()->TextColor(0.65f, 0.67f, 0.72f, 1.0f);
					TextRender()->TextEx(&MoreCursor, aMore);
				}
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}
	}

	m_HasSelection = HasChatSelection;
	if(m_Input.HasSelection())
		m_HasSelection = false;

	if(m_WantsSelectionCopy)
	{
		if(!SelectionString.empty())
			Input()->SetClipboardText(SelectionString.c_str());
		m_WantsSelectionCopy = false;
	}

	if(m_MediaViewerOpen && ValidateMediaViewerLine() && g_Config.m_BcChatMediaViewer)
	{
		CLine &ViewerLine = m_aLines[m_MediaViewerLineIndex];
		IGraphics::CTextureHandle MediaTexture;
		float ViewerX = 0.0f;
		float ViewerY = 0.0f;
		float ViewerW = 0.0f;
		float ViewerH = 0.0f;
		if(GetMediaViewerTexture(ViewerLine, MediaTexture) && GetMediaViewerRect(ViewerLine, Width, Height, ViewerX, ViewerY, ViewerW, ViewerH))
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.82f);
			const IGraphics::CQuadItem Backdrop(0.0f, 0.0f, Width, Height);
			Graphics()->QuadsDrawTL(&Backdrop, 1);
			Graphics()->QuadsEnd();

			Graphics()->WrapClamp();
			Graphics()->TextureSet(MediaTexture);
			Graphics()->QuadsBegin();
			Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			const IGraphics::CQuadItem ViewerQuad(ViewerX, ViewerY, ViewerW, ViewerH);
			Graphics()->QuadsDrawTL(&ViewerQuad, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
			Graphics()->TextureClear();

			CTextCursor HintCursor;
			HintCursor.SetPosition(vec2(10.0f, Height - FontSize() * 1.8f));
			HintCursor.m_FontSize = FontSize() * 0.75f;
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f);
			TextRender()->TextEx(&HintCursor, "Esc - close, Wheel - zoom, Drag - move, Double click - reset");
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}

	if(m_Mode != MODE_NONE && Ui()->IsPopupOpen())
	{
		Ui()->StartCheck();
		Ui()->Update();
		Ui()->MapScreen();
		Ui()->RenderPopupMenus();
		Ui()->FinishCheck();
		Ui()->ClearHotkeys();
		Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	}

	if(ChatInteractionActive)
		RenderTools()->RenderCursor(UiMousePos * UiToChatScale, 12.0f);
}

void CChat::EnsureCoherentFontSize() const
{
	// Adjust font size based on width
	if(g_Config.m_ClChatWidth / (float)g_Config.m_ClChatFontSize >= CHAT_FONTSIZE_WIDTH_RATIO)
		return;

	// We want to keep a ration between font size and font width so that we don't have a weird rendering
	g_Config.m_ClChatFontSize = g_Config.m_ClChatWidth / CHAT_FONTSIZE_WIDTH_RATIO;
}

void CChat::EnsureCoherentWidth() const
{
	// Adjust width based on font size
	if(g_Config.m_ClChatWidth / (float)g_Config.m_ClChatFontSize >= CHAT_FONTSIZE_WIDTH_RATIO)
		return;

	// We want to keep a ration between font size and font width so that we don't have a weird rendering
	g_Config.m_ClChatWidth = CHAT_FONTSIZE_WIDTH_RATIO * g_Config.m_ClChatFontSize;
}

CUIRect CChat::GetHudRect(float HudWidth, float HudHeight, bool ForcePreview) const
{
	if(!ForcePreview && !HudLayout::IsEnabled(HudLayout::MODULE_CHAT))
		return CUIRect{0.0f, 0.0f, 0.0f, 0.0f};

	const auto Layout = HudLayout::Get(HudLayout::MODULE_CHAT, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const bool IsScoreBoardOpen = GameClient()->m_Scoreboard.IsActive();
	const bool ShowLargeArea = ForcePreview || m_Show || (m_Mode != MODE_NONE && g_Config.m_ClShowChat == 1) || g_Config.m_ClShowChat == 2;
	const float VisibleHeight = IsScoreBoardOpen ? 93.0f * Scale : (ShowLargeArea ? 223.0f * Scale : 73.0f * Scale);
	float ExtraTop = 0.0f;
	float ExtraBottom = 0.0f;
	float VisibleWidth = ChatWidth();

	// In HUD editor preview and while chat input is open, include the input row and
	// both input action buttons in the hitbox/outline area.
	if(ForcePreview || m_Mode != MODE_NONE)
	{
		const float ScaledFontSize = FontSize() * (8.0f / 6.0f);
		const float TranslateButtonSize = maximum(16.0f, ScaledFontSize * 1.35f);
		const float TranslateButtonGap = 4.0f;
		const float InputLineWidth = maximum(ChatWidth() - 190.0f * Scale, 190.0f * Scale);
		const float ModeSuffixWidth = TextRender()->TextWidth(ScaledFontSize, ": ");
		const float PrefixWidth = maximum(
			TextRender()->TextWidth(ScaledFontSize, Localize("All")) + ModeSuffixWidth,
			maximum(
				TextRender()->TextWidth(ScaledFontSize, Localize("Team")) + ModeSuffixWidth,
				maximum(
					TextRender()->TextWidth(ScaledFontSize, "UClient") + ModeSuffixWidth,
					TextRender()->TextWidth(ScaledFontSize, Localize("Chat")) + ModeSuffixWidth)));
		const float InputAndTranslateWidth = maximum(InputLineWidth, PrefixWidth + 40.0f + 2.0f * (TranslateButtonGap + TranslateButtonSize));

		VisibleWidth = maximum(VisibleWidth, InputAndTranslateWidth);
		float ReplyExtra = m_PendingReplyActive ? ReplyBannerHeight(ScaledFontSize) : 0.0f;
		ExtraTop = ScaledFontSize + ReplyExtra;
		ExtraBottom = maximum(2.25f * ScaledFontSize, maximum(ScaledFontSize + 4.0f, 16.0f));
	}

	CUIRect Rect = {Layout.m_X, Layout.m_Y - VisibleHeight - ExtraTop, VisibleWidth, VisibleHeight + ExtraTop + ExtraBottom};
	Rect.x = std::clamp(Rect.x, 0.0f, maximum(0.0f, HudWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, maximum(0.0f, HudHeight - Rect.h));
	return Rect;
}

void CChat::RenderHud(bool ForcePreview)
{
	if(!ForcePreview && !HudLayout::IsEnabled(HudLayout::MODULE_CHAT))
		return;

	if(ForcePreview && !m_aLines[m_CurrentLine].m_Initialized && m_Mode == MODE_NONE && !m_Show)
	{
		const float Height = HudLayout::CANVAS_HEIGHT;
		const float Width = Height * Graphics()->ScreenAspect();
		CUIRect Rect = GetHudRect(Width, Height, true);
		Graphics()->TextureClear();
		Rect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, MessageRounding());

		CUIRect Content = Rect;
		Content.Margin(FontSize() * 0.5f, &Content);
		CUIRect Slider, TextArea, InputRow;
		Content.VSplitRight(maximum(2.0f, FontSize() * 0.28f), &TextArea, &Slider);
		Graphics()->DrawRect(Slider.x, Slider.y, Slider.w, Slider.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f), IGraphics::CORNER_ALL, 2.0f);
		Graphics()->DrawRect(Slider.x, Slider.y + Slider.h * 0.30f, Slider.w, Slider.h * 0.28f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.34f), IGraphics::CORNER_ALL, 2.0f);

		TextArea.HSplitBottom(maximum(9.0f, FontSize() * 1.4f), &TextArea, &InputRow);
		Graphics()->DrawRect(InputRow.x, InputRow.y, InputRow.w, InputRow.h, ColorRGBA(1.0f, 1.0f, 1.0f, 0.11f), IGraphics::CORNER_ALL, 2.0f);

		const float LineStep = maximum(4.4f, FontSize() * 0.70f);
		const float RowHeight = maximum(1.8f, FontSize() * 0.22f);
		for(int i = 0; i < 5; ++i)
		{
			const float LineY = TextArea.y + 1.0f + i * LineStep;
			if(LineY + RowHeight > TextArea.y + TextArea.h)
				break;
			const float LineW = maximum(16.0f, TextArea.w - 6.0f - i * 4.0f);
			Graphics()->DrawRect(TextArea.x + 1.0f, LineY, LineW, RowHeight, ColorRGBA(1.0f, 1.0f, 1.0f, 0.18f), IGraphics::CORNER_ALL, 1.0f);
		}
		return;
	}
	OnRender();
}

// ----- send functions -----

void CChat::SendChat(int Team, const char *pLine)
{
	// don't send empty messages
	if(*str_utf8_skip_whitespaces(pLine) == '\0')
		return;

	// Offer to enable auto-login when a `/login <code>` is sent on a known
	// server, whether typed in chat or executed via a `say /login <code>` bind.
	MaybeOfferAutoLoginFromChat(pLine);

	if(GameClient()->m_FastPractice.ConsumePracticeChatCommand(Team, pLine))
		return;
	if(GameClient()->m_UClient.ChatDoSkin(pLine))
		return;
	if(GameClient()->m_VoiceChat.TryHandleChatCommand(pLine))
		return;

	m_LastChatSend = time();

	if(GameClient()->Client()->IsSixup())
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = Team == 1 ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
		Msg7.m_Target = -1;
		Msg7.m_pMessage = pLine;
		Client()->SendPackMsgActive(&Msg7, MSGFLAG_VITAL, true);
		return;
	}

	// send chat message
	CNetMsg_Cl_Say Msg;
	Msg.m_Team = Team;
	Msg.m_pMessage = pLine;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
}

// `say` for UClient chat: sends without touching the chat input, so it works from a bind
// regardless of the current chat mode. Like `say`, it skips what only the interactive path adds:
// input history, auto-translation and reply threading.
void CChat::SayUClient(const char *pLine)
{
	if(!pLine || *str_utf8_skip_whitespaces(pLine) == '\0')
		return;

	GameClient()->m_ClientIndicator.SendUClientChat(pLine);
}

void CChat::AddHistoryEntry(int Team, const char *pLine)
{
	if(!pLine || str_length(pLine) < 1)
		return;

	const int Length = str_length(pLine);
	CHistoryEntry *pEntry = m_History.Allocate(sizeof(CHistoryEntry) + Length);
	pEntry->m_Team = Team;
	str_copy(pEntry->m_aText, pLine, Length + 1);
}

void CChat::SendChatPayloadQueued(int Team, const char *pLine)
{
	if(!pLine || str_length(pLine) < 1)
		return;

	if(m_LastChatSend + time_freq() < time())
	{
		SendChat(Team, pLine);
	}
	else if(m_vPendingChatQueue.size() < 3)
	{
		CPendingChatEntry Entry;
		Entry.m_Team = Team;
		str_copy(Entry.m_aText, pLine, sizeof(Entry.m_aText));
		m_vPendingChatQueue.emplace_back(Entry);
	}
}

void CChat::SendChatQueued(const char *pLine)
{
	if(!pLine || *str_utf8_skip_whitespaces(pLine) == '\0')
		return;

	char aConvertedLine[MAX_LINE_LENGTH];
	if(TryConvertWrongLayoutSlashCommand(pLine, aConvertedLine, sizeof(aConvertedLine)))
		pLine = aConvertedLine;

	if(m_Mode == MODE_UCLIENT)
	{
		if(!g_Config.m_UcChat)
			return;
		AddHistoryEntry(TEAM_UCLIENT, pLine);

		// Auto-translate outgoing UClient messages just like normal chat. When a
		// translation job is queued it sends asynchronously via
		// SendTranslatedChatQueued(TEAM_UCLIENT, ...) once the response arrives.
		if(GameClient()->m_Translate.TryTranslateOutgoingChat(TEAM_UCLIENT, pLine))
		{
			ClearPendingReply();
			return;
		}

		const char *pPayload = pLine;
		char aPayload[MAX_LINE_LENGTH];
		if(m_PendingReplyActive && CUClientChatReply::IsReplyFeatureEnabled())
		{
			const int ReplyIndex = ComputeSenderRecentIndex(m_PendingReplySourceLineIndex, m_aPendingReplyName, m_PendingReplyClientId);
			if(ReplyIndex > 0 && CUClientChatReply::EncodeReply(m_aPendingReplyName, ReplyIndex, pLine, aPayload, sizeof(aPayload), m_PendingReplyClientId))
			{
				pPayload = aPayload;
				m_LastOutgoingReplyTime = time();
				str_copy(m_aLastOutgoingReplyWire, aPayload, sizeof(m_aLastOutgoingReplyWire));
				m_LastOutgoingReplyToClientId = m_PendingReplyClientId;
				m_LastOutgoingReplyMessageIndex = ReplyIndex;
				str_copy(m_aLastOutgoingReplyToName, m_aPendingReplyName, sizeof(m_aLastOutgoingReplyToName));
				str_copy(m_aLastOutgoingReplyPreview, m_aPendingReplyPreview, sizeof(m_aLastOutgoingReplyPreview));
				CUClientChatReply::SReplyMeta OutMeta;
				if(CUClientChatReply::TryParseReply(aPayload, OutMeta, m_aLastOutgoingReplyBody, sizeof(m_aLastOutgoingReplyBody)))
				{
					str_copy(m_aLastOutgoingReplyToName, OutMeta.m_aReplyToName, sizeof(m_aLastOutgoingReplyToName));
					m_LastOutgoingReplyMessageIndex = OutMeta.m_ReplyMessageIndex;
				}
				else
				{
					m_aLastOutgoingReplyBody[0] = '\0';
				}
			}
			ClearPendingReply();
		}

		GameClient()->m_ClientIndicator.SendUClientChat(pPayload);
		return;
	}

	const int Team = m_Mode == MODE_ALL ? 0 : 1;
	AddHistoryEntry(Team, pLine);
	if(GameClient()->m_UClient.ChatDoSkin(pLine))
		return;
	if(GameClient()->m_VoiceChat.TryHandleChatCommand(pLine))
		return;
	if(GameClient()->m_Translate.TryTranslateOutgoingChat(Team, pLine))
		return;

	char aPayload[MAX_LINE_LENGTH];
	const char *pPayload = pLine;
	if(m_PendingReplyActive && CUClientChatReply::IsReplyFeatureEnabled())
	{
		const int ReplyIndex = ComputeSenderRecentIndex(m_PendingReplySourceLineIndex, m_aPendingReplyName, m_PendingReplyClientId);
		if(ReplyIndex > 0 && CUClientChatReply::EncodeReply(m_aPendingReplyName, ReplyIndex, pLine, aPayload, sizeof(aPayload), m_PendingReplyClientId))
		{
			pPayload = aPayload;
			m_LastOutgoingReplyTime = time();
			str_copy(m_aLastOutgoingReplyWire, aPayload, sizeof(m_aLastOutgoingReplyWire));
			m_LastOutgoingReplyToClientId = m_PendingReplyClientId;
			m_LastOutgoingReplyMessageIndex = ReplyIndex;
			str_copy(m_aLastOutgoingReplyToName, m_aPendingReplyName, sizeof(m_aLastOutgoingReplyToName));
			str_copy(m_aLastOutgoingReplyPreview, m_aPendingReplyPreview, sizeof(m_aLastOutgoingReplyPreview));
			CUClientChatReply::SReplyMeta OutMeta;
			if(CUClientChatReply::TryParseReply(aPayload, OutMeta, m_aLastOutgoingReplyBody, sizeof(m_aLastOutgoingReplyBody)))
			{
				str_copy(m_aLastOutgoingReplyToName, OutMeta.m_aReplyToName, sizeof(m_aLastOutgoingReplyToName));
				m_LastOutgoingReplyMessageIndex = OutMeta.m_ReplyMessageIndex;
			}
			else
			{
				m_aLastOutgoingReplyBody[0] = '\0';
			}
		}
		ClearPendingReply();
	}
	SendChatPayloadQueued(Team, pPayload);
}

static bool TryParseChatLoginCode(const char *pLine, char *pCodeOut, int CodeOutSize)
{
	if(!pLine || !pCodeOut || CodeOutSize <= 0)
		return false;
	pCodeOut[0] = '\0';

	const char *pCursor = str_utf8_skip_whitespaces(pLine);
	const char *pAfterLogin = str_startswith_nocase(pCursor, "/login");
	if(!pAfterLogin)
		return false;
	if(*pAfterLogin != ' ' && *pAfterLogin != '\t')
		return false;
	pCursor = str_utf8_skip_whitespaces(pAfterLogin);
	if(*pCursor == '\0')
		return false;

	str_copy(pCodeOut, pCursor, CodeOutSize);
	int Len = str_length(pCodeOut);
	while(Len > 0 && (pCodeOut[Len - 1] == ' ' || pCodeOut[Len - 1] == '\t' || pCodeOut[Len - 1] == '\r' || pCodeOut[Len - 1] == '\n'))
	{
		pCodeOut[Len - 1] = '\0';
		--Len;
	}
	return pCodeOut[0] != '\0';
}

void CChat::MaybeOfferAutoLoginFromChat(const char *pLine)
{
	if(!pLine || Client()->State() != IClient::STATE_ONLINE)
		return;
	if(GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_GAMEPLAY_AUTO_LOGIN))
		return;

	char aCode[128];
	if(!TryParseChatLoginCode(pLine, aCode, sizeof(aCode)))
		return;

	CMenus &Menus = GameClient()->m_Menus;
	if(GameClient()->IsAutoLoginJapanServer())
	{
		if(g_Config.m_UcAutoLoginJapan != 0 || g_Config.m_UcAutoLoginJapanPromptShown != 0)
			return;
		Menus.OfferAutoLoginFromChat(CMenus::AUTO_LOGIN_OFFER_JAPAN, aCode);
	}
	else if(GameClient()->IsAutoLoginKogServer())
	{
		if(g_Config.m_UcAutoLoginKog != 0 || g_Config.m_UcAutoLoginKogPromptShown != 0)
			return;
		Menus.OfferAutoLoginFromChat(CMenus::AUTO_LOGIN_OFFER_KOG, aCode);
	}
}

void CChat::SendTranslatedChatQueued(int Team, const char *pLine)
{
	// Outgoing translation for UClient chat routes back to the UClient sender
	// instead of a normal Cl_Say message.
	if(Team == TEAM_UCLIENT)
	{
		if(!pLine || *str_utf8_skip_whitespaces(pLine) == '\0' || !g_Config.m_UcChat)
			return;
		GameClient()->m_ClientIndicator.SendUClientChat(pLine);
		return;
	}
	SendChatPayloadQueued(Team, pLine);
}

bool CChat::LineHighlighted(int ClientId, const char *pLine)
{
	bool Highlighted = false;

	if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(ClientId >= 0 && ClientId != GameClient()->m_aLocalIds[0] && ClientId != GameClient()->m_aLocalIds[1])
		{
			for(int LocalId : GameClient()->m_aLocalIds)
			{
				Highlighted |= LocalId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[LocalId].m_aName);
			}
		}
	}
	else
	{
		// on demo playback use local id from snap directly,
		// since m_aLocalIds isn't valid there
		Highlighted |= GameClient()->m_Snap.m_LocalClientId >= 0 && LineShouldHighlight(pLine, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_aName);
	}

	return Highlighted;
}


// ============================================================
// Giphy GIF search
// ============================================================

static ColorRGBA GiphySkeletonColor(size_t Seed)
{
	static const ColorRGBA s_aPalette[] = {
		ColorRGBA(0.43f, 0.51f, 0.84f, 0.88f),
		ColorRGBA(0.55f, 0.60f, 0.90f, 0.88f),
		ColorRGBA(0.49f, 0.72f, 0.78f, 0.88f),
		ColorRGBA(0.62f, 0.69f, 0.83f, 0.88f),
		ColorRGBA(0.58f, 0.54f, 0.86f, 0.88f),
		ColorRGBA(0.50f, 0.64f, 0.88f, 0.88f),
	};
	return s_aPalette[Seed % std::size(s_aPalette)];
}

void CChat::OpenGiphyPopup(const CUIRect &ButtonRect)
{
	if(Input()->HasComposition() || Input()->GetCandidateCount())
	{
		Input()->StopTextInput();
		Input()->StartTextInput();
	}
	m_Input.Deactivate();

	const float PopupWidth = 400.0f;
	const float PopupHeight = 360.0f;
	const float ScreenW = Ui()->Screen()->w;
	const float ScreenH = Ui()->Screen()->h;
	const float Margin = 10.0f;

	float PopupX = maximum(Margin, ButtonRect.x + ButtonRect.w - PopupWidth);
	float PopupY = maximum(Margin, ButtonRect.y - 10.0f);
	if(!m_GiphyPopupHasStoredPos && g_Config.m_BcGiphyPopupX >= 0 && g_Config.m_BcGiphyPopupY >= 0)
	{
		m_GiphyPopupPos = vec2((float)g_Config.m_BcGiphyPopupX, (float)g_Config.m_BcGiphyPopupY);
		m_GiphyPopupHasStoredPos = true;
	}
	if(m_GiphyPopupHasStoredPos)
	{
		PopupX = m_GiphyPopupPos.x;
		PopupY = m_GiphyPopupPos.y;
	}

	PopupX = std::clamp(PopupX, Margin, maximum(Margin, ScreenW - PopupWidth - Margin));
	PopupY = std::clamp(PopupY, Margin, maximum(Margin, ScreenH - PopupHeight - Margin));
	m_GiphyPopupPos = vec2(PopupX, PopupY);
	m_GiphyPopupHasStoredPos = true;
	g_Config.m_BcGiphyPopupX = round_to_int(PopupX);
	g_Config.m_BcGiphyPopupY = round_to_int(PopupY);
	m_GiphySearchInput.Activate(EInputPriority::CHAT);
	Ui()->SetActiveItem(&m_GiphySearchInput);

	Ui()->DoPopupMenu(&m_GiphyPopupId, PopupX, PopupY, PopupWidth, 360.0f, this, PopupGiphyBrowser, {}, CUi::EButtonSoundType::DEFAULT);
}

void CChat::RenderGiphyButton(const CUIRect &ButtonRect)
{
	m_GiphyButtonRect = {ButtonRect.x, ButtonRect.y, ButtonRect.w, ButtonRect.h};
	m_GiphyButtonRectValid = true;

	const vec2 MousePos = ChatMousePos();
	const bool Hovered = MousePos.x >= ButtonRect.x && MousePos.x <= ButtonRect.x + ButtonRect.w &&
		MousePos.y >= ButtonRect.y && MousePos.y <= ButtonRect.y + ButtonRect.h;
	const bool IsOpen = Ui()->IsPopupOpen(&m_GiphyPopupId);
	const ColorRGBA ButtonColor = IsOpen ? ColorRGBA(0.61f, 0.24f, 0.31f, 0.92f) :
		(Hovered ? ColorRGBA(0.28f, 0.28f, 0.28f, 0.90f) : ColorRGBA(0.16f, 0.16f, 0.16f, 0.82f));
	const float ButtonRounding = maximum(3.0f, ButtonRect.h * 0.28f);

	ButtonRect.Draw(ButtonColor, IGraphics::CORNER_ALL, ButtonRounding);
	Ui()->DoLabel(&ButtonRect, "GIF", ButtonRect.h * 0.54f, TEXTALIGN_MC);

	if(Hovered)
		Ui()->SetHotItem(&m_GiphyButton);
	GameClient()->m_Tooltips.DoToolTip(&m_GiphyButton, &ButtonRect, Localize("Search Giphy GIFs"));
}

CUi::EPopupMenuFunctionResult CChat::PopupGiphyBrowser(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	const float Spacing = 5.0f;
	const float RowHeight = 22.0f;
	const float CellSpacing = 6.0f;
	const int Columns = 2;

	if(Active)
	{
		const float PopupWidth = 400.0f;
		const float PopupHeight = 360.0f;
		const float Margin = 10.0f;
		const float PopupPadding = 5.0f; // popup border + margin in CUi
		const vec2 MousePos(pChat->Ui()->MouseX(), pChat->Ui()->MouseY());
		const vec2 PopupTopLeft(View.x - PopupPadding, View.y - PopupPadding);
		CUIRect DragRect = View;
		DragRect.HSplitTop(16.0f, &DragRect, nullptr);

		const bool HeaderHovered = MousePos.x >= DragRect.x && MousePos.x <= DragRect.x + DragRect.w &&
			MousePos.y >= DragRect.y && MousePos.y <= DragRect.y + DragRect.h;

		if(!pChat->m_GiphyPopupDragging && pChat->Ui()->MouseButtonClicked(0) && HeaderHovered)
		{
			pChat->m_GiphyPopupDragging = true;
			pChat->m_GiphyPopupDragOffset = MousePos - PopupTopLeft;
		}

		if(pChat->m_GiphyPopupDragging)
		{
			if(!pChat->Ui()->MouseButton(0))
			{
				pChat->m_GiphyPopupDragging = false;
			}
			else
			{
				const float ScreenW = pChat->Ui()->Screen()->w;
				const float ScreenH = pChat->Ui()->Screen()->h;
				const float NewX = std::clamp(MousePos.x - pChat->m_GiphyPopupDragOffset.x, Margin, maximum(Margin, ScreenW - PopupWidth - Margin));
				const float NewY = std::clamp(MousePos.y - pChat->m_GiphyPopupDragOffset.y, Margin, maximum(Margin, ScreenH - PopupHeight - Margin));
				const vec2 NewPos(NewX, NewY);
				if(distance(NewPos, pChat->m_GiphyPopupPos) > 0.01f)
				{
					pChat->m_GiphyPopupPos = NewPos;
					pChat->m_GiphyPopupHasStoredPos = true;
					g_Config.m_BcGiphyPopupX = round_to_int(NewPos.x);
					g_Config.m_BcGiphyPopupY = round_to_int(NewPos.y);
					// Reopen popup at new position
					pChat->Ui()->ClosePopupMenu(&pChat->m_GiphyPopupId);
					pChat->Ui()->DoPopupMenu(&pChat->m_GiphyPopupId, NewPos.x, NewPos.y, 400.0f, 360.0f, pChat, PopupGiphyBrowser, {}, CUi::EButtonSoundType::DEFAULT);
				}
			}
		}
	}

	CUIRect Row;
	View.HSplitTop(16.0f, &Row, &View);
	pChat->Ui()->DoLabel(&Row, Localize("Giphy"), 12.0f, TEXTALIGN_ML);

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(RowHeight, &Row, &View);
	CUIRect SearchEdit, SearchButton;
	Row.VSplitRight(86.0f, &SearchEdit, &SearchButton);
	SearchEdit.VSplitRight(Spacing, &SearchEdit, nullptr);
	pChat->m_GiphySearchInput.SetEmptyText(Localize("Search GIFs"));
	pChat->m_GiphySearchInput.Activate(EInputPriority::CHAT);
	pChat->Ui()->DoClearableEditBox(&pChat->m_GiphySearchInput, &SearchEdit, 12.0f);
	const bool SearchPressed = pChat->GameClient()->m_Menus.DoButton_Menu(&pChat->m_GiphySearchButton, Localize("Search"), 0, &SearchButton) ||
		(Active && pChat->Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER));
	if(SearchPressed)
	{
		pChat->m_GiphyBrowser.SetQuery(pChat->m_GiphySearchInput.GetString());
		pChat->BeginGiphySearch(false);
	}

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(16.0f, &Row, &View);
	if(pChat->m_GiphySearching)
		pChat->Ui()->DoLabel(&Row, pChat->m_GiphyLoadingMore ? "Loading more..." : Localize("Searching..."), 10.0f, TEXTALIGN_ML);
	else if(!pChat->m_GiphyStatusText.empty())
		pChat->Ui()->DoLabel(&Row, pChat->m_GiphyStatusText.c_str(), 10.0f, TEXTALIGN_ML);
	else
	{
		char aStatus[128];
		str_format(aStatus, sizeof(aStatus), "%d %s", pChat->m_GiphyBrowser.GetTotalCount(), Localize("results"));
		pChat->Ui()->DoLabel(&Row, aStatus, 10.0f, TEXTALIGN_ML);
	}

	if(g_Config.m_UcChatGiphyApiKey[0] == '\0')
	{
		const char *pGuideText = "Go to developers.giphy.com/dashboard/, sign in, get an API key, and put it into uc_chat_giphy_api_key.";
		const char *pDashboardUrl = "https://developers.giphy.com/dashboard/";

		CUIRect CenterBlock;
		const float BlockHeight = 72.0f;
		View.HSplitTop(maximum(0.0f, (View.h - BlockHeight) / 2.0f), nullptr, &View);
		View.HSplitTop(BlockHeight, &CenterBlock, nullptr);

		CUIRect GuideLabel, LinkLabel;
		CenterBlock.HSplitTop(42.0f, &GuideLabel, &CenterBlock);
		CenterBlock.HSplitTop(6.0f, nullptr, &CenterBlock);
		CenterBlock.HSplitTop(18.0f, &LinkLabel, nullptr);

		pChat->Ui()->DoLabel(&GuideLabel, pGuideText, 11.0f, TEXTALIGN_MC, {.m_MaxWidth = GuideLabel.w});

		static CButtonContainer s_GiphyDashboardLinkButton;
		const bool LinkHovered = pChat->Ui()->HotItem() == &s_GiphyDashboardLinkButton;
		if(pChat->Ui()->DoButtonLogic(&s_GiphyDashboardLinkButton, 0, &LinkLabel, BUTTONFLAG_LEFT, CUi::EButtonSoundType::BUTTON))
			pChat->Client()->ViewLink(pDashboardUrl);

		pChat->TextRender()->TextColor(LinkHovered ? 0.60f : 0.45f, LinkHovered ? 0.80f : 0.70f, 1.0f, 1.0f);
		pChat->Ui()->DoLabel(&LinkLabel, pDashboardUrl, 11.0f, TEXTALIGN_MC);
		pChat->TextRender()->TextColor(pChat->TextRender()->DefaultTextColor());

		return CUi::POPUP_KEEP_OPEN;
	}

	View.HSplitTop(Spacing, nullptr, &View);

	const auto &vResults = pChat->m_GiphyBrowser.GetResults();
	const int PlaceholderCount = pChat->m_GiphySearching ? 6 : 0;
	const size_t TotalSlots = vResults.size() + (size_t)PlaceholderCount;
	pChat->m_GiphyVisibleResultIds.clear();
	if(pChat->m_vGiphyResultButtons.size() < TotalSlots)
		pChat->m_vGiphyResultButtons.resize(TotalSlots);

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarWidth = 8.0f;
	ScrollParams.m_ScrollbarMargin = 0.0f;
	ScrollParams.m_ScrollbarNoMarginRight = true;
	ScrollParams.m_SliderMinHeight = 18.0f;
	ScrollParams.m_ScrollUnit = 18.0f;
	ScrollParams.m_ClipBgColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	ScrollParams.m_ScrollbarBgColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	ScrollParams.m_RailBgColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f);
	pChat->m_GiphyScrollRegion.Begin(&View, &pChat->m_GiphyScrollOffset, &ScrollParams);

	CUIRect Content = View;
	Content.y += pChat->m_GiphyScrollOffset.y;

	const float CellWidth = (View.w - CellSpacing * (Columns - 1)) / Columns;
	float aColumnHeights[Columns] = {0.0f, 0.0f};

	for(size_t Index = 0; Index < TotalSlots; ++Index)
	{
		const bool IsPlaceholder = Index >= vResults.size();
		const SGifResult *pResult = IsPlaceholder ? nullptr : &vResults[Index];

		const int Column = aColumnHeights[0] <= aColumnHeights[1] ? 0 : 1;
		float CardHeight = CellWidth * 0.85f;
		if(pResult && pResult->m_Width > 0 && pResult->m_Height > 0)
		{
			const float Aspect = std::clamp((float)pResult->m_Height / (float)pResult->m_Width, 0.55f, 1.65f);
			CardHeight = CellWidth * Aspect;
		}
		CardHeight = std::clamp(CardHeight, 72.0f, 210.0f);

		CUIRect Card = {Content.x + Column * (CellWidth + CellSpacing), Content.y + aColumnHeights[Column], CellWidth, CardHeight};
		aColumnHeights[Column] += CardHeight + CellSpacing;

		const bool Visible = pChat->m_GiphyScrollRegion.AddRect(Card);
		if(!Visible)
			continue;

		CUIRect Inner;
		Card.Margin(2.0f, &Inner);

		if(IsPlaceholder)
		{
			Inner.Draw(GiphySkeletonColor(Index), IGraphics::CORNER_ALL, 4.0f);
			continue;
		}

		const bool Clicked = pChat->GameClient()->m_Menus.DoButton_Menu(&pChat->m_vGiphyResultButtons[Index], "", 0, &Card);
		pChat->m_GiphyVisibleResultIds.insert(pResult->m_Id);

		IGraphics::CTextureHandle Texture;
		int Width = 0;
		int Height = 0;
		if(pChat->GetGiphyPreviewTexture(*pResult, Texture, Width, Height))
		{
			DrawRoundedMediaPreview(pChat->Graphics(), Texture, Inner.x, Inner.y, Inner.w, Inner.h, 4.0f, 1.0f);
		}
		else
		{
			auto It = pChat->m_GiphyPreviewCache.find(pResult->m_Id);
			if(It != pChat->m_GiphyPreviewCache.end() && It->second.m_State == EMediaState::FAILED)
			{
				Inner.Draw(ColorRGBA(0.10f, 0.10f, 0.10f, 0.70f), IGraphics::CORNER_ALL, 4.0f);
				pChat->Ui()->DoLabel(&Inner, Localize("Failed"), 10.0f, TEXTALIGN_MC);
			}
			else
			{
				size_t Seed = Index;
				for(char C : pResult->m_Id)
					Seed = Seed * 131u + (unsigned char)C;
				Inner.Draw(GiphySkeletonColor(Seed), IGraphics::CORNER_ALL, 4.0f);
			}
		}

		if(Clicked)
		{
			char aLine[MAX_LINE_LENGTH];
			const char *pInputText = pChat->m_Input.GetString();
			if(pInputText[0] != '\0')
				str_format(aLine, sizeof(aLine), "%s %s", pInputText, pResult->m_Url.c_str());
			else
				str_copy(aLine, pResult->m_Url.c_str(), sizeof(aLine));

			if(pChat->m_Mode == MODE_UCLIENT)
			{
				pChat->AddHistoryEntry(TEAM_UCLIENT, aLine);
				if(g_Config.m_UcChat)
					pChat->GameClient()->m_ClientIndicator.SendUClientChat(aLine);
			}
			else
			{
				const int Team = pChat->m_Mode == MODE_TEAM ? 1 : 0;
				pChat->AddHistoryEntry(Team, aLine);
				if(!pChat->GameClient()->m_Translate.TryTranslateOutgoingChat(Team, aLine))
					pChat->SendChatPayloadQueued(Team, aLine);
			}
			pChat->DisableMode();
			pChat->GameClient()->OnRelease();
			pChat->m_SavedInputPending = false;
			pChat->m_aSavedInputText[0] = '\0';
			pChat->m_pHistoryEntry = nullptr;
			pChat->m_Input.Clear();
			pChat->m_HasSelection = false;
			pChat->m_WantsSelectionCopy = false;
			
			// Properly close scroll region before closing popup
			pChat->m_GiphyScrollRegion.End();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	pChat->m_GiphyScrollRegion.End();

	const float ContentHeight = maximum(aColumnHeights[0], aColumnHeights[1]);
	const float ContentBottom = Content.y + ContentHeight;
	const float NearBottomThreshold = 140.0f;
	if(!pChat->m_GiphySearching && pChat->m_GiphyHasMoreResults && !vResults.empty() && ContentBottom < View.y + View.h + NearBottomThreshold)
		pChat->BeginGiphySearch(true);

	if(!pChat->m_GiphySearching && pChat->m_GiphyStatusText == "No search results")
		pChat->Ui()->DoLabel(&View, "No search results", 12.0f, TEXTALIGN_MC);

	return CUi::POPUP_KEEP_OPEN;
}

void CChat::BeginGiphySearch(bool LoadMore)
{
	if(m_pGiphyRequest)
	{
		m_pGiphyRequest->Abort();
		m_pGiphyRequest.reset();
	}

	if(!LoadMore)
	{
		m_GiphyNextPageToLoad = 0;
		m_GiphyRequestedPage = 0;
		m_GiphyHasMoreResults = false;
		m_GiphyScrollOffset = vec2(0.0f, 0.0f);
		m_GiphyScrollRegion.Reset();
		m_GiphyVisibleResultIds.clear();
	}

	if(m_GiphySearchInput.GetString()[0] == '\0')
	{
		m_GiphyBrowser.ClearResults();
		m_GiphyStatusText = Localize("Search for a GIF");
		ClearGiphyPreviewCache();
		m_GiphySearching = false;
		return;
	}

	if(g_Config.m_UcChatGiphyApiKey[0] == '\0')
	{
		m_GiphyBrowser.ClearResults();
		m_GiphyStatusText = "Set uc_chat_giphy_api_key in console";
		ClearGiphyPreviewCache();
		m_GiphySearching = false;
		return;
	}

	const int PageToLoad = LoadMore ? m_GiphyNextPageToLoad : 0;
	m_GiphyRequestedPage = maximum(0, PageToLoad);
	m_GiphyLoadingMore = LoadMore;
	m_GiphySearching = true;
	m_GiphyStatusText = m_GiphyLoadingMore ? "Loading more..." : Localize("Searching...");
	const std::string Url = m_GiphyBrowser.BuildSearchUrl(m_GiphyRequestedPage * CGiphyBrowser::RESULTS_PER_PAGE);
	std::shared_ptr<CHttpRequest> pGet = HttpGet(Url.c_str());
	pGet->Timeout(CTimeout{4000, 0, 4096, 5});
	pGet->MaxResponseSize(2 * 1024 * 1024);
	pGet->FailOnErrorStatus(false);
	pGet->LogProgress(HTTPLOG::FAILURE);
	m_pGiphyRequest = pGet;
	Http()->Run(pGet);
}

void CChat::UpdateGiphySearch()
{
	if(!m_pGiphyRequest || !m_pGiphyRequest->Done())
		return;

	std::shared_ptr<CHttpRequest> pRequest = m_pGiphyRequest;
	m_pGiphyRequest.reset();
	m_GiphySearching = false;

	if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() >= 200 && pRequest->StatusCode() < 400 && pRequest->ResultJson() != nullptr)
	{
		m_GiphyBrowser.ParseGiphyResponse(pRequest->ResultJson(), m_GiphyLoadingMore);
		if(!m_GiphyLoadingMore)
			ClearGiphyPreviewCache();

		const int LoadedCount = (int)m_GiphyBrowser.GetResults().size();
		const int TotalCount = m_GiphyBrowser.GetTotalCount();
		m_GiphyHasMoreResults = LoadedCount < TotalCount;
		m_GiphyNextPageToLoad = m_GiphyRequestedPage + 1;
		m_GiphyStatusText = m_GiphyBrowser.GetResults().empty() ? "No search results" : std::string();
	}
	else
	{
		char aBuf[128];
		const int StatusCode = pRequest->State() == EHttpState::DONE ? pRequest->StatusCode() : -1;
		str_format(aBuf, sizeof(aBuf), "%s (%d)", Localize("Giphy request failed"), StatusCode);
		m_GiphyStatusText = aBuf;
		m_GiphyHasMoreResults = false;
		m_GiphyBrowser.ClearResults();
		ClearGiphyPreviewCache();
	}

	m_GiphyLoadingMore = false;
}

void CChat::ClearGiphyPreviewCache()
{
	for(auto &[Key, Entry] : m_GiphyPreviewCache)
	{
		if(Entry.m_pRequest)
			Entry.m_pRequest->Abort();
		if(Entry.m_pDecodeJob)
			Entry.m_pDecodeJob->Abort();
		MediaDecoder::UnloadFrames(Graphics(), Entry.m_vFrames);
	}
	m_GiphyPreviewCache.clear();
}

void CChat::UpdateGiphyPreviewCache()
{
	if(!Ui()->IsPopupOpen(&m_GiphyPopupId))
		return;

	const auto &vResults = m_GiphyBrowser.GetResults();
	const auto &vVisibleIds = m_GiphyVisibleResultIds;
	for(auto It = m_GiphyPreviewCache.begin(); It != m_GiphyPreviewCache.end();)
	{
		const bool StillVisible = vVisibleIds.find(It->first) != vVisibleIds.end();
		if(StillVisible)
		{
			++It;
			continue;
		}

		if(It->second.m_pRequest)
			It->second.m_pRequest->Abort();
		if(It->second.m_pDecodeJob)
			It->second.m_pDecodeJob->Abort();
		MediaDecoder::UnloadFrames(Graphics(), It->second.m_vFrames);
		It = m_GiphyPreviewCache.erase(It);
	}

	int ActiveDownloads = 0;
	for(const auto &Result : vResults)
	{
		if(vVisibleIds.find(Result.m_Id) == vVisibleIds.end())
			continue;

		auto &Entry = m_GiphyPreviewCache[Result.m_Id];
		Entry.m_LastUsedTick = time_get();
		if(Entry.m_State == EMediaState::LOADING && Entry.m_pRequest && !Entry.m_pRequest->Done())
			ActiveDownloads++;
	}

	for(const auto &Result : vResults)
	{
		if(vVisibleIds.find(Result.m_Id) == vVisibleIds.end())
			continue;

		auto &Entry = m_GiphyPreviewCache[Result.m_Id];
		if(Entry.m_State != EMediaState::NONE || ActiveDownloads >= 2)
			continue;

		const char *pUrl = !Result.m_PreviewUrl.empty() ? Result.m_PreviewUrl.c_str() : Result.m_Url.c_str();
		if(pUrl == nullptr || pUrl[0] == '\0')
		{
			Entry.m_State = EMediaState::FAILED;
			continue;
		}

		std::shared_ptr<CHttpRequest> pGet = HttpGet(pUrl);
		pGet->Timeout(CTimeout{4000, 0, 4096, 5});
		pGet->MaxResponseSize(16 * 1024 * 1024);
		pGet->FailOnErrorStatus(false);
		pGet->LogProgress(HTTPLOG::FAILURE);
		Entry.m_pRequest = pGet;
		Entry.m_State = EMediaState::LOADING;
		Http()->Run(pGet);
		ActiveDownloads++;
	}

	for(const auto &Result : vResults)
	{
		if(vVisibleIds.find(Result.m_Id) == vVisibleIds.end())
			continue;

		auto It = m_GiphyPreviewCache.find(Result.m_Id);
		if(It == m_GiphyPreviewCache.end())
			continue;
		auto &Entry = It->second;

		if(Entry.m_State == EMediaState::LOADING && Entry.m_pRequest && Entry.m_pRequest->Done())
		{
			if(Entry.m_pRequest->State() == EHttpState::DONE && Entry.m_pRequest->StatusCode() >= 200 && Entry.m_pRequest->StatusCode() < 400)
			{
				unsigned char *pData = nullptr;
				size_t DataSize = 0;
				Entry.m_pRequest->Result(&pData, &DataSize);
				EMediaKind Kind = MediaKindFromUrl(!Result.m_PreviewUrl.empty() ? Result.m_PreviewUrl.c_str() : Result.m_Url.c_str());
				if(Kind == EMediaKind::UNKNOWN)
					Kind = EMediaKind::PHOTO;
				Entry.m_pDecodeJob = std::make_shared<CMediaDecodeJob>(Graphics(), Kind, pData, DataSize, Result.m_Id.c_str());
				Engine()->AddJob(Entry.m_pDecodeJob);
				Entry.m_State = EMediaState::DECODING;
			}
			else
				Entry.m_State = EMediaState::FAILED;
			Entry.m_pRequest.reset();
		}

		if(Entry.m_State == EMediaState::DECODING && Entry.m_pDecodeJob && Entry.m_pDecodeJob->Done())
		{
			if(Entry.m_pDecodeJob->State() == IJob::STATE_DONE && Entry.m_pDecodeJob->Success() && !Entry.m_pDecodeJob->DecodedFrames().Empty())
			{
				SMediaDecodedFrames &DecodedFrames = Entry.m_pDecodeJob->DecodedFrames();
				Entry.m_Animated = DecodedFrames.m_Animated;
				Entry.m_AnimationStart = DecodedFrames.m_AnimationStart;
				Entry.m_Width = DecodedFrames.m_Width;
				Entry.m_Height = DecodedFrames.m_Height;
				Entry.m_State = MediaDecoder::UploadFrames(Graphics(), DecodedFrames, Entry.m_vFrames, Result.m_Id.c_str()) ? EMediaState::READY : EMediaState::FAILED;
			}
			else
				Entry.m_State = EMediaState::FAILED;
			Entry.m_pDecodeJob.reset();
		}
	}
}

bool CChat::GetGiphyPreviewTexture(const SGifResult &Result, IGraphics::CTextureHandle &Texture, int &Width, int &Height) const
{
	auto It = m_GiphyPreviewCache.find(Result.m_Id);
	if(It == m_GiphyPreviewCache.end() || It->second.m_State != EMediaState::READY)
		return false;

	Width = It->second.m_Width;
	Height = It->second.m_Height;
	return MediaDecoder::GetCurrentFrameTexture(It->second.m_vFrames, It->second.m_Animated, It->second.m_AnimationStart, Texture);
}

// ---------------------------------------------------------------------------
// Right-click "save image" context menu for inline chat media
// ---------------------------------------------------------------------------

static const char *const gs_apMediaAssetCategoryNames[] = {
	"Entities", "Game", "Emoticons", "Particles", "HUD", "Extras", "Cursor", "Arrow"};
static const char *const gs_apMediaAssetCategoryDirs[] = {
	"assets/entities", "assets/game", "assets/emoticons", "assets/particles", "assets/hud", "assets/extras", "assets/cursor", "assets/arrow"};

static void MediaExtractUrlFileName(const char *pUrl, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(pUrl == nullptr || pUrl[0] == '\0')
		return;

	const char *pName = pUrl;
	for(const char *p = pUrl; *p != '\0'; ++p)
	{
		if(*p == '/')
			pName = p + 1;
		else if(*p == '?' || *p == '#')
			break;
	}

	int i = 0;
	for(const char *p = pName; *p != '\0' && *p != '?' && *p != '#' && i < OutSize - 1; ++p)
		pOut[i++] = *p;
	pOut[i] = '\0';
}

static void MediaSanitizeAssetName(const char *pIn, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;
	pOut[0] = '\0';

	char aTmp[128];
	str_copy(aTmp, pIn != nullptr ? pIn : "", sizeof(aTmp));

	// Strip a trailing file extension (last dot, but keep leading-dot names).
	char *pLastDot = nullptr;
	for(char *p = aTmp; *p != '\0'; ++p)
	{
		if(*p == '.')
			pLastDot = p;
	}
	if(pLastDot != nullptr && pLastDot != aTmp)
		*pLastDot = '\0';

	// Keep only characters that are safe in file names.
	int o = 0;
	for(const char *p = aTmp; *p != '\0' && o < OutSize - 1; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		if(c < 32 || *p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
			continue;
		pOut[o++] = *p;
	}
	pOut[o] = '\0';

	// Trim leading and trailing spaces.
	int Start = 0;
	while(pOut[Start] == ' ')
		++Start;
	if(Start > 0)
	{
		int k = 0;
		while(pOut[Start + k] != '\0')
		{
			pOut[k] = pOut[Start + k];
			++k;
		}
		pOut[k] = '\0';
	}
	int End = str_length(pOut);
	while(End > 0 && pOut[End - 1] == ' ')
		pOut[--End] = '\0';
}

// Decode a few URL escapes used in shared asset names (especially %2F for category/name).
static void MediaDecodeUrlComponentLite(const char *pIn, char *pOut, int OutSize)
{
	if(OutSize <= 0)
		return;
	pOut[0] = '\0';
	if(!pIn)
		return;

	int o = 0;
	for(const char *p = pIn; *p != '\0' && o < OutSize - 1;)
	{
		if(p[0] == '%' && p[1] && p[2] &&
			((p[1] >= '0' && p[1] <= '9') || (p[1] >= 'A' && p[1] <= 'F') || (p[1] >= 'a' && p[1] <= 'f')) &&
			((p[2] >= '0' && p[2] <= '9') || (p[2] >= 'A' && p[2] <= 'F') || (p[2] >= 'a' && p[2] <= 'f')))
		{
			auto Hex = [](char C) -> int {
				if(C >= '0' && C <= '9')
					return C - '0';
				if(C >= 'A' && C <= 'F')
					return C - 'A' + 10;
				if(C >= 'a' && C <= 'f')
					return C - 'a' + 10;
				return 0;
			};
			pOut[o++] = (char)((Hex(p[1]) << 4) | Hex(p[2]));
			p += 3;
		}
		else if(p[0] == '+')
		{
			pOut[o++] = ' ';
			++p;
		}
		else
		{
			pOut[o++] = *p++;
		}
	}
	pOut[o] = '\0';
}

static const char *MediaAssetCategoryBase(size_t Index)
{
	const char *pDir = gs_apMediaAssetCategoryDirs[Index];
	const char *pBase = pDir;
	for(const char *p = pDir; *p; ++p)
	{
		if(*p == '/')
			pBase = p + 1;
	}
	return pBase;
}

static int MediaAssetCategoryIndexFromBase(const char *pBase)
{
	if(!pBase || pBase[0] == '\0')
		return -1;
	for(size_t i = 0; i < std::size(gs_apMediaAssetCategoryDirs); ++i)
	{
		if(str_comp_nocase(pBase, MediaAssetCategoryBase(i)) == 0)
			return (int)i;
	}
	return -1;
}

// Extract uc_asset=<category> from a shared media URL query string.
static bool MediaExtractUcAssetCategory(const char *pUrl, char *pOut, int OutSize)
{
	if(pOut && OutSize > 0)
		pOut[0] = '\0';
	if(!pUrl || !pOut || OutSize <= 0)
		return false;

	const char *pQuery = str_find(pUrl, "?");
	if(!pQuery)
		return false;
	++pQuery;

	while(*pQuery)
	{
		const char *pAmp = str_find(pQuery, "&");
		const char *pHash = str_find(pQuery, "#");
		const char *pEnd = pQuery + str_length(pQuery);
		if(pAmp && pAmp < pEnd)
			pEnd = pAmp;
		if(pHash && pHash < pEnd)
			pEnd = pHash;

		if(str_startswith_nocase(pQuery, "uc_asset=") != nullptr)
		{
			const char *pVal = pQuery + 9;
			char aRaw[64];
			str_copy(aRaw, pVal, minimum((int)(pEnd - pVal) + 1, (int)sizeof(aRaw)));
			MediaDecodeUrlComponentLite(aRaw, pOut, OutSize);
			return pOut[0] != '\0';
		}

		if(!pAmp)
			break;
		pQuery = pAmp + 1;
	}
	return false;
}

// Returns category index if the URL encodes a shared-asset path like entities/foo.png, else -1.
// Always writes a sanitized bare asset name into pNameOut when possible.
//
// Supported encodings (media CDN often flattens "hud/foo" → "hud_foo.png"):
// 1) ?uc_asset=hud query (preferred; client appends this when sharing)
// 2) slash in file name: hud/foo.png or hud%2Ffoo.png
// 3) /hud/ segment in the URL path
// 4) filename prefix: hud_foo.png
static int MediaDetectAssetCategoryFromUrl(const char *pUrl, char *pNameOut, int NameOutSize)
{
	if(pNameOut && NameOutSize > 0)
		pNameOut[0] = '\0';
	if(!pUrl || pUrl[0] == '\0')
		return -1;

	char aFile[256];
	MediaExtractUrlFileName(pUrl, aFile, sizeof(aFile));
	char aDecoded[256];
	MediaDecodeUrlComponentLite(aFile, aDecoded, sizeof(aDecoded));

	char aCategory[64] = "";
	const char *pBaseName = aDecoded;

	// 1) Explicit share marker from the client after upload.
	MediaExtractUcAssetCategory(pUrl, aCategory, sizeof(aCategory));

	if(aCategory[0] == '\0')
	{
		const char *pSlash = nullptr;
		for(const char *p = aDecoded; *p != '\0'; ++p)
		{
			if(*p == '/' || *p == '\\')
				pSlash = p;
		}
		if(pSlash && pSlash != aDecoded)
		{
			// 2) category/name in the file component.
			const char *pCatStart = aDecoded;
			for(const char *p = aDecoded; p < pSlash; ++p)
			{
				if(*p == '/' || *p == '\\')
					pCatStart = p + 1;
			}
			str_copy(aCategory, pCatStart, minimum((int)(pSlash - pCatStart) + 1, (int)sizeof(aCategory)));
			pBaseName = pSlash + 1;
		}
		else
		{
			// 3) "/entities/" (etc.) earlier in the full URL path.
			for(size_t i = 0; i < std::size(gs_apMediaAssetCategoryDirs); ++i)
			{
				char aNeedle[64];
				str_format(aNeedle, sizeof(aNeedle), "/%s/", MediaAssetCategoryBase(i));
				if(str_find_nocase(pUrl, aNeedle) != nullptr)
				{
					str_copy(aCategory, MediaAssetCategoryBase(i), sizeof(aCategory));
					break;
				}
			}

			// 4) CDN flattened name: hud_705_....png (slash → underscore).
			if(aCategory[0] == '\0')
			{
				for(size_t i = 0; i < std::size(gs_apMediaAssetCategoryDirs); ++i)
				{
					const char *pBase = MediaAssetCategoryBase(i);
					const int BaseLen = str_length(pBase);
					if(BaseLen <= 0)
						continue;
					if(str_comp_nocase_num(aDecoded, pBase, BaseLen) == 0 && aDecoded[BaseLen] == '_')
					{
						str_copy(aCategory, pBase, sizeof(aCategory));
						pBaseName = aDecoded + BaseLen + 1;
						break;
					}
				}
			}
		}
	}

	// If category is known and the CDN flattened "hud/foo" → "hud_foo", strip the prefix from the suggested name.
	if(aCategory[0] != '\0' && pBaseName == aDecoded)
	{
		const int CatLen = str_length(aCategory);
		if(CatLen > 0 && str_comp_nocase_num(aDecoded, aCategory, CatLen) == 0 && aDecoded[CatLen] == '_')
			pBaseName = aDecoded + CatLen + 1;
	}

	if(pNameOut && NameOutSize > 0)
		MediaSanitizeAssetName(pBaseName, pNameOut, NameOutSize);

	return MediaAssetCategoryIndexFromBase(aCategory);
}

// UClient: chat emoji reactions
static const char *const gs_apReactionEmojis[] = {
	"\xF0\x9F\x91\x8D", // thumbs up
	"\xF0\x9F\x91\x8E", // thumbs down
	"\xE2\x9D\xA4", // heart
	"\xF0\x9F\x98\x82", // joy
	"\xF0\x9F\x98\xAE", // open mouth
	"\xF0\x9F\x98\xA2", // cry
	"\xF0\x9F\x98\xA1", // angry
	"\xF0\x9F\x8E\x89", // party
	"\xF0\x9F\x94\xA5", // fire
	"\xF0\x9F\x91\x80", // eyes
	"\xF0\x9F\x99\x8F", // pray
	"\xF0\x9F\x92\xAF", // 100
	"\xF0\x9F\x98\xAD", // loud cry
	"\xF0\x9F\x98\x8D", // heart eyes
	"\xF0\x9F\xA4\x94", // thinking
	"\xF0\x9F\x98\x85", // sweat smile
	"\xF0\x9F\xA5\xB3", // partying face
	"\xF0\x9F\x98\x8E", // sunglasses
	"\xF0\x9F\x91\x8F", // clap
	"\xF0\x9F\x99\x8C", // raised hands
	"\xF0\x9F\x92\x80", // skull
	"\xE2\x9C\x85", // check
	"\xE2\x9D\x8C", // cross
	"\xE2\xAD\x90", // star
};
static constexpr int gs_NumReactionEmojis = (int)(sizeof(gs_apReactionEmojis) / sizeof(gs_apReactionEmojis[0]));
static constexpr int REACTION_PICKER_COLUMNS = 8;

uint64_t CChat::ComputeMessageHash(const char *pText)
{
	// FNV-1a 64-bit over the raw server-delivered message text. Both the reactor and
	// the message author compute this from the identical broadcast text, so the hash
	// lets peers agree on which message a reaction targets.
	uint64_t Hash = 1469598103934665603ull;
	for(const unsigned char *p = (const unsigned char *)pText; *p != '\0'; ++p)
	{
		Hash ^= (uint64_t)*p;
		Hash *= 1099511628211ull;
	}
	return Hash;
}

bool CChat::IsLocalClientId(int ClientId) const
{
	if(ClientId < 0)
		return false;
	for(int i = 0; i < NUM_DUMMIES; ++i)
	{
		if(GameClient()->m_aLocalIds[i] == ClientId)
			return true;
	}
	return false;
}

int CChat::FindLineForReaction(int TargetClientId, uint64_t MessageHash) const
{
	for(int i = 0; i < MAX_LINES; ++i)
	{
		const int LineIndex = ((m_CurrentLine - i) + MAX_LINES) % MAX_LINES;
		const CLine &Line = m_aLines[LineIndex];
		if(!Line.m_Initialized || Line.m_ClientId != TargetClientId)
			continue;
		if(ComputeMessageHash(Line.m_aText) == MessageHash)
			return LineIndex;
	}
	return -1;
}

void CChat::ApplyReactionToLineData(CLine &Line, const char *pEmoji, int ReactorClientId, const char *pReactorName, bool Add, const CUuid *pReactorKey)
{
	if(!pEmoji || pEmoji[0] == '\0')
		return;

	int Idx = -1;
	for(size_t i = 0; i < Line.m_vReactions.size(); ++i)
	{
		if(str_comp(Line.m_vReactions[i].m_aEmoji, pEmoji) == 0)
		{
			Idx = (int)i;
			break;
		}
	}

	// Prefer the live in-game name for same-server reactors; fall back to the name carried
	// in the reaction packet (remote reactors that aren't in our client list).
	auto ResolveReactorName = [&]() -> std::string {
		if(ReactorClientId >= 0 && ReactorClientId < MAX_CLIENTS && GameClient()->m_aClients[ReactorClientId].m_Active &&
			GameClient()->m_aClients[ReactorClientId].m_aName[0] != '\0')
			return GameClient()->m_aClients[ReactorClientId].m_aName;
		if(pReactorName && pReactorName[0] != '\0')
			return pReactorName;
		return "?";
	};

	if(Add)
	{
		if(Idx < 0)
		{
			CLine::SReaction Reaction;
			str_copy(Reaction.m_aEmoji, pEmoji);
			Line.m_vReactions.push_back(Reaction);
			Idx = (int)Line.m_vReactions.size() - 1;
		}
		auto &vIds = Line.m_vReactions[Idx].m_vReactorClientIds;
		auto &vNames = Line.m_vReactions[Idx].m_vReactorNames;
		auto &vKeys = Line.m_vReactions[Idx].m_vReactorKeys;
		bool Found = false;
		for(size_t i = 0; i < vIds.size(); ++i)
		{
			if(pReactorKey && *pReactorKey != UUID_ZEROED ?
					(i < vKeys.size() && vKeys[i] == *pReactorKey) :
					vIds[i] == ReactorClientId)
			{
				Found = true;
				break;
			}
		}
		if(!Found)
		{
			vIds.push_back(ReactorClientId);
			vNames.push_back(ResolveReactorName());
			vKeys.push_back(pReactorKey ? *pReactorKey : UUID_ZEROED);
		}
	}
	else if(Idx >= 0)
	{
		auto &vIds = Line.m_vReactions[Idx].m_vReactorClientIds;
		auto &vNames = Line.m_vReactions[Idx].m_vReactorNames;
		auto &vKeys = Line.m_vReactions[Idx].m_vReactorKeys;
		for(size_t i = 0; i < vIds.size(); ++i)
		{
			if(pReactorKey && *pReactorKey != UUID_ZEROED ?
					(i < vKeys.size() && vKeys[i] == *pReactorKey) :
					vIds[i] == ReactorClientId)
			{
				vIds.erase(vIds.begin() + i);
				if(i < vNames.size())
					vNames.erase(vNames.begin() + i);
				if(i < vKeys.size())
					vKeys.erase(vKeys.begin() + i);
				break;
			}
		}
		if(vIds.empty())
			Line.m_vReactions.erase(Line.m_vReactions.begin() + Idx);
	}

	// Force the line height/layout to be recomputed so the reaction row is (un)reserved.
	Line.m_aYOffset[0] = -1.0f;
	Line.m_aYOffset[1] = -1.0f;
}

void CChat::ToggleLocalReaction(int LineIndex, const char *pEmoji)
{
	if(LineIndex < 0 || LineIndex >= MAX_LINES)
		return;
	CLine &Line = m_aLines[LineIndex];
	if(!CanReactToLine(Line))
		return;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0)
		return;

	bool Mine = false;
	for(const auto &Reaction : Line.m_vReactions)
	{
		if(str_comp(Reaction.m_aEmoji, pEmoji) != 0)
			continue;
		for(int Id : Reaction.m_vReactorClientIds)
		{
			if(Id == LocalId)
			{
				Mine = true;
				break;
			}
		}
		break;
	}

	const bool Add = !Mine;
	const char *pLocalName = (LocalId >= 0 && LocalId < MAX_CLIENTS) ? GameClient()->m_aClients[LocalId].m_aName : "";
	ApplyReactionToLineData(Line, pEmoji, LocalId, pLocalName, Add);
	if(Line.m_UClient && Line.m_UClientMessageId != UUID_ZEROED)
		GameClient()->m_ClientIndicator.SendUClientChatReaction(Line.m_UClientMessageId, Line.m_aUClientServerAddress,
			(uint8_t)Line.m_UClientScope, Line.m_aUClientRoomId, pEmoji, Add);
	else
		GameClient()->m_ClientIndicator.SendChatReaction(Line.m_ClientId, ComputeMessageHash(Line.m_aText), pEmoji, Add);
}

float CChat::LayoutReactionRow(const CLine &Line, float FontSize, float AvailWidth, float OriginX, float OriginY, std::vector<SRenderRect> *pOutRects)
{
	if(pOutRects)
		pOutRects->clear();
	if(Line.m_vReactions.empty())
		return 0.0f;

	const float PillFont = FontSize * 0.85f;
	const float PillH = FontSize * 1.15f;
	const float PadX = FontSize * 0.34f;
	const float Gap = FontSize * 0.28f; // horizontal gap between pills
	const float RowGap = FontSize * 0.18f; // vertical gap between wrapped rows
	const float TopGap = FontSize * 0.32f; // gap above the row
	const float Space = FontSize * 0.22f; // emoji <-> count spacing
	const float Avail = maximum(PillH, AvailWidth);

	float x = 0.0f;
	int Row = 0;
	for(const CLine::SReaction &Reaction : Line.m_vReactions)
	{
		char aCount[12];
		str_format(aCount, sizeof(aCount), "%d", (int)Reaction.m_vReactorClientIds.size());
		const float EmojiW = TextRender()->TextWidth(PillFont, Reaction.m_aEmoji);
		const float CountW = TextRender()->TextWidth(PillFont, aCount);
		const float PillW = PadX + EmojiW + Space + CountW + PadX;
		if(x > 0.0f && x + PillW > Avail)
		{
			x = 0.0f;
			Row++;
		}
		if(pOutRects)
		{
			SRenderRect Rect;
			Rect.m_X = OriginX + x;
			Rect.m_Y = OriginY + TopGap + Row * (PillH + RowGap);
			Rect.m_W = PillW;
			Rect.m_H = PillH;
			pOutRects->push_back(Rect);
		}
		x += PillW + Gap;
	}
	const int Rows = Row + 1;
	return TopGap + Rows * PillH + (Rows - 1) * RowGap;
}

void CChat::OnChatReactionReceived(int TargetClientId, uint64_t MessageHash, const char *pEmoji, int ReactorClientId, const char *pReactorName, bool Add)
{
	// The server excludes the sender's own session, but our dummy is a separate session
	// on the same socket and would echo our reaction back to us. Ignore anything from a
	// local client id since we already applied it optimistically.
	if(IsLocalClientId(ReactorClientId))
		return;
	const int LineIndex = FindLineForReaction(TargetClientId, MessageHash);
	if(LineIndex < 0)
		return;
	ApplyReactionToLineData(m_aLines[LineIndex], pEmoji, ReactorClientId, pReactorName, Add);
}

void CChat::OnUClientReactionReceived(const CUuid &MessageId, const CUuid &ReactorKey, const char *pEmoji, const char *pReactorName, bool Add)
{
	if(MessageId == UUID_ZEROED || ReactorKey == UUID_ZEROED)
		return;
	for(int i = 0; i < MAX_LINES; ++i)
	{
		CLine &Line = m_aLines[((m_CurrentLine - i) + MAX_LINES) % MAX_LINES];
		if(!Line.m_Initialized || !Line.m_UClient || Line.m_UClientMessageId != MessageId)
			continue;
		ApplyReactionToLineData(Line, pEmoji, -1, pReactorName, Add, &ReactorKey);
		return;
	}
}

static std::string UcReaderKeyString(const CUuid &Key)
{
	char aBuf[UUID_MAXSTRSIZE];
	FormatUuid(Key, aBuf, sizeof(aBuf));
	return aBuf;
}

void CChat::OnChatReadReceived(const CUuid &ReaderKey, const char *pReaderName, const CUuid &MessageId)
{
	if(MessageId == UUID_ZEROED)
		return;
	// One marker per reader: the map key is the reader's stable client-instance uuid, so a
	// newer read simply moves that reader's marker to the newer message (KakaoTalk-style).
	SReadMarker &Marker = m_UcReadMarkers[UcReaderKeyString(ReaderKey)];
	Marker.m_MessageId = MessageId;
	Marker.m_Name = (pReaderName && pReaderName[0] != '\0') ? pReaderName : "?";
	Marker.m_Order = ++m_UcReadOrderCounter;
}

void CChat::OpenReactionPicker(int LineIndex, float X, float Y)
{
	if(LineIndex < 0 || LineIndex >= MAX_LINES)
		return;
	CLine &Line = m_aLines[LineIndex];
	if(!CanReactToLine(Line))
		return;

	m_ReactionPickerLineIndex = LineIndex;
	const int Rows = (gs_NumReactionEmojis + REACTION_PICKER_COLUMNS - 1) / REACTION_PICKER_COLUMNS;
	const float Cell = 20.0f;
	const float PopupW = REACTION_PICKER_COLUMNS * Cell + 10.0f;
	const float PopupH = Rows * Cell + 10.0f;
	Ui()->DoPopupMenu(&m_ReactionPickerPopupId, X, Y, PopupW, PopupH, this, PopupReactionPicker, {}, CUi::EButtonSoundType::DEFAULT);
}

CUi::EPopupMenuFunctionResult CChat::PopupReactionPicker(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;
	CMenus &Menus = pChat->GameClient()->m_Menus;
	const float Cell = 20.0f;
	pChat->m_vReactionPickerButtons.resize(gs_NumReactionEmojis);

	int i = 0;
	while(i < gs_NumReactionEmojis)
	{
		CUIRect Row;
		View.HSplitTop(Cell, &Row, &View);
		for(int c = 0; c < REACTION_PICKER_COLUMNS && i < gs_NumReactionEmojis; ++c, ++i)
		{
			CUIRect CellRect;
			Row.VSplitLeft(Cell, &CellRect, &Row);
			if(Menus.DoButton_Menu(&pChat->m_vReactionPickerButtons[i], gs_apReactionEmojis[i], 0, &CellRect))
			{
				pChat->ToggleLocalReaction(pChat->m_ReactionPickerLineIndex, gs_apReactionEmojis[i]);
				return CUi::POPUP_CLOSE_CURRENT;
			}
		}
	}
	return CUi::POPUP_KEEP_OPEN;
}

void CChat::OpenMediaContextMenu(int LineIndex, float X, float Y)
{
	if(LineIndex < 0 || LineIndex >= MAX_LINES)
		return;
	CLine &Line = m_aLines[LineIndex];
	if(!Line.m_Initialized || Line.m_MediaState != EMediaState::READY || Line.m_aMediaUrl[0] == '\0')
		return;

	m_MediaContextLineIndex = LineIndex;
	str_copy(m_aMediaContextUrl, Line.m_aMediaUrl, sizeof(m_aMediaContextUrl));
	m_MediaContextKind = Line.m_MediaKind;

	const bool AllowAsset = m_MediaContextKind == EMediaKind::PHOTO;
	const int ButtonCount = AllowAsset ? 3 : 1;
	const float RowHeight = 18.0f;
	const float Spacing = 2.0f;
	const float PopupW = 160.0f;
	// Popup border (1) + margin (4) on each side => 10px total vertical overhead.
	const float PopupH = ButtonCount * RowHeight + (ButtonCount - 1) * Spacing + 10.0f;

	Ui()->DoPopupMenu(&m_MediaContextPopupId, X, Y, PopupW, PopupH, this, PopupMediaContext, {}, CUi::EButtonSoundType::DEFAULT);
}

CUi::EPopupMenuFunctionResult CChat::PopupMediaContext(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;
	CMenus &Menus = pChat->GameClient()->m_Menus;
	const bool AllowAsset = pChat->m_MediaContextKind == EMediaKind::PHOTO;
	const float RowHeight = 18.0f;
	const float Spacing = 2.0f;
	CUIRect Row;

	View.HSplitTop(RowHeight, &Row, &View);
	if(Menus.DoButton_Menu(&pChat->m_aMediaContextButtons[0], Localize("Save to Computer"), 0, &Row))
	{
		pChat->BeginSaveToComputer();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	if(AllowAsset)
	{
		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(RowHeight, &Row, &View);
		if(Menus.DoButton_Menu(&pChat->m_aMediaContextButtons[1], Localize("Save to Assets"), 0, &Row))
		{
			char aClean[64];
			const int DetectedCategory = MediaDetectAssetCategoryFromUrl(pChat->m_aMediaContextUrl, aClean, sizeof(aClean));
			if(aClean[0] == '\0')
			{
				char aName[128];
				MediaExtractUrlFileName(pChat->m_aMediaContextUrl, aName, sizeof(aName));
				MediaSanitizeAssetName(aName, aClean, sizeof(aClean));
			}
			pChat->m_MediaAssetNameInput.Set(aClean);
			if(DetectedCategory >= 0)
				pChat->m_MediaAssetCategory = DetectedCategory;
			pChat->m_MediaAssetApply = true;
			pChat->Ui()->DoPopupMenu(&pChat->m_MediaSaveAssetPopupId, View.x, Row.y, 200.0f, 206.0f, pChat, PopupMediaSaveAsset, {}, CUi::EButtonSoundType::DEFAULT);
			return CUi::POPUP_CLOSE_CURRENT;
		}

		View.HSplitTop(Spacing, nullptr, &View);
		View.HSplitTop(RowHeight, &Row, &View);
		if(Menus.DoButton_Menu(&pChat->m_aMediaContextButtons[2], Localize("Save to Skin"), 0, &Row))
		{
			char aName[128], aClean[64];
			MediaExtractUrlFileName(pChat->m_aMediaContextUrl, aName, sizeof(aName));
			MediaSanitizeAssetName(aName, aClean, sizeof(aClean));
			pChat->m_MediaSkinNameInput.Set(aClean);
			pChat->m_MediaSkinApply = true;
			pChat->Ui()->DoPopupMenu(&pChat->m_MediaSaveSkinPopupId, View.x, Row.y, 200.0f, 98.0f, pChat, PopupMediaSaveSkin, {}, CUi::EButtonSoundType::DEFAULT);
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CChat::PopupMediaSaveAsset(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;
	CMenus &Menus = pChat->GameClient()->m_Menus;
	const float FontSize = 11.0f;
	const float Spacing = 4.0f;
	CUIRect Row;

	View.HSplitTop(14.0f, &Row, &View);
	pChat->Ui()->DoLabel(&Row, Localize("Save to asset"), 12.0f, TEXTALIGN_ML);

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(12.0f, &Row, &View);
	pChat->Ui()->DoLabel(&Row, Localize("Category"), FontSize, TEXTALIGN_ML);

	for(int r = 0; r < 4; ++r)
	{
		View.HSplitTop(2.0f, nullptr, &View);
		View.HSplitTop(18.0f, &Row, &View);
		CUIRect Left, Right;
		Row.VSplitMid(&Left, &Right, 4.0f);
		for(int c = 0; c < 2; ++c)
		{
			const int Index = r * 2 + c;
			const CUIRect &Cell = c == 0 ? Left : Right;
			if(Menus.DoButton_Menu(&pChat->m_aMediaAssetCategoryButtons[Index], Localize(gs_apMediaAssetCategoryNames[Index]), pChat->m_MediaAssetCategory == Index ? 1 : 0, &Cell))
				pChat->m_MediaAssetCategory = Index;
		}
	}

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(20.0f, &Row, &View);
	CUIRect NameLabel, NameBox;
	Row.VSplitLeft(45.0f, &NameLabel, &NameBox);
	pChat->Ui()->DoLabel(&NameLabel, Localize("Name"), FontSize, TEXTALIGN_ML);
	pChat->m_MediaAssetNameInput.SetEmptyText(Localize("asset name"));
	pChat->Ui()->DoClearableEditBox(&pChat->m_MediaAssetNameInput, &NameBox, 12.0f);

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	if(Menus.DoButton_CheckBox(&pChat->m_MediaAssetApplyButton, Localize("Apply immediately?"), pChat->m_MediaAssetApply ? 1 : 0, &Row))
		pChat->m_MediaAssetApply = !pChat->m_MediaAssetApply;

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(20.0f, &Row, &View);
	CUIRect CancelRect, ConfirmRect;
	Row.VSplitMid(&CancelRect, &ConfirmRect, 4.0f);
	if(Menus.DoButton_Menu(&pChat->m_MediaAssetCancelButton, Localize("Cancel"), 0, &CancelRect))
		return CUi::POPUP_CLOSE_CURRENT;
	if(Menus.DoButton_Menu(&pChat->m_MediaAssetConfirmButton, Localize("Confirm"), 0, &ConfirmRect))
	{
		pChat->BeginSaveToAsset();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CChat::PopupMediaSaveSkin(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;
	CMenus &Menus = pChat->GameClient()->m_Menus;
	const float FontSize = 11.0f;
	const float Spacing = 4.0f;
	CUIRect Row;

	View.HSplitTop(14.0f, &Row, &View);
	pChat->Ui()->DoLabel(&Row, Localize("Save to skin"), 12.0f, TEXTALIGN_ML);

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(20.0f, &Row, &View);
	CUIRect NameLabel, NameBox;
	Row.VSplitLeft(45.0f, &NameLabel, &NameBox);
	pChat->Ui()->DoLabel(&NameLabel, Localize("Name"), FontSize, TEXTALIGN_ML);
	pChat->m_MediaSkinNameInput.SetEmptyText(Localize("skin name"));
	pChat->Ui()->DoClearableEditBox(&pChat->m_MediaSkinNameInput, &NameBox, 12.0f);

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	if(Menus.DoButton_CheckBox(&pChat->m_MediaSkinApplyButton, Localize("Use immediately?"), pChat->m_MediaSkinApply ? 1 : 0, &Row))
		pChat->m_MediaSkinApply = !pChat->m_MediaSkinApply;

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(20.0f, &Row, &View);
	CUIRect CancelRect, ConfirmRect;
	Row.VSplitMid(&CancelRect, &ConfirmRect, 4.0f);
	if(Menus.DoButton_Menu(&pChat->m_MediaSkinCancelButton, Localize("Cancel"), 0, &CancelRect))
		return CUi::POPUP_CLOSE_CURRENT;
	if(Menus.DoButton_Menu(&pChat->m_MediaSkinConfirmButton, Localize("Confirm"), 0, &ConfirmRect))
	{
		pChat->BeginSaveToSkin();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CChat::BeginSaveToComputer()
{
	if(m_aMediaContextUrl[0] == '\0' || m_pMediaSaveRequest)
		return;

	char aDefaultName[128];
	MediaExtractUrlFileName(m_aMediaContextUrl, aDefaultName, sizeof(aDefaultName));
	if(aDefaultName[0] == '\0')
		str_copy(aDefaultName, "image.png");

	char aPath[512] = "";
	const int DialogResult = os_save_file_dialog(Localize("Save image"), aDefaultName, "PNG Image", "*.png", aPath, sizeof(aPath));
	if(DialogResult == 0 && aPath[0] != '\0')
	{
		str_copy(m_aMediaSaveComputerPath, aPath, sizeof(m_aMediaSaveComputerPath));
	}
	else if(DialogResult == 2)
	{
		// Native dialog unsupported on this platform: fall back to a downloads folder.
		Storage()->CreateFolder("downloads", IStorage::TYPE_SAVE);
		char aRel[192];
		str_format(aRel, sizeof(aRel), "downloads/%s", aDefaultName);
		char aAbs[512];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aRel, aAbs, sizeof(aAbs));
		str_copy(m_aMediaSaveComputerPath, aAbs, sizeof(m_aMediaSaveComputerPath));
	}
	else
	{
		// Cancelled or failed.
		return;
	}

	m_MediaSaveTarget = EMediaSaveTarget::COMPUTER;

	std::shared_ptr<CHttpRequest> pGet = HttpGet(m_aMediaContextUrl);
	pGet->Timeout(CTimeout{8000, 0, 4096, 8});
	pGet->MaxResponseSize(CHAT_MEDIA_MAX_RESPONSE_SIZE);
	pGet->FailOnErrorStatus(true);
	pGet->LogProgress(HTTPLOG::NONE);
	m_pMediaSaveRequest = pGet;
	Http()->Run(pGet);
}

void CChat::BeginSaveToAsset()
{
	if(m_aMediaContextUrl[0] == '\0' || m_pMediaSaveRequest)
		return;

	char aName[64];
	MediaSanitizeAssetName(m_MediaAssetNameInput.GetString(), aName, sizeof(aName));
	if(aName[0] == '\0')
	{
		Echo(Localize("Please enter a valid name."));
		return;
	}

	m_MediaSaveTarget = EMediaSaveTarget::ASSET;
	m_MediaSaveAssetCategory = std::clamp(m_MediaAssetCategory, 0, (int)std::size(gs_apMediaAssetCategoryDirs) - 1);
	str_copy(m_aMediaSaveName, aName, sizeof(m_aMediaSaveName));
	m_MediaSaveApply = m_MediaAssetApply;

	std::shared_ptr<CHttpRequest> pGet = HttpGet(m_aMediaContextUrl);
	pGet->Timeout(CTimeout{8000, 0, 4096, 8});
	pGet->MaxResponseSize(CHAT_MEDIA_MAX_RESPONSE_SIZE);
	pGet->FailOnErrorStatus(true);
	pGet->LogProgress(HTTPLOG::NONE);
	m_pMediaSaveRequest = pGet;
	Http()->Run(pGet);
}

void CChat::BeginSaveToSkin()
{
	if(m_aMediaContextUrl[0] == '\0' || m_pMediaSaveRequest)
		return;

	char aName[64];
	MediaSanitizeAssetName(m_MediaSkinNameInput.GetString(), aName, sizeof(aName));
	if(aName[0] == '\0')
	{
		Echo(Localize("Please enter a valid name."));
		return;
	}

	m_MediaSaveTarget = EMediaSaveTarget::SKIN;
	str_copy(m_aMediaSaveName, aName, sizeof(m_aMediaSaveName));
	m_MediaSaveApply = m_MediaSkinApply;

	std::shared_ptr<CHttpRequest> pGet = HttpGet(m_aMediaContextUrl);
	pGet->Timeout(CTimeout{8000, 0, 4096, 8});
	pGet->MaxResponseSize(CHAT_MEDIA_MAX_RESPONSE_SIZE);
	pGet->FailOnErrorStatus(true);
	pGet->LogProgress(HTTPLOG::NONE);
	m_pMediaSaveRequest = pGet;
	Http()->Run(pGet);
}

void CChat::UpdateMediaSave()
{
	if(!m_pMediaSaveRequest || !m_pMediaSaveRequest->Done())
		return;

	std::shared_ptr<CHttpRequest> pRequest = m_pMediaSaveRequest;
	m_pMediaSaveRequest = nullptr;

	if(pRequest->State() != EHttpState::DONE)
	{
		Echo(Localize("Failed to download the image."));
		return;
	}

	unsigned char *pResult = nullptr;
	size_t ResultSize = 0;
	pRequest->Result(&pResult, &ResultSize);
	if(pResult == nullptr || ResultSize == 0)
	{
		Echo(Localize("Failed to download the image."));
		return;
	}

	if(m_MediaSaveTarget == EMediaSaveTarget::COMPUTER)
	{
		IOHANDLE File = io_open(m_aMediaSaveComputerPath, IOFLAG_WRITE);
		if(!File)
		{
			Echo(Localize("Failed to save the image."));
			return;
		}
		io_write(File, pResult, ResultSize);
		io_close(File);
		Echo(Localize("Image saved."));
		return;
	}

	char aPath[192];
	if(m_MediaSaveTarget == EMediaSaveTarget::SKIN)
	{
		Storage()->CreateFolder("skins", IStorage::TYPE_SAVE);
		str_format(aPath, sizeof(aPath), "skins/%s.png", m_aMediaSaveName);
	}
	else
	{
		const int Category = std::clamp(m_MediaSaveAssetCategory, 0, (int)std::size(gs_apMediaAssetCategoryDirs) - 1);
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(gs_apMediaAssetCategoryDirs[Category], IStorage::TYPE_SAVE);
		str_format(aPath, sizeof(aPath), "%s/%s.png", gs_apMediaAssetCategoryDirs[Category], m_aMediaSaveName);
	}

	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		Echo(Localize("Failed to save the file."));
		return;
	}
	io_write(File, pResult, ResultSize);
	io_close(File);

	if(m_MediaSaveTarget == EMediaSaveTarget::SKIN)
	{
		if(m_MediaSaveApply)
		{
			GameClient()->RefreshSkins(CSkinDescriptor::FLAG_SIX);
			str_copy(g_Config.m_ClPlayerSkin, m_aMediaSaveName, sizeof(g_Config.m_ClPlayerSkin));
		}
		Echo(Localize("Skin saved."));
		return;
	}

	if(m_MediaSaveApply)
	{
		const int Category = std::clamp(m_MediaSaveAssetCategory, 0, (int)std::size(gs_apMediaAssetCategoryDirs) - 1);
		switch(Category)
		{
		case 0: // Entities
			str_copy(g_Config.m_ClAssetsEntities, m_aMediaSaveName, sizeof(g_Config.m_ClAssetsEntities));
			GameClient()->m_MapImages.ChangeEntitiesPath(m_aMediaSaveName);
			break;
		case 1: // Game
			str_copy(g_Config.m_ClAssetGame, m_aMediaSaveName, sizeof(g_Config.m_ClAssetGame));
			GameClient()->LoadGameSkin(m_aMediaSaveName);
			break;
		case 2: // Emoticons
			str_copy(g_Config.m_ClAssetEmoticons, m_aMediaSaveName, sizeof(g_Config.m_ClAssetEmoticons));
			GameClient()->LoadEmoticonsSkin(m_aMediaSaveName);
			break;
		case 3: // Particles
			str_copy(g_Config.m_ClAssetParticles, m_aMediaSaveName, sizeof(g_Config.m_ClAssetParticles));
			GameClient()->LoadParticlesSkin(m_aMediaSaveName);
			break;
		case 4: // HUD
			str_copy(g_Config.m_ClAssetHud, m_aMediaSaveName, sizeof(g_Config.m_ClAssetHud));
			GameClient()->LoadHudSkin(m_aMediaSaveName);
			break;
		case 5: // Extras
			str_copy(g_Config.m_ClAssetExtras, m_aMediaSaveName, sizeof(g_Config.m_ClAssetExtras));
			GameClient()->LoadExtrasSkin(m_aMediaSaveName);
			break;
		case 6: // Cursor
			str_copy(g_Config.m_ClAssetCursor, m_aMediaSaveName, sizeof(g_Config.m_ClAssetCursor));
			GameClient()->LoadCursorAsset(m_aMediaSaveName);
			break;
		case 7: // Arrow
			str_copy(g_Config.m_ClAssetArrow, m_aMediaSaveName, sizeof(g_Config.m_ClAssetArrow));
			GameClient()->LoadArrowAsset(m_aMediaSaveName);
			break;
		}
	}
	Echo(Localize("Asset saved."));
}


// ---------------------------------------------------------------------------
// UClient: shared background .map attachment card in chat
// ---------------------------------------------------------------------------

bool CChat::ExtractMapUrlFromText(const char *pText, char *pOutUrl, int UrlSize, char *pOutName, int NameSize)
{
	if(pOutUrl && UrlSize > 0)
		pOutUrl[0] = '\0';
	if(pOutName && NameSize > 0)
		pOutName[0] = '\0';
	if(!pText || !pOutUrl || UrlSize <= 0)
		return false;

	const char *pCur = pText;
	while(*pCur)
	{
		if(!IsUrlStart(pCur))
		{
			++pCur;
			continue;
		}

		const char *pEnd = pCur;
		while(!IsTokenEnd(*pEnd))
			++pEnd;

		std::string Url(pCur, pEnd - pCur);
		while(!Url.empty() && IsTrimmedUrlChar(Url.back()))
			Url.pop_back();

		const std::string Ext = ExtractUrlExtensionLower(Url);
		const std::string Host = ExtractUrlHostLower(Url);
		bool IsMap = Ext == "map";
		if(!IsMap && HostIsOrEndsWith(Host, "media.under1111.com"))
		{
			const size_t NamePos = Url.find("name=");
			if(NamePos != std::string::npos)
			{
				std::string NamePart = Url.substr(NamePos + 5);
				const size_t Amp = NamePart.find('&');
				if(Amp != std::string::npos)
					NamePart = NamePart.substr(0, Amp);
				const size_t Hash = NamePart.find('#');
				if(Hash != std::string::npos)
					NamePart = NamePart.substr(0, Hash);
				std::string Lower = NamePart;
				std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				if(Lower.size() >= 4 && Lower.compare(Lower.size() - 4, 4, ".map") == 0)
					IsMap = true;
			}
		}

		if(IsMap && (int)Url.size() < UrlSize)
		{
			str_copy(pOutUrl, Url.c_str(), UrlSize);
			if(pOutName && NameSize > 0)
			{
				MediaExtractUrlFileName(Url.c_str(), pOutName, NameSize);
				if(pOutName[0] == '\0')
					str_copy(pOutName, "map.map", NameSize);
				else if(!str_endswith(pOutName, ".map") && !str_endswith(pOutName, ".MAP"))
				{
					char aTmp[128];
					str_copy(aTmp, pOutName, sizeof(aTmp));
					str_format(pOutName, NameSize, "%s.map", aTmp);
				}
			}
			return true;
		}

		pCur = pEnd;
	}
	return false;
}

void CChat::SetMapAttachment(CLine &Line, const char *pUrl, const char *pFileName)
{
	if(!pUrl || pUrl[0] == '\0')
		return;

	str_copy(Line.m_aMapUrl, pUrl, sizeof(Line.m_aMapUrl));
	if(pFileName && pFileName[0] != '\0')
		str_copy(Line.m_aMapFileName, pFileName, sizeof(Line.m_aMapFileName));
	else
		MediaExtractUrlFileName(pUrl, Line.m_aMapFileName, sizeof(Line.m_aMapFileName));
	if(Line.m_aMapFileName[0] == '\0')
		str_copy(Line.m_aMapFileName, "map.map");

	Line.m_MapFileSize = -1;
	Line.m_aMapCardHeight[0] = 0.0f;
	Line.m_aMapCardHeight[1] = 0.0f;
	Line.m_MapCardRectValid = false;
	Line.m_MapDownloadBtnRectValid = false;
	Line.m_aYOffset[0] = -1.0f;
	Line.m_aYOffset[1] = -1.0f;

	if(Line.m_pMapSizeRequest)
	{
		Line.m_pMapSizeRequest->Abort();
		Line.m_pMapSizeRequest = nullptr;
	}

	std::shared_ptr<CHttpRequest> pHead = HttpHead(Line.m_aMapUrl);
	pHead->Timeout(CTimeout{5000, 0, 1024, 4});
	pHead->FailOnErrorStatus(false);
	pHead->LogProgress(HTTPLOG::NONE);
	pHead->MaxResponseSize(1024);
	Line.m_pMapSizeRequest = pHead;
	Http()->Run(pHead);
}

bool CChat::HasMapAttachment(const CLine &Line) const
{
	return Line.m_aMapUrl[0] != '\0';
}

void CChat::UpdateMapSizeRequests()
{
	for(auto &Line : m_aLines)
	{
		if(!Line.m_pMapSizeRequest || !Line.m_pMapSizeRequest->Done())
			continue;
		std::shared_ptr<CHttpRequest> pReq = Line.m_pMapSizeRequest;
		Line.m_pMapSizeRequest = nullptr;
		if(pReq->State() == EHttpState::DONE)
		{
			const double Size = pReq->Size();
			if(Size > 0.0)
			{
				Line.m_MapFileSize = (int64_t)Size;
				Line.m_aYOffset[0] = -1.0f;
				Line.m_aYOffset[1] = -1.0f;
			}
		}
	}
}

void CChat::OpenMapContextMenu(int LineIndex)
{
	if(LineIndex < 0 || LineIndex >= MAX_LINES)
		return;
	CLine &Line = m_aLines[LineIndex];
	if(!Line.m_Initialized || !HasMapAttachment(Line) || !Line.m_MapDownloadBtnRectValid)
		return;

	m_MapContextLineIndex = LineIndex;
	str_copy(m_aMapContextUrl, Line.m_aMapUrl, sizeof(m_aMapContextUrl));
	str_copy(m_aMapContextFileName, Line.m_aMapFileName, sizeof(m_aMapContextFileName));

	const float RowHeight = 18.0f;
	const float Spacing = 2.0f;
	const float PopupW = 140.0f;
	const float PopupH = 2 * RowHeight + Spacing + 10.0f;
	const float Gap = 4.0f;

	// Button rect is in chat screen space; popups use UI screen space.
	const float ChatH = 300.0f;
	const float ChatW = ChatH * Graphics()->ScreenAspect();
	const float ScaleX = Ui()->Screen()->w / ChatW;
	const float ScaleY = Ui()->Screen()->h / ChatH;
	const SRenderRect &Btn = Line.m_MapDownloadBtnRect;
	const float BtnX = Btn.m_X * ScaleX;
	const float BtnY = Btn.m_Y * ScaleY;
	const float BtnW = Btn.m_W * ScaleX;

	// POPUP_BORDER(1) + POPUP_MARGIN(4); SPopupMenu is private to CUi.
	constexpr float Margin = 5.0f;
	float PopupX = BtnX + BtnW + Gap;
	if(PopupX + PopupW > Ui()->Screen()->w - Margin)
		PopupX = BtnX - Gap - PopupW;
	const float PopupY = BtnY; // top edge aligned with the button

	SPopupMenuProperties Props;
	Props.m_FixedPosition = true;
	Ui()->DoPopupMenu(&m_MapContextPopupId, PopupX, PopupY, PopupW, PopupH, this, PopupMapContext, Props, CUi::EButtonSoundType::DEFAULT);
}

CUi::EPopupMenuFunctionResult CChat::PopupMapContext(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;
	CMenus &Menus = pChat->GameClient()->m_Menus;
	const float RowHeight = 18.0f;
	const float Spacing = 2.0f;
	CUIRect Row;

	View.HSplitTop(RowHeight, &Row, &View);
	if(Menus.DoButton_Menu(&pChat->m_aMapContextButtons[0], Localize("Download"), 0, &Row))
	{
		if(pChat->m_aMapContextUrl[0] != '\0')
		{
			Menus.PopupConfirmOpenLink(
				Localize("Download"),
				Localize("Are you sure you want to download?"),
				Localize("Yes"),
				Localize("Cancel"),
				pChat->m_aMapContextUrl,
				false);
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(RowHeight, &Row, &View);
	if(Menus.DoButton_Menu(&pChat->m_aMapContextButtons[1], Localize("Add to map"), 0, &Row))
	{
		char aName[128], aClean[64];
		str_copy(aName, pChat->m_aMapContextFileName, sizeof(aName));
		MediaSanitizeAssetName(aName, aClean, sizeof(aClean));
		if(aClean[0] == '\0')
			str_copy(aClean, "shared_map");
		pChat->m_MapAddNameInput.Set(aClean);
		pChat->m_MapAddUseAsBackground = true;
		pChat->Ui()->DoPopupMenu(&pChat->m_MapAddPopupId, View.x, Row.y, 220.0f, 110.0f, pChat, PopupMapAdd, {}, CUi::EButtonSoundType::DEFAULT);
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CChat::PopupMapAdd(void *pContext, CUIRect View, bool Active)
{
	CChat *pChat = static_cast<CChat *>(pContext);
	(void)Active;
	CMenus &Menus = pChat->GameClient()->m_Menus;
	const float FontSize = 11.0f;
	const float Spacing = 4.0f;
	CUIRect Row;

	View.HSplitTop(14.0f, &Row, &View);
	pChat->Ui()->DoLabel(&Row, Localize("Add to map"), 12.0f, TEXTALIGN_ML);

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(20.0f, &Row, &View);
	CUIRect NameLabel, NameBox;
	Row.VSplitLeft(45.0f, &NameLabel, &NameBox);
	pChat->Ui()->DoLabel(&NameLabel, Localize("Name"), FontSize, TEXTALIGN_ML);
	pChat->m_MapAddNameInput.SetEmptyText(Localize("map name"));
	pChat->Ui()->DoClearableEditBox(&pChat->m_MapAddNameInput, &NameBox, 12.0f);

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	if(Menus.DoButton_CheckBox(&pChat->m_MapAddUseBgButton, Localize("Use this map as background"), pChat->m_MapAddUseAsBackground ? 1 : 0, &Row))
		pChat->m_MapAddUseAsBackground = !pChat->m_MapAddUseAsBackground;

	View.HSplitTop(Spacing, nullptr, &View);
	View.HSplitTop(20.0f, &Row, &View);
	CUIRect CancelRect, ConfirmRect;
	Row.VSplitMid(&CancelRect, &ConfirmRect, 4.0f);
	if(Menus.DoButton_Menu(&pChat->m_MapAddCancelButton, Localize("Cancel"), 0, &CancelRect))
		return CUi::POPUP_CLOSE_CURRENT;
	if(Menus.DoButton_Menu(&pChat->m_MapAddConfirmButton, Localize("Confirm"), 0, &ConfirmRect))
	{
		pChat->BeginMapAddDownload();
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CChat::BeginMapAddDownload()
{
	if(m_aMapContextUrl[0] == '\0' || m_pMapSaveRequest)
		return;

	char aName[64];
	MediaSanitizeAssetName(m_MapAddNameInput.GetString(), aName, sizeof(aName));
	if(aName[0] == '\0')
	{
		Echo(Localize("Please enter a valid name."));
		return;
	}

	str_copy(m_aMapSaveName, aName, sizeof(m_aMapSaveName));
	m_MapSaveApplyBackground = m_MapAddUseAsBackground;

	const int64_t MaxBytes = g_Config.m_UcMapShareMaxBytes > 0 ? g_Config.m_UcMapShareMaxBytes : (32 * 1024 * 1024);
	std::shared_ptr<CHttpRequest> pGet = HttpGet(m_aMapContextUrl);
	pGet->Timeout(CTimeout{15000, 0, 4096, 16});
	pGet->MaxResponseSize(MaxBytes);
	pGet->FailOnErrorStatus(true);
	pGet->LogProgress(HTTPLOG::NONE);
	m_pMapSaveRequest = pGet;
	Http()->Run(pGet);
	Echo(Localize("Downloading map..."));
}

void CChat::UpdateMapSave()
{
	if(!m_pMapSaveRequest || !m_pMapSaveRequest->Done())
		return;

	std::shared_ptr<CHttpRequest> pRequest = m_pMapSaveRequest;
	m_pMapSaveRequest = nullptr;

	if(pRequest->State() != EHttpState::DONE)
	{
		Echo(Localize("Failed to download the map."));
		return;
	}

	unsigned char *pResult = nullptr;
	size_t ResultSize = 0;
	pRequest->Result(&pResult, &ResultSize);
	if(pResult == nullptr || ResultSize == 0)
	{
		Echo(Localize("Failed to download the map."));
		return;
	}

	Storage()->CreateFolder("maps", IStorage::TYPE_SAVE);
	char aPath[192];
	str_format(aPath, sizeof(aPath), "maps/%s.map", m_aMapSaveName);

	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		Echo(Localize("Failed to save the map."));
		return;
	}
	io_write(File, pResult, ResultSize);
	io_close(File);

	if(m_MapSaveApplyBackground)
	{
		str_copy(g_Config.m_ClBackgroundEntities, m_aMapSaveName, sizeof(g_Config.m_ClBackgroundEntities));
		g_Config.m_ClOverlayEntities = 100;
		GameClient()->m_Background.LoadBackground();
		Echo(Localize("Map saved and set as background."));
	}
	else
	{
		Echo(Localize("Map saved."));
	}
}
