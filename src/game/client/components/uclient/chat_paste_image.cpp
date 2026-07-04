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
#include <optional>

static void UnloadPendingUploadTextures(IGraphics *pGraphics, IGraphics::CTextureHandle &Texture, IGraphics::CTextureHandle &OriginalTexture)
{
	if(Texture.IsValid())
	{
		const int TextureId = Texture.Id();
		pGraphics->UnloadTexture(&Texture);
		if(OriginalTexture.IsValid() && OriginalTexture.Id() == TextureId)
			OriginalTexture.Invalidate();
	}
	if(OriginalTexture.IsValid())
		pGraphics->UnloadTexture(&OriginalTexture);
}

static void UnloadPendingUploadDisplayTexture(IGraphics *pGraphics, IGraphics::CTextureHandle &Texture, IGraphics::CTextureHandle &OriginalTexture)
{
	if(!Texture.IsValid())
		return;

	const int TextureId = Texture.Id();
	pGraphics->UnloadTexture(&Texture);
	if(OriginalTexture.IsValid() && OriginalTexture.Id() == TextureId)
		OriginalTexture.Invalidate();
}

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

bool CUClientChatPasteImage::CropRectIsFull(const SImageCropRect &Crop) const
{
	return Crop.m_X0 <= 0.001f && Crop.m_Y0 <= 0.001f && Crop.m_X1 >= 0.999f && Crop.m_Y1 >= 0.999f;
}

void CUClientChatPasteImage::NormalizeCropRect(SImageCropRect &Crop) const
{
	if(Crop.m_X0 > Crop.m_X1)
		std::swap(Crop.m_X0, Crop.m_X1);
	if(Crop.m_Y0 > Crop.m_Y1)
		std::swap(Crop.m_Y0, Crop.m_Y1);
	Crop.m_X0 = std::clamp(Crop.m_X0, 0.0f, 1.0f);
	Crop.m_Y0 = std::clamp(Crop.m_Y0, 0.0f, 1.0f);
	Crop.m_X1 = std::clamp(Crop.m_X1, 0.0f, 1.0f);
	Crop.m_Y1 = std::clamp(Crop.m_Y1, 0.0f, 1.0f);
}

float CUClientChatPasteImage::CropAspectRatio(ECropAspectPreset Preset) const
{
	switch(Preset)
	{
	case ECropAspectPreset::SQUARE:
		return 1.0f;
	case ECropAspectPreset::RATIO_4_3:
		return 4.0f / 3.0f;
	case ECropAspectPreset::RATIO_16_9:
		return 16.0f / 9.0f;
	default:
		return 0.0f;
	}
}

float CUClientChatPasteImage::ImageEditorToolbarHeight() const
{
	return m_ImageEditor.m_CurrentTool == EImageEditorTool::CROP ? 128.0f : 88.0f;
}

void CUClientChatPasteImage::ClampCropRect(SImageCropRect &Crop, int ImgW, int ImgH, ECropAspectPreset Aspect, ECropHandle ActiveHandle) const
{
	NormalizeCropRect(Crop);

	const float MinNormW = (float)CROP_MIN_PIXELS / maximum(1, ImgW);
	const float MinNormH = (float)CROP_MIN_PIXELS / maximum(1, ImgH);

	float Width = maximum(MinNormW, Crop.m_X1 - Crop.m_X0);
	float Height = maximum(MinNormH, Crop.m_Y1 - Crop.m_Y0);

	const float TargetAspect = CropAspectRatio(Aspect);
	if(TargetAspect > 0.0f)
	{
		const float CurrentAspect = Width / maximum(Height, 0.0001f);
		if(CurrentAspect > TargetAspect)
			Width = Height * TargetAspect;
		else
			Height = Width / TargetAspect;
		Width = maximum(Width, MinNormW);
		Height = maximum(Height, MinNormH);
	}

	const bool AnchorRight = ActiveHandle == ECropHandle::TOP_LEFT || ActiveHandle == ECropHandle::LEFT || ActiveHandle == ECropHandle::BOTTOM_LEFT;
	const bool AnchorLeft = ActiveHandle == ECropHandle::TOP_RIGHT || ActiveHandle == ECropHandle::RIGHT || ActiveHandle == ECropHandle::BOTTOM_RIGHT;
	const bool AnchorBottom = ActiveHandle == ECropHandle::TOP_LEFT || ActiveHandle == ECropHandle::TOP || ActiveHandle == ECropHandle::TOP_RIGHT;
	const bool AnchorTop = ActiveHandle == ECropHandle::BOTTOM_LEFT || ActiveHandle == ECropHandle::BOTTOM || ActiveHandle == ECropHandle::BOTTOM_RIGHT;

	float X0 = Crop.m_X0;
	float Y0 = Crop.m_Y0;
	float X1 = Crop.m_X1;
	float Y1 = Crop.m_Y1;

	if(ActiveHandle == ECropHandle::MOVE || ActiveHandle == ECropHandle::NEW_SELECTION || ActiveHandle == ECropHandle::NONE)
	{
		X0 = Crop.m_X0;
		Y0 = Crop.m_Y0;
		X1 = X0 + Width;
		Y1 = Y0 + Height;
	}
	else if(AnchorRight && !AnchorLeft)
	{
		X0 = X1 - Width;
	}
	else if(AnchorLeft && !AnchorRight)
	{
		X1 = X0 + Width;
	}
	else
	{
		const float CenterX = (Crop.m_X0 + Crop.m_X1) * 0.5f;
		X0 = CenterX - Width * 0.5f;
		X1 = CenterX + Width * 0.5f;
	}

	if(ActiveHandle == ECropHandle::MOVE || ActiveHandle == ECropHandle::NEW_SELECTION || ActiveHandle == ECropHandle::NONE)
	{
		// already set
	}
	else if(AnchorBottom && !AnchorTop)
	{
		Y0 = Y1 - Height;
	}
	else if(AnchorTop && !AnchorBottom)
	{
		Y1 = Y0 + Height;
	}
	else
	{
		const float CenterY = (Crop.m_Y0 + Crop.m_Y1) * 0.5f;
		Y0 = CenterY - Height * 0.5f;
		Y1 = CenterY + Height * 0.5f;
	}

	if(X0 < 0.0f)
	{
		X1 -= X0;
		X0 = 0.0f;
	}
	if(Y0 < 0.0f)
	{
		Y1 -= Y0;
		Y0 = 0.0f;
	}
	if(X1 > 1.0f)
	{
		X0 -= X1 - 1.0f;
		X1 = 1.0f;
	}
	if(Y1 > 1.0f)
	{
		Y0 -= Y1 - 1.0f;
		Y1 = 1.0f;
	}

	X0 = std::clamp(X0, 0.0f, 1.0f - MinNormW);
	Y0 = std::clamp(Y0, 0.0f, 1.0f - MinNormH);
	X1 = std::clamp(X1, X0 + MinNormW, 1.0f);
	Y1 = std::clamp(Y1, Y0 + MinNormH, 1.0f);

	Crop.m_X0 = X0;
	Crop.m_Y0 = Y0;
	Crop.m_X1 = X1;
	Crop.m_Y1 = Y1;
}

