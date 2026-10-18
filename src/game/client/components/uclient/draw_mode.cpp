#include "draw_mode.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
constexpr int64_t SEGMENT_FLUSH_MS = 50;
constexpr int64_t RESEND_MS = 20;
constexpr size_t HISTORY_LIMIT = 100;
constexpr int64_t SUBSCRIBE_RETRY_MS = 1000;
// Catches up on live segments lost over UDP and recreates the relay canvas after it idled out.
constexpr int64_t RESYNC_MS = 30000;
constexpr int CAP_SEGMENTS = 12;

const ColorRGBA s_aPalette[] = {
	ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f),
	ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f),
	ColorRGBA(0.92f, 0.16f, 0.16f, 1.0f),
	ColorRGBA(1.0f, 0.56f, 0.1f, 1.0f),
	ColorRGBA(1.0f, 0.9f, 0.15f, 1.0f),
	ColorRGBA(0.2f, 0.8f, 0.25f, 1.0f),
	ColorRGBA(0.2f, 0.8f, 0.92f, 1.0f),
	ColorRGBA(0.22f, 0.4f, 1.0f, 1.0f),
	ColorRGBA(0.66f, 0.3f, 0.95f, 1.0f),
	ColorRGBA(1.0f, 0.46f, 0.76f, 1.0f),
};

uint32_t PackColor(const ColorRGBA &Color)
{
	auto Channel = [](float Value) { return (uint32_t)std::clamp(round_to_int(Value * 255.0f), 0, 255); };
	return Channel(Color.r) << 24 | Channel(Color.g) << 16 | Channel(Color.b) << 8 | Channel(Color.a);
}

ColorRGBA UnpackColor(uint32_t Value)
{
	return ColorRGBA(((Value >> 24) & 0xff) / 255.0f, ((Value >> 16) & 0xff) / 255.0f, ((Value >> 8) & 0xff) / 255.0f, (Value & 0xff) / 255.0f);
}

float Hash01(uint32_t Seed)
{
	Seed ^= Seed >> 16;
	Seed *= 0x7feb352du;
	Seed ^= Seed >> 15;
	Seed *= 0x846ca68bu;
	Seed ^= Seed >> 16;
	return (Seed & 0xffff) / 65535.0f;
}

float DistanceToSegment(vec2 P, vec2 A, vec2 B)
{
	const vec2 AB = B - A;
	const float LengthSquared = dot(AB, AB);
	if(LengthSquared <= 0.0001f)
		return distance(P, A);
	const float t = std::clamp(dot(P - A, AB) / LengthSquared, 0.0f, 1.0f);
	return distance(P, A + AB * t);
}

void AddQuad(std::vector<IGraphics::CFreeformItem> &vItems, vec2 A, vec2 B, float Radius)
{
	const vec2 Dir = B - A;
	const float Length = length(Dir);
	if(Length < 0.001f)
		return;
	const vec2 Normal = vec2(-Dir.y, Dir.x) / Length * Radius;
	vItems.emplace_back(A.x + Normal.x, A.y + Normal.y, B.x + Normal.x, B.y + Normal.y,
		A.x - Normal.x, A.y - Normal.y, B.x - Normal.x, B.y - Normal.y);
}

void AddCircle(std::vector<IGraphics::CFreeformItem> &vItems, vec2 Center, float Radius)
{
	static vec2 s_aUnit[CAP_SEGMENTS + 1];
	static bool s_UnitInit = false;
	if(!s_UnitInit)
	{
		for(int i = 0; i <= CAP_SEGMENTS; i++)
			s_aUnit[i] = direction(2.0f * pi * i / CAP_SEGMENTS);
		s_UnitInit = true;
	}
	for(int i = 0; i < CAP_SEGMENTS; i += 2)
	{
		const vec2 P1 = Center + s_aUnit[i] * Radius;
		const vec2 P2 = Center + s_aUnit[i + 1] * Radius;
		const vec2 P3 = Center + s_aUnit[i + 2] * Radius;
		vItems.emplace_back(Center.x, Center.y, P1.x, P1.y, P3.x, P3.y, P2.x, P2.y);
	}
}
}

void CUClientDrawMode::OnConsoleInit()
{
	Console()->Register("draw_mode", "", CFGFLAG_CLIENT, ConDrawMode, this, "Toggle UClient drawing mode");
}

void CUClientDrawMode::ConDrawMode(IConsole::IResult *pResult, void *pUserData)
{
	CUClientDrawMode *pSelf = static_cast<CUClientDrawMode *>(pUserData);
	pSelf->SetActive(!pSelf->m_Active);
}

bool CUClientDrawMode::CanEnter() const
{
	return g_Config.m_UcDraw && Client()->State() == IClient::STATE_ONLINE;
}

bool CUClientDrawMode::HideTees() const
{
	return m_Active && g_Config.m_UcDrawHideTees;
}

