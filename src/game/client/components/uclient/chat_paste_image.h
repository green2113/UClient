#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_CHAT_PASTE_IMAGE_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_CHAT_PASTE_IMAGE_H

#include <engine/graphics.h>
#include <engine/input.h>

#include <engine/client/clipboard_image.h>

#include <game/client/ui.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class CChat;
class CHttpRequest;

class CUClientChatPasteImage
{
	struct SRenderRect
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
	};

	enum class EPendingUploadState
	{
		NONE = 0,
		READY,
		UPLOADING,
		FAILED,
	};

	struct SPendingUploadImage
	{
		EPendingUploadState m_State = EPendingUploadState::NONE;
		IGraphics::CTextureHandle m_Texture;
		IGraphics::CTextureHandle m_OriginalTexture;
		std::shared_ptr<CHttpRequest> m_pRequest;
		std::vector<uint8_t> m_vOriginalPng;
		std::vector<uint8_t> m_vPng;
		int m_Width = 0;
		int m_Height = 0;
		int m_Team = 0;
		char m_aMessagePrefix[256] = "";
		char m_aError[128] = "";
		char m_aUploadContentType[32] = "image/png";
		char m_aFileName[256] = "";
		bool m_IsGif = false;

		bool HasImage() const
		{
			return !m_vPng.empty() && (m_IsGif || (m_Texture.IsValid() && m_Width > 0 && m_Height > 0));
		}

		bool AllowsEditing() const
		{
			return !m_IsGif;
		}
	};

	enum class EImageEditorTool
	{
		PEN = 0,
		ERASER = 1,
		CROP = 2,
	};

	enum class ECropAspectPreset
	{
		FREE = 0,
		SQUARE,
		RATIO_4_3,
		RATIO_16_9,
	};

	enum class ECropHandle
	{
		NONE = 0,
		MOVE,
		NEW_SELECTION,
		TOP_LEFT,
		TOP,
		TOP_RIGHT,
		LEFT,
		RIGHT,
		BOTTOM_LEFT,
		BOTTOM,
		BOTTOM_RIGHT,
	};

	struct SImageCropRect
	{
		float m_X0 = 0.0f;
		float m_Y0 = 0.0f;
		float m_X1 = 1.0f;
		float m_Y1 = 1.0f;
	};

	struct SImageEditorStroke
	{
		std::vector<vec2> m_vPoints;
		ColorRGBA m_Color;
		float m_Thickness;
		EImageEditorTool m_Tool;
	};

	struct SImageEditorState
	{
		bool m_Active = false;
		std::vector<SImageEditorStroke> m_vStrokes;
		std::vector<SImageEditorStroke> m_vStrokeSnapshot;
		SImageEditorStroke m_CurrentStroke;
		EImageEditorTool m_CurrentTool = EImageEditorTool::PEN;
		ColorRGBA m_PenColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		float m_PenThickness = 3.0f;
		bool m_IsDrawing = false;
		bool m_MouseDownLastFrame = false;
		SImageCropRect m_CropRect;
		SImageCropRect m_CropSnapshot;
		ECropAspectPreset m_CropAspect = ECropAspectPreset::FREE;
		bool m_CropDragging = false;
		ECropHandle m_CropActiveHandle = ECropHandle::NONE;
		vec2 m_CropDragAnchor = vec2(0.0f, 0.0f);
		SImageCropRect m_CropDragStartRect;
		bool m_EyedropperActive = false;
	};

public:
	void Reset(CChat *pChat);
	bool OnInput(CChat *pChat, const IInput::CEvent &Event);
	bool TryPasteFromClipboard(CChat *pChat);
	bool IsPasteWarningPending() const;
	void OnUpdate(CChat *pChat);
	bool OnRenderEditor(CChat *pChat);
	float PreviewHeight(CChat *pChat, float Width, float FontSize) const;
	void RenderPreview(CChat *pChat, float X, float Y, float Width, float Height, float FontSize);
	bool TrySendOnEnter(CChat *pChat, const char *pMessagePrefix);
	void ConfirmPasteWarning(CChat *pChat, bool DontAskAgain);
	void CancelPasteWarning(CChat *pChat);

