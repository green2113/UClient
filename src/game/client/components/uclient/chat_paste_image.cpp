#include "chat_paste_image.h"

#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/textrender.h>

#include <game/client/components/chat.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

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

static void DrawRoundedMediaPreview(IGraphics *pGraphics, const IGraphics::CTextureHandle &Texture, float X, float Y, float W, float H, float Rounding, float Alpha)
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


void CUClientChatPasteImage::Reset(CChat *pChat)
{
	m_WarningPendingClipboardImage = {};
	if(pChat->GameClient()->m_Menus.IsActive())
		pChat->GameClient()->m_Menus.SetActive(false);
	ClearPendingUploadImage(pChat);
}

void CUClientChatPasteImage::OnUpdate(CChat *pChat)
{
	UpdatePendingUpload(pChat);
}

bool CUClientChatPasteImage::OnRenderEditor(CChat *pChat)
{
	if(!m_ImageEditor.m_Active || !m_PendingUploadImage.HasImage())
		return false;

	const vec2 WindowSize(maximum(1.0f, (float)pChat->Graphics()->WindowWidth()), maximum(1.0f, (float)pChat->Graphics()->WindowHeight()));
	pChat->Graphics()->MapScreen(0.0f, 0.0f, WindowSize.x, WindowSize.y);
	RenderImageEditor(pChat);

	const float Height = 300.0f;
	const float Width = Height * pChat->Graphics()->ScreenAspect();
	pChat->Graphics()->MapScreen(0.0f, 0.0f, Width, Height);
	return true;
}

bool CUClientChatPasteImage::TrySendOnEnter(CChat *pChat, const char *pMessagePrefix)
{
	if(!m_PendingUploadImage.HasImage())
		return false;
	StartPendingUpload(pChat, pMessagePrefix);
	return true;
}

bool CUClientChatPasteImage::OnInput(CChat *pChat, const IInput::CEvent &Event)
{
	const bool ChatInputActive = pChat->m_Mode != CChat::MODE_NONE;

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE && m_ImageEditor.m_Active)
	{
		CancelImageEditor(pChat);
		return true;
	}

	if(ChatInputActive && Event.m_Key == KEY_MOUSE_1 && m_PendingUploadCloseRectValid)
	{
		const vec2 MousePos = pChat->ChatMousePos();
		const bool InsideCloseButton = MousePos.x >= m_PendingUploadCloseRect.m_X && MousePos.x <= m_PendingUploadCloseRect.m_X + m_PendingUploadCloseRect.m_W &&
			MousePos.y >= m_PendingUploadCloseRect.m_Y && MousePos.y <= m_PendingUploadCloseRect.m_Y + m_PendingUploadCloseRect.m_H;

		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			m_PendingUploadClosePressed = InsideCloseButton;
			if(InsideCloseButton)
				return true;
		}
		else if(Event.m_Flags & IInput::FLAG_RELEASE)
		{
			const bool ActivateButton = m_PendingUploadClosePressed && InsideCloseButton;
			m_PendingUploadClosePressed = false;
			if(ActivateButton)
			{
				ClearPendingUploadImage(pChat);
				return true;
			}
		}
	}

	if(ChatInputActive && (Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_MOUSE_1 && m_ImageEditorEditButtonRectValid)
	{
		const vec2 MousePos = pChat->ChatMousePos();
		const bool InsideEditButton = MousePos.x >= m_ImageEditorEditButtonRect.m_X && MousePos.x <= m_ImageEditorEditButtonRect.m_X + m_ImageEditorEditButtonRect.m_W &&
			MousePos.y >= m_ImageEditorEditButtonRect.m_Y && MousePos.y <= m_ImageEditorEditButtonRect.m_Y + m_ImageEditorEditButtonRect.m_H;
		if(InsideEditButton)
		{
			OpenImageEditor(pChat);
			return true;
		}
	}

	return false;
}

bool CUClientChatPasteImage::TryHandlePasteKey(CChat *pChat, const IInput::CEvent &Event)
{
	if(pChat->m_Mode == CChat::MODE_NONE)
		return false;
	if(!(Event.m_Flags & IInput::FLAG_PRESS) || Event.m_Key != KEY_V || !pChat->Input()->ModifierIsPressed())
		return false;
	return TryPasteClipboardImage(pChat);
}

bool CUClientChatPasteImage::TryPasteFromClipboard(CChat *pChat)
{
	return TryPasteClipboardImage(pChat);
}

bool CUClientChatPasteImage::IsPasteWarningPending() const
{
	return m_WarningPendingClipboardImage.IsValid();
}

void CUClientChatPasteImage::ClearPendingUploadImage(CChat *pChat)
{
	if(m_PendingUploadImage.m_pRequest)
	{
		m_PendingUploadImage.m_pRequest->Abort();
		m_PendingUploadImage.m_pRequest.reset();
	}
	if(m_PendingUploadImage.m_Texture.IsValid())
		pChat->Graphics()->UnloadTexture(&m_PendingUploadImage.m_Texture);
	if(m_PendingUploadImage.m_OriginalTexture.IsValid())
		pChat->Graphics()->UnloadTexture(&m_PendingUploadImage.m_OriginalTexture);
	m_PendingUploadImage = CUClientChatPasteImage::SPendingUploadImage();
	m_PendingUploadClosePressed = false;
	m_PendingUploadCloseRectValid = false;
	m_ImageEditor.m_vStrokes.clear();
	m_ImageEditor.m_vStrokeSnapshot.clear();
	m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
	m_ImageEditor.m_IsDrawing = false;
	m_ImageEditor.m_Active = false;
	m_ImageEditor.m_MouseDownLastFrame = false;
}

bool CUClientChatPasteImage::SetPendingUploadImage(CChat *pChat, const IInput::SClipboardImage &Image)
{
	if(!Image.IsValid())
		return false;

	CImageInfo ClipboardImage;
	ClipboardImage.m_Width = (size_t)Image.m_Width;
	ClipboardImage.m_Height = (size_t)Image.m_Height;
	ClipboardImage.m_Format = CImageInfo::FORMAT_RGBA;
	ClipboardImage.m_pData = static_cast<uint8_t *>(malloc(Image.m_vRgba.size()));
	if(ClipboardImage.m_pData == nullptr)
		return false;
	mem_copy(ClipboardImage.m_pData, Image.m_vRgba.data(), Image.m_vRgba.size());

	CByteBufferWriter Writer;
	if(!CImageLoader::SavePng(Writer, ClipboardImage))
	{
		ClipboardImage.Free();
		return false;
	}

	if(g_Config.m_UcChatPasteUploadMaxBytes > 0 && Writer.Size() > (size_t)g_Config.m_UcChatPasteUploadMaxBytes)
	{
		ClipboardImage.Free();
		pChat->Echo("Pasted image exceeds the configured upload size limit.");
		return false;
	}

	ClearPendingUploadImage(pChat);
	m_PendingUploadImage.m_OriginalTexture = pChat->Graphics()->LoadTextureRaw(ClipboardImage, 0, "chat-paste-upload-original");
	if(!m_PendingUploadImage.m_OriginalTexture.IsValid())
	{
		if(ClipboardImage.m_pData != nullptr)
			ClipboardImage.Free();
		return false;
	}
	m_PendingUploadImage.m_Texture = pChat->Graphics()->LoadTextureRawMove(ClipboardImage, 0, "chat-paste-upload");
	if(!m_PendingUploadImage.m_Texture.IsValid())
	{
		if(m_PendingUploadImage.m_OriginalTexture.IsValid())
			pChat->Graphics()->UnloadTexture(&m_PendingUploadImage.m_OriginalTexture);
		if(ClipboardImage.m_pData != nullptr)
			ClipboardImage.Free();
		return false;
	}

	m_PendingUploadImage.m_vPng.assign(Writer.Data(), Writer.Data() + Writer.Size());
	m_PendingUploadImage.m_vOriginalPng = m_PendingUploadImage.m_vPng;
	m_PendingUploadImage.m_Width = Image.m_Width;
	m_PendingUploadImage.m_Height = Image.m_Height;
	m_PendingUploadImage.m_State = CUClientChatPasteImage::EPendingUploadState::READY;
	m_ImageEditor.m_vStrokes.clear();
	m_ImageEditor.m_vStrokeSnapshot.clear();
	m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
	m_ImageEditor.m_IsDrawing = false;
	m_ImageEditor.m_Active = false;
	m_ImageEditor.m_MouseDownLastFrame = false;
	return true;
}

