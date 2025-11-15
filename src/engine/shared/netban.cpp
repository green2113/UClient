#include "netban.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

static constexpr const char *PERSISTENT_REASON_SUFFIX = " (discord.gg/PNpxPxvcws)";

CNetBan::CNetHash::CNetHash(const NETADDR *pAddr)
{
	if(pAddr->type == NETTYPE_IPV4)
		m_Hash = (pAddr->ip[0] + pAddr->ip[1] + pAddr->ip[2] + pAddr->ip[3]) & 0xFF;
	else
		m_Hash = (pAddr->ip[0] + pAddr->ip[1] + pAddr->ip[2] + pAddr->ip[3] + pAddr->ip[4] + pAddr->ip[5] + pAddr->ip[6] + pAddr->ip[7] +
				 pAddr->ip[8] + pAddr->ip[9] + pAddr->ip[10] + pAddr->ip[11] + pAddr->ip[12] + pAddr->ip[13] + pAddr->ip[14] + pAddr->ip[15]) &
			 0xFF;
	m_HashIndex = 0;
}

CNetBan::CNetHash::CNetHash(const CNetRange *pRange)
{
	m_Hash = 0;
	m_HashIndex = 0;
	for(int i = 0; pRange->m_LB.ip[i] == pRange->m_UB.ip[i]; ++i)
	{
		m_Hash += pRange->m_LB.ip[i];
		++m_HashIndex;
	}
	m_Hash &= 0xFF;
}

int CNetBan::CNetHash::MakeHashArray(const NETADDR *pAddr, CNetHash aHash[17])
{
	int Length = pAddr->type == NETTYPE_IPV4 ? 4 : 16;
	aHash[0].m_Hash = 0;
	aHash[0].m_HashIndex = 0;
	for(int i = 1, Sum = 0; i <= Length; ++i)
	{
		Sum += pAddr->ip[i - 1];
		aHash[i].m_Hash = Sum & 0xFF;
		aHash[i].m_HashIndex = i % Length;
	}
	return Length;
}

template<class T, int HashCount>
void CNetBan::CBanPool<T, HashCount>::InsertUsed(CBan<T> *pBan)
{
	if(m_pFirstUsed)
	{
		for(CBan<T> *p = m_pFirstUsed;; p = p->m_pNext)
		{
			if(p->m_Info.m_Expires == CBanInfo::EXPIRES_NEVER || (pBan->m_Info.m_Expires != CBanInfo::EXPIRES_NEVER && pBan->m_Info.m_Expires <= p->m_Info.m_Expires))
			{
				// insert before
				pBan->m_pNext = p;
				pBan->m_pPrev = p->m_pPrev;
				if(p->m_pPrev)
					p->m_pPrev->m_pNext = pBan;
				else
					m_pFirstUsed = pBan;
				p->m_pPrev = pBan;
				break;
			}

			if(!p->m_pNext)
			{
				// last entry
				p->m_pNext = pBan;
				pBan->m_pPrev = p;
				pBan->m_pNext = 0;
				break;
			}
		}
	}
	else
	{
		m_pFirstUsed = pBan;
		pBan->m_pNext = pBan->m_pPrev = 0;
	}
}

template<class T, int HashCount>
typename CNetBan::CBan<T> *CNetBan::CBanPool<T, HashCount>::Add(const T *pData, const CBanInfo *pInfo, const CNetHash *pNetHash)
{
	if(!m_pFirstFree)
		return nullptr;

	// create new ban
	CBan<T> *pBan = m_pFirstFree;
	pBan->m_Data = *pData;
	pBan->m_Info = *pInfo;
	pBan->m_NetHash = *pNetHash;
	if(pBan->m_pNext)
		pBan->m_pNext->m_pPrev = pBan->m_pPrev;
	if(pBan->m_pPrev)
		pBan->m_pPrev->m_pNext = pBan->m_pNext;
	else
		m_pFirstFree = pBan->m_pNext;

	// add it to the hash list
	if(m_aapHashList[pNetHash->m_HashIndex][pNetHash->m_Hash])
		m_aapHashList[pNetHash->m_HashIndex][pNetHash->m_Hash]->m_pHashPrev = pBan;
	pBan->m_pHashPrev = 0;
	pBan->m_pHashNext = m_aapHashList[pNetHash->m_HashIndex][pNetHash->m_Hash];
	m_aapHashList[pNetHash->m_HashIndex][pNetHash->m_Hash] = pBan;

	// insert it into the used list
	InsertUsed(pBan);

	// update ban count
	++m_CountUsed;

	return pBan;
}

