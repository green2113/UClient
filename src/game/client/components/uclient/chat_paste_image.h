#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_CHAT_PASTE_IMAGE_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_CHAT_PASTE_IMAGE_H

#include <engine/graphics.h>
#include <engine/input.h>

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

		bool HasImage() const
		{
			return m_Texture.IsValid() && m_Width > 0 && m_Height > 0 && !m_vPng.empty();
		}
	};

	enum class EImageEditorTool
	{
		PEN = 0,
		ERASER = 1,
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
	};

public:
	void Reset(CChat *pChat);
	bool OnInput(CChat *pChat, const IInput::CEvent &Event);
	bool TryHandlePasteKey(CChat *pChat, const IInput::CEvent &Event);
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
	void ClearPendingUploadImage(CChat *pChat);
	bool SetPendingUploadImage(CChat *pChat, const IInput::SClipboardImage &Image);
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

	SPendingUploadImage m_PendingUploadImage;
	IInput::SClipboardImage m_WarningPendingClipboardImage;
	bool m_PendingUploadClosePressed = false;
	bool m_PendingUploadCloseRectValid = false;
	SRenderRect m_PendingUploadCloseRect;
	SImageEditorState m_ImageEditor;
	SRenderRect m_ImageEditorEditButtonRect;
	bool m_ImageEditorEditButtonRectValid = false;
	SRenderRect m_ImageEditorPenButtonRect;
	SRenderRect m_ImageEditorEraserButtonRect;
	std::array<SRenderRect, 6> m_aImageEditorColorRects;
	SRenderRect m_ImageEditorCanvasRect;
	SRenderRect m_ImageEditorThicknessRect;
	SRenderRect m_ImageEditorThicknessMinusRect;
	SRenderRect m_ImageEditorThicknessPlusRect;
	SRenderRect m_ImageEditorClearButtonRect;
	SRenderRect m_ImageEditorSaveButtonRect;
	SRenderRect m_ImageEditorCancelButtonRect;
};

#endif
