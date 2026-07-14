#include "menus.h"

#include <base/log.h>
#include <base/system.h>

#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>

using namespace std::chrono_literals;

typedef std::function<void()> TMenuAssetScanLoadedFunc;

struct SMenuAssetScanUser
{
	void *m_pUser;
	TMenuAssetScanLoadedFunc m_LoadedFunc;
};

// IDs of the tabs in the Assets menu
enum
{
	ASSETS_TAB_ENTITIES = 0,
	ASSETS_TAB_GAME = 1,
	ASSETS_TAB_EMOTICONS = 2,
	ASSETS_TAB_PARTICLES = 3,
	ASSETS_TAB_HUD = 4,
	ASSETS_TAB_EXTRAS = 5,
	ASSETS_TAB_CURSOR = 6,
	ASSETS_TAB_ARROW = 7,
	ASSETS_TAB_AUDIO = 8,
	NUMBER_OF_ASSETS_TABS = 9,
};

static int FavoriteAssetTabFromString(const char *pTab)
{
	if(str_comp_nocase(pTab, "entities") == 0)
		return ASSETS_TAB_ENTITIES;
	if(str_comp_nocase(pTab, "game") == 0)
		return ASSETS_TAB_GAME;
	if(str_comp_nocase(pTab, "emoticons") == 0)
		return ASSETS_TAB_EMOTICONS;
	if(str_comp_nocase(pTab, "particles") == 0)
		return ASSETS_TAB_PARTICLES;
	if(str_comp_nocase(pTab, "hud") == 0)
		return ASSETS_TAB_HUD;
	if(str_comp_nocase(pTab, "extras") == 0)
		return ASSETS_TAB_EXTRAS;
	if(str_comp_nocase(pTab, "cursor") == 0)
		return ASSETS_TAB_CURSOR;
	if(str_comp_nocase(pTab, "arrow") == 0)
		return ASSETS_TAB_ARROW;
	if(str_comp_nocase(pTab, "audio") == 0)
		return ASSETS_TAB_AUDIO;
	return -1;
}

static const char *FavoriteAssetTabToString(int Tab)
{
	switch(Tab)
	{
	case ASSETS_TAB_ENTITIES:
		return "entities";
	case ASSETS_TAB_GAME:
		return "game";
	case ASSETS_TAB_EMOTICONS:
		return "emoticons";
	case ASSETS_TAB_PARTICLES:
		return "particles";
	case ASSETS_TAB_HUD:
		return "hud";
	case ASSETS_TAB_EXTRAS:
		return "extras";
	case ASSETS_TAB_CURSOR:
		return "cursor";
	case ASSETS_TAB_ARROW:
		return "arrow";
	case ASSETS_TAB_AUDIO:
		return "audio";
	default:
		return "";
	}
}

void CMenus::LoadEntities(SCustomEntities *pEntitiesItem, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pEntitiesItem->m_aName, "default") == 0)
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
	else
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pEntitiesItem->m_aName, gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(pEntitiesItem->m_aImages[i].m_Texture.IsNullTexture())
			{
				str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pEntitiesItem->m_aName);
				pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			}
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
}

int CMenus::EntitiesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;

		SCustomEntities EntitiesItem;
		str_copy(EntitiesItem.m_aName, pName);
		CMenus::LoadEntities(&EntitiesItem, pUser);
		pThis->m_vEntitiesList.push_back(EntitiesItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;

			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, aName);
			CMenus::LoadEntities(&EntitiesItem, pUser);
			pThis->m_vEntitiesList.push_back(EntitiesItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

template<typename TName>
static void LoadAsset(TName *pAssetItem, const char *pAssetName, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pAssetItem->m_aName, "default") == 0)
	{
		str_format(aPath, sizeof(aPath), "%s.png", pAssetName);
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pAssetName, pAssetItem->m_aName);
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		if(pAssetItem->m_RenderTexture.IsNullTexture())
		{
			str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pAssetName, pAssetItem->m_aName, pAssetName);
			pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		}
	}
}

template<typename TName>
static int AssetScan(const char *pName, int IsDir, int DirType, std::vector<TName> &vAssetList, const char *pAssetName, IGraphics *pGraphics, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;

		TName AssetItem;
		str_copy(AssetItem.m_aName, pName);
		LoadAsset(&AssetItem, pAssetName, pGraphics);
		vAssetList.push_back(AssetItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;

			TName AssetItem;
			str_copy(AssetItem.m_aName, aName);
			LoadAsset(&AssetItem, pAssetName, pGraphics);
			vAssetList.push_back(AssetItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

int CMenus::GameScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vGameList, "game", pGraphics, pUser);
}

int CMenus::EmoticonsScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vEmoticonList, "emoticons", pGraphics, pUser);
}

int CMenus::ParticlesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vParticlesList, "particles", pGraphics, pUser);
}

int CMenus::HudScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vHudList, "hud", pGraphics, pUser);
}

int CMenus::ExtrasScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vExtrasList, "extras", pGraphics, pUser);
}

static void LoadCursorPreview(CMenus::SCustomCursor *pCursorItem, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pCursorItem->m_aName, "default") == 0)
	{
		pCursorItem->m_RenderTexture = g_pData->m_aImages[IMAGE_CURSOR].m_Id;
		return;
	}

	str_format(aPath, sizeof(aPath), "assets/cursor/%s.png", pCursorItem->m_aName);
	pCursorItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	if(pCursorItem->m_RenderTexture.IsNullTexture())
	{
		str_format(aPath, sizeof(aPath), "assets/cursor/%s/gui_cursor.png", pCursorItem->m_aName);
		pCursorItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		if(pCursorItem->m_RenderTexture.IsNullTexture())
		{
			str_format(aPath, sizeof(aPath), "assets/cursor/%s/cursor.png", pCursorItem->m_aName);
			pCursorItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		}
	}
}

int CMenus::CursorScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();

	auto Exists = [&](const char *pItemName) {
		for(const auto &Item : pThis->m_vCursorList)
		{
			if(str_comp(Item.m_aName, pItemName) == 0)
				return true;
		}
		return false;
	};

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;
		if(str_comp(pName, "default") == 0)
			return 0;
		if(Exists(pName))
			return 0;

		SCustomCursor CursorItem;
		str_copy(CursorItem.m_aName, pName);
		LoadCursorPreview(&CursorItem, pGraphics);
		pThis->m_vCursorList.push_back(CursorItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			if(str_comp(aName, "default") == 0)
				return 0;
			if(Exists(aName))
				return 0;

			SCustomCursor CursorItem;
			str_copy(CursorItem.m_aName, aName);
			LoadCursorPreview(&CursorItem, pGraphics);
			pThis->m_vCursorList.push_back(CursorItem);
		}
	}

	pRealUser->m_LoadedFunc();
	return 0;
}