template<class T, int HashCount>
int CNetBan::CBanPool<T, HashCount>::Remove(CBan<T> *pBan)
{
	if(pBan == nullptr)
		return -1;

	// remove from hash list
	if(pBan->m_pHashNext)
		pBan->m_pHashNext->m_pHashPrev = pBan->m_pHashPrev;
	if(pBan->m_pHashPrev)
		pBan->m_pHashPrev->m_pHashNext = pBan->m_pHashNext;
	else
		m_aapHashList[pBan->m_NetHash.m_HashIndex][pBan->m_NetHash.m_Hash] = pBan->m_pHashNext;
	pBan->m_pHashNext = pBan->m_pHashPrev = 0;

	// remove from used list
	if(pBan->m_pNext)
		pBan->m_pNext->m_pPrev = pBan->m_pPrev;
	if(pBan->m_pPrev)
		pBan->m_pPrev->m_pNext = pBan->m_pNext;
	else
		m_pFirstUsed = pBan->m_pNext;

	// add to recycle list
	if(m_pFirstFree)
		m_pFirstFree->m_pPrev = pBan;
	pBan->m_pPrev = 0;
	pBan->m_pNext = m_pFirstFree;
	m_pFirstFree = pBan;

	// update ban count
	--m_CountUsed;

	return 0;
}

template<class T, int HashCount>
void CNetBan::CBanPool<T, HashCount>::Update(CBan<CDataType> *pBan, const CBanInfo *pInfo)
{
	pBan->m_Info = *pInfo;

	// remove from used list
	if(pBan->m_pNext)
		pBan->m_pNext->m_pPrev = pBan->m_pPrev;
	if(pBan->m_pPrev)
		pBan->m_pPrev->m_pNext = pBan->m_pNext;
	else
		m_pFirstUsed = pBan->m_pNext;

	// insert it into the used list
	InsertUsed(pBan);
}

void CNetBan::UnbanAll()
{
	m_BanAddrPool.Reset();
	m_BanRangePool.Reset();
	if(!m_PersistentBans.empty())
	{
		m_PersistentBans.clear();
		RewritePersistentBanFile();
	}
}

template<class T, int HashCount>
void CNetBan::CBanPool<T, HashCount>::Reset()
{
	mem_zero(m_aapHashList, sizeof(m_aapHashList));
	mem_zero(m_aBans, sizeof(m_aBans));
	m_pFirstUsed = 0;
	m_CountUsed = 0;

	for(int i = 1; i < MAX_BANS - 1; ++i)
	{
		m_aBans[i].m_pNext = &m_aBans[i + 1];
		m_aBans[i].m_pPrev = &m_aBans[i - 1];
	}

	m_aBans[0].m_pNext = &m_aBans[1];
	m_aBans[MAX_BANS - 1].m_pPrev = &m_aBans[MAX_BANS - 2];
	m_pFirstFree = &m_aBans[0];
}

template<class T, int HashCount>
typename CNetBan::CBan<T> *CNetBan::CBanPool<T, HashCount>::Get(int Index) const
{
	if(Index < 0 || Index >= Num())
		return nullptr;

	for(CNetBan::CBan<T> *pBan = m_pFirstUsed; pBan; pBan = pBan->m_pNext, --Index)
	{
		if(Index == 0)
			return pBan;
	}

	return nullptr;
}

template<class T>
int CNetBan::Ban(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason, bool VerbatimReason)
{
	// do not ban localhost
	if(NetMatch(pData, &m_LocalhostIpV4) || NetMatch(pData, &m_LocalhostIpV6))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (localhost)");
		return -1;
	}

	int64_t Stamp = Seconds > 0 ? time_timestamp() + Seconds : static_cast<int64_t>(CBanInfo::EXPIRES_NEVER);

	// set up info
	CBanInfo Info = {0};
	Info.m_Expires = Stamp;
	Info.m_VerbatimReason = VerbatimReason;
	str_copy(Info.m_aReason, pReason);

	// check if it already exists
	CNetHash NetHash(pData);
	CBan<typename T::CDataType> *pBan = pBanPool->Find(pData, &NetHash);
	if(pBan)
	{
		// adjust the ban
		pBanPool->Update(pBan, &Info);
		char aBuf[256];
		MakeBanInfo(pBan, aBuf, sizeof(aBuf), MSGTYPE_LIST);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aBuf);
		return 1;
	}

	// add ban and print result
	pBan = pBanPool->Add(pData, &Info, &NetHash);
	if(pBan)
	{
		char aBuf[256];
		MakeBanInfo(pBan, aBuf, sizeof(aBuf), MSGTYPE_BANADD);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aBuf);
		return 0;
	}
	else
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (full banlist)");
	return -1;
}