CUClientChatPasteImage::SRenderRect CUClientChatPasteImage::CropRectToCanvasRect(const SImageCropRect &Crop) const
{
	SImageCropRect Normalized = Crop;
	NormalizeCropRect(Normalized);
	CUClientChatPasteImage::SRenderRect Result;
	Result.m_X = m_ImageEditorCanvasRect.m_X + Normalized.m_X0 * m_ImageEditorCanvasRect.m_W;
	Result.m_Y = m_ImageEditorCanvasRect.m_Y + Normalized.m_Y0 * m_ImageEditorCanvasRect.m_H;
	Result.m_W = (Normalized.m_X1 - Normalized.m_X0) * m_ImageEditorCanvasRect.m_W;
	Result.m_H = (Normalized.m_Y1 - Normalized.m_Y0) * m_ImageEditorCanvasRect.m_H;
	return Result;
}

vec2 CUClientChatPasteImage::CanvasToImageNorm(const vec2 &CanvasPoint) const
{
	const float X = (CanvasPoint.x - m_ImageEditorCanvasRect.m_X) / maximum(1.0f, m_ImageEditorCanvasRect.m_W);
	const float Y = (CanvasPoint.y - m_ImageEditorCanvasRect.m_Y) / maximum(1.0f, m_ImageEditorCanvasRect.m_H);
	return vec2(std::clamp(X, 0.0f, 1.0f), std::clamp(Y, 0.0f, 1.0f));
}

vec2 CUClientChatPasteImage::ImageNormToCanvas(const vec2 &Norm) const
{
	return vec2(
		m_ImageEditorCanvasRect.m_X + std::clamp(Norm.x, 0.0f, 1.0f) * m_ImageEditorCanvasRect.m_W,
		m_ImageEditorCanvasRect.m_Y + std::clamp(Norm.y, 0.0f, 1.0f) * m_ImageEditorCanvasRect.m_H);
}

void CUClientChatPasteImage::CropRectToPixelRect(const SImageCropRect &Crop, int ImgW, int ImgH, int &OutX0, int &OutY0, int &OutW, int &OutH) const
{
	SImageCropRect Normalized = Crop;
	NormalizeCropRect(Normalized);

	const int X0 = std::clamp((int)floorf(Normalized.m_X0 * ImgW), 0, maximum(0, ImgW - 1));
	const int Y0 = std::clamp((int)floorf(Normalized.m_Y0 * ImgH), 0, maximum(0, ImgH - 1));
	const int X1 = std::clamp((int)ceilf(Normalized.m_X1 * ImgW), X0 + 1, ImgW);
	const int Y1 = std::clamp((int)ceilf(Normalized.m_Y1 * ImgH), Y0 + 1, ImgH);
	OutX0 = X0;
	OutY0 = Y0;
	OutW = maximum(CROP_MIN_PIXELS, X1 - X0);
	OutH = maximum(CROP_MIN_PIXELS, Y1 - Y0);
	if(OutX0 + OutW > ImgW)
		OutX0 = maximum(0, ImgW - OutW);
	if(OutY0 + OutH > ImgH)
		OutY0 = maximum(0, ImgH - OutH);
}

CUClientChatPasteImage::ECropHandle CUClientChatPasteImage::HitTestCropHandle(const vec2 &MousePos, const SRenderRect &SelectionCanvas) const
{
	if(SelectionCanvas.m_W <= 0.0f || SelectionCanvas.m_H <= 0.0f)
		return ECropHandle::NONE;

	const float HandleRadius = 7.0f;
	const float HandleRadiusSq = HandleRadius * HandleRadius;

	auto NearPoint = [&](float X, float Y) {
		const float Dx = MousePos.x - X;
		const float Dy = MousePos.y - Y;
		return Dx * Dx + Dy * Dy <= HandleRadiusSq;
	};

	const float L = SelectionCanvas.m_X;
	const float T = SelectionCanvas.m_Y;
	const float R = SelectionCanvas.m_X + SelectionCanvas.m_W;
	const float B = SelectionCanvas.m_Y + SelectionCanvas.m_H;
	const float CX = (L + R) * 0.5f;
	const float CY = (T + B) * 0.5f;

	if(NearPoint(L, T))
		return ECropHandle::TOP_LEFT;
	if(NearPoint(CX, T))
		return ECropHandle::TOP;
	if(NearPoint(R, T))
		return ECropHandle::TOP_RIGHT;
	if(NearPoint(L, CY))
		return ECropHandle::LEFT;
	if(NearPoint(R, CY))
		return ECropHandle::RIGHT;
	if(NearPoint(L, B))
		return ECropHandle::BOTTOM_LEFT;
	if(NearPoint(CX, B))
		return ECropHandle::BOTTOM;
	if(NearPoint(R, B))
		return ECropHandle::BOTTOM_RIGHT;

	return ECropHandle::NONE;
}

bool CUClientChatPasteImage::IsPointInsideCropSelection(const vec2 &MousePos, const SRenderRect &SelectionCanvas) const
{
	return MousePos.x >= SelectionCanvas.m_X && MousePos.x <= SelectionCanvas.m_X + SelectionCanvas.m_W &&
		MousePos.y >= SelectionCanvas.m_Y && MousePos.y <= SelectionCanvas.m_Y + SelectionCanvas.m_H;
}

void CUClientChatPasteImage::ApplyCropAspectToRect(SImageCropRect &Crop, ECropAspectPreset Aspect, ECropHandle AnchorHandle) const
{
	if(Aspect == ECropAspectPreset::FREE)
		return;
	ClampCropRect(Crop, m_PendingUploadImage.m_Width, m_PendingUploadImage.m_Height, Aspect, AnchorHandle);
}

void CUClientChatPasteImage::ResetCropRectToFull()
{
	m_ImageEditor.m_CropRect = {};
}