void CUClientDrawMode::SetActive(bool Active)
{
	if(Active && !CanEnter())
		return;
	if(m_Active == Active)
		return;

	if(Active)
	{
		m_CameraCenter = GameClient()->m_Camera.m_Center;
		// A button already held when entering must not start a stroke.
		m_WasLeftDown = true;
	}
	else
	{
		if(m_Drawing && m_Tool != ETool::ERASER)
			FinishPenGesture();
		else if(m_Drawing)
			FinishEraserGesture();
		Ui()->ClosePopupMenus();
	}
	m_Active = Active;
	m_Drawing = false;
	m_Panning = false;
	m_vPendingPoints.clear();
	m_HasSegmentStart = false;
}

void CUClientDrawMode::OnReset()
{
	if(m_Active)
		Ui()->ClosePopupMenus();
	m_Active = false;
	m_Drawing = false;
	m_Panning = false;
	m_vPendingPoints.clear();
	m_HasSegmentStart = false;
	m_vPendingErase.clear();
	ClearStrokes();
	ResetSync();
}

void CUClientDrawMode::OnRelease()
{
	SetActive(false);
}

void CUClientDrawMode::ClearStrokes()
{
	m_vStrokes.clear();
	m_StrokeKeys.clear();
	m_vUndo.clear();
	m_vRedo.clear();
	m_vResend.clear();
	m_vCurrentGesture.clear();
	m_vErasedGesture.clear();
	m_vPendingPoints.clear();
	m_vPendingErase.clear();
	m_HasSegmentStart = false;
	m_Drawing = false;
	m_CurrentGestureId = 0;
}

void CUClientDrawMode::ResetSync()
{
	m_HasMap = false;
	m_SnapshotAfterSeq = 0;
	m_SnapshotDone = false;
	m_LastSubscribe = 0;
}

void CUClientDrawMode::SendSubscribe()
{
	std::vector<uint8_t> vPayload;
	UClientPresence::WriteDrawSubscribe(vPayload, m_SnapshotAfterSeq);
	GameClient()->m_ClientIndicator.SendUcDrawPacket(UClientPresence::DRAW_OP_SUBSCRIBE, m_MapSha256, vPayload);
	m_LastSubscribe = time_get();
}

void CUClientDrawMode::OnUpdate()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		SetActive(false);
		return;
	}
	if(m_Active && (!g_Config.m_UcDraw || GameClient()->m_Menus.IsActive()))
		SetActive(false);

	if(!m_HasMap)
	{
		m_MapSha256 = GameClient()->Map()->Sha256();
		m_HasMap = true;
	}

	const int64_t Now = time_get();
	const int64_t Wait = (m_SnapshotDone ? RESYNC_MS : SUBSCRIBE_RETRY_MS) * time_freq() / 1000;
	if(m_LastSubscribe == 0 || Now - m_LastSubscribe >= Wait)
	{
		m_SnapshotDone = false;
		SendSubscribe();
	}

	if(!m_vPendingErase.empty() && Now - m_LastEraseFlush >= SEGMENT_FLUSH_MS * time_freq() / 1000)
		FlushErase();

	if(!m_vResend.empty() && m_HasMap && (m_LastResend == 0 || Now - m_LastResend >= RESEND_MS * time_freq() / 1000))
	{
		SendSegment(m_vResend.front());
		m_vResend.erase(m_vResend.begin());
		m_LastResend = Now;
	}
}

void CUClientDrawMode::OnDrawBroadcast(const UClientPresence::CDrawBroadcast &Draw)
{
	if(Client()->State() != IClient::STATE_ONLINE || !m_HasMap)
		return;

	if(Draw.m_Op == UClientPresence::DRAW_OP_RESET)
	{
		// The relay canvas switched to our map and dropped what it held, so start over from it.
		if(Draw.m_MapSha256 == m_MapSha256)
		{
			ClearStrokes();
			ResetSync();
		}
		return;
	}
	if(!(Draw.m_MapSha256 == m_MapSha256))
		return;

	switch(Draw.m_Op)
	{
	case UClientPresence::DRAW_OP_SEGMENTS:
	case UClientPresence::DRAW_OP_SNAPSHOT:
		for(const UClientPresence::CDrawSegment &Segment : Draw.m_vSegments)
		{
			SStroke Stroke;
			Stroke.m_Owner = Segment.m_Owner;
			Stroke.m_Id = Segment.m_Id;
			Stroke.m_Tool = Segment.m_Tool == UClientPresence::DRAW_TOOL_CRAYON ? UClientPresence::DRAW_TOOL_CRAYON : UClientPresence::DRAW_TOOL_PEN;
			Stroke.m_Color = UnpackColor(Segment.m_Color);
			Stroke.m_Width = std::clamp((int)Segment.m_Width, UClientPresence::DRAW_MIN_WIDTH, UClientPresence::DRAW_MAX_WIDTH);
			Stroke.m_vPoints.reserve(Segment.m_vPoints.size());
			for(const UClientPresence::CDrawPoint &Point : Segment.m_vPoints)
				Stroke.m_vPoints.emplace_back((float)Point.m_X, (float)Point.m_Y);
			AddStroke(std::move(Stroke));
		}
		if(Draw.m_Op == UClientPresence::DRAW_OP_SNAPSHOT)
		{
			m_SnapshotAfterSeq = Draw.m_LastSeq;
			m_SnapshotDone = Draw.m_Done;
			if(!m_SnapshotDone)
				SendSubscribe();
		}
		break;
	case UClientPresence::DRAW_OP_ERASE:
		RemoveStrokes(Draw.m_Owner, &Draw.m_vIds);
		break;
	case UClientPresence::DRAW_OP_CLEAR_OWNER:
		RemoveStrokes(Draw.m_Owner, nullptr);
		break;
	default:
		break;
	}
}