template<class T>
int CNetBan::Unban(T *pBanPool, const typename T::CDataType *pData)
{
	CNetHash NetHash(pData);
	CBan<typename T::CDataType> *pBan = pBanPool->Find(pData, &NetHash);
	if(pBan)
	{
		char aBuf[256];
		MakeBanInfo(pBan, aBuf, sizeof(aBuf), MSGTYPE_BANREM);
		pBanPool->Remove(pBan);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aBuf);
		return 0;
	}
	else
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "unban failed (invalid entry)");
	return -1;
}

void CNetBan::Init(IConsole *pConsole, IStorage *pStorage)
{
	m_pConsole = pConsole;
	m_pStorage = pStorage;
	m_BanAddrPool.Reset();
	m_BanRangePool.Reset();

	net_host_lookup("localhost", &m_LocalhostIpV4, NETTYPE_IPV4);
	net_host_lookup("localhost", &m_LocalhostIpV6, NETTYPE_IPV6);

	Console()->Register("ban", "s[ip|id] ?i[minutes] r[reason]", CFGFLAG_SERVER | CFGFLAG_MASTER | CFGFLAG_STORE, ConBan, this, "Ban ip for x minutes for any reason");
	Console()->Register("ban_range", "s[first ip] s[last ip] ?i[minutes] r[reason]", CFGFLAG_SERVER | CFGFLAG_MASTER | CFGFLAG_STORE, ConBanRange, this, "Ban ip range for x minutes for any reason");
	Console()->Register("unban", "s[ip|entry]", CFGFLAG_SERVER | CFGFLAG_MASTER | CFGFLAG_STORE, ConUnban, this, "Unban ip/banlist entry");
	Console()->Register("unban_range", "s[first ip] s[last ip]", CFGFLAG_SERVER | CFGFLAG_MASTER | CFGFLAG_STORE, ConUnbanRange, this, "Unban ip range");
	Console()->Register("unban_all", "", CFGFLAG_SERVER | CFGFLAG_MASTER | CFGFLAG_STORE, ConUnbanAll, this, "Unban all entries");
	Console()->Register("bans", "?i[page]", CFGFLAG_SERVER | CFGFLAG_MASTER, ConBans, this, "Show banlist (page 1 by default, 20 entries per page)");
	Console()->Register("bans_find", "s[ip]", CFGFLAG_SERVER | CFGFLAG_MASTER, ConBansFind, this, "Find all ban records for the specified IP address");
	Console()->Register("bans_save", "s[file]", CFGFLAG_SERVER | CFGFLAG_MASTER | CFGFLAG_STORE, ConBansSave, this, "Save banlist in a file");

	m_NextPersistentSync = 0;
	SyncPersistentBans(true);
}

void CNetBan::Update()
{
	int64_t Now = time_timestamp();

	// remove expired bans
	char aBuf[256], aNetStr[256];
	while(m_BanAddrPool.First() && m_BanAddrPool.First()->m_Info.m_Expires != CBanInfo::EXPIRES_NEVER && m_BanAddrPool.First()->m_Info.m_Expires < Now)
	{
		str_format(aBuf, sizeof(aBuf), "ban %s expired", NetToString(&m_BanAddrPool.First()->m_Data, aNetStr, sizeof(aNetStr)));
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aBuf);
		m_BanAddrPool.Remove(m_BanAddrPool.First());
	}
	while(m_BanRangePool.First() && m_BanRangePool.First()->m_Info.m_Expires != CBanInfo::EXPIRES_NEVER && m_BanRangePool.First()->m_Info.m_Expires < Now)
	{
		str_format(aBuf, sizeof(aBuf), "ban %s expired", NetToString(&m_BanRangePool.First()->m_Data, aNetStr, sizeof(aNetStr)));
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aBuf);
		m_BanRangePool.Remove(m_BanRangePool.First());
	}

	if(time_get() >= m_NextPersistentSync)
	{
		SyncPersistentBans(false);
	}
}