void CUClientChatPasteImage::UpdateCropDrag(const vec2 &MousePos, int ImgW, int ImgH)
{
	SImageCropRect &Crop = m_ImageEditor.m_CropRect;
	const SImageCropRect &Start = m_ImageEditor.m_CropDragStartRect;
	const vec2 Anchor = m_ImageEditor.m_CropDragAnchor;
	const vec2 Norm = CanvasToImageNorm(MousePos);

	switch(m_ImageEditor.m_CropActiveHandle)
	{
	case ECropHandle::NEW_SELECTION:
		Crop.m_X0 = Anchor.x;
		Crop.m_Y0 = Anchor.y;
		Crop.m_X1 = Norm.x;
		Crop.m_Y1 = Norm.y;
		break;
	case ECropHandle::MOVE:
	{
		const float Width = Start.m_X1 - Start.m_X0;
		const float Height = Start.m_Y1 - Start.m_Y0;
		const float DeltaX = Norm.x - Anchor.x;
		const float DeltaY = Norm.y - Anchor.y;
		Crop.m_X0 = Start.m_X0 + DeltaX;
		Crop.m_Y0 = Start.m_Y0 + DeltaY;
		Crop.m_X1 = Crop.m_X0 + Width;
		Crop.m_Y1 = Crop.m_Y0 + Height;
		break;
	}
	case ECropHandle::TOP_LEFT:
		Crop.m_X0 = Norm.x;
		Crop.m_Y0 = Norm.y;
		Crop.m_X1 = Start.m_X1;
		Crop.m_Y1 = Start.m_Y1;
		break;
	case ECropHandle::TOP:
		Crop.m_X0 = Start.m_X0;
		Crop.m_Y0 = Norm.y;
		Crop.m_X1 = Start.m_X1;
		Crop.m_Y1 = Start.m_Y1;
		break;
	case ECropHandle::TOP_RIGHT:
		Crop.m_X0 = Start.m_X0;
		Crop.m_Y0 = Norm.y;
		Crop.m_X1 = Norm.x;
		Crop.m_Y1 = Start.m_Y1;
		break;
	case ECropHandle::LEFT:
		Crop.m_X0 = Norm.x;
		Crop.m_Y0 = Start.m_Y0;
		Crop.m_X1 = Start.m_X1;
		Crop.m_Y1 = Start.m_Y1;
		break;
	case ECropHandle::RIGHT:
		Crop.m_X0 = Start.m_X0;
		Crop.m_Y0 = Start.m_Y0;
		Crop.m_X1 = Norm.x;
		Crop.m_Y1 = Start.m_Y1;
		break;
	case ECropHandle::BOTTOM_LEFT:
		Crop.m_X0 = Norm.x;
		Crop.m_Y0 = Start.m_Y0;
		Crop.m_X1 = Start.m_X1;
		Crop.m_Y1 = Norm.y;
		break;
	case ECropHandle::BOTTOM:
		Crop.m_X0 = Start.m_X0;
		Crop.m_Y0 = Start.m_Y0;
		Crop.m_X1 = Start.m_X1;
		Crop.m_Y1 = Norm.y;
		break;
	case ECropHandle::BOTTOM_RIGHT:
		Crop.m_X0 = Start.m_X0;
		Crop.m_Y0 = Start.m_Y0;
		Crop.m_X1 = Norm.x;
		Crop.m_Y1 = Norm.y;
		break;
	default:
		break;
	}

	ClampCropRect(Crop, ImgW, ImgH, m_ImageEditor.m_CropAspect, m_ImageEditor.m_CropActiveHandle);
}

void CUClientChatPasteImage::RemapStrokesAfterCrop(const SImageCropRect &AppliedCrop, int OldImgW, int OldImgH, const SRenderRect &OldCanvasRect, const SRenderRect &NewCanvasRect)
{
	SImageCropRect Crop = AppliedCrop;
	NormalizeCropRect(Crop);
	const float CropW = maximum(0.0001f, Crop.m_X1 - Crop.m_X0);
	const float CropH = maximum(0.0001f, Crop.m_Y1 - Crop.m_Y0);

	auto RemapPoint = [&](const vec2 &CanvasPoint) -> std::optional<vec2> {
		const float NormX = (CanvasPoint.x - OldCanvasRect.m_X) / maximum(1.0f, OldCanvasRect.m_W);
		const float NormY = (CanvasPoint.y - OldCanvasRect.m_Y) / maximum(1.0f, OldCanvasRect.m_H);
		if(NormX < Crop.m_X0 || NormX > Crop.m_X1 || NormY < Crop.m_Y0 || NormY > Crop.m_Y1)
			return std::nullopt;

		const float NewNormX = (NormX - Crop.m_X0) / CropW;
		const float NewNormY = (NormY - Crop.m_Y0) / CropH;
		return vec2(
			NewCanvasRect.m_X + NewNormX * NewCanvasRect.m_W,
			NewCanvasRect.m_Y + NewNormY * NewCanvasRect.m_H);
	};

	auto RemapStroke = [&](SImageEditorStroke &Stroke) {
		std::vector<vec2> vNewPoints;
		vNewPoints.reserve(Stroke.m_vPoints.size());
		for(const vec2 &Point : Stroke.m_vPoints)
		{
			if(const std::optional<vec2> NewPoint = RemapPoint(Point))
				vNewPoints.push_back(*NewPoint);
		}
		Stroke.m_vPoints = std::move(vNewPoints);
	};

	for(SImageEditorStroke &Stroke : m_ImageEditor.m_vStrokes)
		RemapStroke(Stroke);

	m_ImageEditor.m_vStrokes.erase(
		std::remove_if(m_ImageEditor.m_vStrokes.begin(), m_ImageEditor.m_vStrokes.end(),
			[](const SImageEditorStroke &Stroke) { return Stroke.m_vPoints.size() < 2; }),
		m_ImageEditor.m_vStrokes.end());

	RemapStroke(m_ImageEditor.m_CurrentStroke);
	if(m_ImageEditor.m_CurrentStroke.m_vPoints.size() < 2)
		m_ImageEditor.m_CurrentStroke.m_vPoints.clear();

	(void)OldImgW;
	(void)OldImgH;
}

