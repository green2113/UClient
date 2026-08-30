#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_WEAPON_TRAJ_POLICY_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_WEAPON_TRAJ_POLICY_H

#include <base/net.h>

#include <engine/shared/http.h>

#include <game/client/component.h>

#include <memory>
#include <string>
#include <vector>

class CUClientWeaponTrajPolicy : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;

	// IP rules beat mode rules; allow beats deny within the same tier.
	// Default (no match): allowed.
	bool IsAllowed(const NETADDR &Addr, const char *pGameType) const;
	bool IsBlockedForCurrentServer() const;

private:
	void Update();
	static bool MatchesIpEntry(const NETADDR &Addr, const char *pEntry);
	static bool MatchesIpList(const NETADDR &Addr, const std::vector<std::string> &vList);
	static bool MatchesModeList(const char *pGameType, const std::vector<std::string> &vList);
	static void ParseStringArray(const json_value *pValue, std::vector<std::string> &vOut);

	int64_t m_LastRefreshAttempt = 0;
	std::shared_ptr<CHttpRequest> m_pRequest;
	std::vector<std::string> m_vAllowIps;
	std::vector<std::string> m_vDenyIps;
	std::vector<std::string> m_vAllowModes;
	std::vector<std::string> m_vDenyModes;
};

#endif