int CNetBan::BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason, bool VerbatimReason)
{
	int Result = Ban(&m_BanAddrPool, pAddr, Seconds, pReason, VerbatimReason);
	if(Result == 0 && Seconds <= 0 && !m_LoadingPersistentBans && !m_SyncingPersistentBans)
	{
		AppendPersistentBan(pAddr, pReason);
	}
	return Result;
}

int CNetBan::BanRange(const CNetRange *pRange, int Seconds, const char *pReason)
{
	if(pRange->IsValid())
		return Ban(&m_BanRangePool, pRange, Seconds, pReason, false);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (invalid range)");
	return -1;
}

int CNetBan::UnbanByAddr(const NETADDR *pAddr)
{
	int Result = Unban(&m_BanAddrPool, pAddr);
	if(Result == 0 && !m_SyncingPersistentBans)
	{
		RemovePersistentBan(pAddr);
	}
	return Result;
}

int CNetBan::UnbanByRange(const CNetRange *pRange)
{
	if(pRange->IsValid())
		return Unban(&m_BanRangePool, pRange);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (invalid range)");
	return -1;
}

int CNetBan::UnbanByIndex(int Index)
{
	int Result;
	char aBuf[256];
	CBanAddr *pBan = m_BanAddrPool.Get(Index);
	if(pBan)
	{
		NetToString(&pBan->m_Data, aBuf, sizeof(aBuf));
		Result = m_BanAddrPool.Remove(pBan);
	}
	else
	{
		CBanRange *pBanRange = m_BanRangePool.Get(Index - m_BanAddrPool.Num());
		if(pBanRange)
		{
			NetToString(&pBanRange->m_Data, aBuf, sizeof(aBuf));
			Result = m_BanRangePool.Remove(pBanRange);
		}
		else
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "unban failed (invalid index)");
			return -1;
		}
	}

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg), "unbanned index %i (%s)", Index, aBuf);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aMsg);
	return Result;
}

bool CNetBan::IsBanned(const NETADDR *pOrigAddr, char *pBuf, unsigned BufferSize) const
{
	NETADDR Addr;
	const NETADDR *pAddr = pOrigAddr;
	if(pOrigAddr->type == NETTYPE_WEBSOCKET_IPV4)
	{
		Addr = *pOrigAddr;
		pAddr = &Addr;
		Addr.type = NETTYPE_IPV4;
	}
	else if(pOrigAddr->type == NETTYPE_WEBSOCKET_IPV6)
	{
		Addr = *pOrigAddr;
		pAddr = &Addr;
		Addr.type = NETTYPE_IPV6;
	}
	CNetHash aHash[17];
	int Length = CNetHash::MakeHashArray(pAddr, aHash);

	// check ban addresses
	CBanAddr *pBan = m_BanAddrPool.Find(pAddr, &aHash[Length]);
	if(pBan)
	{
		MakeBanInfo(pBan, pBuf, BufferSize, MSGTYPE_PLAYER);
		return true;
	}

	// check ban ranges
	for(int i = Length - 1; i >= 0; --i)
	{
		for(CBanRange *pBanRange = m_BanRangePool.First(&aHash[i]); pBanRange; pBanRange = pBanRange->m_pHashNext)
		{
			if(NetMatch(&pBanRange->m_Data, pAddr, i, Length))
			{
				MakeBanInfo(pBanRange, pBuf, BufferSize, MSGTYPE_PLAYER);
				return true;
			}
		}
	}

	return false;
}

void CNetBan::ConBan(IConsole::IResult *pResult, void *pUser)
{
	CNetBan *pThis = static_cast<CNetBan *>(pUser);

	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments() > 1 ? std::clamp(pResult->GetInteger(1), 0, 525600) : 30;
	const char *pReason = pResult->NumArguments() > 2 ? pResult->GetString(2) : "No reason given";

	NETADDR Addr;
	if(net_addr_from_str(&Addr, pStr) == 0)
		pThis->BanAddr(&Addr, Minutes * 60, pReason, false);
	else
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid network address)");
}