bool CUClientChatPasteImage::ApplyImageCrop(CChat *pChat)
{
	if(!m_PendingUploadImage.HasImage() || m_PendingUploadImage.m_vOriginalPng.empty())
		return false;
	if(CropRectIsFull(m_ImageEditor.m_CropRect))
		return true;

	const SImageCropRect AppliedCrop = m_ImageEditor.m_CropRect;
	const SRenderRect OldCanvasRect = m_ImageEditorCanvasRect;
	const int OldImgW = m_PendingUploadImage.m_Width;
	const int OldImgH = m_PendingUploadImage.m_Height;

	CImageInfo BaseImage;
	if(!pChat->Graphics()->LoadPng(BaseImage, m_PendingUploadImage.m_vOriginalPng.data(), m_PendingUploadImage.m_vOriginalPng.size(), "chat-paste-crop"))
	{
		pChat->Echo("Unable to decode the pending image for cropping.");
		return false;
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
			pChat->Echo("Unable to allocate image buffer for cropping.");
			return false;
		}

		for(size_t y = 0; y < BaseImage.m_Height; ++y)
			for(size_t x = 0; x < BaseImage.m_Width; ++x)
				RgbaImage.SetPixelColor(x, y, BaseImage.PixelColor(x, y));

		BaseImage.Free();
		BaseImage = std::move(RgbaImage);
	}

	int CropX = 0;
	int CropY = 0;
	int CropW = 0;
	int CropH = 0;
	CropRectToPixelRect(AppliedCrop, (int)BaseImage.m_Width, (int)BaseImage.m_Height, CropX, CropY, CropW, CropH);

	CImageInfo CroppedImage;
	CroppedImage.m_Width = CropW;
	CroppedImage.m_Height = CropH;
	CroppedImage.m_Format = CImageInfo::FORMAT_RGBA;
	CroppedImage.m_pData = (uint8_t *)malloc(CroppedImage.DataSize());
	if(CroppedImage.m_pData == nullptr)
	{
		BaseImage.Free();
		pChat->Echo("Unable to allocate cropped image buffer.");
		return false;
	}

	const int SrcW = (int)BaseImage.m_Width;
	for(int y = 0; y < CropH; ++y)
	{
		const uint8_t *pSrc = &BaseImage.m_pData[((size_t)(CropY + y) * (size_t)SrcW + (size_t)CropX) * 4ull];
		uint8_t *pDst = &CroppedImage.m_pData[(size_t)y * (size_t)CropW * 4ull];
		mem_copy(pDst, pSrc, (size_t)CropW * 4ull);
	}
	BaseImage.Free();

	CByteBufferWriter Writer;
	if(!CImageLoader::SavePng(Writer, CroppedImage))
	{
		CroppedImage.Free();
		pChat->Echo("Unable to save cropped image.");
		return false;
	}

	const IGraphics::CTextureHandle NewTexture = pChat->Graphics()->LoadTextureRaw(CroppedImage, 0, "chat-paste-cropped");
	CroppedImage.Free();
	if(!NewTexture.IsValid())
	{
		pChat->Echo("Unable to load cropped image texture.");
		return false;
	}

	UnloadPendingUploadTextures(pChat->Graphics(), m_PendingUploadImage.m_Texture, m_PendingUploadImage.m_OriginalTexture);

	m_PendingUploadImage.m_Texture = NewTexture;
	m_PendingUploadImage.m_OriginalTexture = NewTexture;
	m_PendingUploadImage.m_Width = CropW;
	m_PendingUploadImage.m_Height = CropH;
	m_PendingUploadImage.m_vOriginalPng.assign(Writer.Data(), Writer.Data() + Writer.Size());
	m_PendingUploadImage.m_vPng = m_PendingUploadImage.m_vOriginalPng;

	const float ScreenW = maximum(1.0f, (float)pChat->Graphics()->WindowWidth());
	const float ScreenH = maximum(1.0f, (float)pChat->Graphics()->WindowHeight());
	const float WindowMargin = 18.0f;
	const float WindowW = ScreenW - WindowMargin * 2.0f;
	const float WindowH = ScreenH - WindowMargin * 2.0f;
	const float ToolbarH = ImageEditorToolbarHeight();
	const float CanvasPad = 16.0f;
	const float CanvasX = WindowMargin + CanvasPad;
	const float CanvasY = WindowMargin + ToolbarH + CanvasPad;
	const float CanvasW = WindowW - CanvasPad * 2.0f;
	const float CanvasH = WindowH - ToolbarH - CanvasPad * 2.0f;
	const float ImgW = (float)m_PendingUploadImage.m_Width;
	const float ImgH = (float)m_PendingUploadImage.m_Height;
	const float Scale = minimum(CanvasW / maximum(1.0f, ImgW), CanvasH / maximum(1.0f, ImgH));
	const float FinalW = maximum(1.0f, ImgW * Scale);
	const float FinalH = maximum(1.0f, ImgH * Scale);
	const float ImgX = CanvasX + (CanvasW - FinalW) / 2.0f;
	const float ImgY = CanvasY + (CanvasH - FinalH) / 2.0f;
	const SRenderRect NewCanvasRect = {ImgX, ImgY, FinalW, FinalH};

	RemapStrokesAfterCrop(AppliedCrop, OldImgW, OldImgH, OldCanvasRect, NewCanvasRect);
	m_ImageEditorCanvasRect = NewCanvasRect;

	ResetCropRectToFull();
	m_ImageEditor.m_CropDragging = false;
	m_ImageEditor.m_CropActiveHandle = ECropHandle::NONE;
	m_ImageEditor.m_CurrentTool = EImageEditorTool::PEN;
	return true;
}