void CUClientDrawMode::AddStroke(SStroke &&Stroke)
{
	if(Stroke.m_vPoints.empty() || !m_StrokeKeys.emplace(Stroke.m_Owner, Stroke.m_Id).second)
		return;
	const float Radius = Stroke.m_Width * 0.5f;
	Stroke.m_Min = Stroke.m_Max = Stroke.m_vPoints[0];
	for(const vec2 &Point : Stroke.m_vPoints)
	{
		Stroke.m_Min = vec2(minimum(Stroke.m_Min.x, Point.x), minimum(Stroke.m_Min.y, Point.y));
		Stroke.m_Max = vec2(maximum(Stroke.m_Max.x, Point.x), maximum(Stroke.m_Max.y, Point.y));
	}
	Stroke.m_Min -= vec2(Radius, Radius);
	Stroke.m_Max += vec2(Radius, Radius);
	m_vStrokes.push_back(std::move(Stroke));
}

void CUClientDrawMode::RemoveStrokes(const CUuid &Owner, const std::vector<uint32_t> *pIds)
{
	auto Matches = [&](const SStroke &Stroke) {
		if(!(Stroke.m_Owner == Owner))
			return false;
		return !pIds || std::find(pIds->begin(), pIds->end(), Stroke.m_Id) != pIds->end();
	};
	for(const SStroke &Stroke : m_vStrokes)
	{
		if(Matches(Stroke))
			m_StrokeKeys.erase(std::make_pair(Stroke.m_Owner, Stroke.m_Id));
	}
	m_vStrokes.erase(std::remove_if(m_vStrokes.begin(), m_vStrokes.end(), Matches), m_vStrokes.end());
}

void CUClientDrawMode::WorldScreen(float *pPoints) const
{
	const vec2 Center = GameClient()->m_Camera.m_Center;
	Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, pPoints);
}

vec2 CUClientDrawMode::MouseWorldPos() const
{
	float aPoints[4];
	WorldScreen(aPoints);
	const CUIRect *pScreen = Ui()->Screen();
	const vec2 Mouse = Ui()->MousePos();
	return vec2(aPoints[0] + Mouse.x / pScreen->w * (aPoints[2] - aPoints[0]),
		aPoints[1] + Mouse.y / pScreen->h * (aPoints[3] - aPoints[1]));
}

void CUClientDrawMode::AddPoint(vec2 Pos)
{
	Pos = vec2(std::round(Pos.x), std::round(Pos.y));
	if(!m_vPendingPoints.empty() || m_HasSegmentStart)
	{
		const vec2 Last = m_vPendingPoints.empty() ? m_SegmentStart : m_vPendingPoints.back();
		if(distance(Last, Pos) < maximum(1.0f, m_Width * 0.2f))
			return;
	}
	m_vPendingPoints.push_back(Pos);
}

void CUClientDrawMode::SendSegment(const SStroke &Stroke)
{
	UClientPresence::CDrawSegment Segment;
	Segment.m_Id = Stroke.m_Id;
	Segment.m_Tool = Stroke.m_Tool;
	Segment.m_Color = PackColor(Stroke.m_Color);
	Segment.m_Width = (uint16_t)std::clamp(round_to_int(Stroke.m_Width), UClientPresence::DRAW_MIN_WIDTH, UClientPresence::DRAW_MAX_WIDTH);
	Segment.m_vPoints.reserve(Stroke.m_vPoints.size());
	for(const vec2 &Point : Stroke.m_vPoints)
		Segment.m_vPoints.push_back({(int32_t)Point.x, (int32_t)Point.y});
	std::vector<uint8_t> vPayload;
	UClientPresence::WriteDrawSegment(vPayload, Segment, false);
	GameClient()->m_ClientIndicator.SendUcDrawPacket(UClientPresence::DRAW_OP_SEGMENTS, m_MapSha256, vPayload);
}

void CUClientDrawMode::SendEraseIds(const std::vector<uint32_t> &vIds)
{
	for(size_t Start = 0; Start < vIds.size(); Start += UClientPresence::DRAW_MAX_ERASE_IDS)
	{
		const size_t End = minimum(vIds.size(), Start + (size_t)UClientPresence::DRAW_MAX_ERASE_IDS);
		const std::vector<uint32_t> vChunk(vIds.begin() + Start, vIds.begin() + End);
		std::vector<uint8_t> vPayload;
		UClientPresence::WriteDrawErase(vPayload, vChunk);
		GameClient()->m_ClientIndicator.SendUcDrawPacket(UClientPresence::DRAW_OP_ERASE, m_MapSha256, vPayload);
	}
}