void CNetBan::ConBanRange(IConsole::IResult *pResult, void *pUser)
{
	CNetBan *pThis = static_cast<CNetBan *>(pUser);

	const char *pStr1 = pResult->GetString(0);
	const char *pStr2 = pResult->GetString(1);
	int Minutes = pResult->NumArguments() > 2 ? std::clamp(pResult->GetInteger(2), 0, 525600) : 30;
	const char *pReason = pResult->NumArguments() > 3 ? pResult->GetString(3) : "No reason given";

	CNetRange Range;
	if(net_addr_from_str(&Range.m_LB, pStr1) == 0 && net_addr_from_str(&Range.m_UB, pStr2) == 0)
		pThis->BanRange(&Range, Minutes * 60, pReason);
	else
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid range)");
}

void CNetBan::ConUnban(IConsole::IResult *pResult, void *pUser)
{
	CNetBan *pThis = static_cast<CNetBan *>(pUser);

	const char *pStr = pResult->GetString(0);
	if(str_isallnum(pStr))
		pThis->UnbanByIndex(str_toint(pStr));
	else
	{
		NETADDR Addr;
		if(net_addr_from_str(&Addr, pStr) == 0)
			pThis->UnbanByAddr(&Addr);
		else
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "unban error (invalid network address)");
	}
}

void CNetBan::ConUnbanRange(IConsole::IResult *pResult, void *pUser)
{
	CNetBan *pThis = static_cast<CNetBan *>(pUser);

	const char *pStr1 = pResult->GetString(0);
	const char *pStr2 = pResult->GetString(1);

	CNetRange Range;
	if(net_addr_from_str(&Range.m_LB, pStr1) == 0 && net_addr_from_str(&Range.m_UB, pStr2) == 0)
		pThis->UnbanByRange(&Range);
	else
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "unban error (invalid range)");
}

void CNetBan::ConUnbanAll(IConsole::IResult *pResult, void *pUser)
{
	CNetBan *pThis = static_cast<CNetBan *>(pUser);

	pThis->UnbanAll();
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "unbanned all entries");
}

void CNetBan::ConBans(IConsole::IResult *pResult, void *pUser)
{
	CNetBan *pThis = static_cast<CNetBan *>(pUser);

	const int NumBans = pThis->m_BanAddrPool.Num() + pThis->m_BanRangePool.Num();
	if(NumBans == 0)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "The ban list is empty.");
		return;
	}

	static constexpr int ENTRIES_PER_PAGE = 20;
	const int NumPages = std::ceil(NumBans / (float)ENTRIES_PER_PAGE);
	const int Page = pResult->NumArguments() > 0 ? pResult->GetInteger(0) : 1;

	char aBuf[256], aMsg[256];
	if(Page <= 0 || Page > NumPages)
	{
		str_format(aMsg, sizeof(aMsg), "Invalid page number. There %s %d %s available.", NumPages == 1 ? "is" : "are", NumPages, NumPages == 1 ? "page" : "pages");
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aMsg);
		return;
	}

	const int Start = (Page - 1) * ENTRIES_PER_PAGE;
	const int End = Page * ENTRIES_PER_PAGE;
	int Count = 0;
	for(const CBanAddr *pBan = pThis->m_BanAddrPool.First(); pBan; pBan = pBan->m_pNext, Count++)
	{
		if(Count < Start)
		{
			continue;
		}
		else if(Count >= End)
		{
			break;
		}
		pThis->MakeBanInfo(pBan, aBuf, sizeof(aBuf), MSGTYPE_LIST);
		str_format(aMsg, sizeof(aMsg), "#%i %s", Count, aBuf);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aMsg);
	}
	for(const CBanRange *pBan = pThis->m_BanRangePool.First(); pBan; pBan = pBan->m_pNext, Count++)
	{
		if(Count < Start)
		{
			continue;
		}
		else if(Count >= End)
		{
			break;
		}
		pThis->MakeBanInfo(pBan, aBuf, sizeof(aBuf), MSGTYPE_LIST);
		str_format(aMsg, sizeof(aMsg), "#%i %s", Count, aBuf);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aMsg);
	}
	str_format(aMsg, sizeof(aMsg), "%d %s, showing entries %d - %d (page %d/%d)", NumBans, NumBans == 1 ? "ban" : "bans", Start, Count - 1, Page, NumPages);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aMsg);
}