void CUClientChatPasteImage::RenderCropOverlay(CChat *pChat, const SRenderRect &SelectionCanvas, const vec2 &MousePos) const
{
	const ColorRGBA MaskColor(0.02f, 0.03f, 0.05f, 0.62f);
	const float CanvasX = m_ImageEditorCanvasRect.m_X;
	const float CanvasY = m_ImageEditorCanvasRect.m_Y;
	const float CanvasW = m_ImageEditorCanvasRect.m_W;
	const float CanvasH = m_ImageEditorCanvasRect.m_H;
	const float SelL = SelectionCanvas.m_X;
	const float SelT = SelectionCanvas.m_Y;
	const float SelR = SelectionCanvas.m_X + SelectionCanvas.m_W;
	const float SelB = SelectionCanvas.m_Y + SelectionCanvas.m_H;

	if(SelT > CanvasY)
		pChat->Graphics()->DrawRect(CanvasX, CanvasY, CanvasW, SelT - CanvasY, MaskColor, 0, 0);
	if(SelB < CanvasY + CanvasH)
		pChat->Graphics()->DrawRect(CanvasX, SelB, CanvasW, CanvasY + CanvasH - SelB, MaskColor, 0, 0);
	if(SelL > CanvasX)
		pChat->Graphics()->DrawRect(CanvasX, SelT, SelL - CanvasX, SelectionCanvas.m_H, MaskColor, 0, 0);
	if(SelR < CanvasX + CanvasW)
		pChat->Graphics()->DrawRect(SelR, SelT, CanvasX + CanvasW - SelR, SelectionCanvas.m_H, MaskColor, 0, 0);

	pChat->Graphics()->DrawRect(SelL, SelT, SelectionCanvas.m_W, SelectionCanvas.m_H, ColorRGBA(0.95f, 0.97f, 1.0f, 0.92f), IGraphics::CORNER_ALL, 2.0f);

	const ColorRGBA GuideColor(1.0f, 1.0f, 1.0f, 0.18f);
	for(int i = 1; i <= 2; ++i)
	{
		const float TX = SelL + SelectionCanvas.m_W * (float)i / 3.0f;
		const float TY = SelT + SelectionCanvas.m_H * (float)i / 3.0f;
		pChat->Graphics()->DrawRect(TX, SelT, 1.0f, SelectionCanvas.m_H, GuideColor, 0, 0);
		pChat->Graphics()->DrawRect(SelL, TY, SelectionCanvas.m_W, 1.0f, GuideColor, 0, 0);
	}

	const float HandleRadius = 6.0f;
	const ECropHandle HoveredHandle = HitTestCropHandle(MousePos, SelectionCanvas);
	auto DrawHandle = [&](float X, float Y, ECropHandle Handle) {
		const bool Hovered = HoveredHandle == Handle;
		const float Radius = Hovered ? HandleRadius + 1.5f : HandleRadius;
		pChat->Graphics()->DrawRect(X - Radius - 1.0f, Y - Radius - 1.0f, (Radius + 1.0f) * 2.0f, (Radius + 1.0f) * 2.0f, ColorRGBA(0.21f, 0.56f, 0.94f, 0.95f), IGraphics::CORNER_ALL, Radius + 1.0f);
		pChat->Graphics()->DrawRect(X - Radius, Y - Radius, Radius * 2.0f, Radius * 2.0f, ColorRGBA(0.97f, 0.98f, 1.0f, 0.98f), IGraphics::CORNER_ALL, Radius);
	};

	const float CX = (SelL + SelR) * 0.5f;
	const float CY = (SelT + SelB) * 0.5f;
	DrawHandle(SelL, SelT, ECropHandle::TOP_LEFT);
	DrawHandle(CX, SelT, ECropHandle::TOP);
	DrawHandle(SelR, SelT, ECropHandle::TOP_RIGHT);
	DrawHandle(SelL, CY, ECropHandle::LEFT);
	DrawHandle(SelR, CY, ECropHandle::RIGHT);
	DrawHandle(SelL, SelB, ECropHandle::BOTTOM_LEFT);
	DrawHandle(CX, SelB, ECropHandle::BOTTOM);
	DrawHandle(SelR, SelB, ECropHandle::BOTTOM_RIGHT);

	const int CropPxW = maximum(1, (int)roundf((m_ImageEditor.m_CropRect.m_X1 - m_ImageEditor.m_CropRect.m_X0) * m_PendingUploadImage.m_Width));
	const int CropPxH = maximum(1, (int)roundf((m_ImageEditor.m_CropRect.m_Y1 - m_ImageEditor.m_CropRect.m_Y0) * m_PendingUploadImage.m_Height));
	char aSizeLabel[64];
	str_format(aSizeLabel, sizeof(aSizeLabel), "%d x %d", CropPxW, CropPxH);
	const float LabelSize = 11.0f;
	const float LabelWidth = pChat->TextRender()->TextWidth(LabelSize, aSizeLabel, -1, -1);
	const float LabelX = SelL + maximum(0.0f, (SelectionCanvas.m_W - LabelWidth) * 0.5f);
	const float LabelY = SelB + 6.0f;
	pChat->Graphics()->DrawRect(LabelX - 6.0f, LabelY - 2.0f, LabelWidth + 12.0f, LabelSize + 6.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f), IGraphics::CORNER_ALL, 3.0f);
	CTextCursor SizeCursor;
	SizeCursor.SetPosition(vec2(LabelX, LabelY));
	SizeCursor.m_FontSize = LabelSize;
	pChat->TextRender()->TextColor(0.95f, 0.97f, 1.0f, 0.95f);
	pChat->TextRender()->TextEx(&SizeCursor, aSizeLabel);
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

	// Render the color picker popup on top of the editor in UI space.
	if(pChat->Ui()->IsPopupOpen())
	{
		pChat->Ui()->StartCheck();
		pChat->Ui()->Update();
		pChat->Ui()->MapScreen();
		pChat->Ui()->RenderPopupMenus();
		pChat->Ui()->FinishCheck();
		pChat->Ui()->ClearHotkeys();
		// Draw the cursor above the popup so it is never hidden behind it.
		pChat->RenderTools()->RenderCursor(pChat->Ui()->MousePos(), 24.0f);
	}

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

	// While the color picker popup is open let the CUi input system handle
	// everything (hex field, ESC to close, mouse) instead of the editor.
	if(m_ImageEditor.m_Active && pChat->Ui()->IsPopupOpen())
	{
		pChat->Ui()->OnInput(Event);
		return true;
	}

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
	UnloadPendingUploadTextures(pChat->Graphics(), m_PendingUploadImage.m_Texture, m_PendingUploadImage.m_OriginalTexture);
	m_PendingUploadImage = CUClientChatPasteImage::SPendingUploadImage();
	m_PendingUploadClosePressed = false;
	m_PendingUploadCloseRectValid = false;
	m_ImageEditor.m_vStrokes.clear();
	m_ImageEditor.m_vStrokeSnapshot.clear();
	m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
	m_ImageEditor.m_IsDrawing = false;
	m_ImageEditor.m_Active = false;
	m_ImageEditor.m_MouseDownLastFrame = false;
	m_ImageEditor.m_CropDragging = false;
	m_ImageEditor.m_CropActiveHandle = ECropHandle::NONE;
	ResetCropRectToFull();
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
	m_ImageEditor.m_CropDragging = false;
	m_ImageEditor.m_CropActiveHandle = ECropHandle::NONE;
	ResetCropRectToFull();
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
	m_ImageEditor.m_CropSnapshot = m_ImageEditor.m_CropRect;
	m_ImageEditor.m_Active = true;
	m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
	m_ImageEditor.m_CurrentTool = CUClientChatPasteImage::EImageEditorTool::PEN;
	m_ImageEditor.m_PenThickness = maximum(1.0f, m_ImageEditor.m_PenThickness);
	m_ImageEditor.m_IsDrawing = false;
	m_ImageEditor.m_MouseDownLastFrame = false;
	m_ImageEditor.m_CropDragging = false;
	m_ImageEditor.m_CropActiveHandle = ECropHandle::NONE;
}