static void LoadArrowPreview(CMenus::SCustomArrow *pArrowItem, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pArrowItem->m_aName, "default") == 0)
	{
		pArrowItem->m_RenderTexture = g_pData->m_aImages[IMAGE_ARROW].m_Id;
		return;
	}

	str_format(aPath, sizeof(aPath), "assets/arrow/%s.png", pArrowItem->m_aName);
	pArrowItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	if(pArrowItem->m_RenderTexture.IsNullTexture())
	{
		str_format(aPath, sizeof(aPath), "assets/arrow/%s/arrow.png", pArrowItem->m_aName);
		pArrowItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
}

int CMenus::ArrowScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();

	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;
		if(str_comp(pName, "default") == 0)
			return 0;

		SCustomArrow ArrowItem;
		str_copy(ArrowItem.m_aName, pName);
		LoadArrowPreview(&ArrowItem, pGraphics);
		pThis->m_vArrowList.push_back(ArrowItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			if(str_comp(aName, "default") == 0)
				return 0;

			SCustomArrow ArrowItem;
			str_copy(ArrowItem.m_aName, aName);
			LoadArrowPreview(&ArrowItem, pGraphics);
			pThis->m_vArrowList.push_back(ArrowItem);
		}
	}

	pRealUser->m_LoadedFunc();
	return 0;
}

static bool AudioPackExists(const std::vector<CMenus::SCustomAudioPack> &vList, const char *pName)
{
	for(const auto &Item : vList)
	{
		if(str_comp(Item.m_aName, pName) == 0)
			return true;
	}
	return false;
}

int CMenus::AudioPackScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	if(!IsDir || pName[0] == '.')
		return 0;
	if(str_comp(pName, "default") == 0)
		return 0;
	if(AudioPackExists(pThis->m_vAudioPackList, pName))
		return 0;

	SCustomAudioPack PackItem;
	str_copy(PackItem.m_aName, pName);
	PackItem.m_RenderTexture = IGraphics::CTextureHandle();
	pThis->m_vAudioPackList.push_back(PackItem);

	pRealUser->m_LoadedFunc();
	return 0;
}

static void ClearCursorAssetList(std::vector<CMenus::SCustomCursor> &vList, IGraphics *pGraphics)
{
	for(CMenus::SCustomCursor &Asset : vList)
	{
		if(str_comp(Asset.m_aName, "default") == 0)
			continue;
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

static void ClearArrowAssetList(std::vector<CMenus::SCustomArrow> &vList, IGraphics *pGraphics)
{
	for(CMenus::SCustomArrow &Asset : vList)
	{
		if(str_comp(Asset.m_aName, "default") == 0)
			continue;
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

static std::vector<const CMenus::SCustomEntities *> gs_vpSearchEntitiesList;
static std::vector<const CMenus::SCustomGame *> gs_vpSearchGamesList;
static std::vector<const CMenus::SCustomEmoticon *> gs_vpSearchEmoticonsList;
static std::vector<const CMenus::SCustomParticle *> gs_vpSearchParticlesList;
static std::vector<const CMenus::SCustomHud *> gs_vpSearchHudList;
static std::vector<const CMenus::SCustomExtras *> gs_vpSearchExtrasList;
static std::vector<const CMenus::SCustomCursor *> gs_vpSearchCursorList;
static std::vector<const CMenus::SCustomArrow *> gs_vpSearchArrowList;
static std::vector<const CMenus::SCustomAudioPack *> gs_vpSearchAudioPackList;

static bool gs_aInitCustomList[NUMBER_OF_ASSETS_TABS] = {
	true,
};

static size_t gs_aCustomListSize[NUMBER_OF_ASSETS_TABS] = {
	0,
};

static CLineInputBuffered<64> s_aFilterInputs[NUMBER_OF_ASSETS_TABS];

static int s_CurCustomTab = ASSETS_TAB_ENTITIES;

static const CMenus::SCustomItem *GetCustomItem(int CurTab, size_t Index)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
		return gs_vpSearchEntitiesList[Index];
	else if(CurTab == ASSETS_TAB_GAME)
		return gs_vpSearchGamesList[Index];
	else if(CurTab == ASSETS_TAB_EMOTICONS)
		return gs_vpSearchEmoticonsList[Index];
	else if(CurTab == ASSETS_TAB_PARTICLES)
		return gs_vpSearchParticlesList[Index];
	else if(CurTab == ASSETS_TAB_HUD)
		return gs_vpSearchHudList[Index];
	else if(CurTab == ASSETS_TAB_EXTRAS)
		return gs_vpSearchExtrasList[Index];
	else if(CurTab == ASSETS_TAB_CURSOR)
		return gs_vpSearchCursorList[Index];
	else if(CurTab == ASSETS_TAB_ARROW)
		return gs_vpSearchArrowList[Index];
	else if(CurTab == ASSETS_TAB_AUDIO)
		return gs_vpSearchAudioPackList[Index];

	return nullptr;
}

template<typename TName>
static void ClearAssetList(std::vector<TName> &vList, IGraphics *pGraphics)
{
	for(TName &Asset : vList)
	{
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

void CMenus::ConAddFavoriteAsset(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CMenus *>(pUserData);
	pSelf->AddFavoriteAsset(pResult->GetString(0), pResult->GetString(1));
}

void CMenus::ConRemoveFavoriteAsset(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CMenus *>(pUserData);
	pSelf->RemoveFavoriteAsset(pResult->GetString(0), pResult->GetString(1));
}

void CMenus::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	auto *pSelf = static_cast<CMenus *>(pUserData);
	pSelf->OnConfigSave(pConfigManager);
}

void CMenus::OnConfigSave(IConfigManager *pConfigManager)
{
	for(int Tab = 0; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		const char *pTabName = FavoriteAssetTabToString(Tab);
		for(const auto &Favorite : m_aAssetFavorites[Tab])
		{
			char aBuffer[IO_MAX_PATH_LENGTH + 64];
			const char *pEnd = aBuffer + sizeof(aBuffer) - 2;

			str_copy(aBuffer, "add_favorite_asset \"");
			char *pDst = aBuffer + str_length(aBuffer);
			str_escape(&pDst, pTabName, pEnd);
			str_append(aBuffer, "\" \"");
			pDst = aBuffer + str_length(aBuffer);
			str_escape(&pDst, Favorite.c_str(), pEnd);
			str_append(aBuffer, "\"");

			pConfigManager->WriteLine(aBuffer, ConfigDomain::BESTCLIENT);
		}
	}
}

void CMenus::AddFavoriteAsset(const char *pTab, const char *pName)
{
	AddFavoriteAsset(FavoriteAssetTabFromString(pTab), pName);
}

void CMenus::RemoveFavoriteAsset(const char *pTab, const char *pName)
{
	RemoveFavoriteAsset(FavoriteAssetTabFromString(pTab), pName);
}

void CMenus::AddFavoriteAsset(int Tab, const char *pName)
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS)
	{
		log_error("menus", "Invalid favorite asset tab '%d'", Tab);
		return;
	}
	if(pName[0] == '\0')
	{
		return;
	}

	const auto &[_, Inserted] = m_aAssetFavorites[Tab].emplace(pName);
	if(Inserted)
	{
		gs_aInitCustomList[Tab] = true;
	}
}

void CMenus::RemoveFavoriteAsset(int Tab, const char *pName)
{
	if(Tab < 0 || Tab >= NUMBER_OF_ASSETS_TABS)
	{
		log_error("menus", "Invalid favorite asset tab '%d'", Tab);
		return;
	}

	const auto FavoriteIt = m_aAssetFavorites[Tab].find(pName);
	if(FavoriteIt != m_aAssetFavorites[Tab].end())
	{
		m_aAssetFavorites[Tab].erase(FavoriteIt);
		gs_aInitCustomList[Tab] = true;
	}
}

bool CMenus::IsFavoriteAsset(int Tab, const char *pName) const
{
	return Tab >= 0 && Tab < NUMBER_OF_ASSETS_TABS && m_aAssetFavorites[Tab].contains(pName);
}

void CMenus::ClearCustomItems(int CurTab)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
	{
		for(auto &Entity : m_vEntitiesList)
		{
			for(auto &Image : Entity.m_aImages)
			{
				Graphics()->UnloadTexture(&Image.m_Texture);
			}
		}
		m_vEntitiesList.clear();

		// reload current entities
		GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
	}
	else if(CurTab == ASSETS_TAB_GAME)
	{
		ClearAssetList(m_vGameList, Graphics());

		// reload current game skin
		GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
	}
	else if(CurTab == ASSETS_TAB_EMOTICONS)
	{
		ClearAssetList(m_vEmoticonList, Graphics());

		// reload current emoticons skin
		GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
	}
	else if(CurTab == ASSETS_TAB_PARTICLES)
	{
		ClearAssetList(m_vParticlesList, Graphics());

		// reload current particles skin
		GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
	}
	else if(CurTab == ASSETS_TAB_HUD)
	{
		ClearAssetList(m_vHudList, Graphics());

		// reload current hud skin
		GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
	}
	else if(CurTab == ASSETS_TAB_EXTRAS)
	{
		ClearAssetList(m_vExtrasList, Graphics());

		// reload current DDNet particles skin
		GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
	}
	else if(CurTab == ASSETS_TAB_CURSOR)
	{
		ClearCursorAssetList(m_vCursorList, Graphics());
		GameClient()->LoadCursorAsset(g_Config.m_ClAssetCursor);
	}
	else if(CurTab == ASSETS_TAB_ARROW)
	{
		ClearArrowAssetList(m_vArrowList, Graphics());
		GameClient()->LoadArrowAsset(g_Config.m_ClAssetArrow);
	}
	else if(CurTab == ASSETS_TAB_AUDIO)
	{
		m_vAudioPackList.clear();
		GameClient()->m_Sounds.Clear();
	}
	gs_aInitCustomList[CurTab] = true;
}

template<typename TName, typename TCaller>
static void InitAssetList(std::vector<TName> &vAssetList, const char *pAssetPath, const char *pAssetName, FS_LISTDIR_CALLBACK pfnCallback, IGraphics *pGraphics, IStorage *pStorage, TCaller Caller)
{
	if(vAssetList.empty())
	{
		TName AssetItem;
		str_copy(AssetItem.m_aName, "default");
		LoadAsset(&AssetItem, pAssetName, pGraphics);
		vAssetList.push_back(AssetItem);

		// load assets
		pStorage->ListDirectory(IStorage::TYPE_ALL, pAssetPath, pfnCallback, Caller);
		std::sort(vAssetList.begin(), vAssetList.end());
	}
	if(vAssetList.size() != gs_aCustomListSize[s_CurCustomTab])
		gs_aInitCustomList[s_CurCustomTab] = true;
}

template<typename TName>
static int InitSearchList(std::vector<const TName *> &vpSearchList, std::vector<TName> &vAssetList)
{
	vpSearchList.clear();
	int ListSize = vAssetList.size();
	for(int i = 0; i < ListSize; ++i)
	{
		const TName *pAsset = &vAssetList[i];

		// filter quick search
		if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pAsset->m_aName, s_aFilterInputs[s_CurCustomTab].GetString()))
			continue;

		vpSearchList.push_back(pAsset);
	}
	return vAssetList.size();
}