private:
	static constexpr int CROP_MIN_PIXELS = 32;

	void ClearPendingUploadImage(CChat *pChat);
	bool SetPendingUploadImage(CChat *pChat, const SClipboardImage &Image);
	bool SetPendingUploadImageFromMedia(CChat *pChat, const SClipboardPastedMedia &Media);
	bool TryPasteClipboardImage(CChat *pChat);
	bool StartPendingUpload(CChat *pChat, const char *pMessagePrefix);
	void UpdatePendingUpload(CChat *pChat);
	void PendingUploadPreviewSize(CChat *pChat, float Width, float FontSize, float &PreviewW, float &PreviewH) const;
	bool PendingUploadCloseButtonRect(CChat *pChat, float X, float Y, float Width, float FontSize, SRenderRect &ButtonRect) const;
	void OpenImageEditor(CChat *pChat);
	void CloseImageEditor();
	void CancelImageEditor(CChat *pChat);
	void SaveImageEditorChanges(CChat *pChat);
	void UpdateImageEditorInput(CChat *pChat);
	void RenderImageEditor(CChat *pChat);
	void OpenPasteWarningPopup(CChat *pChat);

	bool CropRectIsFull(const SImageCropRect &Crop) const;
	void NormalizeCropRect(SImageCropRect &Crop) const;
	float CropAspectRatio(ECropAspectPreset Preset) const;
	void ClampCropRect(SImageCropRect &Crop, int ImgW, int ImgH, ECropAspectPreset Aspect, ECropHandle ActiveHandle) const;
	SRenderRect CropRectToCanvasRect(const SImageCropRect &Crop) const;
	vec2 CanvasToImageNorm(const vec2 &CanvasPoint) const;
	vec2 ImageNormToCanvas(const vec2 &Norm) const;
	void CropRectToPixelRect(const SImageCropRect &Crop, int ImgW, int ImgH, int &OutX0, int &OutY0, int &OutW, int &OutH) const;
	ECropHandle HitTestCropHandle(const vec2 &MousePos, const SRenderRect &SelectionCanvas) const;
	bool IsPointInsideCropSelection(const vec2 &MousePos, const SRenderRect &SelectionCanvas) const;
	void ApplyCropAspectToRect(SImageCropRect &Crop, ECropAspectPreset Aspect, ECropHandle AnchorHandle) const;
	void RemapStrokesAfterCrop(const SImageCropRect &AppliedCrop, int OldImgW, int OldImgH, const SRenderRect &OldCanvasRect, const SRenderRect &NewCanvasRect);
	bool ApplyImageCrop(CChat *pChat);
	void ResetCropRectToFull();
	void UpdateCropDrag(const vec2 &MousePos, int ImgW, int ImgH);
	void RenderCropOverlay(CChat *pChat, const SRenderRect &SelectionCanvas, const vec2 &MousePos) const;
	static constexpr int PEN_COLOR_HISTORY_MAX = 5;
	static constexpr float TOOL_BUTTON_SIZE = 40.0f;
	static constexpr float COLOR_SWATCH_SIZE = 26.0f;
	static constexpr float HISTORY_SWATCH_SIZE = 22.0f;
	static constexpr float HISTORY_SWATCH_GAP = 6.0f;

	void PushPenColorToHistory(unsigned HslaColor);
	void InvalidateEyedropperBaseCache();
	bool EnsureEyedropperBaseCache(CChat *pChat);
	bool SampleEyedropperBasePixel(const vec2 &CanvasPoint, ColorRGBA &OutColor) const;
	bool TryPickColorAtCanvas(CChat *pChat, const vec2 &CanvasPoint, ColorRGBA &OutColor);
	float ImageEditorToolbarHeight() const;

	struct SEyedropperBaseCache
	{
		std::vector<ColorRGBA> m_vPixels;
		int m_W = 0;
		int m_H = 0;
		bool m_Valid = false;
	};

	SEyedropperBaseCache m_EyedropperBaseCache;
	ColorRGBA m_EyedropperPreviewColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	bool m_EyedropperPreviewValid = false;

	SPendingUploadImage m_PendingUploadImage;
	SClipboardPastedMedia m_WarningPendingMedia;
	bool m_PendingUploadClosePressed = false;
	bool m_PendingUploadCloseRectValid = false;
	SRenderRect m_PendingUploadCloseRect;
	SImageEditorState m_ImageEditor;
	SRenderRect m_ImageEditorEditButtonRect;
	bool m_ImageEditorEditButtonRectValid = false;
	SRenderRect m_ImageEditorPenButtonRect;
	SRenderRect m_ImageEditorEraserButtonRect;
	SRenderRect m_ImageEditorCropButtonRect;
	std::array<SRenderRect, 4> m_aImageEditorAspectRects;
	SRenderRect m_ImageEditorCropResetRect;
	SRenderRect m_ImageEditorCropApplyRect;
	SRenderRect m_ImageEditorColorSwatchRect;
	SRenderRect m_ImageEditorEyedropperButtonRect;
	bool m_ImageEditorColorSwatchPressed = false;
	std::array<SRenderRect, PEN_COLOR_HISTORY_MAX> m_aImageEditorColorHistoryRects;
	std::array<unsigned, PEN_COLOR_HISTORY_MAX> m_aPenColorHistory{};
	int m_PenColorHistoryCount = 0;
	CUi::SColorPickerPopupContext m_PenColorPickerContext;
	SRenderRect m_ImageEditorCanvasRect;
	SRenderRect m_ImageEditorThicknessRect;
	SRenderRect m_ImageEditorThicknessMinusRect;
	SRenderRect m_ImageEditorThicknessPlusRect;
	SRenderRect m_ImageEditorClearButtonRect;
	SRenderRect m_ImageEditorSaveButtonRect;
	SRenderRect m_ImageEditorCancelButtonRect;
};

#endif