bool CUClientChatPasteImage::TryPasteClipboardImage(CChat *pChat)
{
	if(!g_Config.m_UcChatPasteUpload || m_PendingUploadImage.m_State == CUClientChatPasteImage::EPendingUploadState::UPLOADING)
		return false;

	IInput::SClipboardImage ClipboardImage;
	if(!pChat->Input()->GetClipboardImage(ClipboardImage))
		return false;

	if(!g_Config.m_UcChatPasteImageWarningSkip)
	{
		m_WarningPendingClipboardImage = std::move(ClipboardImage);
		OpenPasteWarningPopup(pChat);
		return true;
	}

	if(!SetPendingUploadImage(pChat, ClipboardImage))
		pChat->Echo("Unable to attach pasted image.");
	return true;
}

bool CUClientChatPasteImage::StartPendingUpload(CChat *pChat, const char *pMessagePrefix)
{
	if(!m_PendingUploadImage.HasImage())
		return false;
	if(m_PendingUploadImage.m_State == CUClientChatPasteImage::EPendingUploadState::UPLOADING)
		return true;
	if(g_Config.m_UcChatPasteUploadUrl[0] == '\0')
	{
		m_PendingUploadImage.m_State = CUClientChatPasteImage::EPendingUploadState::FAILED;
		str_copy(m_PendingUploadImage.m_aError, "Upload URL is not configured.", sizeof(m_PendingUploadImage.m_aError));
		pChat->Echo(m_PendingUploadImage.m_aError);
		return true;
	}

	std::shared_ptr<CHttpRequest> pPost = HttpPost(g_Config.m_UcChatPasteUploadUrl, m_PendingUploadImage.m_vPng.data(), m_PendingUploadImage.m_vPng.size());
	pPost->Header("Content-Type: image/png");
	pPost->Header("Accept: application/json");
	pPost->FailOnErrorStatus(false);
	pPost->MaxResponseSize(64 * 1024);
	pPost->LogProgress(HTTPLOG::FAILURE);

	m_PendingUploadImage.m_Team = pChat->m_Mode == CChat::MODE_TEAM ? 1 : 0;
	str_copy(m_PendingUploadImage.m_aMessagePrefix, pMessagePrefix ? pMessagePrefix : "", sizeof(m_PendingUploadImage.m_aMessagePrefix));
	m_PendingUploadImage.m_pRequest = pPost;
	m_PendingUploadImage.m_State = CUClientChatPasteImage::EPendingUploadState::UPLOADING;
	m_PendingUploadImage.m_aError[0] = '\0';
	pChat->m_Input.Clear();
	pChat->m_SavedInputPending = false;
	pChat->m_aSavedInputText[0] = '\0';
	pChat->m_pHistoryEntry = nullptr;
	pChat->Http()->Run(pPost);
	return true;
}

void CUClientChatPasteImage::UpdatePendingUpload(CChat *pChat)
{
	if(!m_PendingUploadImage.m_pRequest || !m_PendingUploadImage.m_pRequest->Done())
		return;

	std::shared_ptr<CHttpRequest> pRequest = m_PendingUploadImage.m_pRequest;
	m_PendingUploadImage.m_pRequest.reset();

	char aUrl[512] = "";
	bool Success = false;
	if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() >= 200 && pRequest->StatusCode() < 400)
	{
		json_value *pRoot = pRequest->ResultJson();
		if(pRoot != nullptr && pRoot->type == json_object)
		{
			const json_value &UrlValue = (*pRoot)["url"];
			if(UrlValue.type == json_string)
			{
				str_copy(aUrl, UrlValue.u.string.ptr, sizeof(aUrl));
				Success = aUrl[0] != '\0';
			}
			if(!Success)
			{
				const json_value &PublicUrlValue = (*pRoot)["publicUrl"];
				if(PublicUrlValue.type == json_string)
				{
					str_copy(aUrl, PublicUrlValue.u.string.ptr, sizeof(aUrl));
					Success = aUrl[0] != '\0';
				}
			}
		}
	}

	if(Success)
	{
		char aLine[256];
		if(m_PendingUploadImage.m_aMessagePrefix[0] != '\0')
			str_format(aLine, sizeof(aLine), "%s %s", m_PendingUploadImage.m_aMessagePrefix, aUrl);
		else
			str_copy(aLine, aUrl, sizeof(aLine));

		pChat->AddHistoryEntry(m_PendingUploadImage.m_Team, aLine);
		if(!pChat->GameClient()->m_Translate.TryTranslateOutgoingChat(m_PendingUploadImage.m_Team, aLine))
			pChat->SendChatPayloadQueued(m_PendingUploadImage.m_Team, aLine);
		pChat->DisableMode();
		pChat->GameClient()->OnRelease();
		ClearPendingUploadImage(pChat);
		return;
	}

	m_PendingUploadImage.m_State = CUClientChatPasteImage::EPendingUploadState::FAILED;
	if(m_PendingUploadImage.m_aError[0] == '\0')
		str_format(m_PendingUploadImage.m_aError, sizeof(m_PendingUploadImage.m_aError), "Upload failed (HTTP %d)", pRequest->StatusCode());
	pChat->Echo(m_PendingUploadImage.m_aError);
}

void CUClientChatPasteImage::PendingUploadPreviewSize(CChat *pChat, float Width, float FontSize, float &PreviewW, float &PreviewH) const
{
	if(!m_PendingUploadImage.HasImage())
	{
		PreviewW = 0.0f;
		PreviewH = 0.0f;
		return;
	}

	const float MaxPreviewW = minimum(Width, 120.0f);
	const float MaxPreviewH = maximum(56.0f, FontSize * 5.0f);
	const float Aspect = m_PendingUploadImage.m_Height > 0 ? (float)m_PendingUploadImage.m_Width / (float)m_PendingUploadImage.m_Height : 1.0f;
	PreviewW = MaxPreviewW;
	PreviewH = PreviewW / maximum(0.1f, Aspect);
	if(PreviewH > MaxPreviewH)
	{
		PreviewH = MaxPreviewH;
		PreviewW = PreviewH * maximum(0.1f, Aspect);
	}
	PreviewW = maximum(56.0f, PreviewW);
	PreviewH = maximum(40.0f, PreviewH);
}

float CUClientChatPasteImage::PreviewHeight(CChat *pChat, float Width, float FontSize) const
{
	if(!m_PendingUploadImage.HasImage())
		return 0.0f;
	float PreviewW = 0.0f;
	float PreviewH = 0.0f;
	PendingUploadPreviewSize(pChat, Width, FontSize, PreviewW, PreviewH);
	return PreviewH;
}