void CUClientChatPasteImage::CloseImageEditor()
{
	m_ImageEditor.m_Active = false;
	m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
	m_ImageEditor.m_IsDrawing = false;
	m_ImageEditor.m_MouseDownLastFrame = false;
	m_ImageEditor.m_CropDragging = false;
	m_ImageEditor.m_CropActiveHandle = ECropHandle::NONE;
}

void CUClientChatPasteImage::CancelImageEditor(CChat *pChat)
{
	pChat->Ui()->ClosePopupMenu(&m_PenColorPickerContext);
	m_ImageEditor.m_vStrokes = m_ImageEditor.m_vStrokeSnapshot;
	m_ImageEditor.m_CropRect = m_ImageEditor.m_CropSnapshot;
	m_ImageEditor.m_vStrokeSnapshot.clear();
	CloseImageEditor();
}

void CUClientChatPasteImage::SaveImageEditorChanges(CChat *pChat)
{
	pChat->Ui()->ClosePopupMenu(&m_PenColorPickerContext);
	if(!m_ImageEditor.m_Active)
		return;
	if(!m_PendingUploadImage.HasImage() || m_PendingUploadImage.m_vOriginalPng.empty())
	{
		CloseImageEditor();
		return;
	}

	const bool HasStrokes = !m_ImageEditor.m_vStrokes.empty() || !m_ImageEditor.m_CurrentStroke.m_vPoints.empty();
	const bool HasPendingCrop = !CropRectIsFull(m_ImageEditor.m_CropRect);
	if(!HasStrokes && !HasPendingCrop)
	{
		m_ImageEditor.m_vStrokeSnapshot.clear();
		CloseImageEditor();
		return;
	}

	if(HasPendingCrop && !ApplyImageCrop(pChat))
		return;

	if(!HasStrokes)
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

	UnloadPendingUploadDisplayTexture(pChat->Graphics(), m_PendingUploadImage.m_Texture, m_PendingUploadImage.m_OriginalTexture);
	m_PendingUploadImage.m_Texture = NewTexture;
	m_PendingUploadImage.m_vPng.assign(Writer.Data(), Writer.Data() + Writer.Size());
	m_ImageEditor.m_vStrokeSnapshot.clear();

	CloseImageEditor();
}

