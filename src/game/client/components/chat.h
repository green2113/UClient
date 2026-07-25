/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CHAT_H
#define GAME_CLIENT_COMPONENTS_CHAT_H
#include <base/str.h>

#include <engine/console.h>
#include <engine/external/regex.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/protocol.h>
#include <engine/shared/ringbuffer.h>
#include <engine/shared/uuid_manager.h>

#include <generated/protocol7.h>

#include <game/client/component.h>
#include <game/client/components/hud_layout.h>
#include <game/client/components/media_decoder.h>
#include <game/client/lineinput.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>

#include <engine/textrender.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "giphy_browser.h"
#include "uclient/chat_paste_image.h"
#include "uclient/settings_link.h"

class CTranslateResponse
{
public:
	bool m_Error = false;
	char m_Text[1024] = "";
	char m_Language[16] = "";
};

constexpr auto SAVES_FILE = "ddnet-saves.txt";

constexpr int MAX_LINE_LENGTH = 256; // Global constant for chat line length
class CHttpRequest;

// Shared with CGifBubbles so the above-head bubble matches the chat preview's rounded style.
void DrawRoundedMediaPreview(IGraphics *pGraphics, const IGraphics::CTextureHandle &Texture, float X, float Y, float W, float H, float Rounding, float Alpha);

class CChat : public CComponent
{
	static constexpr float CHAT_HEIGHT_FULL = 200.0f;
	static constexpr float CHAT_HEIGHT_MIN = 50.0f;
	static constexpr float CHAT_FONTSIZE_WIDTH_RATIO = 2.5f;

	enum
	{
		MAX_LINES = 64,
		MAX_LINE_LENGTH = ::MAX_LINE_LENGTH,
		CHAT_LINE_LENGTH = ::MAX_LINE_LENGTH,
	};

	enum class EMediaState
	{
		NONE = 0,
		QUEUED,
		LOADING,
		DECODING,
		READY,
		FAILED,
	};

	enum class EMediaKind
	{
		UNKNOWN = 0,
		PHOTO,
		ANIMATED,
		VIDEO,
	};

