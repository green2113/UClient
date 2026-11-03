#include "draw_mode.h"

#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/client/ui_scrollregion.h>
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
constexpr const char *DRAWINGS_DIR = "uclient_drawings";

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

bool PointInPolygon(vec2 P, const std::vector<vec2> &vPoly)
{
	const size_t Count = vPoly.size();
	if(Count < 3)
		return false;
	bool Inside = false;
	for(size_t i = 0, j = Count - 1; i < Count; j = i++)
	{
		const vec2 &A = vPoly[j];
		const vec2 &B = vPoly[i];
		if((A.y > P.y) != (B.y > P.y) && P.x < (B.x - A.x) * (P.y - A.y) / (B.y - A.y) + A.x)
			Inside = !Inside;
	}
	return Inside;
}

bool SegmentsCross(vec2 A, vec2 B, vec2 C, vec2 D)
{
	const vec2 R = B - A;
	const vec2 S = D - C;
	const float Den = R.x * S.y - R.y * S.x;
	if(std::fabs(Den) < 0.0001f)
		return false;
	const vec2 Delta = C - A;
	const float T = (Delta.x * S.y - Delta.y * S.x) / Den;
	const float U = (Delta.x * R.y - Delta.y * R.x) / Den;
	return T >= 0.0f && T <= 1.0f && U >= 0.0f && U <= 1.0f;
}

bool StrokeHitsLasso(const std::vector<vec2> &vPoints, vec2 StrokeMin, vec2 StrokeMax, const std::vector<vec2> &vPoly, vec2 PolyMin, vec2 PolyMax)
{
	if(StrokeMax.x < PolyMin.x || StrokeMin.x > PolyMax.x || StrokeMax.y < PolyMin.y || StrokeMin.y > PolyMax.y)
		return false;
	for(const vec2 &Point : vPoints)
	{
		if(PointInPolygon(Point, vPoly))
			return true;
	}
	if(vPoints.size() < 2)
		return false;
	for(size_t i = 1; i < vPoints.size(); i++)
	{
		for(size_t j = 0; j < vPoly.size(); j++)
		{
			if(SegmentsCross(vPoints[i - 1], vPoints[i], vPoly[j], vPoly[(j + 1) % vPoly.size()]))
				return true;
		}
	}
	return false;
}

bool DrawingFileName(const char *pName, char *pOut, int OutSize)
{
	if(!pName || OutSize < 2)
		return false;
	str_copy(pOut, pName, OutSize);
	str_utf8_trim_right(pOut);
	const char *pStart = str_utf8_skip_whitespaces(pOut);
	if(pStart != pOut)
	{
		char aMoved[128];
		str_copy(aMoved, pStart);
		str_copy(pOut, aMoved, OutSize);
	}
	for(char *pChar = pOut; *pChar; pChar++)
	{
		const unsigned char C = (unsigned char)*pChar;
		if(C < 32 || C == 127 || C == '\\' || C == '/' || C == '|' || C == ':' || C == '*' || C == '?' || C == '<' || C == '>' || C == '"')
			*pChar = '_';
	}
	str_utf8_trim_right(pOut);
	return pOut[0] != '\0' && str_comp(pOut, ".") != 0 && str_comp(pOut, "..") != 0 && str_valid_filename(pOut);
}

int ListDrawingFile(const char *pName, int IsDir, int DirType, void *pUser)
{
	(void)DirType;
	if(IsDir || !pName || !str_endswith(pName, ".json"))
		return 0;
	const char *pBase = pName;
	if(const char *pSlash = str_rchr(pName, '/'))
		pBase = pSlash + 1;
	std::string Name = pBase;
	if(Name.size() <= 5)
		return 0;
	Name.resize(Name.size() - 5);
	if(!Name.empty())
		static_cast<std::vector<std::string> *>(pUser)->push_back(std::move(Name));
	return 0;
}
}

ColorRGBA CUClientDrawMode::CurrentColor() const
{
	if(m_UseCustomColor)
		return color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UcDrawCustomColor, false));
	return s_aPalette[m_ColorIndex];
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
		m_Eyedropper = false;
		m_EyedropperHasSample = false;
		ClearLasso();
		m_SaveNameInput.Deactivate();
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
	m_Eyedropper = false;
	m_EyedropperHasSample = false;
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
	ClearLasso();
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
	Stroke.m_Color = CurrentColor();
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

void CUClientDrawMode::PushHistory(EHistoryKind Kind, std::vector<SStroke> vStrokes, std::vector<SStroke> vBefore)
{
	if(vStrokes.empty() && vBefore.empty())
		return;
	m_vRedo.clear();
	SHistoryEntry Entry;
	Entry.m_Kind = Kind;
	Entry.m_vStrokes = std::move(vStrokes);
	Entry.m_vBefore = std::move(vBefore);
	m_vUndo.push_back(std::move(Entry));
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
	if(m_Placing)
	{
		m_Placing = false;
		m_vPlace.clear();
		return;
	}
	if(m_Moving)
	{
		m_Moving = false;
		m_MoveDelta = vec2(0.0f, 0.0f);
		return;
	}
	if(m_LassoDrawing)
	{
		m_LassoDrawing = false;
		m_vLasso.clear();
		return;
	}
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
	else if(Entry.m_Kind == EHistoryKind::MOVE)
	{
		DropStrokes(Entry.m_vStrokes);
		RestoreStrokes(Entry.m_vBefore);
	}
	else
		RestoreStrokes(Entry.m_vStrokes);
	m_vSelectedIds.clear();
	m_vRedo.push_back(std::move(Entry));
}