void CUClientDrawMode::FlushSegment(bool Final)
{
	std::vector<vec2> vPoints;
	if(m_HasSegmentStart)
		vPoints.push_back(m_SegmentStart);
	vPoints.insert(vPoints.end(), m_vPendingPoints.begin(), m_vPendingPoints.end());
	// A lone point is only sent as a dot when the stroke ends without moving.
	if(m_vPendingPoints.empty() || (vPoints.size() < 2 && !Final))
		return;

	SStroke Stroke;
	Stroke.m_Owner = GameClient()->m_ClientIndicator.ClientInstanceId();
	Stroke.m_Id = m_NextStrokeId++;
	Stroke.m_GestureId = m_CurrentGestureId;
	Stroke.m_Tool = m_Tool == ETool::CRAYON ? UClientPresence::DRAW_TOOL_CRAYON : UClientPresence::DRAW_TOOL_PEN;
	Stroke.m_Color = s_aPalette[m_ColorIndex];
	Stroke.m_Width = m_Width;
	Stroke.m_vPoints = vPoints;

	SendSegment(Stroke);
	m_vCurrentGesture.push_back(Stroke);
	AddStroke(std::move(Stroke));
	m_HasSegmentStart = true;
	m_SegmentStart = vPoints.back();
	m_vPendingPoints.clear();
	m_LastSegmentFlush = time_get();
}

void CUClientDrawMode::EraseAt(vec2 Pos)
{
	const CUuid &Own = GameClient()->m_ClientIndicator.ClientInstanceId();
	const float Radius = (float)m_Width;
	std::vector<uint32_t> vGestureIds;
	std::vector<uint32_t> vLoneIds;
	for(const SStroke &Stroke : m_vStrokes)
	{
		if(!(Stroke.m_Owner == Own))
			continue;
		if(Pos.x < Stroke.m_Min.x - Radius || Pos.x > Stroke.m_Max.x + Radius || Pos.y < Stroke.m_Min.y - Radius || Pos.y > Stroke.m_Max.y + Radius)
			continue;
		const float HitDistance = Radius + Stroke.m_Width * 0.5f;
		bool Hit = Stroke.m_vPoints.size() == 1 && distance(Pos, Stroke.m_vPoints[0]) <= HitDistance;
		for(size_t i = 1; i < Stroke.m_vPoints.size() && !Hit; i++)
			Hit = DistanceToSegment(Pos, Stroke.m_vPoints[i - 1], Stroke.m_vPoints[i]) <= HitDistance;
		if(!Hit)
			continue;
		if(Stroke.m_GestureId != 0)
		{
			if(std::find(vGestureIds.begin(), vGestureIds.end(), Stroke.m_GestureId) == vGestureIds.end())
				vGestureIds.push_back(Stroke.m_GestureId);
		}
		else
		{
			vLoneIds.push_back(Stroke.m_Id);
		}
	}
	if(vGestureIds.empty() && vLoneIds.empty())
		return;

	std::vector<SStroke> vRemoved;
	std::vector<uint32_t> vIds;
	for(const SStroke &Stroke : m_vStrokes)
	{
		if(!(Stroke.m_Owner == Own))
			continue;
		const bool Take = (Stroke.m_GestureId != 0 && std::find(vGestureIds.begin(), vGestureIds.end(), Stroke.m_GestureId) != vGestureIds.end()) ||
				  std::find(vLoneIds.begin(), vLoneIds.end(), Stroke.m_Id) != vLoneIds.end();
		if(!Take)
			continue;
		vRemoved.push_back(Stroke);
		vIds.push_back(Stroke.m_Id);
	}
	if(vIds.empty())
		return;
	m_vErasedGesture.insert(m_vErasedGesture.end(), vRemoved.begin(), vRemoved.end());
	RemoveStrokes(Own, &vIds);
	m_vPendingErase.insert(m_vPendingErase.end(), vIds.begin(), vIds.end());
	if(m_vPendingErase.size() >= (size_t)UClientPresence::DRAW_MAX_ERASE_IDS)
		FlushErase();
}

void CUClientDrawMode::FlushErase()
{
	m_LastEraseFlush = time_get();
	for(size_t Start = 0; Start < m_vPendingErase.size(); Start += UClientPresence::DRAW_MAX_ERASE_IDS)
	{
		const size_t End = minimum(m_vPendingErase.size(), Start + (size_t)UClientPresence::DRAW_MAX_ERASE_IDS);
		const std::vector<uint32_t> vIds(m_vPendingErase.begin() + Start, m_vPendingErase.begin() + End);
		std::vector<uint8_t> vPayload;
		UClientPresence::WriteDrawErase(vPayload, vIds);
		GameClient()->m_ClientIndicator.SendUcDrawPacket(UClientPresence::DRAW_OP_ERASE, m_MapSha256, vPayload);
	}
	m_vPendingErase.clear();
}