void CNetBan::ConBansFind(IConsole::IResult *pResult, void *pUser)
{
	CNetBan *pThis = static_cast<CNetBan *>(pUser);

	const char *pStr = pResult->GetString(0);
	char aBuf[256], aMsg[256];

	NETADDR Addr;
	if(net_addr_from_str(&Addr, pStr) != 0)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "bans_find error (invalid network address)");
		return;
	}

	int Count = 0;
	int Found = 0;
	// Check first for bans
	for(CBanAddr *pBan = pThis->m_BanAddrPool.First(); pBan; pBan = pBan->m_pNext, Count++)
	{
		if(NetComp(&pBan->m_Data, &Addr) == 0)
		{
			pThis->MakeBanInfo(pBan, aBuf, sizeof(aBuf), MSGTYPE_LIST);
			str_format(aMsg, sizeof(aMsg), "#%i %s", Count, aBuf);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aMsg);

			Found++;
		}
	}
	// check ban ranges
	for(CBanRange *pBan = pThis->m_BanRangePool.First(); pBan; pBan = pBan->m_pNext, Count++)
	{
		if(pThis->NetMatch(&pBan->m_Data, &Addr))
		{
			pThis->MakeBanInfo(pBan, aBuf, sizeof(aBuf), MSGTYPE_LIST);
			str_format(aMsg, sizeof(aMsg), "#%i %s", Count, aBuf);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aMsg);

			Found++;
		}
	}

	if(Found)
		str_format(aMsg, sizeof(aMsg), "%i ban records found.", Found);
	else
		str_copy(aMsg, "No ban records found.", sizeof(aMsg));

	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aMsg);
}

void CNetBan::ConBansSave(IConsole::IResult *pResult, void *pUser)
{
	CNetBan *pThis = static_cast<CNetBan *>(pUser);

	char aBuf[256];
	IOHANDLE File = pThis->Storage()->OpenFile(pResult->GetString(0), IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		str_format(aBuf, sizeof(aBuf), "failed to save banlist to '%s'", pResult->GetString(0));
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aBuf);
		return;
	}

	int64_t Now = time_timestamp();
	char aAddrStr1[NETADDR_MAXSTRSIZE], aAddrStr2[NETADDR_MAXSTRSIZE];
	for(CBanAddr *pBan = pThis->m_BanAddrPool.First(); pBan; pBan = pBan->m_pNext)
	{
		int Min = pBan->m_Info.m_Expires > -1 ? (pBan->m_Info.m_Expires - Now + 59) / 60 : -1;
		net_addr_str(&pBan->m_Data, aAddrStr1, sizeof(aAddrStr1), false);
		str_format(aBuf, sizeof(aBuf), "ban %s %i %s", aAddrStr1, Min, pBan->m_Info.m_aReason);
		io_write(File, aBuf, str_length(aBuf));
		io_write_newline(File);
	}
	for(CBanRange *pBan = pThis->m_BanRangePool.First(); pBan; pBan = pBan->m_pNext)
	{
		int Min = pBan->m_Info.m_Expires > -1 ? (pBan->m_Info.m_Expires - Now + 59) / 60 : -1;
		net_addr_str(&pBan->m_Data.m_LB, aAddrStr1, sizeof(aAddrStr1), false);
		net_addr_str(&pBan->m_Data.m_UB, aAddrStr2, sizeof(aAddrStr2), false);
		str_format(aBuf, sizeof(aBuf), "ban_range %s %s %i %s", aAddrStr1, aAddrStr2, Min, pBan->m_Info.m_aReason);
		io_write(File, aBuf, str_length(aBuf));
		io_write_newline(File);
	}

	io_close(File);
	str_format(aBuf, sizeof(aBuf), "saved banlist to '%s'", pResult->GetString(0));
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aBuf);
}

void CNetBan::ScheduleNextPersistentSync()
{
	m_NextPersistentSync = time_get() + time_freq();
}

bool CNetBan::ResolvePersistentBanPath(char *pPath, unsigned PathSize) const
{
	if(!m_pStorage)
		return false;

	const char *pConfigPath = g_Config.m_SvPersistentBansFile;
	if(pConfigPath[0] == '\0')
		return false;

	if(fs_is_relative_path(pConfigPath))
	{
		m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, pConfigPath, pPath, PathSize);
	}
	else
	{
		str_copy(pPath, pConfigPath, PathSize);
	}
	return true;
}