void CUClientChatPasteImage::UpdateImageEditorInput(CChat *pChat)
{
	if(!m_ImageEditor.m_Active || !m_PendingUploadImage.HasImage())
		return;

	// Keep the active pen color in sync with the persistent color picker value.
	m_ImageEditor.m_PenColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UcChatImagePenColor, false));

	// While the color picker popup is open the CUi input system owns the mouse,
	// so skip the editor's manual hit-testing to avoid drawing behind the popup.
	if(pChat->Ui()->IsPopupOpen())
	{
		m_ImageEditor.m_IsDrawing = false;
		m_ImageEditor.m_MouseDownLastFrame = pChat->Input()->KeyIsPressed(KEY_MOUSE_1);
		return;
	}

	const float ScreenW = maximum(1.0f, (float)pChat->Graphics()->WindowWidth());
	const float ScreenH = maximum(1.0f, (float)pChat->Graphics()->WindowHeight());
	const vec2 WindowSize(ScreenW, ScreenH);
	const vec2 UiMousePos = pChat->Ui()->UpdatedMousePos() * vec2(pChat->Ui()->Screen()->w, pChat->Ui()->Screen()->h) / WindowSize;
	const vec2 MousePos(UiMousePos.x * ScreenW / pChat->Ui()->Screen()->w, UiMousePos.y * ScreenH / pChat->Ui()->Screen()->h);
	const bool MouseDown = pChat->Input()->KeyIsPressed(KEY_MOUSE_1);
	const bool MouseClicked = MouseDown && !m_ImageEditor.m_MouseDownLastFrame;
	const bool MouseReleased = !MouseDown && m_ImageEditor.m_MouseDownLastFrame;

	auto InRect = [&](const CUClientChatPasteImage::SRenderRect &Rect) {
		return MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
			MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H;
	};

	if(MouseClicked && InRect(m_ImageEditorPenButtonRect))
		m_ImageEditor.m_CurrentTool = CUClientChatPasteImage::EImageEditorTool::PEN;

	if(MouseClicked && InRect(m_ImageEditorEraserButtonRect))
		m_ImageEditor.m_CurrentTool = CUClientChatPasteImage::EImageEditorTool::ERASER;

	if(MouseClicked && InRect(m_ImageEditorCropButtonRect))
	{
		m_ImageEditor.m_CurrentTool = CUClientChatPasteImage::EImageEditorTool::CROP;
		m_ImageEditor.m_IsDrawing = false;
		m_ImageEditor.m_CurrentStroke.m_vPoints.clear();
	}

	if(MouseClicked && m_ImageEditor.m_CurrentTool == EImageEditorTool::CROP && InRect(m_ImageEditorCropApplyRect))
	{
		ApplyImageCrop(pChat);
		m_ImageEditor.m_MouseDownLastFrame = MouseDown;
		return;
	}

	if(MouseClicked && m_ImageEditor.m_CurrentTool == EImageEditorTool::CROP && InRect(m_ImageEditorCropResetRect))
		ResetCropRectToFull();

	for(size_t i = 0; i < std::size(m_aImageEditorAspectRects); ++i)
	{
		if(MouseClicked && m_ImageEditor.m_CurrentTool == EImageEditorTool::CROP && InRect(m_aImageEditorAspectRects[i]))
		{
			m_ImageEditor.m_CropAspect = (ECropAspectPreset)i;
			ApplyCropAspectToRect(m_ImageEditor.m_CropRect, m_ImageEditor.m_CropAspect, ECropHandle::NONE);
		}
	}

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

	if(m_ImageEditor.m_CurrentTool != EImageEditorTool::CROP)
	{
		if(MouseClicked && InRect(m_ImageEditorColorSwatchRect))
			m_ImageEditorColorSwatchPressed = true;

		// Open on release so the click that opens the popup is not immediately
		// interpreted as an "outside click" that closes it again.
		if(MouseReleased && m_ImageEditorColorSwatchPressed && InRect(m_ImageEditorColorSwatchRect))
		{
			const float UiW = pChat->Ui()->Screen()->w;
			const float UiH = pChat->Ui()->Screen()->h;
			const float PopupX = (m_ImageEditorColorSwatchRect.m_X + m_ImageEditorColorSwatchRect.m_W + 8.0f) * UiW / ScreenW;
			const float PopupY = m_ImageEditorColorSwatchRect.m_Y * UiH / ScreenH;

			m_PenColorPickerContext.m_pHslaColor = &g_Config.m_UcChatImagePenColor;
			m_PenColorPickerContext.m_HslaColor = ColorHSLA(g_Config.m_UcChatImagePenColor, false);
			m_PenColorPickerContext.m_HsvaColor = color_cast<ColorHSVA>(m_PenColorPickerContext.m_HslaColor);
			m_PenColorPickerContext.m_RgbaColor = color_cast<ColorRGBA>(m_PenColorPickerContext.m_HsvaColor);
			m_PenColorPickerContext.m_Alpha = false;
			pChat->Ui()->ShowPopupColorPicker(PopupX, PopupY, &m_PenColorPickerContext);
		}

		if(MouseReleased)
			m_ImageEditorColorSwatchPressed = false;
	}

	if(MouseClicked && InRect(m_ImageEditorThicknessMinusRect) && m_ImageEditor.m_CurrentTool != EImageEditorTool::CROP)
		m_ImageEditor.m_PenThickness = maximum(1.0f, m_ImageEditor.m_PenThickness - 1.0f);

	if(MouseClicked && InRect(m_ImageEditorThicknessPlusRect) && m_ImageEditor.m_CurrentTool != EImageEditorTool::CROP)
		m_ImageEditor.m_PenThickness = minimum(14.0f, m_ImageEditor.m_PenThickness + 1.0f);

	if(MouseDown && InRect(m_ImageEditorThicknessRect) && m_ImageEditor.m_CurrentTool != EImageEditorTool::CROP)
	{
		const float T = std::clamp((MousePos.x - m_ImageEditorThicknessRect.m_X) / maximum(1.0f, m_ImageEditorThicknessRect.m_W), 0.0f, 1.0f);
		m_ImageEditor.m_PenThickness = 1.0f + T * 13.0f;
	}

	const bool MouseOverCanvas = InRect(m_ImageEditorCanvasRect);
	const SRenderRect CropSelectionCanvas = CropRectToCanvasRect(m_ImageEditor.m_CropRect);

	if(m_ImageEditor.m_CurrentTool == CUClientChatPasteImage::EImageEditorTool::CROP)
	{
		if(MouseClicked && MouseOverCanvas)
		{
			const ECropHandle Handle = HitTestCropHandle(MousePos, CropSelectionCanvas);
			if(Handle != ECropHandle::NONE)
			{
				m_ImageEditor.m_CropDragging = true;
				m_ImageEditor.m_CropActiveHandle = Handle;
				m_ImageEditor.m_CropDragStartRect = m_ImageEditor.m_CropRect;
				m_ImageEditor.m_CropDragAnchor = CanvasToImageNorm(MousePos);
			}
			else if(IsPointInsideCropSelection(MousePos, CropSelectionCanvas))
			{
				m_ImageEditor.m_CropDragging = true;
				m_ImageEditor.m_CropActiveHandle = ECropHandle::MOVE;
				m_ImageEditor.m_CropDragStartRect = m_ImageEditor.m_CropRect;
				m_ImageEditor.m_CropDragAnchor = CanvasToImageNorm(MousePos);
			}
			else
			{
				m_ImageEditor.m_CropDragging = true;
				m_ImageEditor.m_CropActiveHandle = ECropHandle::NEW_SELECTION;
				m_ImageEditor.m_CropDragStartRect = m_ImageEditor.m_CropRect;
				m_ImageEditor.m_CropDragAnchor = CanvasToImageNorm(MousePos);
				m_ImageEditor.m_CropRect.m_X0 = m_ImageEditor.m_CropDragAnchor.x;
				m_ImageEditor.m_CropRect.m_Y0 = m_ImageEditor.m_CropDragAnchor.y;
				m_ImageEditor.m_CropRect.m_X1 = m_ImageEditor.m_CropDragAnchor.x;
				m_ImageEditor.m_CropRect.m_Y1 = m_ImageEditor.m_CropDragAnchor.y;
			}
		}

		if(m_ImageEditor.m_CropDragging && MouseDown)
			UpdateCropDrag(MousePos, m_PendingUploadImage.m_Width, m_PendingUploadImage.m_Height);

		if(m_ImageEditor.m_CropDragging && MouseReleased)
		{
			m_ImageEditor.m_CropDragging = false;
			m_ImageEditor.m_CropActiveHandle = ECropHandle::NONE;
		}
	}
	else if(m_ImageEditor.m_CurrentTool == CUClientChatPasteImage::EImageEditorTool::PEN)
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
	else if(m_ImageEditor.m_CurrentTool == CUClientChatPasteImage::EImageEditorTool::ERASER)
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

	auto InRect = [&](const CUClientChatPasteImage::SRenderRect &Rect) {
		return MousePos.x >= Rect.m_X && MousePos.x <= Rect.m_X + Rect.m_W &&
			MousePos.y >= Rect.m_Y && MousePos.y <= Rect.m_Y + Rect.m_H;
	};

	const float WindowMargin = 18.0f;
	const float WindowX = WindowMargin;
	const float WindowY = WindowMargin;
	const float WindowW = ScreenW - WindowMargin * 2.0f;
	const float WindowH = ScreenH - WindowMargin * 2.0f;
	const float ToolbarH = ImageEditorToolbarHeight();
	const float ToolbarPad = 16.0f;
	const float ButtonH = 56.0f;

	m_ImageEditorPenButtonRect = {WindowX + 18.0f, WindowY + ToolbarPad, 84.0f, ButtonH};
	m_ImageEditorEraserButtonRect = {m_ImageEditorPenButtonRect.m_X + m_ImageEditorPenButtonRect.m_W + 8.0f, WindowY + ToolbarPad, 96.0f, ButtonH};
	m_ImageEditorCropButtonRect = {m_ImageEditorEraserButtonRect.m_X + m_ImageEditorEraserButtonRect.m_W + 8.0f, WindowY + ToolbarPad, 84.0f, ButtonH};
	m_ImageEditorClearButtonRect = {m_ImageEditorCropButtonRect.m_X + m_ImageEditorCropButtonRect.m_W + 8.0f, WindowY + ToolbarPad, 84.0f, ButtonH};
	m_ImageEditorCancelButtonRect = {WindowX + WindowW - 226.0f, WindowY + ToolbarPad, 96.0f, ButtonH};
	m_ImageEditorSaveButtonRect = {WindowX + WindowW - 118.0f, WindowY + ToolbarPad, 100.0f, ButtonH};

	const float ColorStartX = m_ImageEditorClearButtonRect.m_X + m_ImageEditorClearButtonRect.m_W + 20.0f;
	const float ColorLabelW = 40.0f;
	const float ColorSize = 28.0f;
	m_ImageEditorColorSwatchRect = {ColorStartX + ColorLabelW, WindowY + 16.0f, ColorSize, ColorSize};

	m_ImageEditorThicknessMinusRect = {ColorStartX, WindowY + 54.0f, 26.0f, 14.0f};
	m_ImageEditorThicknessRect = {m_ImageEditorThicknessMinusRect.m_X + 32.0f, WindowY + 54.0f, 156.0f, 14.0f};
	m_ImageEditorThicknessPlusRect = {m_ImageEditorThicknessRect.m_X + m_ImageEditorThicknessRect.m_W + 6.0f, WindowY + 54.0f, 26.0f, 14.0f};

	const float CropRowY = WindowY + 88.0f;
	const char *apAspectLabels[4] = {"Free", "1:1", "4:3", "16:9"};
	const float AspectChipW = 52.0f;
	const float AspectChipH = 24.0f;
	float AspectX = WindowX + 18.0f;
	for(size_t i = 0; i < std::size(m_aImageEditorAspectRects); ++i)
	{
		m_aImageEditorAspectRects[i] = {AspectX, CropRowY, AspectChipW, AspectChipH};
		AspectX += AspectChipW + 8.0f;
	}
	m_ImageEditorCropResetRect = {WindowX + WindowW - 226.0f, CropRowY, 96.0f, AspectChipH};
	m_ImageEditorCropApplyRect = {WindowX + WindowW - 118.0f, CropRowY, 100.0f, AspectChipH};

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
	DrawButton(m_ImageEditorCropButtonRect, "Crop", m_ImageEditor.m_CurrentTool == CUClientChatPasteImage::EImageEditorTool::CROP, ColorRGBA(0.22f, 0.40f, 0.34f, 0.96f));
	DrawButton(m_ImageEditorClearButtonRect, "Clear", false, ColorRGBA(0.45f, 0.20f, 0.20f, 0.96f));
	DrawButton(m_ImageEditorCancelButtonRect, "Cancel", false, ColorRGBA(0.30f, 0.24f, 0.24f, 0.96f));
	DrawButton(m_ImageEditorSaveButtonRect, "Save", false, ColorRGBA(0.19f, 0.44f, 0.28f, 0.96f));

	if(m_ImageEditor.m_CurrentTool == EImageEditorTool::CROP)
	{
		pChat->Graphics()->DrawRect(WindowX + 2.0f, CropRowY - 6.0f, WindowW - 4.0f, 36.0f, ColorRGBA(0.06f, 0.08f, 0.11f, 0.92f), IGraphics::CORNER_NONE, 0.0f);
		for(size_t i = 0; i < std::size(m_aImageEditorAspectRects); ++i)
		{
			const bool Selected = (int)m_ImageEditor.m_CropAspect == (int)i;
			DrawButton(m_aImageEditorAspectRects[i], apAspectLabels[i], Selected, ColorRGBA(0.24f, 0.36f, 0.54f, 0.96f));
		}
		DrawButton(m_ImageEditorCropResetRect, "Reset", false, ColorRGBA(0.30f, 0.24f, 0.24f, 0.96f));
		DrawButton(m_ImageEditorCropApplyRect, "Apply", false, ColorRGBA(0.19f, 0.44f, 0.28f, 0.96f));
	}

	if(m_ImageEditor.m_CurrentTool != EImageEditorTool::CROP)
	{
		Cursor.SetPosition(vec2(ColorStartX, m_ImageEditorColorSwatchRect.m_Y + (ColorSize - 12.0f) * 0.5f));
		Cursor.m_FontSize = 12.0f;
		pChat->TextRender()->TextColor(0.89f, 0.92f, 0.97f, 0.95f);
		pChat->TextRender()->TextEx(&Cursor, "Color");

		const CUClientChatPasteImage::SRenderRect &SwatchRect = m_ImageEditorColorSwatchRect;
		const bool SwatchHovered = InRect(SwatchRect);
		const float SwatchBorder = SwatchHovered ? 2.0f : 1.0f;
		pChat->Graphics()->DrawRect(SwatchRect.m_X - SwatchBorder, SwatchRect.m_Y - SwatchBorder, SwatchRect.m_W + SwatchBorder * 2.0f, SwatchRect.m_H + SwatchBorder * 2.0f, ColorRGBA(0.96f, 0.98f, 1.0f, SwatchHovered ? 0.95f : 0.45f), IGraphics::CORNER_ALL, 3.0f);
		pChat->Graphics()->DrawRect(SwatchRect.m_X, SwatchRect.m_Y, SwatchRect.m_W, SwatchRect.m_H, m_ImageEditor.m_PenColor, IGraphics::CORNER_ALL, 3.0f);

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
	}

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

	if(m_ImageEditor.m_CurrentTool == EImageEditorTool::CROP)
		RenderCropOverlay(pChat, CropRectToCanvasRect(m_ImageEditor.m_CropRect), MousePos);

	const bool MouseInsideEditor = MousePos.x >= WindowX && MousePos.x <= WindowX + WindowW &&
		MousePos.y >= WindowY && MousePos.y <= WindowY + WindowH;
	const bool MouseInsideCanvas = InRect(m_ImageEditorCanvasRect);

	// While the color picker popup is open the standard UI cursor is drawn on top
	// of the popup instead, so skip the editor's custom brush cursor here.
	if(MouseInsideEditor && !pChat->Ui()->IsPopupOpen())
	{
		if(m_ImageEditor.m_CurrentTool == EImageEditorTool::CROP)
		{
			const float CrosshairRadius = 12.0f;
			pChat->Graphics()->DrawRect(MousePos.x - CrosshairRadius, MousePos.y - 1.0f, CrosshairRadius * 2.0f, 2.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.95f), IGraphics::CORNER_ALL, 0.5f);
			pChat->Graphics()->DrawRect(MousePos.x - 1.0f, MousePos.y - CrosshairRadius, 2.0f, CrosshairRadius * 2.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.95f), IGraphics::CORNER_ALL, 0.5f);
			pChat->Graphics()->DrawRect(MousePos.x - CrosshairRadius + 1.0f, MousePos.y - 0.5f, CrosshairRadius * 2.0f - 2.0f, 1.0f, ColorRGBA(0.95f, 0.97f, 1.0f, 0.95f), IGraphics::CORNER_ALL, 0.5f);
			pChat->Graphics()->DrawRect(MousePos.x - 0.5f, MousePos.y - CrosshairRadius + 1.0f, 1.0f, CrosshairRadius * 2.0f - 2.0f, ColorRGBA(0.95f, 0.97f, 1.0f, 0.95f), IGraphics::CORNER_ALL, 0.5f);
		}
		else
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