void CUClientDrawMode::PushHistory(EHistoryKind Kind, std::vector<SStroke> vStrokes)
{
	if(vStrokes.empty())
		return;
	m_vRedo.clear();
	m_vUndo.push_back({Kind, std::move(vStrokes)});
	if(m_vUndo.size() > HISTORY_LIMIT)
		m_vUndo.erase(m_vUndo.begin());
}

void CUClientDrawMode::DropStrokes(const std::vector<SStroke> &vStrokes)
{
	if(vStrokes.empty())
		return;
	const CUuid Owner = vStrokes.front().m_Owner;
	std::vector<uint32_t> vIds;
	vIds.reserve(vStrokes.size());
	for(const SStroke &Stroke : vStrokes)
		vIds.push_back(Stroke.m_Id);
	m_vResend.erase(std::remove_if(m_vResend.begin(), m_vResend.end(), [&](const SStroke &Stroke) {
		return Stroke.m_Owner == Owner && std::find(vIds.begin(), vIds.end(), Stroke.m_Id) != vIds.end();
	}),
		m_vResend.end());
	RemoveStrokes(Owner, &vIds);
	SendEraseIds(vIds);
}

void CUClientDrawMode::RestoreStrokes(const std::vector<SStroke> &vStrokes)
{
	for(const SStroke &Stroke : vStrokes)
	{
		if(m_StrokeKeys.find(std::make_pair(Stroke.m_Owner, Stroke.m_Id)) != m_StrokeKeys.end())
			continue;
		m_vResend.push_back(Stroke);
		AddStroke(SStroke(Stroke));
	}
}

void CUClientDrawMode::FinishPenGesture()
{
	FlushSegment(true);
	if(!m_vCurrentGesture.empty())
		PushHistory(EHistoryKind::ADD, std::move(m_vCurrentGesture));
	m_vCurrentGesture.clear();
	m_vPendingPoints.clear();
	m_HasSegmentStart = false;
	m_Drawing = false;
	m_CurrentGestureId = 0;
}

void CUClientDrawMode::FinishEraserGesture()
{
	FlushErase();
	if(!m_vErasedGesture.empty())
		PushHistory(EHistoryKind::REMOVE, std::move(m_vErasedGesture));
	m_vErasedGesture.clear();
	m_Drawing = false;
}

void CUClientDrawMode::CancelPenGesture()
{
	m_vPendingPoints.clear();
	m_HasSegmentStart = false;
	m_Drawing = false;
	m_CurrentGestureId = 0;
	if(!m_vCurrentGesture.empty())
		DropStrokes(m_vCurrentGesture);
	m_vCurrentGesture.clear();
}

void CUClientDrawMode::Undo()
{
	if(m_Drawing && m_Tool != ETool::ERASER)
	{
		CancelPenGesture();
		return;
	}
	if(m_Drawing)
	{
		m_vPendingErase.clear();
		RestoreStrokes(m_vErasedGesture);
		m_vErasedGesture.clear();
		m_Drawing = false;
		return;
	}
	if(m_vUndo.empty())
		return;
	SHistoryEntry Entry = std::move(m_vUndo.back());
	m_vUndo.pop_back();
	if(Entry.m_Kind == EHistoryKind::ADD)
		DropStrokes(Entry.m_vStrokes);
	else
		RestoreStrokes(Entry.m_vStrokes);
	m_vRedo.push_back(std::move(Entry));
}

void CUClientDrawMode::Redo()
{
	if(m_Drawing || m_vRedo.empty())
		return;
	SHistoryEntry Entry = std::move(m_vRedo.back());
	m_vRedo.pop_back();
	if(Entry.m_Kind == EHistoryKind::ADD)
		RestoreStrokes(Entry.m_vStrokes);
	else
		DropStrokes(Entry.m_vStrokes);
	m_vUndo.push_back(std::move(Entry));
}

void CUClientDrawMode::ClearOwnStrokes()
{
	m_vPendingPoints.clear();
	m_HasSegmentStart = false;
	m_Drawing = false;
	m_CurrentGestureId = 0;
	m_vCurrentGesture.clear();
	m_vErasedGesture.clear();
	m_vPendingErase.clear();

	const CUuid Own = GameClient()->m_ClientIndicator.ClientInstanceId();
	std::vector<SStroke> vOwn;
	for(const SStroke &Stroke : m_vStrokes)
	{
		if(Stroke.m_Owner == Own)
			vOwn.push_back(Stroke);
	}
	m_vResend.erase(std::remove_if(m_vResend.begin(), m_vResend.end(), [&](const SStroke &Stroke) {
		return Stroke.m_Owner == Own;
	}),
		m_vResend.end());
	if(!vOwn.empty())
		PushHistory(EHistoryKind::REMOVE, std::move(vOwn));
	RemoveStrokes(Own, nullptr);
	GameClient()->m_ClientIndicator.SendUcDrawPacket(UClientPresence::DRAW_OP_CLEAR_OWNER, m_MapSha256, {});
}