void CNetBan::EnsurePersistentDir(const char *pPath) const
{
	fs_makedir_rec_for(pPath);
}

void CNetBan::UpdatePersistentBanTimestamp(const char *pPath)
{
	time_t Created = 0, Modified = 0;
	if(fs_file_time(pPath, &Created, &Modified) == 0)
		m_PersistentFileLastWrite = Modified;
	else
		m_PersistentFileLastWrite = 0;
}

void CNetBan::SyncPersistentBans(bool Force)
{
	if(g_Config.m_SvPersistentBansFile[0] == '\0')
	{
		ScheduleNextPersistentSync();
		return;
	}

	char aPath[IO_MAX_PATH_LENGTH];
	if(!ResolvePersistentBanPath(aPath, sizeof(aPath)))
	{
		ScheduleNextPersistentSync();
		return;
	}

	time_t Created = 0, Modified = 0;
	const bool HasFile = fs_file_time(aPath, &Created, &Modified) == 0;
	if(!Force && HasFile && m_PersistentFileLastWrite != 0 && Modified <= m_PersistentFileLastWrite)
	{
		ScheduleNextPersistentSync();
		return;
	}

	std::map<std::string, std::string> Parsed;
	if(HasFile)
	{
		IOHANDLE File = io_open(aPath, IOFLAG_READ);
		if(File)
		{
			CLineReader Reader;
			if(Reader.OpenFile(File))
			{
				while(const char *pLine = Reader.Get())
				{
					if(!pLine || pLine[0] == 0 || pLine[0] == '#')
						continue;
					char aLine[1024];
					str_copy(aLine, pLine, sizeof(aLine));
					char aIp[NETADDR_MAXSTRSIZE];
					const char *pRest = str_next_token(aLine, " \t", aIp, sizeof(aIp));
					if(aIp[0] == 0)
						continue;
					const char *pReason = pRest ? str_skip_whitespaces_const(pRest) : nullptr;
					char aReason[CBanInfo::REASON_LENGTH];
					if(pReason && pReason[0])
						str_copy(aReason, pReason, sizeof(aReason));
					else
						str_copy(aReason, "No reason given", sizeof(aReason));
					str_sanitize_cc(aReason);
					Parsed[aIp] = aReason;
				}
			}
			else
				io_close(File);
		}
	}

	std::vector<std::pair<std::string, std::string>> Added;
	std::vector<std::pair<std::string, std::string>> Updated;
	std::vector<std::string> Removed;

	for(const auto &Entry : Parsed)
	{
		auto ItOld = m_PersistentBans.find(Entry.first);
		if(ItOld == m_PersistentBans.end())
			Added.push_back(Entry);
		else if(ItOld->second != Entry.second)
			Updated.push_back(Entry);
	}
	for(const auto &Entry : m_PersistentBans)
	{
		if(!Parsed.count(Entry.first))
			Removed.push_back(Entry.first);
	}

	if(!Added.empty() || !Updated.empty())
	{
		m_LoadingPersistentBans = true;
		for(const auto &Entry : Added)
		{
			NETADDR Addr;
			if(net_addr_from_str(&Addr, Entry.first.c_str()) == 0)
				BanAddr(&Addr, 0, Entry.second.c_str(), false);
		}
		for(const auto &Entry : Updated)
		{
			NETADDR Addr;
			if(net_addr_from_str(&Addr, Entry.first.c_str()) == 0)
				BanAddr(&Addr, 0, Entry.second.c_str(), false);
		}
		m_LoadingPersistentBans = false;
	}

	if(!Removed.empty())
	{
		m_SyncingPersistentBans = true;
		for(const auto &Ip : Removed)
		{
			NETADDR Addr;
			if(net_addr_from_str(&Addr, Ip.c_str()) == 0)
				UnbanByAddr(&Addr);
		}
		m_SyncingPersistentBans = false;
	}

	m_PersistentBans = std::move(Parsed);
	if(HasFile)
		UpdatePersistentBanTimestamp(aPath);
	else
		m_PersistentFileLastWrite = 0;

	ScheduleNextPersistentSync();
}