void CMenus::RenderSettingsCustom(CUIRect MainView)
{
	CUIRect TabBar, CustomList, QuickSearch, DirectoryButton, ReloadButton;
	static bool s_EntityGamePreview = true;
	auto SortSearchList = [this](auto &vpSearchList) {
		std::sort(vpSearchList.begin(), vpSearchList.end(), [this](const auto *pLeft, const auto *pRight) {
			const bool LeftFavorite = IsFavoriteAsset(s_CurCustomTab, pLeft->m_aName);
			const bool RightFavorite = IsFavoriteAsset(s_CurCustomTab, pRight->m_aName);
			if(LeftFavorite != RightFavorite)
			{
				return LeftFavorite;
			}
			return str_comp(pLeft->m_aName, pRight->m_aName) < 0;
		});
	};

	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / static_cast<float>(NUMBER_OF_ASSETS_TABS);
	static CButtonContainer s_aPageTabs[NUMBER_OF_ASSETS_TABS] = {};
	const char *apTabNames[NUMBER_OF_ASSETS_TABS] = {
		Localize("Entities"),
		Localize("Game"),
		Localize("Emoticons"),
		Localize("Particles"),
		Localize("HUD"),
		Localize("Extras"),
		Localize("Cursor"),
		Localize("Arrow"),
		Localize("Audio")};

	for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		CUIRect Button;
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == ASSETS_TAB_ENTITIES ? IGraphics::CORNER_L : (Tab == NUMBER_OF_ASSETS_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			s_CurCustomTab = Tab;
		}
	}

	auto LoadStartTime = time_get_nanoseconds();
	SMenuAssetScanUser User;
	User.m_pUser = this;
	User.m_LoadedFunc = [&]() {
		if(time_get_nanoseconds() - LoadStartTime > 500ms)
			RenderLoading(Localize("Loading assets"), "", 0);
	};
	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		if(m_vEntitiesList.empty())
		{
			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, "default");
			LoadEntities(&EntitiesItem, &User);
			m_vEntitiesList.push_back(EntitiesItem);

			// load entities
			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/entities", EntitiesScan, &User);
			std::sort(m_vEntitiesList.begin(), m_vEntitiesList.end());
		}
		if(m_vEntitiesList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		InitAssetList(m_vGameList, "assets/game", "game", GameScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		InitAssetList(m_vEmoticonList, "assets/emoticons", "emoticons", EmoticonsScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		InitAssetList(m_vParticlesList, "assets/particles", "particles", ParticlesScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		InitAssetList(m_vHudList, "assets/hud", "hud", HudScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		InitAssetList(m_vExtrasList, "assets/extras", "extras", ExtrasScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
	{
		if(m_vCursorList.empty())
		{
			SCustomCursor CursorItem;
			str_copy(CursorItem.m_aName, "default");
			LoadCursorPreview(&CursorItem, Graphics());
			m_vCursorList.push_back(CursorItem);

			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/cursor", CursorScan, &User);
			std::sort(m_vCursorList.begin(), m_vCursorList.end());
		}
		if(m_vCursorList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else if(s_CurCustomTab == ASSETS_TAB_ARROW)
	{
		InitAssetList(m_vArrowList, "assets/arrow", "arrow", ArrowScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
	{
		if(m_vAudioPackList.empty())
		{
			SCustomAudioPack DefaultItem;
			str_copy(DefaultItem.m_aName, "default");
			DefaultItem.m_RenderTexture = IGraphics::CTextureHandle();
			m_vAudioPackList.push_back(DefaultItem);

			Storage()->ListDirectory(IStorage::TYPE_SAVE, "assets/audio", AudioPackScan, &User);
			Storage()->ListDirectory(IStorage::TYPE_SAVE, "audio", AudioPackScan, &User);
			std::sort(m_vAudioPackList.begin(), m_vAudioPackList.end());
		}
		if(m_vAudioPackList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	// skin selector
	MainView.HSplitTop(MainView.h - 10.0f - ms_ButtonHeight, &CustomList, &MainView);
	if(gs_aInitCustomList[s_CurCustomTab])
	{
		int ListSize = 0;
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			gs_vpSearchEntitiesList.clear();
			ListSize = m_vEntitiesList.size();
			for(int i = 0; i < ListSize; ++i)
			{
				const SCustomEntities *pEntity = &m_vEntitiesList[i];

				// filter quick search
				if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pEntity->m_aName, s_aFilterInputs[s_CurCustomTab].GetString()))
					continue;

				gs_vpSearchEntitiesList.push_back(pEntity);
			}
			SortSearchList(gs_vpSearchEntitiesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			ListSize = InitSearchList(gs_vpSearchGamesList, m_vGameList);
			SortSearchList(gs_vpSearchGamesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			ListSize = InitSearchList(gs_vpSearchEmoticonsList, m_vEmoticonList);
			SortSearchList(gs_vpSearchEmoticonsList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			ListSize = InitSearchList(gs_vpSearchParticlesList, m_vParticlesList);
			SortSearchList(gs_vpSearchParticlesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			ListSize = InitSearchList(gs_vpSearchHudList, m_vHudList);
			SortSearchList(gs_vpSearchHudList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			ListSize = InitSearchList(gs_vpSearchExtrasList, m_vExtrasList);
			SortSearchList(gs_vpSearchExtrasList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
		{
			ListSize = InitSearchList(gs_vpSearchCursorList, m_vCursorList);
			SortSearchList(gs_vpSearchCursorList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_ARROW)
		{
			ListSize = InitSearchList(gs_vpSearchArrowList, m_vArrowList);
			SortSearchList(gs_vpSearchArrowList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			ListSize = InitSearchList(gs_vpSearchAudioPackList, m_vAudioPackList);
			SortSearchList(gs_vpSearchAudioPackList);
		}
		gs_aInitCustomList[s_CurCustomTab] = false;
		gs_aCustomListSize[s_CurCustomTab] = ListSize;
	}

	int OldSelected = -1;
	float Margin = 10;
	float TextureWidth = 150;
	float TextureHeight = 150;

	size_t SearchListSize = 0;

	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		SearchListSize = gs_vpSearchEntitiesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		SearchListSize = gs_vpSearchGamesList.size();
		TextureHeight = 75;
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		SearchListSize = gs_vpSearchEmoticonsList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		SearchListSize = gs_vpSearchParticlesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		SearchListSize = gs_vpSearchHudList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		SearchListSize = gs_vpSearchExtrasList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
	{
		SearchListSize = gs_vpSearchCursorList.size();
		TextureHeight = 64;
		TextureWidth = 64;
	}
	else if(s_CurCustomTab == ASSETS_TAB_ARROW)
	{
		SearchListSize = gs_vpSearchArrowList.size();
		TextureHeight = 64;
		TextureWidth = 64;
	}
	else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
	{
		SearchListSize = gs_vpSearchAudioPackList.size();
		TextureHeight = 0;
		TextureWidth = 0;
	}

	static CListBox s_ListBox;
	const float ItemHeight = s_CurCustomTab == ASSETS_TAB_AUDIO ? 28.0f : (TextureHeight + 15.0f + 10.0f + Margin);
	const int ItemsPerRow = s_CurCustomTab == ASSETS_TAB_AUDIO ? 1 : maximum(1, (int)(CustomList.w / (Margin + TextureWidth)));
	s_ListBox.DoStart(ItemHeight, SearchListSize, ItemsPerRow, 1, OldSelected, &CustomList, false);
	for(size_t i = 0; i < SearchListSize; ++i)
	{
		const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, i);
		if(pItem == nullptr)
			continue;
		const bool Favorite = IsFavoriteAsset(s_CurCustomTab, pItem->m_aName);

		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetsEntities) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetGame) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetEmoticons) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetParticles) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetHud) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetExtras) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetCursor) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_ARROW)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetArrow) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			if(str_comp(pItem->m_aName, g_Config.m_SndPack) == 0)
				OldSelected = i;
		}

		const CListboxItem Item = s_ListBox.DoNextItem(pItem, OldSelected >= 0 && (size_t)OldSelected == i);
		CUIRect ItemRect = Item.m_Rect;
		ItemRect.Margin(Margin / 2, &ItemRect);
		if(!Item.m_Visible)
			continue;

		CUIRect FavoriteButton;
		ItemRect.HSplitTop(20.0f, &FavoriteButton, nullptr);
		FavoriteButton.VSplitRight(20.0f, nullptr, &FavoriteButton);

		if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			CUIRect LabelRect = ItemRect;
			LabelRect.VSplitRight(24.0f, &LabelRect, nullptr);
			Ui()->DoLabel(&LabelRect, pItem->m_aName, 14.0f, TEXTALIGN_ML);
		}
		else
		{
			CUIRect TextureRect;
			ItemRect.HSplitTop(15, &ItemRect, &TextureRect);
			TextureRect.HSplitTop(10, nullptr, &TextureRect);
			Ui()->DoLabel(&ItemRect, pItem->m_aName, ItemRect.h - 2, TEXTALIGN_MC);
			if(s_CurCustomTab == ASSETS_TAB_ENTITIES && s_EntityGamePreview)
			{
				const auto *pEntitiesItem = static_cast<const SCustomEntities *>(pItem);
				IGraphics::CTextureHandle Tex;
				for(int m = 0; m < MAP_IMAGE_MOD_TYPE_COUNT && !Tex.IsValid(); m++)
					Tex = pEntitiesItem->m_aImages[m].m_Texture;
				if(!Tex.IsValid())
					Tex = pItem->m_RenderTexture;

				if(Tex.IsValid())
				{
					// Game-like preview: hookable/unhookable walls + freeze/death/unfreeze tiles
					static const int COLS = 7, ROWS = 7;
					static const unsigned char aLayout[ROWS][COLS] = {
						{TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID, TILE_SOLID},
						{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
						{TILE_SOLID, TILE_FREEZE, 0, 0, 0, 0, TILE_NOHOOK},
						{TILE_SOLID, 0, TILE_DEATH, 0, TILE_UNFREEZE, 0, TILE_NOHOOK},
						{TILE_SOLID, 0, 0, 0, 0, TILE_DFREEZE, TILE_NOHOOK},
						{TILE_SOLID, 0, 0, 0, 0, 0, TILE_NOHOOK},
						{TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK, TILE_NOHOOK},
					};

					float TileSize = TextureWidth / (float)COLS;
					float OffX = TextureRect.x + (TextureRect.w - TextureWidth) / 2.0f;
					float OffY = TextureRect.y + (TextureRect.h - ROWS * TileSize) / 2.0f;

					// inset UVs by ~1.5px to avoid bilinear bleeding at tile boundaries
					const float KInset = 1.5f / 1024.0f;
					const float KTile = 1.0f / 16.0f;

					Graphics()->WrapClamp();
					Graphics()->TextureSet(Tex);
					Graphics()->QuadsBegin();
					Graphics()->SetColor(1, 1, 1, 1);
					for(int r = 0; r < ROWS; r++)
					{
						for(int c = 0; c < COLS; c++)
						{
							unsigned char Tile = aLayout[r][c];
							if(Tile == 0)
								continue;
							int Tx = Tile % 16;
							int Ty = Tile / 16;
							float U0 = Tx * KTile + KInset;
							float V0 = Ty * KTile + KInset;
							float U1 = U0 + KTile - KInset * 2;
							float V1 = V0 + KTile - KInset * 2;
							Graphics()->QuadsSetSubset(U0, V0, U1, V1);
							IGraphics::CQuadItem Q(OffX + c * TileSize, OffY + r * TileSize, TileSize, TileSize);
							Graphics()->QuadsDrawTL(&Q, 1);
						}
					}
					Graphics()->QuadsEnd();
					Graphics()->WrapNormal();
				}
			}
			else if(pItem->m_RenderTexture.IsValid())
			{
				Graphics()->WrapClamp();
				Graphics()->TextureSet(pItem->m_RenderTexture);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(1, 1, 1, 1);
				IGraphics::CQuadItem QuadItem(TextureRect.x + (TextureRect.w - TextureWidth) / 2, TextureRect.y + (TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
				Graphics()->WrapNormal();
			}
		}

		if(DoButton_Favorite(&pItem->m_FavoriteButtonId, pItem, Favorite, &FavoriteButton))
		{
			if(Favorite)
			{
				RemoveFavoriteAsset(s_CurCustomTab, pItem->m_aName);
			}
			else
			{
				AddFavoriteAsset(s_CurCustomTab, pItem->m_aName);
			}
		}
		GameClient()->m_Tooltips.DoToolTip(&pItem->m_FavoriteButtonId, &FavoriteButton,
			Favorite ? Localize("Click to remove this item from your favorites.") : Localize("Click to add this item to your favorites."));
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(OldSelected != NewSelected && NewSelected >= 0)
	{
		const SCustomItem *pSelectedItem = GetCustomItem(s_CurCustomTab, NewSelected);
		if(pSelectedItem != nullptr && pSelectedItem->m_aName[0] != '\0')
		{
			if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			{
				str_copy(g_Config.m_ClAssetsEntities, pSelectedItem->m_aName);
				GameClient()->m_MapImages.ChangeEntitiesPath(pSelectedItem->m_aName);
			}
			else if(s_CurCustomTab == ASSETS_TAB_GAME)
			{
				str_copy(g_Config.m_ClAssetGame, pSelectedItem->m_aName);
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			{
				str_copy(g_Config.m_ClAssetEmoticons, pSelectedItem->m_aName);
				GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
			}
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			{
				str_copy(g_Config.m_ClAssetParticles, pSelectedItem->m_aName);
				GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
			}
			else if(s_CurCustomTab == ASSETS_TAB_HUD)
			{
				str_copy(g_Config.m_ClAssetHud, pSelectedItem->m_aName);
				GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			{
				str_copy(g_Config.m_ClAssetExtras, pSelectedItem->m_aName);
				GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
			}
			else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
			{
				str_copy(g_Config.m_ClAssetCursor, pSelectedItem->m_aName);
				GameClient()->LoadCursorAsset(g_Config.m_ClAssetCursor);
			}
			else if(s_CurCustomTab == ASSETS_TAB_ARROW)
			{
				str_copy(g_Config.m_ClAssetArrow, pSelectedItem->m_aName);
				GameClient()->LoadArrowAsset(g_Config.m_ClAssetArrow);
			}
			else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
			{
				str_copy(g_Config.m_SndPack, pSelectedItem->m_aName);
				GameClient()->m_Sounds.Clear();
			}
		}
	}

	// Quick search
	MainView.HSplitBottom(ms_ButtonHeight, &MainView, &QuickSearch);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, &DirectoryButton);
	QuickSearch.HSplitTop(5.0f, nullptr, &QuickSearch);
	if(Ui()->DoEditBox_Search(&s_aFilterInputs[s_CurCustomTab], &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		gs_aInitCustomList[s_CurCustomTab] = true;
	}

	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		CUIRect ToggleRect;
		DirectoryButton.VSplitLeft(5.0f, nullptr, &DirectoryButton);
		DirectoryButton.VSplitLeft(140.0f, &ToggleRect, &DirectoryButton);
		DirectoryButton.VSplitLeft(5.0f, nullptr, &DirectoryButton);
		ToggleRect.HSplitTop(5.0f, nullptr, &ToggleRect);
		static CButtonContainer s_EntityPreviewToggleId;
		if(DoButton_Menu(&s_EntityPreviewToggleId, Localize("Better Preview"), s_EntityGamePreview, &ToggleRect))
			s_EntityGamePreview = !s_EntityGamePreview;
		GameClient()->m_Tooltips.DoToolTip(&s_EntityPreviewToggleId, &ToggleRect, Localize("Toggle between game scene preview and raw texture"));
	}

	DirectoryButton.HSplitTop(5.0f, nullptr, &DirectoryButton);
	DirectoryButton.VSplitRight(255.0f, nullptr, &DirectoryButton);
	DirectoryButton.VSplitRight(25.0f, &DirectoryButton, &ReloadButton);
	DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);

	CUIRect ShareButton;
	DirectoryButton.VSplitLeft(70.0f, &ShareButton, &DirectoryButton);
	DirectoryButton.VSplitLeft(10.0f, nullptr, &DirectoryButton);
	static CButtonContainer s_ShareAssetId;
	if(DoButton_Menu(&s_ShareAssetId, Localize("Share"), 0, &ShareButton))
	{
		if(s_CurCustomTab == ASSETS_TAB_AUDIO)
			GameClient()->m_Chat.Echo(Localize("Audio packs cannot be shared."));
		else
			OpenShareAssetPopup(s_CurCustomTab);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_ShareAssetId, &ShareButton, Localize("Share the selected asset with a player on this server"));

	static CButtonContainer s_AssetsDirId;
	if(DoButton_Menu(&s_AssetsDirId, Localize("Assets directory"), 0, &DirectoryButton))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		char aBufFull[IO_MAX_PATH_LENGTH + 7];
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			str_copy(aBufFull, "assets/entities");
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
			str_copy(aBufFull, "assets/game");
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			str_copy(aBufFull, "assets/emoticons");
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			str_copy(aBufFull, "assets/particles");
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
			str_copy(aBufFull, "assets/hud");
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			str_copy(aBufFull, "assets/extras");
		else if(s_CurCustomTab == ASSETS_TAB_CURSOR)
			str_copy(aBufFull, "assets/cursor");
		else if(s_CurCustomTab == ASSETS_TAB_ARROW)
			str_copy(aBufFull, "assets/arrow");
		else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
			str_copy(aBufFull, "assets/audio");
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aBufFull, aBuf, sizeof(aBuf));
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aBufFull, IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_AssetsDirId, &DirectoryButton, Localize("Open the directory to add custom assets"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_AssetsReloadBtnId;
	if(DoButton_Menu(&s_AssetsReloadBtnId, FontIcon::ARROW_ROTATE_RIGHT, 0, &ReloadButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		ClearCustomItems(s_CurCustomTab);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void CMenus::ConchainAssetsEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetsEntities) != 0)
		{
			pThis->GameClient()->m_MapImages.ChangeEntitiesPath(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetGame(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetGame) != 0)
		{
			pThis->GameClient()->LoadGameSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetParticles(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetParticles) != 0)
		{
			pThis->GameClient()->LoadParticlesSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetEmoticons(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetEmoticons) != 0)
		{
			pThis->GameClient()->LoadEmoticonsSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetHud(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetHud) != 0)
		{
			pThis->GameClient()->LoadHudSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetExtras(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetExtras) != 0)
		{
			pThis->GameClient()->LoadExtrasSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetCursor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetCursor) != 0)
		{
			pThis->GameClient()->LoadCursorAsset(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetArrow(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetArrow) != 0)
		{
			pThis->GameClient()->LoadArrowAsset(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainSndPack(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	char aOldSndPack[64];
	str_copy(aOldSndPack, g_Config.m_SndPack, sizeof(aOldSndPack));
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		if(str_comp(aOldSndPack, g_Config.m_SndPack) != 0)
			pThis->GameClient()->m_Sounds.Clear();
	}
}

// UClient: share the selected asset to a player on the current server
void CMenus::OpenShareAssetPopup(int Tab)
{
	const char *pName = "";
	switch(Tab)
	{
	case ASSETS_TAB_ENTITIES: pName = g_Config.m_ClAssetsEntities; break;
	case ASSETS_TAB_GAME: pName = g_Config.m_ClAssetGame; break;
	case ASSETS_TAB_EMOTICONS: pName = g_Config.m_ClAssetEmoticons; break;
	case ASSETS_TAB_PARTICLES: pName = g_Config.m_ClAssetParticles; break;
	case ASSETS_TAB_HUD: pName = g_Config.m_ClAssetHud; break;
	case ASSETS_TAB_EXTRAS: pName = g_Config.m_ClAssetExtras; break;
	case ASSETS_TAB_CURSOR: pName = g_Config.m_ClAssetCursor; break;
	case ASSETS_TAB_ARROW: pName = g_Config.m_ClAssetArrow; break;
	default: return;
	}

	m_ShareAssetTab = Tab;
	str_copy(m_aShareAssetName, pName);
	m_ShareAssetAgree = false;
	m_ShareAssetState = EShareAssetState::NONE;
	m_aShareAssetStatus[0] = '\0';
	m_aShareAssetTargetName[0] = '\0';
	if(m_pShareAssetUploadRequest)
	{
		m_pShareAssetUploadRequest->Abort();
		m_pShareAssetUploadRequest.reset();
	}

	const float Width = 420.0f;
	const float Height = 150.0f;
	const float X = (Ui()->Screen()->w - Width) / 2.0f;
	const float Y = (Ui()->Screen()->h - Height) / 2.0f;
	Ui()->DoPopupMenu(&m_ShareAssetPopupId, X, Y, Width, Height, this, PopupShareAsset);
}

bool CMenus::ResolveAssetSharePng(int Tab, const char *pName, void **ppData, unsigned *pLen) const
{
	*ppData = nullptr;
	*pLen = 0;
	if(pName[0] == '\0' || str_comp(pName, "default") == 0)
		return false;

	const char *pDir = "";
	switch(Tab)
	{
	case ASSETS_TAB_ENTITIES: pDir = "entities"; break;
	case ASSETS_TAB_GAME: pDir = "game"; break;
	case ASSETS_TAB_EMOTICONS: pDir = "emoticons"; break;
	case ASSETS_TAB_PARTICLES: pDir = "particles"; break;
	case ASSETS_TAB_HUD: pDir = "hud"; break;
	case ASSETS_TAB_EXTRAS: pDir = "extras"; break;
	case ASSETS_TAB_CURSOR: pDir = "cursor"; break;
	case ASSETS_TAB_ARROW: pDir = "arrow"; break;
	default: return false;
	}

	char aaCandidates[5][IO_MAX_PATH_LENGTH];
	int NumCandidates = 0;
	str_format(aaCandidates[NumCandidates++], IO_MAX_PATH_LENGTH, "assets/%s/%s.png", pDir, pName);
	str_format(aaCandidates[NumCandidates++], IO_MAX_PATH_LENGTH, "assets/%s/%s/%s.png", pDir, pName, pDir);
	if(Tab == ASSETS_TAB_CURSOR)
	{
		str_format(aaCandidates[NumCandidates++], IO_MAX_PATH_LENGTH, "assets/cursor/%s/gui_cursor.png", pName);
		str_format(aaCandidates[NumCandidates++], IO_MAX_PATH_LENGTH, "assets/cursor/%s/cursor.png", pName);
	}
	else if(Tab == ASSETS_TAB_ENTITIES)
	{
		str_format(aaCandidates[NumCandidates++], IO_MAX_PATH_LENGTH, "assets/entities/%s/%s.png", pName, gs_apModEntitiesNames[0]);
	}

	for(int i = 0; i < NumCandidates; i++)
	{
		if(Storage()->ReadFile(aaCandidates[i], IStorage::TYPE_ALL, ppData, pLen) && *ppData != nullptr && *pLen > 0)
			return true;
		if(*ppData != nullptr)
		{
			free(*ppData);
			*ppData = nullptr;
			*pLen = 0;
		}
	}
	return false;
}

void CMenus::BeginShareAssetUpload()
{
	if(m_aShareAssetTargetName[0] == '\0')
	{
		m_ShareAssetState = EShareAssetState::FAILED;
		str_copy(m_aShareAssetStatus, Localize("Select a player first."));
		return;
	}

	void *pData = nullptr;
	unsigned Len = 0;
	if(!ResolveAssetSharePng(m_ShareAssetTab, m_aShareAssetName, &pData, &Len))
	{
		m_ShareAssetState = EShareAssetState::FAILED;
		str_copy(m_aShareAssetStatus, Localize("Could not find the asset image file."));
		return;
	}

	if(g_Config.m_UcChatPasteUploadUrl[0] == '\0')
	{
		free(pData);
		m_ShareAssetState = EShareAssetState::FAILED;
		str_copy(m_aShareAssetStatus, Localize("Upload URL is not configured."));
		return;
	}

	std::shared_ptr<CHttpRequest> pPost = HttpPost(g_Config.m_UcChatPasteUploadUrl, (const unsigned char *)pData, Len);
	pPost->Header("Content-Type: image/png");
	pPost->Header("Accept: application/json");
	pPost->FailOnErrorStatus(false);
	pPost->MaxResponseSize(64 * 1024);
	pPost->LogProgress(HTTPLOG::FAILURE);
	free(pData);

	m_pShareAssetUploadRequest = pPost;
	m_ShareAssetState = EShareAssetState::UPLOADING;
	str_copy(m_aShareAssetStatus, Localize("Uploading..."));
	Http()->Run(pPost);
}

void CMenus::UpdateShareAssetUpload()
{
	if(!m_pShareAssetUploadRequest || !m_pShareAssetUploadRequest->Done())
		return;

	std::shared_ptr<CHttpRequest> pRequest = m_pShareAssetUploadRequest;
	m_pShareAssetUploadRequest.reset();

	char aUrl[512] = "";
	if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() >= 200 && pRequest->StatusCode() < 400)
	{
		json_value *pRoot = pRequest->ResultJson();
		if(pRoot != nullptr && pRoot->type == json_object)
		{
			const json_value &UrlValue = (*pRoot)["url"];
			if(UrlValue.type == json_string)
				str_copy(aUrl, UrlValue.u.string.ptr);
			if(aUrl[0] == '\0')
			{
				const json_value &PublicUrlValue = (*pRoot)["publicUrl"];
				if(PublicUrlValue.type == json_string)
					str_copy(aUrl, PublicUrlValue.u.string.ptr);
			}
		}
	}

	if(aUrl[0] == '\0')
	{
		m_ShareAssetState = EShareAssetState::FAILED;
		str_format(m_aShareAssetStatus, sizeof(m_aShareAssetStatus), Localize("Upload failed (HTTP %d)"), pRequest->StatusCode());
		return;
	}

	char aMsg[600];
	str_format(aMsg, sizeof(aMsg), "/w %s %s", m_aShareAssetTargetName, aUrl);
	GameClient()->m_Chat.SendChat(0, aMsg);

	m_ShareAssetState = EShareAssetState::SENT;
	str_copy(m_aShareAssetStatus, Localize("Shared!"));
	Ui()->ClosePopupMenu(&m_ShareAssetPopupId);
}

CUi::EPopupMenuFunctionResult CMenus::PopupShareAsset(void *pContext, CUIRect View, bool Active)
{
	CMenus *pThis = static_cast<CMenus *>(pContext);
	pThis->UpdateShareAssetUpload();

	const float FontSize = 14.0f;
	const float SmallFontSize = 10.0f;
	CUIRect Row;

	// gather the players currently on this server (and whether they use UClient)
	bool aUserUsesUClient[MAX_CLIENTS] = {false};
	pThis->m_vShareAssetUserNames.clear();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!pThis->GameClient()->m_aClients[i].m_Active)
			continue;
		if(pThis->GameClient()->m_aClients[i].m_aName[0] == '\0')
			continue;
		const size_t Slot = pThis->m_vShareAssetUserNames.size();
		if(Slot < MAX_CLIENTS)
			aUserUsesUClient[Slot] = pThis->GameClient()->m_ClientIndicator.IsPlayerUClient(i);
		pThis->m_vShareAssetUserNames.emplace_back(pThis->GameClient()->m_aClients[i].m_aName);
	}

	std::vector<const char *> vpNames;
	vpNames.reserve(pThis->m_vShareAssetUserNames.size());
	for(const auto &Name : pThis->m_vShareAssetUserNames)
		vpNames.push_back(Name.c_str());

	int CurSelection = -1;
	for(size_t i = 0; i < pThis->m_vShareAssetUserNames.size(); i++)
	{
		if(str_comp(pThis->m_vShareAssetUserNames[i].c_str(), pThis->m_aShareAssetTargetName) == 0)
		{
			CurSelection = (int)i;
			break;
		}
	}
	if(CurSelection < 0 && !pThis->m_vShareAssetUserNames.empty())
	{
		CurSelection = 0;
		str_copy(pThis->m_aShareAssetTargetName, pThis->m_vShareAssetUserNames[0].c_str());
	}

	// gather the shareable assets in the current tab (skip the built-in "default")
	pThis->m_vShareAssetAssetNames.clear();
	const auto AddAssetNames = [&](const auto &vList) {
		for(const auto &It : vList)
		{
			if(str_comp(It.m_aName, "default") == 0)
				continue;
			pThis->m_vShareAssetAssetNames.emplace_back(It.m_aName);
		}
	};
	switch(pThis->m_ShareAssetTab)
	{
	case ASSETS_TAB_ENTITIES: AddAssetNames(pThis->m_vEntitiesList); break;
	case ASSETS_TAB_GAME: AddAssetNames(pThis->m_vGameList); break;
	case ASSETS_TAB_EMOTICONS: AddAssetNames(pThis->m_vEmoticonList); break;
	case ASSETS_TAB_PARTICLES: AddAssetNames(pThis->m_vParticlesList); break;
	case ASSETS_TAB_HUD: AddAssetNames(pThis->m_vHudList); break;
	case ASSETS_TAB_EXTRAS: AddAssetNames(pThis->m_vExtrasList); break;
	case ASSETS_TAB_CURSOR: AddAssetNames(pThis->m_vCursorList); break;
	case ASSETS_TAB_ARROW: AddAssetNames(pThis->m_vArrowList); break;
	default: break;
	}

	std::vector<const char *> vpAssetNames;
	vpAssetNames.reserve(pThis->m_vShareAssetAssetNames.size());
	for(const auto &Name : pThis->m_vShareAssetAssetNames)
		vpAssetNames.push_back(Name.c_str());

	int AssetSelection = -1;
	for(size_t i = 0; i < pThis->m_vShareAssetAssetNames.size(); i++)
	{
		if(str_comp(pThis->m_vShareAssetAssetNames[i].c_str(), pThis->m_aShareAssetName) == 0)
		{
			AssetSelection = (int)i;
			break;
		}
	}
	if(AssetSelection < 0 && !pThis->m_vShareAssetAssetNames.empty())
	{
		AssetSelection = 0;
		str_copy(pThis->m_aShareAssetName, pThis->m_vShareAssetAssetNames[0].c_str());
	}

	// single line: Share [asset v] with [player v]
	View.HSplitTop(22.0f, &Row, &View);
	CUIRect ShareRect, R0, AssetDropRect, R1, WithRect, PlayerDropRect;
	const float AssetDropW = 100.0f;
	const float PlayerDropW = 100.0f;
	const float ShareW = pThis->TextRender()->TextWidth(FontSize, "Share ") + 4.0f;
	const float WithW = pThis->TextRender()->TextWidth(FontSize, " with ") + 4.0f;
	Row.VSplitLeft(ShareW, &ShareRect, &R0);
	R0.VSplitLeft(AssetDropW, &AssetDropRect, &R1);
	R1.VSplitLeft(WithW, &WithRect, &PlayerDropRect);
	PlayerDropRect.VSplitLeft(PlayerDropW, &PlayerDropRect, nullptr);

	CUIRect AssetDropSmall, PlayerDropSmall;
	AssetDropRect.HMargin((AssetDropRect.h - 17.0f) / 2.0f, &AssetDropSmall);
	PlayerDropRect.HMargin((PlayerDropRect.h - 17.0f) / 2.0f, &PlayerDropSmall);

	pThis->Ui()->DoLabel(&ShareRect, "Share", FontSize, TEXTALIGN_ML);

	// asset selector
	static CScrollRegion s_ShareAssetScrollRegion;
	pThis->m_ShareAssetNameDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ShareAssetScrollRegion;
	if(!vpAssetNames.empty())
	{
		const int NewAsset = pThis->Ui()->DoDropDown(&AssetDropSmall, AssetSelection, vpAssetNames.data(), (int)vpAssetNames.size(), pThis->m_ShareAssetNameDropDownState);
		if(NewAsset >= 0 && NewAsset < (int)pThis->m_vShareAssetAssetNames.size())
			str_copy(pThis->m_aShareAssetName, pThis->m_vShareAssetAssetNames[NewAsset].c_str());
	}
	else
	{
		SLabelProperties AssetProps;
		AssetProps.m_MaxWidth = AssetDropSmall.w;
		AssetProps.m_EllipsisAtEnd = true;
		pThis->Ui()->DoLabel(&AssetDropSmall, pThis->m_aShareAssetName, SmallFontSize, TEXTALIGN_ML, AssetProps);
	}

	pThis->Ui()->DoLabel(&WithRect, "with", FontSize, TEXTALIGN_MC);

	// player selector
	static CScrollRegion s_ShareUserScrollRegion;
	pThis->m_ShareAssetUserDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ShareUserScrollRegion;
	if(!vpNames.empty())
	{
		const int NewSelection = pThis->Ui()->DoDropDown(&PlayerDropSmall, CurSelection, vpNames.data(), (int)vpNames.size(), pThis->m_ShareAssetUserDropDownState, aUserUsesUClient, pThis->m_UcLogoTexture);
		if(NewSelection >= 0 && NewSelection < (int)pThis->m_vShareAssetUserNames.size())
			str_copy(pThis->m_aShareAssetTargetName, pThis->m_vShareAssetUserNames[NewSelection].c_str());
	}
	else
	{
		pThis->m_aShareAssetTargetName[0] = '\0';
		pThis->Ui()->DoLabel(&PlayerDropSmall, Localize("(no players)"), SmallFontSize, TEXTALIGN_ML);
	}

	// status line (errors / progress) just below the sentence
	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(14.0f, &Row, &View);
	if(pThis->m_aShareAssetStatus[0] != '\0')
	{
		if(pThis->m_ShareAssetState == EShareAssetState::FAILED)
			pThis->TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		else
			pThis->TextRender()->TextColor(0.6f, 0.8f, 1.0f, 1.0f);
		pThis->Ui()->DoLabel(&Row, pThis->m_aShareAssetStatus, SmallFontSize, TEXTALIGN_ML);
		pThis->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	// bottom-anchored block: [checkbox] / [disclaimer] / (gap) / [buttons]
	CUIRect ButtonRow, CancelRect, ConfirmRect, DisclaimerRow, CheckRow;
	View.HSplitBottom(20.0f, &View, &ButtonRow);
	View.HSplitBottom(8.0f, &View, nullptr);
	View.HSplitBottom(24.0f, &View, &DisclaimerRow);
	View.HSplitBottom(4.0f, &View, nullptr);
	View.HSplitBottom(20.0f, &View, &CheckRow);

	// agreement checkbox (above the disclaimer)
	if(pThis->DoButton_CheckBox(&pThis->m_ShareAssetAgree, "I agree to upload this asset to media.under1111.com.", pThis->m_ShareAssetAgree ? 1 : 0, &CheckRow))
		pThis->m_ShareAssetAgree = !pThis->m_ShareAssetAgree;

	// disclaimer (small, dim) directly above the buttons
	{
		SLabelProperties Props;
		Props.m_MaxWidth = DisclaimerRow.w;
		pThis->TextRender()->TextColor(0.6f, 0.6f, 0.6f, 1.0f);
		pThis->Ui()->DoLabel(&DisclaimerRow, "If the receiving player is not using UClient, the image may not be displayed.", SmallFontSize, TEXTALIGN_ML, Props);
		pThis->TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	// buttons
	ButtonRow.VSplitMid(&CancelRect, &ConfirmRect, 10.0f);
	if(pThis->DoButton_Menu(&pThis->m_ShareAssetCancelButton, Localize("Cancel"), 0, &CancelRect))
	{
		if(pThis->m_pShareAssetUploadRequest)
		{
			pThis->m_pShareAssetUploadRequest->Abort();
			pThis->m_pShareAssetUploadRequest.reset();
		}
		return CUi::POPUP_CLOSE_CURRENT;
	}

	const bool Uploading = pThis->m_ShareAssetState == EShareAssetState::UPLOADING;
	const bool CanConfirm = pThis->m_ShareAssetAgree && !Uploading && !pThis->m_vShareAssetUserNames.empty() && !pThis->m_vShareAssetAssetNames.empty();
	const char *pConfirmText = Uploading ? Localize("Uploading...") : Localize("Confirm");
	if(pThis->DoButton_Menu(&pThis->m_ShareAssetConfirmButton, pConfirmText, 0, &ConfirmRect) && CanConfirm)
		pThis->BeginShareAssetUpload();

	return CUi::POPUP_KEEP_OPEN;
}
