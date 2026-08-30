#include "spec_tele_preview.h"

#include <base/system.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/camera.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <generated/protocol.h>


bool CUClientSpecTelePreview::IsFeatureActive() const
{
	if(!g_Config.m_UcSpecTelePreview)
		return false;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;
	if(GameClient()->m_Menus.IsActive())
		return false;
	if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;
	if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		return false;
	return Collision()->TeleLayer() != nullptr;
}

void CUClientSpecTelePreview::UpdateHover()
{
	m_HoverActive = false;
	m_OutCount = 0;

	if(!IsFeatureActive())
	{
		m_TeleNumber = 0;
		m_OutIndex = 0;
		return;
	}

	const vec2 Center = GameClient()->m_Camera.m_Center;
	const int MapIndex = Collision()->GetPureMapIndex(Center);
	const int Width = Collision()->GetWidth();
	const int Height = Collision()->GetHeight();
	if(MapIndex < 0 || MapIndex >= Width * Height)
	{
		m_TeleNumber = 0;
		m_OutIndex = 0;
		return;
	}

	const CTeleTile &Tele = Collision()->TeleLayer()[MapIndex];
	if(Tele.m_Number == 0 || (Tele.m_Type != TILE_TELEIN && Tele.m_Type != TILE_TELEINEVIL))
	{
		m_TeleNumber = 0;
		m_OutIndex = 0;
		return;
	}

	const std::vector<vec2> &vOuts = Collision()->TeleOuts(Tele.m_Number - 1);
	if(vOuts.empty())
	{
		m_TeleNumber = 0;
		m_OutIndex = 0;
		return;
	}

	if(m_TeleNumber != Tele.m_Number)
	{
		m_TeleNumber = Tele.m_Number;
		m_OutIndex = 0;
	}

	m_OutCount = (int)vOuts.size();
	if(m_OutIndex < 0)
		m_OutIndex = 0;
	if(m_OutIndex >= m_OutCount)
		m_OutIndex = m_OutCount - 1;
	m_HoverActive = true;
}

bool CUClientSpecTelePreview::OnInput(const IInput::CEvent &Event)
{
	UpdateHover();
	if(!m_HoverActive || m_OutCount <= 0)
		return false;

	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
		{
			if(m_OutIndex + 1 < m_OutCount)
				++m_OutIndex;
			return true;
		}
		if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
		{
			if(m_OutIndex > 0)
				--m_OutIndex;
			return true;
		}
		if(Event.m_Key == KEY_MOUSE_1)
		{
			const std::vector<vec2> &vOuts = Collision()->TeleOuts(m_TeleNumber - 1);
			if(m_OutIndex >= 0 && m_OutIndex < (int)vOuts.size())
			{
				// TeleOuts already store tile centers (+16,+16).
				GameClient()->m_Camera.SetViewWorld(vOuts[m_OutIndex]);
			}
			return true;
		}
	}

	return false;
}

void CUClientSpecTelePreview::OnRender()
{
	UpdateHover();
	if(!m_HoverActive || m_OutCount <= 0)
		return;

	float PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1;
	Graphics()->GetScreen(&PrevScreenX0, &PrevScreenY0, &PrevScreenX1, &PrevScreenY1);

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	const char *pLine1 = Localize("Click to view teleport destination");
	char aLine2[32];
	str_format(aLine2, sizeof(aLine2), "%d/%d", m_OutIndex + 1, m_OutCount);

	const float FontSize1 = 7.0f;
	const float FontSize2 = 6.5f;

	// Slightly upper-right of screen center.
	const float AnchorX = Width * 0.5f + 14.0f;
	const float AnchorY = Height * 0.5f - 28.0f;

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.75f);
	TextRender()->Text(AnchorX, AnchorY, FontSize1, pLine1);
	TextRender()->Text(AnchorX, AnchorY + FontSize1 + 1.5f, FontSize2, aLine2);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

	Graphics()->MapScreen(PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1);
}
