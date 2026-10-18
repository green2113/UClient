#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_DRAW_MODE_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_DRAW_MODE_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/shared/uclient_presence_protocol.h>

#include <game/client/component.h>
#include <game/client/ui.h>

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

/**
 * Shared drawing on the map. Strokes are relayed through the UClient presence server to UClient
 * players on the same game server and map, and stay visible outside the drawing mode.
 *
 * This component renders the strokes in world space (behind tees). The toolbar is a separate
 * component (m_Toolbar) so it renders in the HUD layer right below chat.
 */
class CUClientDrawMode : public CComponent
{
public:
	class CToolbar : public CComponent
	{
	public:
		CUClientDrawMode *m_pDrawMode = nullptr;
		int Sizeof() const override { return sizeof(*this); }
		void OnRender() override { m_pDrawMode->RenderToolbar(); }
	};
	CToolbar m_Toolbar;

	CUClientDrawMode() { m_Toolbar.m_pDrawMode = this; }
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnReset() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnRelease() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

	bool IsActive() const { return m_Active; }
	vec2 CameraCenter() const { return m_CameraCenter; }
	bool HideTees() const;
	void OnDrawBroadcast(const UClientPresence::CDrawBroadcast &Draw);

private:
	enum class ETool
	{
		PEN,
		CRAYON,
		ERASER,
	};

	struct SStroke
	{
		CUuid m_Owner;
		uint32_t m_Id = 0;
		uint8_t m_Tool = UClientPresence::DRAW_TOOL_PEN;
		ColorRGBA m_Color;
		float m_Width = 0.0f;
		std::vector<vec2> m_vPoints;
		vec2 m_Min;
		vec2 m_Max;
	};

	bool m_Active = false;
	vec2 m_CameraCenter = vec2(0.0f, 0.0f);
	ETool m_Tool = ETool::PEN;
	int m_Width = 8;
	int m_ColorIndex = 0;

	std::vector<SStroke> m_vStrokes;
	std::set<std::pair<CUuid, uint32_t>> m_StrokeKeys;
	uint32_t m_NextStrokeId = 1;

	// Stroke being drawn. Points are sent in segments; each segment starts at the previous one's end.
	bool m_Drawing = false;
	std::vector<vec2> m_vPendingPoints;
	bool m_HasSegmentStart = false;
	vec2 m_SegmentStart;
	int64_t m_LastSegmentFlush = 0;

	std::vector<uint32_t> m_vPendingErase;
	int64_t m_LastEraseFlush = 0;

	bool m_Panning = false;
	vec2 m_LastPanMouse;
	bool m_PointerOverUi = false;
	bool m_WasLeftDown = false;

	// Snapshot download state for the current server and map.
	SHA256_DIGEST m_MapSha256 = {};
	bool m_HasMap = false;
	uint64_t m_SnapshotAfterSeq = 0;
	bool m_SnapshotDone = false;
	int64_t m_LastSubscribe = 0;

	CButtonContainer m_PenButton;
	CButtonContainer m_CrayonButton;
	CButtonContainer m_EraserButton;
	CButtonContainer m_ClearButton;
	CButtonContainer m_SettingsButton;
	CButtonContainer m_ExitButton;
	CButtonContainer m_aColorButtons[10];
	char m_WidthScrollbar = 0;
	char m_HideTeesCheckbox = 0;
	SPopupMenuId m_SettingsPopupId;
	CUi::SConfirmPopupContext m_ClearConfirm;

	static void ConDrawMode(IConsole::IResult *pResult, void *pUserData);
	static CUi::EPopupMenuFunctionResult PopupSettings(void *pContext, CUIRect View, bool Active);

	void SetActive(bool Active);
	bool CanEnter() const;
	void ClearStrokes();
	void ResetSync();
	void SendSubscribe();
	void WorldScreen(float *pPoints) const;
	vec2 MouseWorldPos() const;

	void AddStroke(SStroke &&Stroke);
	void RemoveStrokes(const CUuid &Owner, const std::vector<uint32_t> *pIds);
	void AddPoint(vec2 Pos);
	void FlushSegment(bool Final);
	void EraseAt(vec2 Pos);
	void FlushErase();
	void ClearOwnStrokes();
	void UpdateDrawing();

	void RenderStroke(const SStroke &Stroke, std::vector<IGraphics::CFreeformItem> &vItems);
	void RenderToolbar();
};

#endif
