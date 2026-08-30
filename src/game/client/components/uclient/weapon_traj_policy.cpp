#include "weapon_traj_policy.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/json.h>

#include <game/client/gameclient.h>

#include <cctype>

static constexpr const char *WEAPON_TRAJ_POLICY_URL = "https://ddnet.under1111.com/uclient/weapontrajpolicy.json";

static void TrimInPlace(std::string &Str)
{
	while(!Str.empty() && std::isspace(static_cast<unsigned char>(Str.front())))
		Str.erase(Str.begin());
	while(!Str.empty() && std::isspace(static_cast<unsigned char>(Str.back())))
		Str.pop_back();
}

void CUClientWeaponTrajPolicy::ParseStringArray(const json_value *pValue, std::vector<std::string> &vOut)
{
	vOut.clear();
	if(!pValue || pValue->type != json_array)
		return;

	const int Length = json_array_length(pValue);
	vOut.reserve(Length);
	for(int i = 0; i < Length; ++i)
	{
		const json_value *pEntry = json_array_get(pValue, i);
		if(!pEntry || pEntry->type != json_string || !pEntry->u.string.ptr)
			continue;
		std::string Entry = pEntry->u.string.ptr;
		TrimInPlace(Entry);
		if(Entry.empty())
			continue;
		vOut.push_back(std::move(Entry));
	}
}

bool CUClientWeaponTrajPolicy::MatchesIpEntry(const NETADDR &Addr, const char *pEntry)
{
	if(!pEntry || pEntry[0] == '\0')
		return false;

	char aWithPort[NETADDR_MAXSTRSIZE];
	char aWithoutPort[NETADDR_MAXSTRSIZE];
	net_addr_str(&Addr, aWithPort, sizeof(aWithPort), true);
	net_addr_str(&Addr, aWithoutPort, sizeof(aWithoutPort), false);

	const bool EntryHasPort = pEntry[0] == '[' ? (str_find(pEntry, "]:") != nullptr) : (str_find(pEntry, ":") != nullptr);
	if(EntryHasPort)
		return str_comp_nocase(pEntry, aWithPort) == 0;
	return str_comp_nocase(pEntry, aWithoutPort) == 0;
}

bool CUClientWeaponTrajPolicy::MatchesIpList(const NETADDR &Addr, const std::vector<std::string> &vList)
{
	for(const std::string &Entry : vList)
	{
		if(MatchesIpEntry(Addr, Entry.c_str()))
			return true;
	}
	return false;
}

bool CUClientWeaponTrajPolicy::MatchesModeList(const char *pGameType, const std::vector<std::string> &vList)
{
	if(!pGameType || pGameType[0] == '\0')
		return false;
	for(const std::string &Entry : vList)
	{
		if(str_comp_nocase(pGameType, Entry.c_str()) == 0)
			return true;
	}
	return false;
}

bool CUClientWeaponTrajPolicy::IsAllowed(const NETADDR &Addr, const char *pGameType) const
{
	if(MatchesIpList(Addr, m_vAllowIps))
		return true;
	if(MatchesIpList(Addr, m_vDenyIps))
		return false;
	if(MatchesModeList(pGameType, m_vAllowModes))
		return true;
	if(MatchesModeList(pGameType, m_vDenyModes))
		return false;
	return true;
}

bool CUClientWeaponTrajPolicy::IsBlockedForCurrentServer() const
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return false;

	CServerInfo Info;
	Client()->GetServerInfo(&Info);
	const char *pGameType = Info.m_aGameType[0] != '\0' ? Info.m_aGameType : GameClient()->m_GameInfo.m_aGameType;
	return !IsAllowed(Client()->ServerAddress(), pGameType);
}

void CUClientWeaponTrajPolicy::Update()
{
	if(m_pRequest == nullptr)
	{
		const int64_t Now = time_get();
		if(m_LastRefreshAttempt != 0 && Now < m_LastRefreshAttempt + time_freq() * 5)
			return;

		m_LastRefreshAttempt = Now;
		std::shared_ptr<CHttpRequest> pRequest = HttpGet(WEAPON_TRAJ_POLICY_URL);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->Timeout(CTimeout{2000, 4000, 500, 5});
		m_pRequest = pRequest;
		Http()->Run(pRequest);
		return;
	}

	if(!m_pRequest->Done())
		return;

	if(m_pRequest->State() == EHttpState::DONE)
	{
		unsigned char *pResult = nullptr;
		size_t ResultLength = 0;
		m_pRequest->Result(&pResult, &ResultLength);

		json_settings JsonSettings{};
		char aError[256];
		json_value *pRoot = json_parse_ex(&JsonSettings, (json_char *)pResult, ResultLength, aError);
		if(pRoot != nullptr)
		{
			if(pRoot->type == json_object)
			{
				std::vector<std::string> vAllowIps;
				std::vector<std::string> vDenyIps;
				std::vector<std::string> vAllowModes;
				std::vector<std::string> vDenyModes;
				ParseStringArray(json_object_get(pRoot, "allow_ips"), vAllowIps);
				ParseStringArray(json_object_get(pRoot, "deny_ips"), vDenyIps);
				ParseStringArray(json_object_get(pRoot, "allow_modes"), vAllowModes);
				ParseStringArray(json_object_get(pRoot, "deny_modes"), vDenyModes);
				m_vAllowIps = std::move(vAllowIps);
				m_vDenyIps = std::move(vDenyIps);
				m_vAllowModes = std::move(vAllowModes);
				m_vDenyModes = std::move(vDenyModes);
			}
			json_value_free(pRoot);
		}
	}

	m_pRequest.reset();
}

void CUClientWeaponTrajPolicy::OnRender()
{
	Update();
}