void CUClientDrawMode::Redo()
{
	if(m_Drawing || m_LassoDrawing || m_Moving || m_Placing || m_vRedo.empty())
		return;
	SHistoryEntry Entry = std::move(m_vRedo.back());
	m_vRedo.pop_back();
	if(Entry.m_Kind == EHistoryKind::ADD)
		RestoreStrokes(Entry.m_vStrokes);
	else if(Entry.m_Kind == EHistoryKind::MOVE)
	{
		DropStrokes(Entry.m_vBefore);
		RestoreStrokes(Entry.m_vStrokes);
	}
	else
		DropStrokes(Entry.m_vStrokes);
	m_vSelectedIds.clear();
	m_vUndo.push_back(std::move(Entry));
}

void CUClientDrawMode::ClearOwnStrokes()
{
	ClearLasso();
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

void CUClientDrawMode::ClearLasso()
{
	m_LassoDrawing = false;
	m_Moving = false;
	m_Placing = false;
	m_vLasso.clear();
	m_vSelectedIds.clear();
	m_vPlace.clear();
	m_MoveDelta = vec2(0.0f, 0.0f);
}

bool CUClientDrawMode::IsSelected(uint32_t Id) const
{
	return std::find(m_vSelectedIds.begin(), m_vSelectedIds.end(), Id) != m_vSelectedIds.end();
}

const CUClientDrawMode::SStroke *CUClientDrawMode::FindOwnStroke(uint32_t Id) const
{
	const CUuid Own = GameClient()->m_ClientIndicator.ClientInstanceId();
	for(const SStroke &Stroke : m_vStrokes)
	{
		if(Stroke.m_Id == Id && Stroke.m_Owner == Own)
			return &Stroke;
	}
	return nullptr;
}

bool CUClientDrawMode::HitsSelection(vec2 Pos) const
{
	for(uint32_t Id : m_vSelectedIds)
	{
		const SStroke *pStroke = FindOwnStroke(Id);
		if(!pStroke)
			continue;
		const float Hit = pStroke->m_Width * 0.5f + 4.0f;
		if(pStroke->m_vPoints.size() == 1 && distance(Pos, pStroke->m_vPoints[0]) <= Hit)
			return true;
		for(size_t i = 1; i < pStroke->m_vPoints.size(); i++)
		{
			if(DistanceToSegment(Pos, pStroke->m_vPoints[i - 1], pStroke->m_vPoints[i]) <= Hit)
				return true;
		}
	}
	return false;
}

void CUClientDrawMode::FinishLasso()
{
	m_LassoDrawing = false;
	const std::vector<vec2> vPoly = std::move(m_vLasso);
	m_vLasso.clear();
	m_vSelectedIds.clear();
	if(vPoly.size() < 3)
		return;

	vec2 PolyMin = vPoly[0];
	vec2 PolyMax = vPoly[0];
	for(const vec2 &Point : vPoly)
	{
		PolyMin = vec2(minimum(PolyMin.x, Point.x), minimum(PolyMin.y, Point.y));
		PolyMax = vec2(maximum(PolyMax.x, Point.x), maximum(PolyMax.y, Point.y));
	}

	const CUuid Own = GameClient()->m_ClientIndicator.ClientInstanceId();
	std::vector<uint32_t> vGestures;
	for(const SStroke &Stroke : m_vStrokes)
	{
		if(!(Stroke.m_Owner == Own) || !StrokeHitsLasso(Stroke.m_vPoints, Stroke.m_Min, Stroke.m_Max, vPoly, PolyMin, PolyMax))
			continue;
		if(Stroke.m_GestureId != 0)
		{
			if(std::find(vGestures.begin(), vGestures.end(), Stroke.m_GestureId) == vGestures.end())
				vGestures.push_back(Stroke.m_GestureId);
		}
		else
			m_vSelectedIds.push_back(Stroke.m_Id);
	}
	if(vGestures.empty())
		return;
	for(const SStroke &Stroke : m_vStrokes)
	{
		if(Stroke.m_Owner == Own && Stroke.m_GestureId != 0 && std::find(vGestures.begin(), vGestures.end(), Stroke.m_GestureId) != vGestures.end())
			m_vSelectedIds.push_back(Stroke.m_Id);
	}
}

void CUClientDrawMode::CommitMove()
{
	m_Moving = false;
	std::vector<SStroke> vOld;
	for(uint32_t Id : m_vSelectedIds)
	{
		if(const SStroke *pStroke = FindOwnStroke(Id))
			vOld.push_back(*pStroke);
	}
	bool Changed = false;
	for(const SStroke &Old : vOld)
	{
		for(const vec2 &Point : Old.m_vPoints)
		{
			const vec2 Next(std::round(Point.x + m_MoveDelta.x), std::round(Point.y + m_MoveDelta.y));
			if(Next.x != Point.x || Next.y != Point.y)
				Changed = true;
		}
	}
	if(!Changed)
	{
		m_MoveDelta = vec2(0.0f, 0.0f);
		if(vOld.empty())
			m_vSelectedIds.clear();
		return;
	}

	std::vector<std::pair<uint32_t, uint32_t>> vGestureMap;
	auto MapGesture = [&](uint32_t Old) {
		if(Old == 0)
			return 0u;
		for(const auto &Pair : vGestureMap)
		{
			if(Pair.first == Old)
				return Pair.second;
		}
		const uint32_t New = m_NextGestureId++;
		vGestureMap.emplace_back(Old, New);
		return New;
	};

	const CUuid Own = GameClient()->m_ClientIndicator.ClientInstanceId();
	std::vector<SStroke> vNew;
	std::vector<uint32_t> vOldIds;
	vNew.reserve(vOld.size());
	vOldIds.reserve(vOld.size());
	for(const SStroke &Old : vOld)
	{
		SStroke Moved = Old;
		Moved.m_Id = m_NextStrokeId++;
		Moved.m_GestureId = MapGesture(Old.m_GestureId);
		for(vec2 &Point : Moved.m_vPoints)
			Point = vec2(std::round(Point.x + m_MoveDelta.x), std::round(Point.y + m_MoveDelta.y));
		vOldIds.push_back(Old.m_Id);
		vNew.push_back(std::move(Moved));
	}

	std::vector<SStroke> vHistory = vNew;
	RemoveStrokes(Own, &vOldIds);
	SendEraseIds(vOldIds);
	for(const SStroke &Stroke : vHistory)
		SendSegment(Stroke);
	for(SStroke &Stroke : vNew)
		AddStroke(std::move(Stroke));

	m_vSelectedIds.clear();
	m_vSelectedIds.reserve(vHistory.size());
	for(const SStroke &Stroke : vHistory)
		m_vSelectedIds.push_back(Stroke.m_Id);
	PushHistory(EHistoryKind::MOVE, std::move(vHistory), std::move(vOld));
	m_MoveDelta = vec2(0.0f, 0.0f);
}

void CUClientDrawMode::UpdateLasso(bool Left, bool LeftClicked, vec2 World)
{
	if(m_Placing)
	{
		if(LeftClicked && !m_PointerOverUi)
			PlaceStamp(World);
		return;
	}
	if(m_Moving)
	{
		m_MoveDelta = World - m_MoveAnchor;
		if(!Left)
			CommitMove();
		return;
	}
	if(m_LassoDrawing)
	{
		if(Left)
		{
			if(m_vLasso.empty() || distance(m_vLasso.back(), World) > 4.0f)
				m_vLasso.push_back(World);
		}
		else
			FinishLasso();
		return;
	}
	if(!LeftClicked || m_PointerOverUi)
		return;
	if(HitsSelection(World))
	{
		m_Moving = true;
		m_MoveAnchor = World;
		m_MoveDelta = vec2(0.0f, 0.0f);
		return;
	}
	m_vSelectedIds.clear();
	m_LassoDrawing = true;
	m_vLasso.clear();
	m_vLasso.push_back(World);
}

void CUClientDrawMode::RefreshDrawingList()
{
	m_vDrawingNames.clear();
	Storage()->ListDirectory(IStorage::TYPE_SAVE, DRAWINGS_DIR, ListDrawingFile, &m_vDrawingNames);
	std::sort(m_vDrawingNames.begin(), m_vDrawingNames.end());
}

bool CUClientDrawMode::SaveSelection(const char *pName)
{
	char aFile[128];
	if(!DrawingFileName(pName, aFile, sizeof(aFile)))
		return false;

	std::vector<SStroke> vSelected;
	vec2 Min(0.0f, 0.0f);
	vec2 Max(0.0f, 0.0f);
	bool Any = false;
	for(uint32_t Id : m_vSelectedIds)
	{
		const SStroke *pStroke = FindOwnStroke(Id);
		if(!pStroke || pStroke->m_vPoints.empty())
			continue;
		for(const vec2 &Point : pStroke->m_vPoints)
		{
			if(!Any)
				Min = Max = Point;
			Any = true;
			Min = vec2(minimum(Min.x, Point.x), minimum(Min.y, Point.y));
			Max = vec2(maximum(Max.x, Point.x), maximum(Max.y, Point.y));
		}
		vSelected.push_back(*pStroke);
	}
	if(!Any)
		return false;
	const vec2 Center = (Min + Max) * 0.5f;

	Storage()->CreateFolder(DRAWINGS_DIR, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s.json", DRAWINGS_DIR, aFile);
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;

	auto Channel = [](float Value) { return std::clamp(round_to_int(Value * 255.0f), 0, 255); };
	CJsonFileWriter Writer(File);
	Writer.BeginObject();
	Writer.WriteAttribute("name");
	Writer.WriteStrValue(aFile);
	Writer.WriteAttribute("strokes");
	Writer.BeginArray();
	for(const SStroke &Stroke : vSelected)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("tool");
		Writer.WriteIntValue(Stroke.m_Tool);
		Writer.WriteAttribute("r");
		Writer.WriteIntValue(Channel(Stroke.m_Color.r));
		Writer.WriteAttribute("g");
		Writer.WriteIntValue(Channel(Stroke.m_Color.g));
		Writer.WriteAttribute("b");
		Writer.WriteIntValue(Channel(Stroke.m_Color.b));
		Writer.WriteAttribute("a");
		Writer.WriteIntValue(Channel(Stroke.m_Color.a));
		Writer.WriteAttribute("width");
		Writer.WriteIntValue(std::clamp(round_to_int(Stroke.m_Width), UClientPresence::DRAW_MIN_WIDTH, UClientPresence::DRAW_MAX_WIDTH));
		Writer.WriteAttribute("points");
		Writer.BeginArray();
		for(const vec2 &Point : Stroke.m_vPoints)
		{
			Writer.BeginArray();
			Writer.WriteIntValue(round_to_int(Point.x - Center.x));
			Writer.WriteIntValue(round_to_int(Point.y - Center.y));
			Writer.EndArray();
		}
		Writer.EndArray();
		Writer.EndObject();
	}
	Writer.EndArray();
	Writer.EndObject();
	return true;
}

bool CUClientDrawMode::LoadDrawing(const char *pName)
{
	char aFile[128];
	if(!DrawingFileName(pName, aFile, sizeof(aFile)))
		return false;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s.json", DRAWINGS_DIR, aFile);
	void *pData = nullptr;
	unsigned Length = 0;
	if(!Storage()->ReadFile(aPath, IStorage::TYPE_SAVE, &pData, &Length) || !pData)
		return false;
	json_settings JsonSettings{};
	char aError[256];
	json_value *pRoot = json_parse_ex(&JsonSettings, static_cast<const json_char *>(pData), Length, aError);
	free(pData);
	if(!pRoot)
	{
		(void)aError;
		return false;
	}

	std::vector<SStampStroke> vPlace;
	const json_value *pStrokes = pRoot->type == json_object ? json_object_get(pRoot, "strokes") : nullptr;
	if(pStrokes && pStrokes->type == json_array)
	{
		const int Count = json_array_length(pStrokes);
		for(int i = 0; i < Count; i++)
		{
			const json_value *pItem = json_array_get(pStrokes, i);
			if(!pItem || pItem->type != json_object)
				continue;
			auto IntOf = [&](const char *pKey, int Fallback) {
				const json_value *pValue = json_object_get(pItem, pKey);
				return pValue && pValue->type == json_integer ? json_int_get(pValue) : Fallback;
			};
			SStampStroke Stamp;
			Stamp.m_Tool = IntOf("tool", UClientPresence::DRAW_TOOL_PEN) == UClientPresence::DRAW_TOOL_CRAYON ? UClientPresence::DRAW_TOOL_CRAYON : UClientPresence::DRAW_TOOL_PEN;
			Stamp.m_Width = (float)std::clamp(IntOf("width", 8), UClientPresence::DRAW_MIN_WIDTH, UClientPresence::DRAW_MAX_WIDTH);
			Stamp.m_Color = ColorRGBA(std::clamp(IntOf("r", 255), 0, 255) / 255.0f, std::clamp(IntOf("g", 255), 0, 255) / 255.0f, std::clamp(IntOf("b", 255), 0, 255) / 255.0f, std::clamp(IntOf("a", 255), 0, 255) / 255.0f);
			const json_value *pPoints = json_object_get(pItem, "points");
			if(!pPoints || pPoints->type != json_array)
				continue;
			const int PointCount = json_array_length(pPoints);
			for(int PointIndex = 0; PointIndex < PointCount; PointIndex++)
			{
				const json_value *pPoint = json_array_get(pPoints, PointIndex);
				if(!pPoint || pPoint->type != json_array || json_array_length(pPoint) < 2)
					continue;
				const json_value *pX = json_array_get(pPoint, 0);
				const json_value *pY = json_array_get(pPoint, 1);
				if(!pX || pX->type != json_integer || !pY || pY->type != json_integer)
					continue;
				Stamp.m_vPoints.emplace_back((float)json_int_get(pX), (float)json_int_get(pY));
			}
			if(Stamp.m_vPoints.empty())
				continue;
			const size_t Limit = (size_t)UClientPresence::DRAW_MAX_POINTS_PER_SEGMENT;
			if(Stamp.m_vPoints.size() <= Limit)
			{
				vPlace.push_back(std::move(Stamp));
				continue;
			}
			for(size_t Start = 0; Start < Stamp.m_vPoints.size();)
			{
				const size_t End = minimum(Stamp.m_vPoints.size(), Start + Limit);
				SStampStroke Chunk = Stamp;
				Chunk.m_vPoints.assign(Stamp.m_vPoints.begin() + Start, Stamp.m_vPoints.begin() + End);
				vPlace.push_back(std::move(Chunk));
				if(End >= Stamp.m_vPoints.size())
					break;
				Start = End - 1;
			}
		}
	}
	json_value_free(pRoot);
	if(vPlace.empty())
		return false;

	m_vPlace = std::move(vPlace);
	m_Placing = true;
	m_LassoDrawing = false;
	m_Moving = false;
	m_vLasso.clear();
	m_vSelectedIds.clear();
	m_MoveDelta = vec2(0.0f, 0.0f);
	return true;
}

void CUClientDrawMode::DeleteDrawing(const char *pName)
{
	char aFile[128];
	if(!DrawingFileName(pName, aFile, sizeof(aFile)))
		return;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s.json", DRAWINGS_DIR, aFile);
	Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
	RefreshDrawingList();
}

void CUClientDrawMode::PlaceStamp(vec2 World)
{
	if(m_vPlace.empty())
	{
		m_Placing = false;
		return;
	}
	const uint32_t Gesture = m_NextGestureId++;
	const CUuid Own = GameClient()->m_ClientIndicator.ClientInstanceId();
	std::vector<SStroke> vPlaced;
	for(const SStampStroke &Stamp : m_vPlace)
	{
		SStroke Stroke;
		Stroke.m_Owner = Own;
		Stroke.m_Id = m_NextStrokeId++;
		Stroke.m_GestureId = Gesture;
		Stroke.m_Tool = Stamp.m_Tool;
		Stroke.m_Color = Stamp.m_Color;
		if(Stroke.m_Color.a <= 0.0f)
			Stroke.m_Color.a = 1.0f;
		Stroke.m_Width = Stamp.m_Width;
		Stroke.m_vPoints.reserve(Stamp.m_vPoints.size());
		for(const vec2 &Point : Stamp.m_vPoints)
			Stroke.m_vPoints.push_back(vec2(std::round(World.x + Point.x), std::round(World.y + Point.y)));
		vPlaced.push_back(std::move(Stroke));
	}
	for(const SStroke &Stroke : vPlaced)
		SendSegment(Stroke);
	for(const SStroke &Stroke : vPlaced)
		AddStroke(SStroke(Stroke));
	PushHistory(EHistoryKind::ADD, std::move(vPlaced));
	m_Placing = false;
	m_vPlace.clear();
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

	if(m_Eyedropper)
	{
		if(!m_PointerOverUi && !Ui()->IsPopupOpen())
		{
			const int PixelX = std::clamp<int>(round_to_int((Ui()->MouseX() - 1.0f) / Ui()->Screen()->w * Graphics()->ScreenWidth()), 0, Graphics()->ScreenWidth() - 1);
			const int PixelY = std::clamp<int>(round_to_int((Ui()->MouseY() - 1.0f) / Ui()->Screen()->h * Graphics()->ScreenHeight()), 0, Graphics()->ScreenHeight() - 1);
			Graphics()->ReadPixel(ivec2(PixelX, PixelY), &m_EyedropperColor);

			const bool Left = Ui()->MouseButton(0);
			const bool LeftClicked = Left && !m_WasLeftDown;
			if(LeftClicked && m_EyedropperHasSample)
			{
				ColorRGBA Picked = m_EyedropperColor;
				Picked.a = 1.0f;
				g_Config.m_UcDrawCustomColor = color_cast<ColorHSLA>(Picked).Pack(false);
				m_UseCustomColor = true;
				if(m_Tool == ETool::ERASER)
					m_Tool = ETool::PEN;
				m_Eyedropper = false;
				m_EyedropperHasSample = false;
			}
			else
				m_EyedropperHasSample = true;
		}
		m_WasLeftDown = Ui()->MouseButton(0);
		return;
	}

	const bool Left = Ui()->MouseButton(0);
	const bool LeftClicked = Left && !m_WasLeftDown;
	m_WasLeftDown = Left;
	const vec2 World = MouseWorldPos();
	if(m_Tool == ETool::LASSO || m_Placing)
	{
		UpdateLasso(Left, LeftClicked, World);
		return;
	}
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
	const bool Overlay = m_Placing || !m_vLasso.empty();
	if(Client()->State() != IClient::STATE_ONLINE || !g_Config.m_UcDraw || (m_vStrokes.empty() && m_vPendingPoints.empty() && !Overlay))
		return;

	float aPoints[4];
	WorldScreen(aPoints);
	float PrevX0, PrevY0, PrevX1, PrevY1;
	Graphics()->GetScreen(&PrevX0, &PrevY0, &PrevX1, &PrevY1);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);
	Graphics()->ClipDisable();
	Graphics()->BlendNormal();
	Graphics()->TextureClear();

	auto OffScreen = [&](vec2 Min, vec2 Max) {
		return Max.x < aPoints[0] || Min.x > aPoints[2] || Max.y < aPoints[1] || Min.y > aPoints[3];
	};
	std::vector<IGraphics::CFreeformItem> vItems;
	auto RenderHighlighted = [&](const SStroke &Stroke) {
		SStroke Halo = Stroke;
		Halo.m_Tool = UClientPresence::DRAW_TOOL_PEN;
		Halo.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f);
		Halo.m_Width = Stroke.m_Width + 3.0f;
		RenderStroke(Halo, vItems);
		RenderStroke(Stroke, vItems);
	};

	const CUuid Own = GameClient()->m_ClientIndicator.ClientInstanceId();
	for(const SStroke &Stroke : m_vStrokes)
	{
		const bool Selected = Stroke.m_Owner == Own && IsSelected(Stroke.m_Id);
		if(Selected && m_Moving)
			continue;
		if(OffScreen(Stroke.m_Min, Stroke.m_Max))
			continue;
		if(Selected)
			RenderHighlighted(Stroke);
		else
			RenderStroke(Stroke, vItems);
	}
	if(m_Moving)
	{
		for(uint32_t Id : m_vSelectedIds)
		{
			const SStroke *pStroke = FindOwnStroke(Id);
			if(!pStroke)
				continue;
			SStroke Moved = *pStroke;
			for(vec2 &Point : Moved.m_vPoints)
				Point += m_MoveDelta;
			Moved.m_Min += m_MoveDelta;
			Moved.m_Max += m_MoveDelta;
			if(OffScreen(Moved.m_Min, Moved.m_Max))
				continue;
			RenderHighlighted(Moved);
		}
	}
	if(m_Drawing && m_Tool != ETool::ERASER && !m_vPendingPoints.empty())
	{
		SStroke Preview;
		Preview.m_Tool = m_Tool == ETool::CRAYON ? UClientPresence::DRAW_TOOL_CRAYON : UClientPresence::DRAW_TOOL_PEN;
		Preview.m_Color = CurrentColor();
		Preview.m_Width = (float)m_Width;
		if(m_HasSegmentStart)
			Preview.m_vPoints.push_back(m_SegmentStart);
		Preview.m_vPoints.insert(Preview.m_vPoints.end(), m_vPendingPoints.begin(), m_vPendingPoints.end());
		RenderStroke(Preview, vItems);
	}
	if(m_Placing)
	{
		const vec2 Origin = MouseWorldPos();
		for(const SStampStroke &Stamp : m_vPlace)
		{
			SStroke Preview;
			Preview.m_Tool = Stamp.m_Tool;
			Preview.m_Color = Stamp.m_Color;
			Preview.m_Color.a *= 0.45f;
			Preview.m_Width = Stamp.m_Width;
			Preview.m_Id = 1;
			Preview.m_vPoints.reserve(Stamp.m_vPoints.size());
			for(const vec2 &Point : Stamp.m_vPoints)
				Preview.m_vPoints.push_back(Origin + Point);
			RenderStroke(Preview, vItems);
		}
	}
	if(!m_vLasso.empty())
	{
		std::vector<IGraphics::CLineItem> vLines;
		vLines.reserve(m_vLasso.size());
		for(size_t i = 1; i < m_vLasso.size(); i++)
			vLines.emplace_back(m_vLasso[i - 1], m_vLasso[i]);
		if(m_LassoDrawing || m_vLasso.size() >= 2)
			vLines.emplace_back(m_vLasso.back(), m_LassoDrawing ? MouseWorldPos() : m_vLasso.front());
		if(!vLines.empty())
		{
			Graphics()->LinesBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.9f);
			Graphics()->LinesDraw(vLines.data(), vLines.size());
			Graphics()->LinesEnd();
		}
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
		if(m_Eyedropper)
		{
			m_Eyedropper = false;
			m_EyedropperHasSample = false;
			return true;
		}
		if(m_Placing)
		{
			m_Placing = false;
			m_vPlace.clear();
			return true;
		}
		if(Ui()->IsPopupOpen())
			Ui()->ClosePopupMenus();
		else if(m_LassoDrawing || m_Moving || !m_vSelectedIds.empty())
			ClearLasso();
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