void CUClientDrawMode::UpdateDrawing()
{
	const vec2 Mouse = Ui()->MousePos();
	if(Ui()->MouseButton(2))
	{
		if(m_Panning)
		{
			float aPoints[4];
			WorldScreen(aPoints);
			const CUIRect *pScreen = Ui()->Screen();
			const vec2 Delta = Mouse - m_LastPanMouse;
			m_CameraCenter -= vec2(Delta.x * (aPoints[2] - aPoints[0]) / pScreen->w, Delta.y * (aPoints[3] - aPoints[1]) / pScreen->h);
			m_CameraCenter.x = std::clamp(m_CameraCenter.x, 0.0f, Collision()->GetWidth() * 32.0f);
			m_CameraCenter.y = std::clamp(m_CameraCenter.y, 0.0f, Collision()->GetHeight() * 32.0f);
		}
		m_Panning = true;
		m_LastPanMouse = Mouse;
	}
	else
	{
		m_Panning = false;
	}

	const bool Left = Ui()->MouseButton(0);
	const bool LeftClicked = Left && !m_WasLeftDown;
	m_WasLeftDown = Left;
	const vec2 World = MouseWorldPos();
	if(!m_Drawing)
	{
		if(!LeftClicked || m_PointerOverUi)
			return;
		m_Drawing = true;
		m_HasSegmentStart = false;
		m_vPendingPoints.clear();
		m_LastSegmentFlush = time_get();
		if(m_Tool == ETool::ERASER)
			m_vErasedGesture.clear();
		else
		{
			m_CurrentGestureId = m_NextGestureId++;
			m_vCurrentGesture.clear();
		}
	}

	if(m_Tool == ETool::ERASER)
	{
		if(!m_vCurrentGesture.empty() || !m_vPendingPoints.empty())
			FinishPenGesture();
		else
		{
			EraseAt(World);
			if(!Left)
				FinishEraserGesture();
		}
		return;
	}

	AddPoint(World);
	if(!Left)
	{
		FinishPenGesture();
	}
	else if((int)m_vPendingPoints.size() >= UClientPresence::DRAW_MAX_POINTS_PER_SEGMENT - 1 ||
		time_get() - m_LastSegmentFlush >= SEGMENT_FLUSH_MS * time_freq() / 1000)
	{
		FlushSegment(false);
	}
}

void CUClientDrawMode::RenderStroke(const SStroke &Stroke, std::vector<IGraphics::CFreeformItem> &vItems)
{
	const float Radius = Stroke.m_Width * 0.5f;
	const std::vector<vec2> &vPoints = Stroke.m_vPoints;

	auto Draw = [&](ColorRGBA Color) {
		if(vItems.empty())
			return;
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Color);
		Graphics()->QuadsDrawFreeform(vItems.data(), (int)vItems.size());
		Graphics()->QuadsEnd();
		vItems.clear();
	};

	vItems.clear();
	if(Stroke.m_Tool == UClientPresence::DRAW_TOOL_PEN)
	{
		for(size_t i = 1; i < vPoints.size(); i++)
			AddQuad(vItems, vPoints[i - 1], vPoints[i], Radius);
		for(const vec2 &Point : vPoints)
			AddCircle(vItems, Point, Radius);
		Draw(Stroke.m_Color);
		return;
	}

	// Crayon: a faint body plus thin strands that wobble the same way on every client.
	ColorRGBA Body = Stroke.m_Color;
	Body.a *= 0.45f;
	for(size_t i = 1; i < vPoints.size(); i++)
		AddQuad(vItems, vPoints[i - 1], vPoints[i], Radius * 0.8f);
	if(vPoints.size() == 1)
		AddCircle(vItems, vPoints[0], Radius * 0.8f);
	Draw(Body);

	for(int Strand = 0; Strand < 3; Strand++)
	{
		for(size_t i = 1; i < vPoints.size(); i++)
		{
			const vec2 Dir = vPoints[i] - vPoints[i - 1];
			const float Length = length(Dir);
			if(Length < 0.001f)
				continue;
			const vec2 Normal = vec2(-Dir.y, Dir.x) / Length;
			const uint32_t Seed = Stroke.m_Id * 7919u + (uint32_t)i * 131u + (uint32_t)Strand * 31u;
			const vec2 OffsetA = Normal * Radius * (Hash01(Seed) - 0.5f) * 1.4f;
			const vec2 OffsetB = Normal * Radius * (Hash01(Seed + 1) - 0.5f) * 1.4f;
			AddQuad(vItems, vPoints[i - 1] + OffsetA, vPoints[i] + OffsetB, maximum(0.6f, Radius * 0.18f));
		}
	}
	ColorRGBA Strands = Stroke.m_Color;
	Strands.a *= 0.75f;
	Draw(Strands);
}