bool CUClientChatPasteImage::PendingUploadCloseButtonRect(CChat *pChat, float X, float Y, float Width, float FontSize, CUClientChatPasteImage::SRenderRect &ButtonRect) const
{
	if(!m_PendingUploadImage.HasImage())
		return false;

	float PreviewW = 0.0f;
	float PreviewH = 0.0f;
	PendingUploadPreviewSize(pChat, Width, FontSize, PreviewW, PreviewH);
	if(PreviewW <= 0.0f || PreviewH <= 0.0f)
		return false;

	const float Border = maximum(1.0f, FontSize * 0.08f);
	const float InnerX = X + Border;
	const float InnerY = Y + Border;
	const float InnerW = maximum(1.0f, PreviewW - Border * 2.0f);
	const float ButtonSize = minimum(maximum(10.0f, FontSize * 0.65f), InnerW * 0.28f);
	const float Margin = maximum(2.0f, FontSize * 0.12f);

	ButtonRect.m_W = ButtonSize;
	ButtonRect.m_H = ButtonSize;
	ButtonRect.m_X = InnerX + InnerW - ButtonSize - Margin;
	ButtonRect.m_Y = InnerY + Margin;
	return true;
}

void CUClientChatPasteImage::RenderPreview(CChat *pChat, float X, float Y, float Width, float Height, float FontSize)
{
	if(!m_PendingUploadImage.HasImage())
		return;

	float PreviewW = 0.0f;
	float PreviewH = 0.0f;
	PendingUploadPreviewSize(pChat, Width, FontSize, PreviewW, PreviewH);
	const float PreviewBorder = maximum(0.35f, FontSize * 0.025f);
	const float PreviewRounding = minimum(minimum(PreviewW, PreviewH) / 2.0f, maximum(4.0f, FontSize * 0.55f));
	const float InnerX = X + PreviewBorder;
	const float InnerY = Y + PreviewBorder;
	const float InnerW = maximum(1.0f, PreviewW - PreviewBorder * 2.0f);
	const float InnerH = maximum(1.0f, PreviewH - PreviewBorder * 2.0f);
	const float InnerRounding = maximum(0.0f, PreviewRounding - PreviewBorder);

	pChat->Graphics()->DrawRect(X, Y, PreviewW, PreviewH, ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f), IGraphics::CORNER_ALL, PreviewRounding);
	pChat->Graphics()->DrawRect(InnerX, InnerY, InnerW, InnerH, ColorRGBA(0.07f, 0.07f, 0.07f, 0.85f), IGraphics::CORNER_ALL, InnerRounding);
	DrawRoundedMediaPreview(pChat->Graphics(), m_PendingUploadImage.m_Texture, InnerX, InnerY, InnerW, InnerH, InnerRounding, 1.0f);

	const vec2 MousePos = pChat->ChatMousePos();

	if(PendingUploadCloseButtonRect(pChat, X, Y, Width, FontSize, m_PendingUploadCloseRect))
	{
		m_PendingUploadCloseRectValid = true;
		const float CloseRounding = maximum(2.0f, m_PendingUploadCloseRect.m_W * 0.35f);
		const bool CloseHovered = MousePos.x >= m_PendingUploadCloseRect.m_X && MousePos.x <= m_PendingUploadCloseRect.m_X + m_PendingUploadCloseRect.m_W &&
			MousePos.y >= m_PendingUploadCloseRect.m_Y && MousePos.y <= m_PendingUploadCloseRect.m_Y + m_PendingUploadCloseRect.m_H;
		const ColorRGBA CloseBgColor = CloseHovered ? ColorRGBA(0.75f, 0.12f, 0.12f, 0.92f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.68f);
		pChat->Graphics()->DrawRect(m_PendingUploadCloseRect.m_X, m_PendingUploadCloseRect.m_Y, m_PendingUploadCloseRect.m_W, m_PendingUploadCloseRect.m_H, CloseBgColor, IGraphics::CORNER_ALL, CloseRounding);

		const float LabelFontSize = maximum(8.0f, m_PendingUploadCloseRect.m_H * 0.75f);
		const float LabelWidth = pChat->TextRender()->TextWidth(LabelFontSize, "x", -1, -1);
		CTextCursor CloseCursor;
		CloseCursor.SetPosition(vec2(
			m_PendingUploadCloseRect.m_X + maximum(0.0f, (m_PendingUploadCloseRect.m_W - LabelWidth) / 2.0f),
			m_PendingUploadCloseRect.m_Y + maximum(0.0f, (m_PendingUploadCloseRect.m_H - LabelFontSize) / 2.0f)));
		CloseCursor.m_FontSize = LabelFontSize;
		pChat->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
		pChat->TextRender()->TextEx(&CloseCursor, "x");
		pChat->TextRender()->TextColor(pChat->TextRender()->DefaultTextColor());

	}
	else
		m_PendingUploadCloseRectValid = false;

	// Edit button
	const float EditButtonSize = FontSize * 0.85f;
	const float EditButtonPadding = FontSize * 0.2f;
	m_ImageEditorEditButtonRect.m_X = X + EditButtonPadding;
	m_ImageEditorEditButtonRect.m_Y = Y + EditButtonPadding;
	m_ImageEditorEditButtonRect.m_W = EditButtonSize;
	m_ImageEditorEditButtonRect.m_H = EditButtonSize;
	m_ImageEditorEditButtonRectValid = true;

	const bool EditHovered = MousePos.x >= m_ImageEditorEditButtonRect.m_X && MousePos.x <= m_ImageEditorEditButtonRect.m_X + m_ImageEditorEditButtonRect.m_W &&
		MousePos.y >= m_ImageEditorEditButtonRect.m_Y && MousePos.y <= m_ImageEditorEditButtonRect.m_Y + m_ImageEditorEditButtonRect.m_H;
	const ColorRGBA EditBgColor = EditHovered ? ColorRGBA(0.12f, 0.48f, 0.75f, 0.92f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.68f);
	const float EditRounding = maximum(2.0f, m_ImageEditorEditButtonRect.m_W * 0.35f);
	pChat->Graphics()->DrawRect(m_ImageEditorEditButtonRect.m_X, m_ImageEditorEditButtonRect.m_Y, m_ImageEditorEditButtonRect.m_W, m_ImageEditorEditButtonRect.m_H, EditBgColor, IGraphics::CORNER_ALL, EditRounding);

	const float EditLabelFontSize = maximum(8.0f, m_ImageEditorEditButtonRect.m_H * 0.65f);
	const float EditLabelWidth = pChat->TextRender()->TextWidth(EditLabelFontSize, "✎", -1, -1);
	CTextCursor EditCursor;
	EditCursor.SetPosition(vec2(
		m_ImageEditorEditButtonRect.m_X + maximum(0.0f, (m_ImageEditorEditButtonRect.m_W - EditLabelWidth) / 2.0f),
		m_ImageEditorEditButtonRect.m_Y + maximum(0.0f, (m_ImageEditorEditButtonRect.m_H - EditLabelFontSize) / 2.0f)));
	EditCursor.m_FontSize = EditLabelFontSize;
	pChat->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
	pChat->TextRender()->TextEx(&EditCursor, "✎");
	pChat->TextRender()->TextColor(pChat->TextRender()->DefaultTextColor());

	const bool Uploading = m_PendingUploadImage.m_State == CUClientChatPasteImage::EPendingUploadState::UPLOADING;
	const bool Failed = m_PendingUploadImage.m_State == CUClientChatPasteImage::EPendingUploadState::FAILED;
	if(Uploading || Failed)
	{
		const ColorRGBA OverlayColor = Uploading ? ColorRGBA(0.0f, 0.0f, 0.0f, 0.48f) : ColorRGBA(0.25f, 0.05f, 0.05f, 0.60f);
		pChat->Graphics()->DrawRect(InnerX, InnerY, InnerW, InnerH, OverlayColor, IGraphics::CORNER_ALL, InnerRounding);

		const char *pLabel = Uploading ? "Uploading photo..." : (m_PendingUploadImage.m_aError[0] != '\0' ? m_PendingUploadImage.m_aError : "Upload failed.");
		const float LabelFontSize = FontSize * 0.68f;
		const float LabelWidth = pChat->TextRender()->TextWidth(LabelFontSize, pLabel, -1, -1);
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(InnerX + maximum(6.0f, (InnerW - LabelWidth) / 2.0f), InnerY + maximum(6.0f, (InnerH - LabelFontSize) / 2.0f)));
		Cursor.m_FontSize = LabelFontSize;
		pChat->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);
		pChat->TextRender()->TextEx(&Cursor, pLabel);
		pChat->TextRender()->TextColor(pChat->TextRender()->DefaultTextColor());
	}

	(void)Height;
}