bool CNetBan::RewritePersistentBanFile()
{
	if(g_Config.m_SvPersistentBansFile[0] == '\0')
		return false;

	char aPath[IO_MAX_PATH_LENGTH];
	if(!ResolvePersistentBanPath(aPath, sizeof(aPath)))
		return false;

	EnsurePersistentDir(aPath);
	IOHANDLE File = io_open(aPath, IOFLAG_WRITE);
	if(!File)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "failed to open persistent ban file for writing");
		return false;
	}

	for(const auto &Entry : m_PersistentBans)
	{
		char aLine[NETADDR_MAXSTRSIZE + CBanInfo::REASON_LENGTH + 2];
		str_format(aLine, sizeof(aLine), "%s %s", Entry.first.c_str(), Entry.second.c_str());
		io_write(File, aLine, str_length(aLine));
		io_write_newline(File);
	}

	io_close(File);
	UpdatePersistentBanTimestamp(aPath);
	return true;
}

void CNetBan::AppendPersistentBan(const NETADDR *pAddr, const char *pReason)
{
	if(g_Config.m_SvPersistentBansFile[0] == '\0')
		return;

	char aPath[IO_MAX_PATH_LENGTH];
	if(!ResolvePersistentBanPath(aPath, sizeof(aPath)))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "failed to resolve persistent ban file path");
		return;
	}

	char aIp[NETADDR_MAXSTRSIZE];
	net_addr_str(pAddr, aIp, sizeof(aIp), false);

	char aReason[CBanInfo::REASON_LENGTH];
	SanitizeReason(pReason, aReason, sizeof(aReason));

	auto It = m_PersistentBans.find(aIp);
	if(It != m_PersistentBans.end() && It->second == aReason)
		return;

	m_PersistentBans[aIp] = aReason;

	EnsurePersistentDir(aPath);
	IOHANDLE File = io_open(aPath, IOFLAG_APPEND);
	if(!File)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "failed to open persistent ban file for append");
		m_PersistentBans.erase(aIp);
		return;
	}

	char aLine[NETADDR_MAXSTRSIZE + CBanInfo::REASON_LENGTH + 2];
	str_format(aLine, sizeof(aLine), "%s %s", aIp, aReason);
	io_write(File, aLine, str_length(aLine));
	io_write_newline(File);
	io_close(File);

	UpdatePersistentBanTimestamp(aPath);
	char aLog[256];
	str_format(aLog, sizeof(aLog), "persistent ban file updated at '%s'", aPath);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", aLog);
}

void CNetBan::RemovePersistentBan(const NETADDR *pAddr)
{
	if(g_Config.m_SvPersistentBansFile[0] == '\0')
		return;
	if(m_SyncingPersistentBans)
		return;

	char aIp[NETADDR_MAXSTRSIZE];
	net_addr_str(pAddr, aIp, sizeof(aIp), false);
	auto It = m_PersistentBans.find(aIp);
	if(It == m_PersistentBans.end())
		return;

	m_PersistentBans.erase(It);
	RewritePersistentBanFile();
}

void CNetBan::SanitizeReason(const char *pReason, char *pOut, int OutSize) const
{
	if(pReason && pReason[0])
		str_copy(pOut, pReason, OutSize);
	else
		str_copy(pOut, "No reason given", OutSize);
	str_sanitize_cc(pOut);
	if(!str_find(pOut, PERSISTENT_REASON_SUFFIX))
		str_append(pOut, PERSISTENT_REASON_SUFFIX, OutSize);
}

void CNetBan::HandlePersistentBan(const NETADDR *pAddr, int Seconds, const char *pReason, int Result)
{
	if(Result < 0 || Seconds > 0 || m_LoadingPersistentBans || m_SyncingPersistentBans)
		return;
	if(g_Config.m_SvPersistentBansFile[0] == '\0')
		return;

	char aReason[CBanInfo::REASON_LENGTH];
	SanitizeReason(pReason, aReason, sizeof(aReason));

	if(Result == 0)
	{
		AppendPersistentBan(pAddr, aReason);
		return;
	}

	char aIp[NETADDR_MAXSTRSIZE];
	net_addr_str(pAddr, aIp, sizeof(aIp), false);
	auto It = m_PersistentBans.find(aIp);
	if(It == m_PersistentBans.end())
	{
		m_PersistentBans[aIp] = aReason;
		RewritePersistentBanFile();
		return;
	}

	if(It->second != aReason)
	{
		It->second = aReason;
		RewritePersistentBanFile();
	}
}