void CUClientDrawMode::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE || !g_Config.m_UcDraw || (m_vStrokes.empty() && m_vPendingPoints.empty()))
		return;

	float aPoints[4];
	WorldScreen(aPoints);
	float PrevX0, PrevY0, PrevX1, PrevY1;
	Graphics()->GetScreen(&PrevX0, &PrevY0, &PrevX1, &PrevY1);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);
	Graphics()->ClipDisable();
	Graphics()->BlendNormal();
	Graphics()->TextureClear();

	std::vector<IGraphics::CFreeformItem> vItems;
	for(const SStroke &Stroke : m_vStrokes)
	{
		if(Stroke.m_Max.x < aPoints[0] || Stroke.m_Min.x > aPoints[2] || Stroke.m_Max.y < aPoints[1] || Stroke.m_Min.y > aPoints[3])
			continue;
		RenderStroke(Stroke, vItems);
	}
	if(m_Drawing && m_Tool != ETool::ERASER && !m_vPendingPoints.empty())
	{
		SStroke Preview;
		Preview.m_Tool = m_Tool == ETool::CRAYON ? UClientPresence::DRAW_TOOL_CRAYON : UClientPresence::DRAW_TOOL_PEN;
		Preview.m_Color = s_aPalette[m_ColorIndex];
		Preview.m_Width = (float)m_Width;
		if(m_HasSegmentStart)
			Preview.m_vPoints.push_back(m_SegmentStart);
		Preview.m_vPoints.insert(Preview.m_vPoints.end(), m_vPendingPoints.begin(), m_vPendingPoints.end());
		RenderStroke(Preview, vItems);
	}

	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->MapScreen(PrevX0, PrevY0, PrevX1, PrevY1);
}

bool CUClientDrawMode::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;
	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

bool CUClientDrawMode::OnInput(const IInput::CEvent &Event)
{
	if(!m_Active)
		return false;

	Ui()->OnInput(Event);
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_Z && Input()->ModifierIsPressed())
	{
		if(Input()->ShiftIsPressed())
			Redo();
		else
			Undo();
		return true;
	}
	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
	{
		if(Ui()->IsPopupOpen())
			Ui()->ClosePopupMenus();
		else
			SetActive(false);
		return true;
	}
	// Keyboard keys fall through so chat, binds and this mode's own toggle key keep working.
	// The screen-size bind is also kept when it is on the wheel, which this mode otherwise takes.
	const bool MouseButton = Event.m_Key == KEY_MOUSE_1 || Event.m_Key == KEY_MOUSE_2 || Event.m_Key == KEY_MOUSE_3 ||
				 Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN;
	if(!MouseButton)
		return false;
	const int Modifier = CBinds::GetModifierMask(Input()) & ~CBinds::GetModifierMaskOfKey(Event.m_Key);
	const char *pBind = GameClient()->m_Binds.Get(Event.m_Key, Modifier);
	if(pBind && (str_startswith(pBind, "zoom+") || str_startswith(pBind, "zoom-") ||
			    (str_startswith(pBind, "zoom") && (pBind[4] == '\0' || pBind[4] == ' '))))
		return false;
	return true;
}

CUi::EPopupMenuFunctionResult CUClientDrawMode::PopupSettings(void *pContext, CUIRect View, bool Active)
{
	CUClientDrawMode *pSelf = static_cast<CUClientDrawMode *>(pContext);
	CUIRect Row;
	View.HSplitTop(20.0f, &Row, &View);
	if(pSelf->GameClient()->m_Menus.DoButton_CheckBox(&pSelf->m_HideTeesCheckbox, Localize("Hide tees"), g_Config.m_UcDrawHideTees, &Row))
		g_Config.m_UcDrawHideTees ^= 1;
	return CUi::POPUP_KEEP_OPEN;
}