void CUClientChatPasteImage::OpenImageEditor(CChat *pChat)
{
	if(!m_PendingUploadImage.HasImage())
		return;

	m_ImageEditor.m_vStrokeSnapshot = m_ImageEditor.m_vStrokes;
	m_ImageEditor.m_Active = true;
	m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
	m_ImageEditor.m_CurrentTool = CUClientChatPasteImage::EImageEditorTool::PEN;
	m_ImageEditor.m_PenThickness = maximum(1.0f, m_ImageEditor.m_PenThickness);
	m_ImageEditor.m_IsDrawing = false;
	m_ImageEditor.m_MouseDownLastFrame = false;
}

void CUClientChatPasteImage::CloseImageEditor()
{
	m_ImageEditor.m_Active = false;
	m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
	m_ImageEditor.m_IsDrawing = false;
	m_ImageEditor.m_MouseDownLastFrame = false;
}

void CUClientChatPasteImage::CancelImageEditor(CChat *pChat)
{
	m_ImageEditor.m_vStrokes = m_ImageEditor.m_vStrokeSnapshot;
	m_ImageEditor.m_vStrokeSnapshot.clear();
	CloseImageEditor();
}

void CUClientChatPasteImage::SaveImageEditorChanges(CChat *pChat)
{
	if(!m_ImageEditor.m_Active)
		return;
	if(!m_PendingUploadImage.HasImage() || m_PendingUploadImage.m_vOriginalPng.empty())
	{
		CloseImageEditor();
		return;
	}

	if(m_ImageEditor.m_vStrokes.empty() && m_ImageEditor.m_CurrentStroke.m_vPoints.empty())
	{
		m_ImageEditor.m_vStrokeSnapshot.clear();
		CloseImageEditor();
		return;
	}

	CImageInfo BaseImage;
	if(!pChat->Graphics()->LoadPng(BaseImage, m_PendingUploadImage.m_vOriginalPng.data(), m_PendingUploadImage.m_vOriginalPng.size(), "chat-paste-editor-save"))
	{
		pChat->Echo("Unable to decode the pending image for editing.");
		return;
	}

	if(BaseImage.m_Format != CImageInfo::FORMAT_RGBA)
	{
		CImageInfo RgbaImage;
		RgbaImage.m_Width = BaseImage.m_Width;
		RgbaImage.m_Height = BaseImage.m_Height;
		RgbaImage.m_Format = CImageInfo::FORMAT_RGBA;
		RgbaImage.m_pData = (uint8_t *)malloc(RgbaImage.DataSize());
		if(RgbaImage.m_pData == nullptr)
		{
			BaseImage.Free();
			pChat->Echo("Unable to allocate image buffer for editor save.");
			return;
		}

		for(size_t y = 0; y < BaseImage.m_Height; ++y)
			for(size_t x = 0; x < BaseImage.m_Width; ++x)
				RgbaImage.SetPixelColor(x, y, BaseImage.PixelColor(x, y));

		BaseImage.Free();
		BaseImage = std::move(RgbaImage);
	}

	struct SRasterColor
	{
		float m_R = 1.0f;
		float m_G = 1.0f;
		float m_B = 1.0f;
		float m_A = 1.0f;
	};

	auto BlendOver = [&](uint8_t *pDstRgba, const SRasterColor &Color) {
		const float SrcA = std::clamp(Color.m_A, 0.0f, 1.0f);
		const float DstA = pDstRgba[3] / 255.0f;
		const float OutA = SrcA + DstA * (1.0f - SrcA);

		const float DstR = pDstRgba[0] / 255.0f;
		const float DstG = pDstRgba[1] / 255.0f;
		const float DstB = pDstRgba[2] / 255.0f;

		float OutR = 0.0f;
		float OutG = 0.0f;
		float OutB = 0.0f;
		if(OutA > 0.0f)
		{
			OutR = (Color.m_R * SrcA + DstR * DstA * (1.0f - SrcA)) / OutA;
			OutG = (Color.m_G * SrcA + DstG * DstA * (1.0f - SrcA)) / OutA;
			OutB = (Color.m_B * SrcA + DstB * DstA * (1.0f - SrcA)) / OutA;
		}

		pDstRgba[0] = (uint8_t)std::clamp((int)roundf(OutR * 255.0f), 0, 255);
		pDstRgba[1] = (uint8_t)std::clamp((int)roundf(OutG * 255.0f), 0, 255);
		pDstRgba[2] = (uint8_t)std::clamp((int)roundf(OutB * 255.0f), 0, 255);
		pDstRgba[3] = (uint8_t)std::clamp((int)roundf(OutA * 255.0f), 0, 255);
	};

	auto CanvasToImage = [&](const vec2 &CanvasPoint) {
		const float X = (CanvasPoint.x - m_ImageEditorCanvasRect.m_X) / maximum(1.0f, m_ImageEditorCanvasRect.m_W);
		const float Y = (CanvasPoint.y - m_ImageEditorCanvasRect.m_Y) / maximum(1.0f, m_ImageEditorCanvasRect.m_H);
		return vec2(
			std::clamp(X, 0.0f, 1.0f) * maximum(1.0f, (float)BaseImage.m_Width - 1.0f),
			std::clamp(Y, 0.0f, 1.0f) * maximum(1.0f, (float)BaseImage.m_Height - 1.0f));
	};

	auto StampCircle = [&](const vec2 &Center, float Radius, const SRasterColor &Color) {
		if(BaseImage.m_pData == nullptr || Radius <= 0.0f)
			return;

		const int W = (int)BaseImage.m_Width;
		const int H = (int)BaseImage.m_Height;
		const float RadiusSq = Radius * Radius;

		const int MinX = std::clamp((int)floorf(Center.x - Radius), 0, W - 1);
		const int MaxX = std::clamp((int)ceilf(Center.x + Radius), 0, W - 1);
		const int MinY = std::clamp((int)floorf(Center.y - Radius), 0, H - 1);
		const int MaxY = std::clamp((int)ceilf(Center.y + Radius), 0, H - 1);

		for(int y = MinY; y <= MaxY; ++y)
		{
			for(int x = MinX; x <= MaxX; ++x)
			{
				const float Dx = ((float)x + 0.5f) - Center.x;
				const float Dy = ((float)y + 0.5f) - Center.y;
				if(Dx * Dx + Dy * Dy > RadiusSq)
					continue;

				uint8_t *pDst = &BaseImage.m_pData[((size_t)y * (size_t)W + (size_t)x) * 4ull];
				BlendOver(pDst, Color);
			}
		}
	};

	auto RasterizeStroke = [&](const CUClientChatPasteImage::SImageEditorStroke &Stroke) {
		if(Stroke.m_Tool != CUClientChatPasteImage::EImageEditorTool::PEN || Stroke.m_vPoints.empty())
			return;

		const float ScaleX = (float)BaseImage.m_Width / maximum(1.0f, m_ImageEditorCanvasRect.m_W);
		const float ScaleY = (float)BaseImage.m_Height / maximum(1.0f, m_ImageEditorCanvasRect.m_H);
		const float ThicknessPx = maximum(1.0f, Stroke.m_Thickness * (ScaleX + ScaleY) * 0.5f);
		const float Radius = maximum(0.75f, ThicknessPx * 0.7f);
		const SRasterColor Color{
			std::clamp(Stroke.m_Color.r, 0.0f, 1.0f),
			std::clamp(Stroke.m_Color.g, 0.0f, 1.0f),
			std::clamp(Stroke.m_Color.b, 0.0f, 1.0f),
			std::clamp(Stroke.m_Color.a, 0.0f, 1.0f)};

		vec2 Prev = CanvasToImage(Stroke.m_vPoints.front());
		StampCircle(Prev, Radius, Color);

		for(size_t i = 1; i < Stroke.m_vPoints.size(); ++i)
		{
			const vec2 Next = CanvasToImage(Stroke.m_vPoints[i]);
			const vec2 Delta = Next - Prev;
			const float Length = length(Delta);
			const float Step = maximum(0.5f, Radius * 0.5f);
			const int Segments = maximum(1, (int)ceilf(Length / Step));

			for(int s = 1; s <= Segments; ++s)
			{
				const float T = (float)s / (float)Segments;
				StampCircle(mix(Prev, Next, T), Radius, Color);
			}

			Prev = Next;
		}
	};

	for(const CUClientChatPasteImage::SImageEditorStroke &Stroke : m_ImageEditor.m_vStrokes)
		RasterizeStroke(Stroke);
	if(!m_ImageEditor.m_CurrentStroke.m_vPoints.empty())
		RasterizeStroke(m_ImageEditor.m_CurrentStroke);

	CByteBufferWriter Writer;
	if(!CImageLoader::SavePng(Writer, BaseImage))
	{
		BaseImage.Free();
		pChat->Echo("Unable to save edited image.");
		return;
	}

	const IGraphics::CTextureHandle NewTexture = pChat->Graphics()->LoadTextureRaw(BaseImage, 0, "chat-paste-upload-edited");
	BaseImage.Free();
	if(!NewTexture.IsValid())
	{
		pChat->Echo("Unable to upload edited image texture.");
		return;
	}

	if(m_PendingUploadImage.m_Texture.IsValid())
		pChat->Graphics()->UnloadTexture(&m_PendingUploadImage.m_Texture);
	m_PendingUploadImage.m_Texture = NewTexture;
	m_PendingUploadImage.m_vPng.assign(Writer.Data(), Writer.Data() + Writer.Size());
	m_ImageEditor.m_vStrokeSnapshot.clear();

	CloseImageEditor();
}

