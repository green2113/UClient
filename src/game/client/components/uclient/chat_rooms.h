#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_CHAT_ROOMS_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_CHAT_ROOMS_H

#include <base/color.h>

#include <engine/shared/http.h>

#include <game/client/component.h>

#include <memory>
#include <vector>

class CUClientChatRooms : public CComponent
{
public:
	static ColorRGBA DefaultNameColor() { return ColorRGBA(0.55f, 0.78f, 1.0f, 1.0f); }

	struct SMember
	{
		char m_aId[64] = "";
		char m_aDisplayName[64] = "";
		bool m_Owner = false;
		bool m_Admin = false;
		bool m_Self = false;
	};

	struct SRoom
	{
		char m_aId[64] = "";
		char m_aName[64] = "";
		char m_aInviteCode[40] = "";
		bool m_Owner = false;
		bool m_Admin = false;
		bool m_InviteCodePublic = false;
		unsigned m_NameColor = 0;
		std::vector<SMember> m_vMembers;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnUpdate() override;
	void OnShutdown() override;

	const std::vector<SRoom> &Rooms() const { return m_vRooms; }
	const char *SelectedSendRoomId() const;
	const char *RoomNameById(const char *pRoomId) const;
	ColorRGBA RoomNameColorById(const char *pRoomId) const;
	void SelectSendRoom(const char *pRoomId);
	bool IsBusy() const { return m_pRequest != nullptr; }
	const char *ErrorMessage() const { return m_aError; }

	void Refresh();
	void RefreshIfStale(int MaxAgeSeconds);
	void RequestRefreshSoon();
	bool Create(const char *pName, const char *pDisplayName);
	void Rename(const char *pRoomId, const char *pName);
	void UpdateSettings(const char *pRoomId, const char *pName, bool HasName, unsigned NameColor, bool HasColor, bool InviteCodePublic, bool HasInviteCodePublic);
	void RegenerateCode(const char *pRoomId);
	bool Join(const char *pCode, const char *pDisplayName);
	void Kick(const char *pRoomId, const char *pMemberId);
	void Leave(const char *pRoomId);
	void TransferOwnership(const char *pRoomId, const char *pMemberId);
	void SetMemberAdmin(const char *pRoomId, const char *pMemberId, bool Admin);

private:
	enum class ERequest
	{
		NONE,
		REFRESH,
		MUTATE,
	};

	void AuthHeader(CHttpRequest *pRequest) const;
	bool Begin(std::shared_ptr<CHttpRequest> pRequest, ERequest Request);
	void Finish();
	void ParseRooms(const json_value *pRoot);
	bool BeginJsonPost(const char *pPath, const char *pJson);
	void BeginDelete(const char *pPath);

	std::vector<SRoom> m_vRooms;
	std::shared_ptr<CHttpRequest> m_pRequest;
	ERequest m_Request = ERequest::NONE;
	char m_aError[256] = "";
	bool m_InitialRefresh = false;
	int64_t m_LastRefresh = 0;
	int64_t m_PendingRefreshAt = 0;
};

#endif