void CUClientDrawMode::RenderToolbar()
{
	if(!m_Active || Client()->State() != IClient::STATE_ONLINE)
		return;

	GameClient()->m_TimeoutReconnect.RenderHud();

	Ui()->StartCheck();
	Ui()->Update();
	const CUIRect Screen = *Ui()->Screen();
	Ui()->MapScreen();

	const float Margin = 10.0f;
	const float BottomMargin = GameClient()->m_TimeoutReconnect.ShouldShowHud() ? 30.0f : Margin;
	CUIRect Toolbar = {Margin, Screen.h - BottomMargin - 62.0f, 470.0f, 62.0f};
	Toolbar.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 6.0f);
	m_PointerOverUi = Ui()->MouseInside(&Toolbar) || Ui()->IsPopupOpen();

	CUIRect Content, ToolRow, StyleRow;
	Toolbar.Margin(5.0f, &Content);
	Content.HSplitMid(&ToolRow, &StyleRow, 4.0f);

	bool Exit = false;
	auto ToolButton = [&](CButtonContainer *pButton, const char *pLabel, ETool Tool) {
		CUIRect Button;
		ToolRow.VSplitLeft(64.0f, &Button, &ToolRow);
		ToolRow.VSplitLeft(3.0f, nullptr, &ToolRow);
		if(GameClient()->m_Menus.DoButton_Menu(pButton, pLabel, m_Tool == Tool, &Button))
			m_Tool = Tool;
	};
	ToolButton(&m_PenButton, Localize("Pen"), ETool::PEN);
	ToolButton(&m_CrayonButton, Localize("Crayon"), ETool::CRAYON);
	ToolButton(&m_EraserButton, Localize("Eraser"), ETool::ERASER);

	CUIRect ExitButton, SettingsButton, ClearButton, RedoButton, UndoButton;
	ToolRow.VSplitRight(ToolRow.h, &ToolRow, &ExitButton);
	ToolRow.VSplitRight(3.0f, &ToolRow, nullptr);
	ToolRow.VSplitRight(ToolRow.h, &ToolRow, &SettingsButton);
	ToolRow.VSplitRight(3.0f, &ToolRow, nullptr);
	ToolRow.VSplitRight(ToolRow.h, &ToolRow, &ClearButton);
	ToolRow.VSplitRight(3.0f, &ToolRow, nullptr);
	ToolRow.VSplitRight(ToolRow.h, &ToolRow, &RedoButton);
	ToolRow.VSplitRight(3.0f, &ToolRow, nullptr);
	ToolRow.VSplitRight(ToolRow.h, &ToolRow, &UndoButton);
	if(Ui()->DoButton_FontIcon(&m_UndoButton, FontIcon::CHEVRON_LEFT, 0, &UndoButton, BUTTONFLAG_LEFT))
		Undo();
	if(Ui()->DoButton_FontIcon(&m_RedoButton, FontIcon::CHEVRON_RIGHT, 0, &RedoButton, BUTTONFLAG_LEFT))
		Redo();
	if(Ui()->DoButton_FontIcon(&m_ClearButton, FontIcon::TRASH, 0, &ClearButton, BUTTONFLAG_LEFT))
	{
		m_ClearConfirm.Reset();
		m_ClearConfirm.YesNoButtons();
		str_copy(m_ClearConfirm.m_aMessage, Localize("Erase all of your drawings?"));
		Ui()->ShowPopupConfirm(ClearButton.x, ClearButton.y + ClearButton.h, &m_ClearConfirm);
	}
	if(Ui()->DoButton_FontIcon(&m_SettingsButton, FontIcon::GEAR, 0, &SettingsButton, BUTTONFLAG_LEFT))
		Ui()->DoPopupMenu(&m_SettingsPopupId, SettingsButton.x, SettingsButton.y + SettingsButton.h, 140.0f, 30.0f, this, PopupSettings);
	if(Ui()->DoButton_FontIcon(&m_ExitButton, FontIcon::XMARK, 0, &ExitButton, BUTTONFLAG_LEFT))
		Exit = true;

	CUIRect WidthRect;
	StyleRow.VSplitLeft(150.0f, &WidthRect, &StyleRow);
	StyleRow.VSplitLeft(8.0f, nullptr, &StyleRow);
	Ui()->DoScrollbarOption(&m_WidthScrollbar, &m_Width, &WidthRect, Localize("Width"), UClientPresence::DRAW_MIN_WIDTH, UClientPresence::DRAW_MAX_WIDTH);

	for(int i = 0; i < (int)std::size(s_aPalette); i++)
	{
		CUIRect Swatch;
		StyleRow.VSplitLeft(20.0f, &Swatch, &StyleRow);
		StyleRow.VSplitLeft(3.0f, nullptr, &StyleRow);
		if(i == m_ColorIndex)
			Swatch.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f), IGraphics::CORNER_ALL, 4.0f);
		CUIRect Inner;
		Swatch.Margin(i == m_ColorIndex ? 2.0f : 1.0f, &Inner);
		Inner.Draw(s_aPalette[i], IGraphics::CORNER_ALL, 3.0f);
		if(Ui()->DoButtonLogic(&m_aColorButtons[i], 0, &Swatch, BUTTONFLAG_LEFT))
		{
			m_ColorIndex = i;
			if(m_Tool == ETool::ERASER)
				m_Tool = ETool::PEN;
		}
	}

	Ui()->RenderPopupMenus();
	if(m_ClearConfirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		ClearOwnStrokes();
	if(m_ClearConfirm.m_Result != CUi::SConfirmPopupContext::UNSET)
		m_ClearConfirm.Reset();

	UpdateDrawing();

	if(m_PointerOverUi)
	{
		RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
	}
	else
	{
		float aPoints[4];
		WorldScreen(aPoints);
		const float UiPerWorld = Screen.w / (aPoints[2] - aPoints[0]);
		const bool Eraser = m_Tool == ETool::ERASER;
		const float Radius = maximum(2.0f, (Eraser ? m_Width : m_Width * 0.5f) * UiPerWorld);
		ColorRGBA Color = Eraser ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f) : s_aPalette[m_ColorIndex].WithAlpha(0.8f);
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f));
		Graphics()->DrawCircle(Ui()->MousePos().x, Ui()->MousePos().y, Radius + 1.0f, 24);
		Graphics()->SetColor(Color);
		Graphics()->DrawCircle(Ui()->MousePos().x, Ui()->MousePos().y, Radius, 24);
		Graphics()->QuadsEnd();
	}

	Ui()->FinishCheck();
	Ui()->ClearHotkeys();

	if(Exit)
		SetActive(false);
}