void CUClientChatPasteImage::UpdateImageEditorInput(CChat *pChat)
{
	if(!m_ImageEditor.m_Active || !m_PendingUploadImage.HasImage())
		return;

	const float ScreenW = maximum(1.0f, (float)pChat->Graphics()->WindowWidth());
	const float ScreenH = maximum(1.0f, (float)pChat->Graphics()->WindowHeight());
	const vec2 WindowSize(ScreenW, ScreenH);
	const vec2 UiMousePos = pChat->Ui()->UpdatedMousePos() * vec2(pChat->Ui()->Screen()->w, pChat->Ui()->Screen()->h) / WindowSize;
	const vec2 MousePos(UiMousePos.x * ScreenW / pChat->Ui()->Screen()->w, UiMousePos.y * ScreenH / pChat->Ui()->Screen()->h);
	const bool MouseDown = pChat->Input()->KeyIsPressed(KEY_MOUSE_1);
	const bool MouseClicked = MouseDown && !m_ImageEditor.m_MouseDownLastFrame;
	const bool MouseReleased = !MouseDown && m_ImageEditor.m_MouseDownLastFrame;

	const ColorRGBA aPalette[6] = {
		ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f),
		ColorRGBA(0.12f, 0.12f, 0.12f, 1.0f),
		ColorRGBA(0.96f, 0.31f, 0.27f, 1.0f),
		ColorRGBA(0.23f, 0.74f, 0.36f, 1.0f),
		ColorRGBA(0.21f, 0.56f, 0.94f, 1.0f),
		ColorRGBA(0.99f, 0.81f, 0.20f, 1.0f),
	};

	auto InRect = [&](const CUClientChatPasteImage::SRenderRect &Rect) {
		return MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
			MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H;
	};

	if(MouseClicked && InRect(m_ImageEditorPenButtonRect))
		m_ImageEditor.m_CurrentTool = CUClientChatPasteImage::EImageEditorTool::PEN;

	if(MouseClicked && InRect(m_ImageEditorEraserButtonRect))
		m_ImageEditor.m_CurrentTool = CUClientChatPasteImage::EImageEditorTool::ERASER;

	if(MouseClicked && InRect(m_ImageEditorClearButtonRect))
	{
		m_ImageEditor.m_vStrokes.clear();
		m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
		m_ImageEditor.m_IsDrawing = false;
	}

	if(MouseClicked && InRect(m_ImageEditorSaveButtonRect))
	{
		SaveImageEditorChanges(pChat);
		m_ImageEditor.m_MouseDownLastFrame = MouseDown;
		return;
	}

	if(MouseClicked && InRect(m_ImageEditorCancelButtonRect))
	{
		CancelImageEditor(pChat);
		m_ImageEditor.m_MouseDownLastFrame = MouseDown;
		return;
	}

	for(size_t i = 0; i < std::size(m_aImageEditorColorRects); ++i)
	{
		if(MouseClicked && InRect(m_aImageEditorColorRects[i]))
			m_ImageEditor.m_PenColor = aPalette[i];
	}

	if(MouseClicked && InRect(m_ImageEditorThicknessMinusRect))
		m_ImageEditor.m_PenThickness = maximum(1.0f, m_ImageEditor.m_PenThickness - 1.0f);

	if(MouseClicked && InRect(m_ImageEditorThicknessPlusRect))
		m_ImageEditor.m_PenThickness = minimum(14.0f, m_ImageEditor.m_PenThickness + 1.0f);

	if(MouseDown && InRect(m_ImageEditorThicknessRect))
	{
		const float T = std::clamp((MousePos.x - m_ImageEditorThicknessRect.m_X) / maximum(1.0f, m_ImageEditorThicknessRect.m_W), 0.0f, 1.0f);
		m_ImageEditor.m_PenThickness = 1.0f + T * 13.0f;
	}

	const bool MouseOverCanvas = InRect(m_ImageEditorCanvasRect);

	if(m_ImageEditor.m_CurrentTool == CUClientChatPasteImage::EImageEditorTool::PEN)
	{
		if(MouseDown && MouseOverCanvas)
		{
			if(!m_ImageEditor.m_IsDrawing)
			{
				m_ImageEditor.m_IsDrawing = true;
				m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
				m_ImageEditor.m_CurrentStroke.m_Color = m_ImageEditor.m_PenColor;
				m_ImageEditor.m_CurrentStroke.m_Thickness = m_ImageEditor.m_PenThickness;
				m_ImageEditor.m_CurrentStroke.m_Tool = CUClientChatPasteImage::EImageEditorTool::PEN;
				m_ImageEditor.m_CurrentStroke.m_vPoints.push_back(MousePos);
			}

			if(m_ImageEditor.m_CurrentStroke.m_vPoints.empty())
				m_ImageEditor.m_CurrentStroke.m_vPoints.push_back(MousePos);
			else
			{
				const vec2 Prev = m_ImageEditor.m_CurrentStroke.m_vPoints.back();
				const vec2 Delta = MousePos - Prev;
				if(Delta.x * Delta.x + Delta.y * Delta.y >= 0.8f)
					m_ImageEditor.m_CurrentStroke.m_vPoints.push_back(MousePos);
			}
		}
		else if(m_ImageEditor.m_IsDrawing && (MouseReleased || !MouseDown))
		{
			if(m_ImageEditor.m_CurrentStroke.m_vPoints.size() > 1)
				m_ImageEditor.m_vStrokes.push_back(m_ImageEditor.m_CurrentStroke);

			m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
			m_ImageEditor.m_IsDrawing = false;
		}
	}
	else
	{
		if(m_ImageEditor.m_IsDrawing && MouseReleased)
		{
			m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
			m_ImageEditor.m_IsDrawing = false;
		}

		if(MouseDown && MouseOverCanvas)
		{
			const float EraseRadius = maximum(7.0f, m_ImageEditor.m_PenThickness * 2.0f);
			const float RadiusSq = EraseRadius * EraseRadius;

			for(auto It = m_ImageEditor.m_vStrokes.rbegin(); It != m_ImageEditor.m_vStrokes.rend(); ++It)
			{
				bool Hit = false;
				for(const vec2 &Point : It->m_vPoints)
				{
					const vec2 Delta = Point - MousePos;
					if(Delta.x * Delta.x + Delta.y * Delta.y <= RadiusSq)
					{
						Hit = true;
						break;
					}
				}

				if(Hit)
				{
					m_ImageEditor.m_vStrokes.erase(std::next(It).base());
					break;
				}
			}
		}
	}

	m_ImageEditor.m_MouseDownLastFrame = MouseDown;
}