	struct SRenderRect
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
	};

	class CMediaDecodeJob;

	CLineInputBuffered<CHAT_LINE_LENGTH> m_Input;
	class CLine
	{
	public:
		CLine();
		void Reset(CChat &This);

		bool m_Initialized;
		int64_t m_Time;
		float m_aYOffset[2];
		int m_ClientId;
		int m_TeamNumber;
		bool m_Team;
		bool m_UClient;
		bool m_UClientFromCurrentServer;
		bool m_Whisper;
		int m_NameColor;
		char m_aName[64];
		char m_aText[CHAT_LINE_LENGTH];
		bool m_Friend;
		bool m_Highlighted;
		std::optional<ColorRGBA> m_CustomColor;

		STextContainerIndex m_TextContainerIndex;
		int m_QuadContainerIndex;

		std::shared_ptr<CManagedTeeRenderInfo> m_pManagedTeeRenderInfo;

		float m_TextYOffset;
		int m_SelectionStart;
		int m_SelectionEnd;

		int m_TimesRepeated;

		std::shared_ptr<CTranslateResponse> m_pTranslateResponse;
		std::vector<STextBoundingBox> m_vLinkBounds;
		std::vector<std::string> m_vLinks;
		std::vector<float> m_vLinkFontSizes;
		std::vector<bool> m_vLinkAlwaysConfirm;

		EMediaState m_MediaState;
		EMediaKind m_MediaKind;
		char m_aMediaUrl[512];
		std::vector<std::string> m_vMediaCandidates;
		int m_MediaCandidateIndex;
		int m_MediaRetryCount;
		std::shared_ptr<CHttpRequest> m_pMediaRequest;
		std::shared_ptr<CMediaDecodeJob> m_pMediaDecodeJob;
		std::optional<SMediaDecodedFrames> m_OptMediaDecodedFrames;
		int m_MediaUploadIndex;
		std::vector<SMediaFrame> m_vMediaFrames;
		std::vector<int> m_vMediaFrameEndMs;
		// Original encoded bytes of a static image, retained so the fullscreen viewer can decode
		// it at full resolution (the inline preview is capped to CHAT_MEDIA_MAX_DIMENSION).
		std::vector<unsigned char> m_vMediaOriginalData;
		int m_MediaTotalDurationMs;
		bool m_MediaAnimated;
		bool m_MediaRevealed;
		int m_MediaWidth;
		int m_MediaHeight;
		int m_MediaResolveDepth;
		int64_t m_MediaAnimationStart;
		float m_aTextHeight[2];
		float m_aMediaPreviewWidth[2];
		float m_aMediaPreviewHeight[2];
		SRenderRect m_NameRect;
		bool m_NameRectValid;
		SRenderRect m_TranslateRect;
		bool m_TranslateRectValid;
		SRenderRect m_TranslateLanguageRect;
		bool m_TranslateLanguageRectValid;
		SRenderRect m_MediaPreviewRect;
		bool m_MediaPreviewRectValid;
		SRenderRect m_MediaRetryRect;
		bool m_MediaRetryRectValid;
		// UClient: shared background .map attachment card
		char m_aMapUrl[512];
		char m_aMapFileName[128];
		int64_t m_MapFileSize;
		std::shared_ptr<CHttpRequest> m_pMapSizeRequest;
		float m_aMapCardHeight[2];
		SRenderRect m_MapCardRect;
		bool m_MapCardRectValid;
		SRenderRect m_MapDownloadBtnRect;
		bool m_MapDownloadBtnRectValid;

		bool m_HasReply;
		int m_ReplyToClientId;
		int m_ReplyMessageIndex;
		char m_aReplyToName[64];
		char m_aReplyPreview[64];
		char m_aReplyQuoteText[CHAT_LINE_LENGTH];
		char m_aDisplayText[CHAT_LINE_LENGTH];
		SRenderRect m_ReplyQuoteRect;
		bool m_ReplyQuoteRectValid;
		float m_ReplyQuoteHeight;
		SRenderRect m_LineRect;
		bool m_LineRectValid;
		SRenderRect m_ReplyButtonRect;
		bool m_ReplyButtonRectValid;
		float m_ReplyButtonAnchorX;
		float m_ReplyButtonAnchorY;
		bool m_ReplyButtonAnchorValid;
		float m_MessageFullWidth;

		// UClient: chat emoji reactions (Discord-like), relayed over the presence UDP server.
		struct SReaction
		{
			char m_aEmoji[16] = "";
			std::vector<int> m_vReactorClientIds; // game-server client ids of everyone who reacted
			std::vector<std::string> m_vReactorNames; // parallel to m_vReactorClientIds, for the hover tooltip
		};
		std::vector<SReaction> m_vReactions;
		std::vector<SRenderRect> m_vReactionRects; // one per reaction, screen-space, for click hit testing
		bool m_ReactionRectsValid;
		float m_aReactionRowHeight[2];

		// UClient chat read receipts. m_UClientMessageId is the globally-unique id carried in
		// the chat packet; m_UClientSeq is a local monotonic order used as the read high-water
		// mark; m_UClientMine marks our own sent lines (excluded from our own read marker).
		CUuid m_UClientMessageId = UUID_ZEROED;
		int m_UClientSeq = 0;
		bool m_UClientMine = false;
		SRenderRect m_ReadLabelRect{}; // screen-space rect of the "<name> read" label, for hover
		bool m_ReadLabelRectValid = false;

		// Set when the whole line is a single allowed gif-bubble-domain media link; drives the
		// floating gif bubble rendered above the sender's head (see CGifBubbles).
		bool m_ShowAboveHead;

		// UClient settings:// chat bubble
		bool m_HasSettingsLink = false;
		bool m_SettingsLinkMissing = false;
		bool m_SettingsLinkPageOnly = false;
		char m_aSettingsLinkUri[512] = "";
		CUClientSettingsLink::SParsed m_SettingsLinkParsed{};
		float m_aSettingsLinkHeight[2] = {};
		float m_aSettingsLinkWidth[2] = {};
		SRenderRect m_SettingsLinkRect{};
		bool m_SettingsLinkRectValid = false;
		SRenderRect m_SettingsShortcutRect{};
		bool m_SettingsShortcutRectValid = false;
	};

	bool m_PrevScoreBoardShowed;
	bool m_PrevShowChat;
	bool m_PrevModeActive;
	bool m_PrevChatSelectionActive;
	float m_PrevHudLayoutX;
	float m_PrevHudLayoutY;
	int m_PrevHudLayoutScale;
	bool m_PrevHudLayoutEnabled;

	CLine m_aLines[MAX_LINES];
	int m_CurrentLine;
	int m_BacklogCurLine;
	bool m_ScrollbarDragging;
	float m_ScrollbarDragOffset;
	std::optional<vec2> m_LastMousePos;
	bool m_MouseIsPress;
	vec2 m_MousePress;
	vec2 m_MouseRelease;
	bool m_HasSelection;
	bool m_WantsSelectionCopy;

	enum
	{
		// client IDs for special messages
		CLIENT_MSG = -2,
		SERVER_MSG = -1,
	};

	enum
	{
		MODE_NONE = 0,
		MODE_ALL,
		MODE_TEAM,
		MODE_UCLIENT,
	};

	enum
	{
		TEAM_UCLIENT = 4,
	};

	enum
	{
		CHAT_SERVER = 0,
		CHAT_HIGHLIGHT,
		CHAT_CLIENT,
		CHAT_NUM,
	};

	int m_Mode;
	bool m_Show;
	bool m_CompletionUsed;
	int m_CompletionChosen;
	char m_aCompletionBuffer[CHAT_LINE_LENGTH];
	int m_PlaceholderOffset;
	int m_PlaceholderLength;
	static char ms_aDisplayText[CHAT_LINE_LENGTH];
	class CRateablePlayer
	{
	public:
		int m_ClientId;
		int m_Score;
	};
	CRateablePlayer m_aPlayerCompletionList[MAX_CLIENTS];
	int m_PlayerCompletionListLength;

	struct CCommand
	{
		char m_aName[IConsole::TEMPCMD_NAME_LENGTH];
		char m_aParams[IConsole::TEMPCMD_PARAMS_LENGTH];
		char m_aHelpText[IConsole::TEMPCMD_HELP_LENGTH];

		CCommand() = default;
		CCommand(const char *pName, const char *pParams, const char *pHelpText)
		{
			str_copy(m_aName, pName);
			str_copy(m_aParams, pParams);
			str_copy(m_aHelpText, pHelpText);
		}

		bool operator<(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) < 0; }
		bool operator<=(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) <= 0; }
		bool operator==(const CCommand &Other) const { return str_comp(m_aName, Other.m_aName) == 0; }
	};

	std::vector<CCommand> m_vServerCommands;
	bool m_ServerCommandsNeedSorting;

	struct CHistoryEntry
	{
		int m_Team;
		char m_aText[1];
	};
	struct CPendingChatEntry
	{
		int m_Team;
		char m_aText[CHAT_LINE_LENGTH];
	};
	CHistoryEntry *m_pHistoryEntry;
	CStaticRingBuffer<CHistoryEntry, 64 * 1024, CRingBufferBase::FLAG_RECYCLE> m_History;
	std::vector<CPendingChatEntry> m_vPendingChatQueue;
	int64_t m_LastChatSend;
	int64_t m_aLastSoundPlayed[CHAT_NUM];
	bool m_IsInputCensored;
	char m_aCurrentInputText[CHAT_LINE_LENGTH];
	bool m_EditingNewLine;
	char m_aSavedInputText[CHAT_LINE_LENGTH];
	bool m_SavedInputPending;
	char m_aPreviousDisplayedInputText[CHAT_LINE_LENGTH];
	int64_t m_ChatOpenAnimationStart;
	struct STypingGlyphAnim
	{
		int64_t m_StartTime = 0;
		int m_ByteIndex = 0;
		int m_ByteLength = 0;
		char m_aText[16] = "";
	};
	std::vector<STypingGlyphAnim> m_vTypingGlyphAnims;

	// smoothly animated position of the chat input's blinking caret, replacing the input's own
	// instantly-teleporting one while typing animation is enabled
	vec2 m_CaretVisualPos = vec2(0.0f, 0.0f);
	vec2 m_CaretAnimFromPos = vec2(0.0f, 0.0f);
	vec2 m_CaretAnimTargetPos = vec2(0.0f, 0.0f);
	int64_t m_CaretAnimStartTime = 0;
	bool m_CaretAnimValid = false;
	int64_t m_CaretBlinkAnchor = 0;

	bool m_ServerSupportsCommandInfo;

	CButtonContainer m_TranslateSettingsButton;
	CButtonContainer m_TranslateSettingsEnableButton;
	CButtonContainer m_TranslateSettingsEnableOutgoingButton;
	CButtonContainer m_TranslateSettingsStripPunctuationButton;
	SPopupMenuId m_TranslateSettingsPopupId;
	bool m_TranslateButtonPressed;
	bool m_TranslateButtonRectValid;
	SRenderRect m_TranslateButtonRect; // chat-space, for ChatMousePos() click detection
	CUIRect m_TranslateButtonUiRect = {0, 0, 0, 0}; // UI-space, for DoPopupMenu
	int m_HoveredTranslateLineIndex = -1;
	std::string m_HoveredPlayerName;
	std::string m_HoveredLink;
	bool m_HoveredLinkAlwaysConfirm = false;
	int m_HoveredReplyLineIndex = -1;
	int m_HoveredSettingsShortcutLineIndex = -1;
	// UClient: reaction pill the mouse is over this frame, for the Discord-style reactor tooltip.
	int m_HoveredReactionLineIndex = -1;
	int m_HoveredReactionIndex = -1;
	SRenderRect m_HoveredReactionRect{};

	// UClient chat read receipts (KakaoTalk-style "read up to here" markers).
	struct SReadMarker
	{
		CUuid m_MessageId = UUID_ZEROED; // message this reader has read up to
		std::string m_Name; // reader's display name
		int m_Order = 0; // update order, so the newest reader can be shown as representative
	};
	std::unordered_map<std::string, SReadMarker> m_UcReadMarkers; // reader key (uuid string) -> marker
	int m_UcSeqCounter = 0; // monotonic order assigned to each added UClient line
	int m_UcReadOrderCounter = 0; // monotonic order assigned to each read marker update
	int m_UcLocalReadMarkerSeq = 0; // highest UClient seq we (locally) have read; 0 = none
	CUuid m_UcLocalReadMarkerMsgId = UUID_ZEROED; // message id matching m_UcLocalReadMarkerSeq
	// Read label the mouse is over this frame, for the reader-name tooltip.
	int m_HoveredReadLineIndex = -1;
	SRenderRect m_HoveredReadRect{};

	// Display name for a remote UClient chat line (ClientId == CLIENT_MSG). AddLine reads this
	// so the console log prints the sender's name instead of the "— " client-message dash.
	char m_aPendingUClientName[64] = "";

	bool m_PendingReplyActive;
	int m_PendingReplyClientId;
	int m_PendingReplySourceLineIndex;
	char m_aPendingReplyName[64];
	char m_aPendingReplyPreview[CHAT_LINE_LENGTH];
	bool m_UcReplySendScopePromptPending = false;
	int m_UcReplySendScopePromptClientId = -1;
	int m_UcReplySendScopePromptSourceLineIndex = -1;
	char m_aUcReplySendScopePromptName[64] = "";
	char m_aUcReplySendScopePromptPreview[CHAT_LINE_LENGTH] = "";
	CButtonContainer m_ReplyCancelButton;
	SRenderRect m_ReplyCancelButtonRect;
	bool m_ReplyCancelButtonRectValid;
	int64_t m_LastOutgoingReplyTime;
	char m_aLastOutgoingReplyWire[CHAT_LINE_LENGTH];
	int m_LastOutgoingReplyToClientId;
	int m_LastOutgoingReplyMessageIndex;
	char m_aLastOutgoingReplyToName[64];
	char m_aLastOutgoingReplyPreview[CHAT_LINE_LENGTH];
	char m_aLastOutgoingReplyBody[CHAT_LINE_LENGTH];

	enum class ELinkSafety
	{
		INVALID,
		SAFE,
		WARNING,
		DANGER,
	};

	struct CLinkPolicyCache
	{
		int64_t m_LastRefreshAttempt = 0;
		std::shared_ptr<class CHttpRequest> m_pRequest;
		std::unordered_set<std::string> m_vSafeDomains;
		std::unordered_set<std::string> m_vDangerDomains;
	};

	enum class ELinkPreflightRequestType
	{
		NONE,
		HEAD,
		GET,
	};

	struct CLinkPreflight
	{
		std::shared_ptr<class CHttpRequest> m_pRequest;
		std::string m_Link;
		bool m_AlwaysConfirm = false;
		ELinkPreflightRequestType m_RequestType = ELinkPreflightRequestType::NONE;
	};

	CLinkPolicyCache m_LinkPolicyCache;
	CLinkPreflight m_LinkPreflight;

	void UpdateLinkPolicy();
	void UpdateLinkPreflight();
	void StartLinkPreflight(const std::string &Link, bool AlwaysConfirm);
	bool IsLikelyPreflightDownload(const CHttpRequest &Request) const;
	void HandleLinkActivation(const std::string &Link, bool AlwaysConfirm);
	void ShowLinkPrompt(const std::string &Link, bool AlwaysConfirm, ELinkSafety Safety, bool IsDownloadLink);
	ELinkSafety ClassifyLink(const std::string &Link) const;

	bool m_HideMediaByBind;
	bool m_MediaViewerOpen;
	int m_MediaViewerLineIndex;
	float m_MediaViewerZoom;
	vec2 m_MediaViewerPan;
	bool m_MediaViewerDragging;
	vec2 m_MediaViewerDragStartMouse;
	vec2 m_MediaViewerPanStart;
	int64_t m_MediaViewerLastClickTime;
	// Full-resolution texture decoded on demand when the viewer opens (static photos only).
	IGraphics::CTextureHandle m_MediaViewerFullTexture;
	int m_MediaViewerFullTextureLine;

	static bool IsDirectMediaUrl(const char *pUrl);
	static void ExtractMediaUrlsFromText(const char *pText, std::vector<std::string> &vOutUrls);
	static EMediaKind MediaKindFromUrl(const char *pUrl);
	void SetMediaCandidates(CLine &Line, const std::vector<std::string> &vCandidates);
	void InsertMediaCandidates(CLine &Line, const std::vector<std::string> &vCandidates, int InsertIndex);
	bool QueueNextMediaCandidate(CLine &Line, const char *pReason);
	bool RetryMediaLine(CLine &Line);
	void ResetLineMedia(CLine &Line);
	void ResetHiddenMediaReveals();
	void QueueMediaDownload(CLine &Line);
	void StartMediaDownload(CLine &Line);
	bool StartMediaDecode(CLine &Line, EMediaKind MediaKind, const unsigned char *pData, size_t DataSize);
	void UpdateMediaDownloads();
	bool DecodeStaticImage(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line);
	bool DecodeAnimatedGif(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line);
	bool DecodeImageWithFfmpeg(const unsigned char *pData, size_t DataSize, const char *pContextName, CLine &Line, bool DecodeAllFrames, int MaxAnimationDurationMs);
	void CloseMediaViewer();
	void OpenMediaViewer(int LineIndex);
	void LoadMediaViewerFullTexture(CLine &Line);
	void FreeMediaViewerFullTexture();
	bool ValidateMediaViewerLine() const;
	bool GetCurrentFrameTexture(CLine &Line, IGraphics::CTextureHandle &Texture) const;
	bool GetMediaViewerTexture(CLine &Line, IGraphics::CTextureHandle &Texture) const;
	vec2 ChatMousePos() const;
	void ClampMediaViewerPan(const CLine &Line, float ScreenWidth, float ScreenHeight);
	bool GetMediaViewerRect(const CLine &Line, float ScreenWidth, float ScreenHeight, float &x, float &y, float &w, float &h) const;
	bool AnyMediaAllowed() const;
	bool IsMediaKindAllowed(EMediaKind Kind) const;
	bool IsMediaUrlAllowed(const char *pUrl) const;
	bool HasAllowedMediaCandidates(const CLine &Line) const;
	bool ShouldDisplayMediaSlot(const CLine &Line) const;
	bool ShouldHideMediaPreview(const CLine &Line) const;
	// Unlike ShouldHideMediaPreview (a spoiler toggle the user can click through), this is a hard
	// block: if we know a link is nsfw and the browser's "show NSFW" setting is off, there is no
	// click-to-reveal - the only way to see it is to enable that setting.
	bool ShouldHideNsfwMedia(const CLine &Line) const;
	std::string MediaPlaceholderText(const CLine &Line) const;
	std::string BuildVisibleMessageText(const CLine &Line, bool UseMediaLabelWhenEmpty) const;
	std::string BuildPlainTextLine(const CLine &Line) const;
	std::string BuildPlayerSearchUrl(const char *pPlayerName) const;
	bool ShouldHideLineFromStreamer(const CLine &Line) const;
	bool ShouldShowFriendMarker(const CLine &Line) const;
	void RenderTextLine(CLine &Line, float y, float fontSize, float lineWidth, float textBegin, float realMsgPaddingTee, float realMsgPaddingY, bool isScoreBoardOpen, float blend, std::string *pSelectionString);
	void OpenTranslateSettingsPopup(const CUIRect &ButtonRect);
	void RenderTranslateSettingsButton(const CUIRect &ButtonRect);
	static CUi::EPopupMenuFunctionResult PopupTranslateSettings(void *pContext, CUIRect View, bool Active);
	void SendChatQueuedInternal(int Team, const char *pLine);
	bool HasServerCommand(const char *pName) const;
	bool TryConvertWrongLayoutSlashCommand(const char *pLine, char *pOut, int OutSize) const;

	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSayTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConChat(IConsole::IResult *pResult, void *pUserData);
	static void ConShowChat(IConsole::IResult *pResult, void *pUserData);
	static void ConEcho(IConsole::IResult *pResult, void *pUserData);
	static void ConClearChat(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleHideChatMedia(IConsole::IResult *pResult, void *pUserData);
	static void ConAddCensorList(IConsole::IResult *pResult, void *pUserData);
	static void ConAddWhiteList(IConsole::IResult *pResult, void *pUserData);

	static void ConchainChatOld(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatFontSize(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainChatWidth(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainRegexPlayerWhitelist(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainUcChatShowSameServerOnly(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainUcChatSendSameServerOnly(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	static std::vector<std::string> SplitWords(const char *pMessage);
	Regex m_RegexPlayerWhitelist;

	bool LineShouldHighlight(const char *pLine, const char *pName);
	const char *GetLineDisplayText(const CLine &Line) const;
	const char *GetLineReplyQuoteText(const CLine &Line) const;
	int ComputeSenderRecentIndex(int SourceLineIndex, const char *pName, int ClientId = -1) const;
	bool TryResolveReplyQuoteByIndex(int ReplyLineIndex, const char *pReplyToName, int MessageIndex, char *pOut, int OutSize, int ReplyToClientId = -1) const;
	bool TryResolveReplyQuoteText(int ReplyClientId, const char *pReplyToName, const char *pWirePreview, char *pOut, int OutSize, int SkipLineIndex = -1) const;
	void SetPendingReply(int ClientId, const char *pName, int SourceLineIndex, const char *pQuoteText);
	void ClearPendingReply();
	void StashUcReplySendScopePrompt(int ClientId, const char *pName, int SourceLineIndex, const char *pQuoteText);
	void ApplyStashedUcReplyAfterSendScopePrompt();
	void ClearStashedUcReplySendScopePrompt();
	bool CanShowReplyButton(const CLine &Line) const;
	void ApplySettingsLinkToLine(CLine &Line, const char *pSourceText);
	float MeasureSettingsLinkHeight(const CLine &Line, float FontSize, float CardWidth = -1.0f) const;
	float MeasureSettingsLinkWidth(const CLine &Line, float FontSize) const;
	// Actual on-screen card width after clamping to the available column (scoreboard vs full chat).
	float SettingsCardRenderWidth(const CLine &Line, float FontSize, bool ScoreboardOpen, float AvailWidth, float RealMsgPaddingX) const;
	void RenderSettingsLinkBubble(CLine &Line, float X, float Y, float MaxWidth, float FontSize, float Blend);
	bool CanShowSettingsShortcut(const CLine &Line) const;
	bool TryHandleSettingsLinkClick(CLine &Line, vec2 MousePos, float FontSize);
	static bool LineNeedsNameColon(const CLine &Line);
	static bool LineNeedsTeePadding(const CLine &Line);
	float ReplyBannerHeight(float ScaledFontSize) const;
	void RenderReplyBanner(float x, float InputY, float ScaledFontSize);
	void ResetTypingAnimation();
	void SyncTypingAnimationBaseline();
	void RefreshTypingAnimation();
	bool WasChatAutoHidden() const;
	void StoreSave(const char *pText);
	void SetUiMousePos(vec2 Pos);

	// uclient: chat paste image
	CUClientChatPasteImage m_UcChatPaste;

	// Giphy GIF search
	CButtonContainer m_GiphyButton;
	CButtonContainer m_GiphySearchButton;
	std::vector<CButtonContainer> m_vGiphyResultButtons;
	CScrollRegion m_GiphyScrollRegion;
	vec2 m_GiphyScrollOffset;
	SPopupMenuId m_GiphyPopupId;
	bool m_GiphyButtonPressed;
	bool m_GiphyButtonRectValid;
	SRenderRect m_GiphyButtonRect;
	bool m_GiphyPopupHasStoredPos = false;
	vec2 m_GiphyPopupPos = vec2(0.0f, 0.0f);
	bool m_GiphyPopupDragging = false;
	vec2 m_GiphyPopupDragOffset = vec2(0.0f, 0.0f);

	CGiphyBrowser m_GiphyBrowser;
	CLineInputBuffered<128> m_GiphySearchInput;

	bool m_GiphySearching;
	bool m_GiphyLoadingMore;
	bool m_GiphyHasMoreResults;
	int m_GiphyNextPageToLoad;
	int m_GiphyRequestedPage;
	std::unordered_set<std::string> m_GiphyVisibleResultIds;
	std::string m_GiphyStatusText;
	std::shared_ptr<class CHttpRequest> m_pGiphyRequest;

	struct SGiphyPreviewEntry
	{
		EMediaState m_State = EMediaState::NONE;
		std::shared_ptr<class CHttpRequest> m_pRequest;
		std::shared_ptr<CMediaDecodeJob> m_pDecodeJob;
		std::vector<SMediaFrame> m_vFrames;
		int m_Width = 0;
		int m_Height = 0;
		bool m_Animated = false;
		int64_t m_AnimationStart = 0;
		int64_t m_LastUsedTick = 0;
	};
	std::unordered_map<std::string, SGiphyPreviewEntry> m_GiphyPreviewCache;
	void UpdateGiphyPreviewCache();

	void OpenGiphyPopup(const CUIRect &ButtonRect);
	void BeginGiphySearch(bool LoadMore = false);
	void UpdateGiphySearch();
	void ClearGiphyPreviewCache();
	bool GetGiphyPreviewTexture(const struct SGifResult &Result, IGraphics::CTextureHandle &Texture, int &Width, int &Height) const;
	static CUi::EPopupMenuFunctionResult PopupGiphyBrowser(void *pContext, CUIRect View, bool Active);
	void RenderGiphyButton(const CUIRect &ButtonRect);

	// Media context menu (right-click on inline chat image)
	SPopupMenuId m_MediaContextPopupId;
	SPopupMenuId m_MediaSaveAssetPopupId;
	SPopupMenuId m_MediaSaveSkinPopupId;
	int m_MediaContextLineIndex = -1;
	char m_aMediaContextUrl[512] = "";
	EMediaKind m_MediaContextKind = EMediaKind::UNKNOWN;
	int m_MediaAssetCategory = 0;
	bool m_MediaAssetApply = true;
	CLineInputBuffered<64> m_MediaAssetNameInput;
	bool m_MediaSkinApply = true;
	CLineInputBuffered<64> m_MediaSkinNameInput;
	CButtonContainer m_aMediaContextButtons[3];
	CButtonContainer m_aMediaAssetCategoryButtons[8];
	CButtonContainer m_MediaAssetApplyButton;
	CButtonContainer m_MediaAssetConfirmButton;
	CButtonContainer m_MediaAssetCancelButton;
	CButtonContainer m_MediaSkinApplyButton;
	CButtonContainer m_MediaSkinConfirmButton;
	CButtonContainer m_MediaSkinCancelButton;

	enum class EMediaSaveTarget
	{
		COMPUTER = 0,
		ASSET,
		SKIN,
	};
	std::shared_ptr<class CHttpRequest> m_pMediaSaveRequest;
	EMediaSaveTarget m_MediaSaveTarget = EMediaSaveTarget::COMPUTER;
	char m_aMediaSaveComputerPath[512] = "";
	int m_MediaSaveAssetCategory = 0;
	char m_aMediaSaveName[64] = "";
	bool m_MediaSaveApply = false;

	void OpenMediaContextMenu(int LineIndex, float X, float Y);
	void BeginSaveToComputer();
	void BeginSaveToAsset();
	void BeginSaveToSkin();
	void UpdateMediaSave();
	static CUi::EPopupMenuFunctionResult PopupMediaContext(void *pContext, CUIRect View, bool Active);
	static CUi::EPopupMenuFunctionResult PopupMediaSaveAsset(void *pContext, CUIRect View, bool Active);
	static CUi::EPopupMenuFunctionResult PopupMediaSaveSkin(void *pContext, CUIRect View, bool Active);

	// UClient: background map share card in chat
	SPopupMenuId m_MapContextPopupId;
	SPopupMenuId m_MapAddPopupId;
	int m_MapContextLineIndex = -1;
	char m_aMapContextUrl[512] = "";
	char m_aMapContextFileName[128] = "";
	CLineInputBuffered<64> m_MapAddNameInput;
	bool m_MapAddUseAsBackground = true;
	CButtonContainer m_aMapContextButtons[2];
	CButtonContainer m_MapAddConfirmButton;
	CButtonContainer m_MapAddCancelButton;
	CButtonContainer m_MapAddUseBgButton;
	std::shared_ptr<class CHttpRequest> m_pMapSaveRequest;
	char m_aMapSaveName[64] = "";
	bool m_MapSaveApplyBackground = false;

	static bool ExtractMapUrlFromText(const char *pText, char *pOutUrl, int UrlSize, char *pOutName, int NameSize);
	void SetMapAttachment(CLine &Line, const char *pUrl, const char *pFileName);
	bool HasMapAttachment(const CLine &Line) const;
	void OpenMapContextMenu(int LineIndex);
	void BeginMapAddDownload();
	void UpdateMapSave();
	void UpdateMapSizeRequests();
	static CUi::EPopupMenuFunctionResult PopupMapContext(void *pContext, CUIRect View, bool Active);
	static CUi::EPopupMenuFunctionResult PopupMapAdd(void *pContext, CUIRect View, bool Active);

	// UClient: chat emoji reactions
	SPopupMenuId m_ReactionPickerPopupId;
	int m_ReactionPickerLineIndex = -1;
	std::vector<CButtonContainer> m_vReactionPickerButtons;
	static uint64_t ComputeMessageHash(const char *pText);
	int FindLineForReaction(int TargetClientId, uint64_t MessageHash) const;
	bool IsLocalClientId(int ClientId) const;
	void ApplyReactionToLineData(CLine &Line, const char *pEmoji, int ReactorClientId, const char *pReactorName, bool Add);
	void ToggleLocalReaction(int LineIndex, const char *pEmoji);
	// Lays out the reaction pill row. Returns the total height it occupies (0 if none).
	// When pOutRects is set, fills it with absolute pill rects anchored at (OriginX, OriginY).
	float LayoutReactionRow(const CLine &Line, float FontSize, float AvailWidth, float OriginX, float OriginY, std::vector<SRenderRect> *pOutRects);
	void OpenReactionPicker(int LineIndex, float X, float Y);
	static CUi::EPopupMenuFunctionResult PopupReactionPicker(void *pContext, CUIRect View, bool Active);

	friend class CBindChat;
	friend class CTranslate;
	friend class CBestClient;
	friend class CTClient;
	friend class CUClientChatPasteImage;
	friend class CMenus;
	friend class CGifBubbles;
	friend class CChatBubbles;

public:
	CChat();
	int Sizeof() const override { return sizeof(*this); }

	static constexpr float MESSAGE_TEE_PADDING_RIGHT = 0.5f;

	bool IsActive() const { return m_Mode != MODE_NONE; }
	void AddLine(int ClientId, int Team, const char *pLine);
	void AddUClientChatLine(const char *pName, int SuggestedClientId, const char *pLine, const char *pServerAddress,
		const CUuid &MessageId, bool Mine,
		const char *pSkinName = nullptr, int UseCustomColor = 0, int ColorBody = 0, int ColorFeet = 0);
	const char *FilterText(const char *pMessage, int ClientId = -2, bool IsChat = false);
	void EnableMode(int Team);
	void DisableMode();
	void RegisterCommand(const char *pName, const char *pParams, const char *pHelpText);
	void UnregisterCommand(const char *pName);
	void Echo(const char *pString);

	void OnWindowResize() override;
	void OnConsoleInit() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
	void OnPrepareLines(float y, int StartLine, int HoveredTranslateLineIndex = -1);
	void Reset();
	void OnRelease() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnInit() override;

	// Called by the client indicator when a reaction broadcast arrives over UDP.
	void OnChatReactionReceived(int TargetClientId, uint64_t MessageHash, const char *pEmoji, int ReactorClientId, const char *pReactorName, bool Add);

	// Called by the client indicator when a read-receipt broadcast arrives over UDP.
	void OnChatReadReceived(const CUuid &ReaderKey, const char *pReaderName, const CUuid &MessageId);

	void RebuildChat();
	void ClearLines();
	void RenderHud(bool ForcePreview = false);
	CUIRect GetHudRect(float HudWidth, float HudHeight, bool ForcePreview = false) const;

	void EnsureCoherentFontSize() const;
	void EnsureCoherentWidth() const;

	float FontSize() const { return (g_Config.m_ClChatFontSize / 10.0f) * std::clamp(g_Config.m_BcHudChatScale / 100.0f, 0.25f, 3.0f); }
	float ChatWidth() const { return g_Config.m_ClChatWidth * std::clamp(g_Config.m_BcHudChatScale / 100.0f, 0.25f, 3.0f); }
	float MessagePaddingX() const { return FontSize() * (5 / 6.f); }
	float MessagePaddingY() const { return FontSize() * (1 / 6.f); }
	float MessageTeeSize() const { return FontSize() * (7 / 6.f); }
	float MessageRounding() const { return FontSize() * 0.38f; }

	// ----- send functions -----

	// Sends a chat message to the server.
	//
	// @param Team MODE_ALL=0 MODE_TEAM=1
	// @param pLine the chat message
	void SendChat(int Team, const char *pLine);

	// Sends a chat message to the server.
	//
	// It uses a queue with a maximum of 3 entries
	// that ensures there is a minimum delay of one second
	// between sent messages.
	//
	// It uses team or public chat depending on m_Mode.
	void SendChatQueued(const char *pLine);
	void MaybeOfferAutoLoginFromChat(const char *pLine);
	void SendTranslatedChatQueued(int Team, const char *pLine);
	void AddHistoryEntry(int Team, const char *pLine);
	void SendChatPayloadQueued(int Team, const char *pLine);

	// BestClient
	bool LineHighlighted(int ClientId, const char *pLine);
};
#endif