CUi::EPopupMenuFunctionResult CUClientDrawMode::PopupSaveDrawing(void *pContext, CUIRect View, bool Active)
{
	(void)Active;
	CUClientDrawMode *pSelf = static_cast<CUClientDrawMode *>(pContext);
	static CButtonContainer s_SaveButton;
	CUIRect Row, Button;
	View.HSplitTop(20.0f, &Row, &View);
	pSelf->Ui()->DoEditBox(&pSelf->m_SaveNameInput, &Row, 12.0f);
	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(20.0f, &Button, &View);
	const bool Clicked = pSelf->GameClient()->m_Menus.DoButton_Menu(&s_SaveButton, Localize("Save"), 0, &Button);
	const bool Enter = pSelf->m_SaveNameInput.IsActive() && (pSelf->Input()->KeyPress(KEY_RETURN) || pSelf->Input()->KeyPress(KEY_KP_ENTER));
	if(Clicked || Enter)
	{
		char aName[48];
		str_copy(aName, pSelf->m_SaveNameInput.GetString());
		str_utf8_trim_right(aName);
		const char *pName = str_utf8_skip_whitespaces(aName);
		if(pName[0] != '\0' && pSelf->SaveSelection(pName))
			return CUi::POPUP_CLOSE_CURRENT;
	}
	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CUClientDrawMode::PopupLoadDrawing(void *pContext, CUIRect View, bool Active)
{
	(void)Active;
	CUClientDrawMode *pSelf = static_cast<CUClientDrawMode *>(pContext);
	static CScrollRegion s_ScrollRegion;
	static CButtonContainer s_aLoad[64];
	static CButtonContainer s_aDelete[64];
	vec2 ScrollOffset(0.0f, 0.0f);
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollbarWidth = 10.0f;
	ScrollParams.m_ScrollbarMargin = 2.0f;
	ScrollParams.m_ScrollUnit = 20.0f;
	s_ScrollRegion.Begin(&View, &ScrollOffset, &ScrollParams);
	View.y += ScrollOffset.y;

	if(pSelf->m_vDrawingNames.empty())
	{
		CUIRect Row;
		View.HSplitTop(18.0f, &Row, &View);
		s_ScrollRegion.AddRect(Row);
		pSelf->Ui()->DoLabel(&Row, Localize("No drawings"), 12.0f, TEXTALIGN_ML);
	}
	const int Count = minimum((int)pSelf->m_vDrawingNames.size(), 64);
	for(int i = 0; i < Count; i++)
	{
		CUIRect Row, Name, Delete;
		View.HSplitTop(18.0f, &Row, &View);
		View.HSplitTop(2.0f, nullptr, &View);
		if(!s_ScrollRegion.AddRect(Row))
			continue;
		Row.VSplitRight(14.0f, &Row, nullptr);
		Row.VSplitRight(18.0f, &Name, &Delete);
		if(pSelf->Ui()->DoButton_FontIcon(&s_aDelete[i], FontIcon::XMARK, 0, &Delete, BUTTONFLAG_LEFT))
		{
			str_copy(pSelf->m_aDeleteDrawingName, pSelf->m_vDrawingNames[i].c_str(), sizeof(pSelf->m_aDeleteDrawingName));
			pSelf->m_DeleteConfirm.Reset();
			pSelf->m_DeleteConfirm.YesNoButtons();
			str_format(pSelf->m_DeleteConfirm.m_aMessage, sizeof(pSelf->m_DeleteConfirm.m_aMessage), Localize("Delete saved drawing \"%s\"?"), pSelf->m_aDeleteDrawingName);
			pSelf->Ui()->ClosePopupMenu(&pSelf->m_DeleteConfirm);
			pSelf->Ui()->ShowPopupConfirm(Delete.x, Delete.y + Delete.h, &pSelf->m_DeleteConfirm);
			s_ScrollRegion.End();
			return CUi::POPUP_KEEP_OPEN;
		}
		if(pSelf->GameClient()->m_Menus.DoButton_Menu(&s_aLoad[i], pSelf->m_vDrawingNames[i].c_str(), 0, &Name))
		{
			if(pSelf->LoadDrawing(pSelf->m_vDrawingNames[i].c_str()))
			{
				s_ScrollRegion.End();
				return CUi::POPUP_CLOSE_CURRENT;
			}
		}
	}
	s_ScrollRegion.End();
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
	// Width fits the color row: width slider, palette, custom color, eyedropper.
	CUIRect Toolbar = {Margin, Screen.h - BottomMargin - 62.0f, 447.0f, 62.0f};
	Toolbar.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 6.0f);
	m_PointerOverUi = Ui()->MouseInside(&Toolbar) || Ui()->IsPopupOpen();

	CUIRect Content, ToolRow, StyleRow;
	Toolbar.Margin(5.0f, &Content);
	Content.HSplitMid(&ToolRow, &StyleRow, 4.0f);

	bool Exit = false;
	auto ToolButton = [&](CButtonContainer *pButton, const char *pIcon, const char *pTooltip, ETool Tool) {
		CUIRect Button;
		ToolRow.VSplitLeft(ToolRow.h, &Button, &ToolRow);
		ToolRow.VSplitLeft(3.0f, nullptr, &ToolRow);
		GameClient()->m_Tooltips.DoToolTip(pButton, &Button, pTooltip);
		if(Ui()->DoButton_FontIcon(pButton, pIcon, m_Tool == Tool ? 1 : 0, &Button, BUTTONFLAG_LEFT) && m_Tool != Tool)
		{
			if(Tool == ETool::LASSO || m_Tool == ETool::LASSO)
			{
				if(m_Drawing && m_Tool != ETool::ERASER)
					FinishPenGesture();
				else if(m_Drawing)
					FinishEraserGesture();
				ClearLasso();
				m_Eyedropper = false;
				m_EyedropperHasSample = false;
				Ui()->ClosePopupMenu(&m_SavePopupId);
				Ui()->ClosePopupMenu(&m_LoadPopupId);
				Ui()->ClosePopupMenu(&m_DeleteConfirm);
				m_SaveNameInput.Deactivate();
			}
			m_Tool = Tool;
		}
	};
	ToolButton(&m_PenButton, FontIcon::PEN, Localize("Pen"), ETool::PEN);
	ToolButton(&m_CrayonButton, FontIcon::HIGHLIGHTER, Localize("Crayon"), ETool::CRAYON);
	ToolButton(&m_EraserButton, FontIcon::ERASER, Localize("Eraser"), ETool::ERASER);
	ToolButton(&m_LassoButton, FontIcon::DRAW_POLYGON, Localize("Lasso"), ETool::LASSO);

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

	if(m_Tool == ETool::LASSO)
	{
		CUIRect SaveButton, LoadButton;
		StyleRow.VSplitLeft(90.0f, &SaveButton, &StyleRow);
		StyleRow.VSplitLeft(6.0f, nullptr, &StyleRow);
		StyleRow.VSplitLeft(90.0f, &LoadButton, &StyleRow);
		if(GameClient()->m_Menus.DoButton_Menu(&m_SaveButton, Localize("Save"), 0, &SaveButton) && !m_vSelectedIds.empty())
		{
			m_SaveNameInput.Set("Drawing");
			m_SaveNameInput.SelectAll();
			Ui()->SetActiveItem(&m_SaveNameInput);
			Ui()->ClosePopupMenu(&m_LoadPopupId);
			Ui()->DoPopupMenu(&m_SavePopupId, SaveButton.x, maximum(10.0f, SaveButton.y - 74.0f), 180.0f, 70.0f, this, PopupSaveDrawing);
		}
		if(GameClient()->m_Menus.DoButton_Menu(&m_LoadButton, Localize("Load"), 0, &LoadButton))
		{
			RefreshDrawingList();
			Ui()->ClosePopupMenu(&m_SavePopupId);
			m_SaveNameInput.Deactivate();
			Ui()->DoPopupMenu(&m_LoadPopupId, LoadButton.x, maximum(10.0f, LoadButton.y - 164.0f), 220.0f, 160.0f, this, PopupLoadDrawing);
		}
	}
	else
	{
		CUIRect WidthRect;
		StyleRow.VSplitLeft(150.0f, &WidthRect, &StyleRow);
		StyleRow.VSplitLeft(8.0f, nullptr, &StyleRow);
		Ui()->DoScrollbarOption(&m_WidthScrollbar, &m_Width, &WidthRect, Localize("Width"), UClientPresence::DRAW_MIN_WIDTH, UClientPresence::DRAW_MAX_WIDTH);

		for(int i = 0; i < (int)std::size(s_aPalette); i++)
		{
			CUIRect Swatch;
			StyleRow.VSplitLeft(20.0f, &Swatch, &StyleRow);
			StyleRow.VSplitLeft(3.0f, nullptr, &StyleRow);
			if(!m_UseCustomColor && i == m_ColorIndex)
				Swatch.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f), IGraphics::CORNER_ALL, 4.0f);
			CUIRect Inner;
			Swatch.Margin(!m_UseCustomColor && i == m_ColorIndex ? 2.0f : 1.0f, &Inner);
			Inner.Draw(s_aPalette[i], IGraphics::CORNER_ALL, 3.0f);
			if(Ui()->DoButtonLogic(&m_aColorButtons[i], 0, &Swatch, BUTTONFLAG_LEFT))
			{
				m_ColorIndex = i;
				m_UseCustomColor = false;
				m_Eyedropper = false;
				m_EyedropperHasSample = false;
				if(m_Tool == ETool::ERASER)
					m_Tool = ETool::PEN;
			}
		}

		StyleRow.VSplitLeft(6.0f, nullptr, &StyleRow);
		CUIRect Custom;
		StyleRow.VSplitLeft(20.0f, &Custom, &StyleRow);
		if(m_UseCustomColor)
			Custom.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f), IGraphics::CORNER_ALL, 4.0f);
		CUIRect CustomInner;
		Custom.Margin(m_UseCustomColor ? 2.0f : 1.0f, &CustomInner);
		CustomInner.Draw(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_UcDrawCustomColor, false)), IGraphics::CORNER_ALL, 3.0f);
		GameClient()->m_Tooltips.DoToolTip(&m_CustomColorButton, &Custom, Localize("Custom color"));
		if(Ui()->DoButtonLogic(&m_CustomColorButton, 0, &Custom, BUTTONFLAG_LEFT))
		{
			m_UseCustomColor = true;
			if(m_Tool == ETool::ERASER)
				m_Tool = ETool::PEN;
			if(!Ui()->IsPopupOpen(&m_CustomColorPicker))
			{
				const ColorHSLA HslaColor = ColorHSLA(g_Config.m_UcDrawCustomColor, false);
				m_CustomColorPicker.m_pHslaColor = &g_Config.m_UcDrawCustomColor;
				m_CustomColorPicker.m_HslaColor = HslaColor;
				m_CustomColorPicker.m_HsvaColor = color_cast<ColorHSVA>(HslaColor);
				m_CustomColorPicker.m_RgbaColor = color_cast<ColorRGBA>(m_CustomColorPicker.m_HsvaColor);
				m_CustomColorPicker.m_Alpha = false;
				Ui()->ShowPopupColorPicker(Custom.x, Custom.y, &m_CustomColorPicker);
			}
		}

		StyleRow.VSplitLeft(3.0f, nullptr, &StyleRow);
		CUIRect Dropper;
		StyleRow.VSplitLeft(20.0f, &Dropper, &StyleRow);
		GameClient()->m_Tooltips.DoToolTip(&m_EyedropperButton, &Dropper, Localize("Eyedropper"));
		if(Ui()->DoButton_FontIcon(&m_EyedropperButton, FontIcon::EYE_DROPPER, m_Eyedropper ? 1 : 0, &Dropper, BUTTONFLAG_LEFT))
		{
			m_Eyedropper = !m_Eyedropper;
			m_EyedropperHasSample = false;
		}
	}

	Ui()->RenderPopupMenus();
	if(!Ui()->IsPopupOpen(&m_SavePopupId) && m_SaveNameInput.IsActive())
		m_SaveNameInput.Deactivate();
	if(m_ClearConfirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
		ClearOwnStrokes();
	if(m_ClearConfirm.m_Result != CUi::SConfirmPopupContext::UNSET)
		m_ClearConfirm.Reset();
	if(m_DeleteConfirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED && m_aDeleteDrawingName[0] != '\0')
		DeleteDrawing(m_aDeleteDrawingName);
	if(m_DeleteConfirm.m_Result != CUi::SConfirmPopupContext::UNSET)
	{
		m_DeleteConfirm.Reset();
		m_aDeleteDrawingName[0] = '\0';
	}

	UpdateDrawing();

	if(m_PointerOverUi || m_Eyedropper || m_Tool == ETool::LASSO || m_Placing)
	{
		RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
		if(m_Eyedropper && m_EyedropperHasSample)
		{
			CUIRect Preview = {Ui()->MouseX() + 18.0f, Ui()->MouseY() + 18.0f, 16.0f, 16.0f};
			Preview.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_ALL, 3.0f);
			Preview.Margin(2.0f, &Preview);
			Preview.Draw(m_EyedropperColor.WithAlpha(1.0f), IGraphics::CORNER_ALL, 2.0f);
		}
	}
	else
	{
		float aPoints[4];
		WorldScreen(aPoints);
		const float UiPerWorld = Screen.w / (aPoints[2] - aPoints[0]);
		const bool Eraser = m_Tool == ETool::ERASER;
		const float Radius = maximum(2.0f, (Eraser ? m_Width : m_Width * 0.5f) * UiPerWorld);
		ColorRGBA Color = Eraser ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f) : CurrentColor().WithAlpha(0.8f);
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