void CUClientChatPasteImage::RenderImageEditor(CChat *pChat)
{
	if(!m_ImageEditor.m_Active || !m_PendingUploadImage.HasImage())
		return;

	const float ScreenW = maximum(1.0f, (float)pChat->Graphics()->WindowWidth());
	const float ScreenH = maximum(1.0f, (float)pChat->Graphics()->WindowHeight());
	const vec2 WindowSize(ScreenW, ScreenH);
	const vec2 UiMousePos = pChat->Ui()->UpdatedMousePos() * vec2(pChat->Ui()->Screen()->w, pChat->Ui()->Screen()->h) / WindowSize;
	const vec2 MousePos(UiMousePos.x * ScreenW / pChat->Ui()->Screen()->w, UiMousePos.y * ScreenH / pChat->Ui()->Screen()->h);

	const ColorRGBA aPalette[6] = {
		ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f),
		ColorRGBA(0.12f, 0.12f, 0.12f, 1.0f),
		ColorRGBA(0.96f, 0.31f, 0.27f, 1.0f),
		ColorRGBA(0.23f, 0.74f, 0.36f, 1.0f),
		ColorRGBA(0.21f, 0.56f, 0.94f, 1.0f),
		ColorRGBA(0.99f, 0.81f, 0.20f, 1.0f),
	};

	auto InRect = [&](const CUClientChatPasteImage::SRenderRect &Rect) {
		return MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
			MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H;
	};

	const float WindowMargin = 18.0f;
	const float WindowX = WindowMargin;
	const float WindowY = WindowMargin;
	const float WindowW = ScreenW - WindowMargin * 2.0f;
	const float WindowH = ScreenH - WindowMargin * 2.0f;
	const float ToolbarH = 88.0f;
	const float ToolbarPad = 16.0f;
	const float ButtonH = ToolbarH - ToolbarPad * 2.0f;

	m_ImageEditorPenButtonRect = {WindowX + 18.0f, WindowY + ToolbarPad, 100.0f, ButtonH};
	m_ImageEditorEraserButtonRect = {m_ImageEditorPenButtonRect.m_X + m_ImageEditorPenButtonRect.m_W + 10.0f, WindowY + ToolbarPad, 124.0f, ButtonH};
	m_ImageEditorClearButtonRect = {m_ImageEditorEraserButtonRect.m_X + m_ImageEditorEraserButtonRect.m_W + 10.0f, WindowY + ToolbarPad, 96.0f, ButtonH};
	m_ImageEditorCancelButtonRect = {WindowX + WindowW - 226.0f, WindowY + ToolbarPad, 96.0f, ButtonH};
	m_ImageEditorSaveButtonRect = {WindowX + WindowW - 118.0f, WindowY + ToolbarPad, 100.0f, ButtonH};

	const float ColorStartX = m_ImageEditorClearButtonRect.m_X + m_ImageEditorClearButtonRect.m_W + 28.0f;
	const float ColorSize = 28.0f;
	for(size_t i = 0; i < std::size(m_aImageEditorColorRects); ++i)
	{
		m_aImageEditorColorRects[i] = {ColorStartX + (float)i * (ColorSize + 8.0f), WindowY + 16.0f, ColorSize, ColorSize};
	}

	m_ImageEditorThicknessMinusRect = {ColorStartX, WindowY + 54.0f, 26.0f, 14.0f};
	m_ImageEditorThicknessRect = {m_ImageEditorThicknessMinusRect.m_X + 32.0f, WindowY + 54.0f, 156.0f, 14.0f};
	m_ImageEditorThicknessPlusRect = {m_ImageEditorThicknessRect.m_X + m_ImageEditorThicknessRect.m_W + 6.0f, WindowY + 54.0f, 26.0f, 14.0f};

	const float CanvasPad = 16.0f;
	const float CanvasX = WindowX + CanvasPad;
	const float CanvasY = WindowY + ToolbarH + CanvasPad;
	const float CanvasW = WindowW - CanvasPad * 2.0f;
	const float CanvasH = WindowH - ToolbarH - CanvasPad * 2.0f;

	const float ImgW = (float)m_PendingUploadImage.m_Width;
	const float ImgH = (float)m_PendingUploadImage.m_Height;
	const float Scale = minimum(CanvasW / maximum(1.0f, ImgW), CanvasH / maximum(1.0f, ImgH));
	const float FinalW = maximum(1.0f, ImgW * Scale);
	const float FinalH = maximum(1.0f, ImgH * Scale);
	const float ImgX = CanvasX + (CanvasW - FinalW) / 2.0f;
	const float ImgY = CanvasY + (CanvasH - FinalH) / 2.0f;
	m_ImageEditorCanvasRect = {ImgX, ImgY, FinalW, FinalH};

	UpdateImageEditorInput(pChat);

	pChat->Graphics()->DrawRect(0.0f, 0.0f, ScreenW, ScreenH, ColorRGBA(0.02f, 0.03f, 0.05f, 0.84f), 0, 0);
	pChat->Graphics()->DrawRect(WindowX, WindowY, WindowW, WindowH, ColorRGBA(0.11f, 0.13f, 0.17f, 0.97f), IGraphics::CORNER_ALL, 10.0f);
	pChat->Graphics()->DrawRect(WindowX + 1.0f, WindowY + 1.0f, WindowW - 2.0f, WindowH - 2.0f, ColorRGBA(0.16f, 0.18f, 0.23f, 0.96f), IGraphics::CORNER_ALL, 9.0f);
	pChat->Graphics()->DrawRect(WindowX + 2.0f, WindowY + 2.0f, WindowW - 4.0f, ToolbarH, ColorRGBA(0.08f, 0.10f, 0.14f, 0.96f), IGraphics::CORNER_T, 9.0f);

	CTextCursor Cursor;
	Cursor.SetPosition(vec2(WindowX + 18.0f, WindowY + 8.0f));
	Cursor.m_FontSize = 18.0f;
	pChat->TextRender()->TextColor(0.86f, 0.90f, 0.97f, 0.95f);
	pChat->TextRender()->TextEx(&Cursor, "Image Editor");

	auto DrawButton = [&](const CUClientChatPasteImage::SRenderRect &Rect, const char *pLabel, bool Active, const ColorRGBA &HoverColor) {
		const bool Hovered = InRect(Rect);
		const ColorRGBA Bg = Active ? ColorRGBA(0.20f, 0.36f, 0.62f, 0.96f) : (Hovered ? HoverColor : ColorRGBA(0.19f, 0.22f, 0.29f, 0.96f));
		pChat->Graphics()->DrawRect(Rect.m_X, Rect.m_Y, Rect.m_W, Rect.m_H, Bg, IGraphics::CORNER_ALL, 4.0f);
		pChat->Graphics()->DrawRect(Rect.m_X + 1.0f, Rect.m_Y + 1.0f, Rect.m_W - 2.0f, Rect.m_H - 2.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.06f), IGraphics::CORNER_ALL, 3.0f);

		const float LabelSize = 12.0f;
		const float LabelWidth = pChat->TextRender()->TextWidth(LabelSize, pLabel, -1, -1);
		CTextCursor ButtonCursor;
		ButtonCursor.SetPosition(vec2(Rect.m_X + maximum(4.0f, (Rect.m_W - LabelWidth) * 0.5f), Rect.m_Y + (Rect.m_H - LabelSize) * 0.5f));
		ButtonCursor.m_FontSize = LabelSize;
		pChat->TextRender()->TextColor(0.97f, 0.98f, 1.0f, 0.98f);
		pChat->TextRender()->TextEx(&ButtonCursor, pLabel);
	};

	DrawButton(m_ImageEditorPenButtonRect, "Pen", m_ImageEditor.m_CurrentTool == CUClientChatPasteImage::EImageEditorTool::PEN, ColorRGBA(0.24f, 0.36f, 0.54f, 0.96f));
	DrawButton(m_ImageEditorEraserButtonRect, "Eraser", m_ImageEditor.m_CurrentTool == CUClientChatPasteImage::EImageEditorTool::ERASER, ColorRGBA(0.30f, 0.26f, 0.24f, 0.96f));
	DrawButton(m_ImageEditorClearButtonRect, "Clear", false, ColorRGBA(0.45f, 0.20f, 0.20f, 0.96f));
	DrawButton(m_ImageEditorCancelButtonRect, "Cancel", false, ColorRGBA(0.30f, 0.24f, 0.24f, 0.96f));
	DrawButton(m_ImageEditorSaveButtonRect, "Save", false, ColorRGBA(0.19f, 0.44f, 0.28f, 0.96f));

	for(size_t i = 0; i < std::size(m_aImageEditorColorRects); ++i)
	{
		const CUClientChatPasteImage::SRenderRect &Rect = m_aImageEditorColorRects[i];
		const bool Selected =
			absolute(m_ImageEditor.m_PenColor.r - aPalette[i].r) < 0.001f &&
			absolute(m_ImageEditor.m_PenColor.g - aPalette[i].g) < 0.001f &&
			absolute(m_ImageEditor.m_PenColor.b - aPalette[i].b) < 0.001f;
		const float Border = Selected ? 2.0f : 1.0f;
		pChat->Graphics()->DrawRect(Rect.m_X - Border, Rect.m_Y - Border, Rect.m_W + Border * 2.0f, Rect.m_H + Border * 2.0f, ColorRGBA(0.96f, 0.98f, 1.0f, Selected ? 0.95f : 0.35f), IGraphics::CORNER_ALL, 3.0f);
		pChat->Graphics()->DrawRect(Rect.m_X, Rect.m_Y, Rect.m_W, Rect.m_H, aPalette[i], IGraphics::CORNER_ALL, 3.0f);
	}

	pChat->Graphics()->DrawRect(m_ImageEditorThicknessMinusRect.m_X, m_ImageEditorThicknessMinusRect.m_Y, m_ImageEditorThicknessMinusRect.m_W, m_ImageEditorThicknessMinusRect.m_H, ColorRGBA(0.17f, 0.20f, 0.27f, 0.95f), IGraphics::CORNER_ALL, 2.0f);
	pChat->Graphics()->DrawRect(m_ImageEditorThicknessPlusRect.m_X, m_ImageEditorThicknessPlusRect.m_Y, m_ImageEditorThicknessPlusRect.m_W, m_ImageEditorThicknessPlusRect.m_H, ColorRGBA(0.17f, 0.20f, 0.27f, 0.95f), IGraphics::CORNER_ALL, 2.0f);

	pChat->Graphics()->DrawRect(m_ImageEditorThicknessRect.m_X, m_ImageEditorThicknessRect.m_Y, m_ImageEditorThicknessRect.m_W, m_ImageEditorThicknessRect.m_H, ColorRGBA(0.12f, 0.15f, 0.21f, 0.95f), IGraphics::CORNER_ALL, 2.0f);
	const float ThicknessT = std::clamp((m_ImageEditor.m_PenThickness - 1.0f) / 13.0f, 0.0f, 1.0f);
	const float KnobX = m_ImageEditorThicknessRect.m_X + m_ImageEditorThicknessRect.m_W * ThicknessT;
	pChat->Graphics()->DrawRect(KnobX - 2.0f, m_ImageEditorThicknessRect.m_Y - 1.5f, 4.0f, m_ImageEditorThicknessRect.m_H + 3.0f, ColorRGBA(0.95f, 0.97f, 1.0f, 0.95f), IGraphics::CORNER_ALL, 1.5f);

	Cursor.SetPosition(vec2(m_ImageEditorThicknessRect.m_X - 16.0f, m_ImageEditorThicknessRect.m_Y - 2.0f));
	Cursor.m_FontSize = 11.0f;
	pChat->TextRender()->TextColor(0.89f, 0.92f, 0.97f, 0.95f);
	pChat->TextRender()->TextEx(&Cursor, "-");

	Cursor.SetPosition(vec2(m_ImageEditorThicknessPlusRect.m_X + 7.0f, m_ImageEditorThicknessPlusRect.m_Y - 2.0f));
	Cursor.m_FontSize = 11.0f;
	pChat->TextRender()->TextEx(&Cursor, "+");

	char aThickness[32];
	str_format(aThickness, sizeof(aThickness), "%.1f px", m_ImageEditor.m_PenThickness);
	Cursor.SetPosition(vec2(m_ImageEditorThicknessPlusRect.m_X + 36.0f, m_ImageEditorThicknessPlusRect.m_Y - 2.0f));
	Cursor.m_FontSize = 11.0f;
	pChat->TextRender()->TextEx(&Cursor, aThickness);

	pChat->Graphics()->DrawRect(CanvasX, CanvasY, CanvasW, CanvasH, ColorRGBA(0.07f, 0.09f, 0.13f, 0.98f), IGraphics::CORNER_ALL, 5.0f);
	pChat->Graphics()->DrawRect(ImgX - 2.0f, ImgY - 2.0f, FinalW + 4.0f, FinalH + 4.0f, ColorRGBA(0.95f, 0.97f, 1.0f, 0.24f), IGraphics::CORNER_ALL, 4.0f);

	pChat->Graphics()->WrapClamp();
	const IGraphics::CTextureHandle EditorBaseTexture =
		m_PendingUploadImage.m_OriginalTexture.IsValid() ? m_PendingUploadImage.m_OriginalTexture : m_PendingUploadImage.m_Texture;
	pChat->Graphics()->TextureSet(EditorBaseTexture);
	pChat->Graphics()->QuadsBegin();
	pChat->Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	pChat->Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	pChat->Graphics()->DrawRectExt(ImgX, ImgY, FinalW, FinalH, 4.0f, IGraphics::CORNER_ALL);
	pChat->Graphics()->QuadsEnd();
	pChat->Graphics()->TextureClear();

	auto DrawStroke = [&](const CUClientChatPasteImage::SImageEditorStroke &Stroke) {
		if(Stroke.m_Tool != CUClientChatPasteImage::EImageEditorTool::PEN || Stroke.m_vPoints.empty())
			return;

		const float StrokeRadius = maximum(0.75f, Stroke.m_Thickness * 0.7f);
		auto StampCircle = [&](const vec2 &Pos) {
			pChat->Graphics()->DrawRect(Pos.x - StrokeRadius, Pos.y - StrokeRadius, StrokeRadius * 2.0f, StrokeRadius * 2.0f, Stroke.m_Color, IGraphics::CORNER_ALL, StrokeRadius);
		};

		pChat->Graphics()->SetColor(Stroke.m_Color);
		StampCircle(Stroke.m_vPoints.front());

		for(size_t i = 1; i < Stroke.m_vPoints.size(); ++i)
		{
			const vec2 Prev = Stroke.m_vPoints[i - 1];
			const vec2 Next = Stroke.m_vPoints[i];
			const vec2 Delta = Next - Prev;
			const float Length = length(Delta);
			const float Step = maximum(0.5f, StrokeRadius * 0.5f);
			const int Segments = maximum(1, (int)ceilf(Length / Step));

			for(int s = 1; s <= Segments; ++s)
			{
				const float T = (float)s / (float)Segments;
				StampCircle(mix(Prev, Next, T));
			}
		}
	};

	for(const auto &Stroke : m_ImageEditor.m_vStrokes)
		DrawStroke(Stroke);

	if(m_ImageEditor.m_IsDrawing && m_ImageEditor.m_CurrentStroke.m_vPoints.size() > 1)
		DrawStroke(m_ImageEditor.m_CurrentStroke);

	const bool MouseInsideEditor = MousePos.x >= WindowX && MousePos.x <= WindowX + WindowW &&
		MousePos.y >= WindowY && MousePos.y <= WindowY + WindowH;
	const bool MouseInsideCanvas = InRect(m_ImageEditorCanvasRect);

	if(MouseInsideEditor)
	{
		const float CrosshairRadius = maximum(14.0f, m_ImageEditor.m_PenThickness * 2.2f);
		const float InnerRadius = maximum(4.0f, m_ImageEditor.m_PenThickness * 0.8f);
		const ColorRGBA CursorAccent = m_ImageEditor.m_CurrentTool == CUClientChatPasteImage::EImageEditorTool::PEN ? m_ImageEditor.m_PenColor : ColorRGBA(1.0f, 0.48f, 0.48f, 1.0f);

		pChat->Graphics()->DrawRect(MousePos.x - CrosshairRadius, MousePos.y - 1.0f, CrosshairRadius * 2.0f, 2.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.95f), IGraphics::CORNER_ALL, 0.5f);
		pChat->Graphics()->DrawRect(MousePos.x - 1.0f, MousePos.y - CrosshairRadius, 2.0f, CrosshairRadius * 2.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.95f), IGraphics::CORNER_ALL, 0.5f);
		pChat->Graphics()->DrawRect(MousePos.x - CrosshairRadius + 1.0f, MousePos.y - 0.5f, CrosshairRadius * 2.0f - 2.0f, 1.0f, CursorAccent, IGraphics::CORNER_ALL, 0.5f);
		pChat->Graphics()->DrawRect(MousePos.x - 0.5f, MousePos.y - CrosshairRadius + 1.0f, 1.0f, CrosshairRadius * 2.0f - 2.0f, CursorAccent, IGraphics::CORNER_ALL, 0.5f);
		pChat->Graphics()->DrawRect(MousePos.x - InnerRadius, MousePos.y - InnerRadius, InnerRadius * 2.0f, InnerRadius * 2.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.80f), IGraphics::CORNER_ALL, 1.0f);
		pChat->Graphics()->DrawRect(MousePos.x - InnerRadius + 1.0f, MousePos.y - InnerRadius + 1.0f, maximum(1.0f, InnerRadius * 2.0f - 2.0f), maximum(1.0f, InnerRadius * 2.0f - 2.0f), CursorAccent.WithAlpha(0.95f), IGraphics::CORNER_ALL, 1.0f);

		if(m_ImageEditor.m_CurrentTool == CUClientChatPasteImage::EImageEditorTool::PEN)
		{
			if(MouseInsideCanvas)
			{
				const float CursorSize = maximum(10.0f, m_ImageEditor.m_PenThickness * 1.4f + 6.0f);
				pChat->Graphics()->DrawRect(MousePos.x - CursorSize, MousePos.y + CrosshairRadius + 8.0f, CursorSize * 2.0f, 5.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.82f), IGraphics::CORNER_ALL, 2.0f);
				pChat->Graphics()->DrawRect(MousePos.x - CursorSize + 1.0f, MousePos.y + CrosshairRadius + 9.0f, maximum(1.0f, CursorSize * 2.0f - 2.0f), 3.0f, CursorAccent.WithAlpha(0.96f), IGraphics::CORNER_ALL, 1.5f);
			}
		}
		else
		{
			if(MouseInsideCanvas)
			{
				const float CursorSize = maximum(14.0f, m_ImageEditor.m_PenThickness * 2.1f + 8.0f);
				pChat->Graphics()->DrawRect(MousePos.x - CursorSize, MousePos.y - CursorSize, CursorSize * 2.0f, CursorSize * 2.0f, ColorRGBA(1.0f, 0.72f, 0.72f, 0.20f), IGraphics::CORNER_ALL, 2.0f);
				pChat->Graphics()->DrawRect(MousePos.x - CursorSize, MousePos.y - CursorSize, CursorSize * 2.0f, 2.0f, ColorRGBA(1.0f, 0.60f, 0.60f, 0.95f), IGraphics::CORNER_ALL, 1.0f);
				pChat->Graphics()->DrawRect(MousePos.x - CursorSize, MousePos.y + CursorSize - 2.0f, CursorSize * 2.0f, 2.0f, ColorRGBA(1.0f, 0.60f, 0.60f, 0.95f), IGraphics::CORNER_ALL, 1.0f);
				pChat->Graphics()->DrawRect(MousePos.x - CursorSize, MousePos.y - CursorSize, 2.0f, CursorSize * 2.0f, ColorRGBA(1.0f, 0.60f, 0.60f, 0.95f), IGraphics::CORNER_ALL, 1.0f);
				pChat->Graphics()->DrawRect(MousePos.x + CursorSize - 2.0f, MousePos.y - CursorSize, 2.0f, CursorSize * 2.0f, ColorRGBA(1.0f, 0.60f, 0.60f, 0.95f), IGraphics::CORNER_ALL, 1.0f);
			}
		}
	}
	pChat->TextRender()->TextColor(pChat->TextRender()->DefaultTextColor());
}

void CUClientChatPasteImage::OpenPasteWarningPopup(CChat *pChat)
{
	CMenus &Menus = pChat->GameClient()->m_Menus;
	Menus.PopupConfirmWithCheckbox(
		Localize("Warning"),
		Localize("You are about to paste an image from your clipboard into chat. Make sure it does not contain personal information."),
		Localize("Paste"),
		Localize("Cancel"),
		Localize("Don't ask again"),
		false,
		&CMenus::PopupConfirmPasteImageFromChat,
		CMenus::POPUP_NONE,
		&CMenus::PopupCancelPasteImageFromChat,
		CMenus::POPUP_NONE);
	Menus.SetActive(true);
}

void CUClientChatPasteImage::ConfirmPasteWarning(CChat *pChat, bool DontAskAgain)
{
	if(DontAskAgain)
		g_Config.m_UcChatPasteImageWarningSkip = 1;

	IInput::SClipboardImage Image = std::move(m_WarningPendingClipboardImage);
	m_WarningPendingClipboardImage = {};
	if(!SetPendingUploadImage(pChat, Image))
		pChat->Echo("Unable to attach pasted image.");
}

void CUClientChatPasteImage::CancelPasteWarning(CChat *pChat)
{
	m_WarningPendingClipboardImage = {};
}