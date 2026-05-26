#include "steam_handlers.hpp"
#include "steam.hpp"
#include "logger.hpp"
#include "config.hpp"
#pragma warning(disable: 4100 4505 4127)
#include "gen_tables.txt"
#include "gen_padding.cpp"

namespace femboy { namespace steam {
#include <cstring>
#include <unordered_map>
#include <vector>
#include <string>
#include <ctime>

// ============================================================
// ISteamHTTP - CEG chunk serving
// ============================================================

struct HttpRequest {
    uint64_t handle;
    std::string url;
    bool completed;
    bool success;
    int statusCode;
    std::vector<uint8_t> responseBody;
    std::string responseHeaders;
};

static std::unordered_map<uint64_t, HttpRequest> g_httpRequests;
static uint64_t g_nextHttpHandle = 1000;

// chunk urls be wildin
static bool IsCEGChunkUrl(const char* url)
{
    if (!url) return false;
    if (strstr(url, "content.steampowered.com")) return true;
    if (strstr(url, "ugc")) return true;
    if (strstr(url, "chunk")) return true;
    if (strstr(url, "ceg")) return true;
    if (strstr(url, ".csd")) return true;
    return false;
}

static bool LoadFileFromDisk(const char* path, std::vector<uint8_t>& data)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 0)
    {
        data.resize(sz);
        fread(data.data(), 1, sz, f);
    }
    fclose(f);
    return true;
}

static uint64_t __fastcall Hook_CreateHTTPRequest(void*, int, uint32_t eMethod, const char* pchURL)
{
    (void)eMethod;
    uint64_t h = g_nextHttpHandle++;
    HttpRequest req;
    req.handle = h;
    if (pchURL) req.url = pchURL;
    req.completed = false;
    req.success = true;
    req.statusCode = 200;
    g_httpRequests[h] = req;
    LOG("[Handlers] CreateHTTPRequest(%u, %s) = %llu", eMethod, pchURL ? pchURL : "null", h);
    return h;
}

static bool __fastcall Hook_SetHTTPRequestHeaderValue(void*, int, uint64_t hRequest, const char* pchHeaderName, const char* pchHeaderValue)
{
    LOG("[Handlers] SetHTTPRequestHeaderValue(%llu, %s, %s)", hRequest, pchHeaderName ? pchHeaderName : "", pchHeaderValue ? pchHeaderValue : "");
    return true;
}

static bool __fastcall Hook_SetHTTPRequestGetOrPostParameter(void*, int, uint64_t hRequest, const char* pchParamName, const char* pchParamValue)
{
    LOG("[Handlers] SetHTTPRequestGetOrPostParameter(%llu, %s, %s)", hRequest, pchParamName ? pchParamName : "", pchParamValue ? pchParamValue : "");
    return true;
}

static bool __fastcall Hook_SendHTTPRequest(void*, int, uint64_t hRequest, uint64_t* pCallHandle)
{
    auto it = g_httpRequests.find(hRequest);
    if (it == g_httpRequests.end()) return false;

    LOG("[Handlers] SendHTTPRequest(%llu, url=%s)", hRequest, it->second.url.c_str());

    // For CEG chunk URLs, try to serve from disk
    std::vector<uint8_t> chunkData;
    if (IsCEGChunkUrl(it->second.url.c_str()))
    {
        // Try to load from common CEG cache locations
        wchar_t exeDir[MAX_PATH];
        GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
        if (lastSlash) lastSlash[1] = 0;
        else exeDir[0] = 0;

        std::string urlPath = it->second.url;
        size_t qmark = urlPath.find('?');
        if (qmark != std::string::npos) urlPath = urlPath.substr(0, qmark);

        size_t lastSeg = urlPath.rfind('/');
        std::string filename;
        if (lastSeg != std::string::npos) filename = urlPath.substr(lastSeg + 1);
        else filename = urlPath;

        std::string fullPathA;
        char dirA[MAX_PATH];
        wcstombs(dirA, exeDir, MAX_PATH);
        fullPathA = dirA + filename;
        size_t dot = filename.rfind('.');
        if (dot == std::string::npos) fullPathA += ".chunk";

        if (LoadFileFromDisk(fullPathA.c_str(), chunkData))
        {
            LOG("[Handlers] CEG chunk served from disk: %s (%zu bytes)", fullPathA.c_str(), chunkData.size());
        }
        else
        {
            LOG("[Handlers] CEG chunk not found locally: %s", fullPathA.c_str());
            // Return an empty 404 response
            it->second.statusCode = 404;
            it->second.success = false;
        }
    }

    it->second.completed = true;
    it->second.responseBody = std::move(chunkData);
    it->second.responseHeaders = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n";

    if (pCallHandle) *pCallHandle = hRequest | 0x100000000ULL;
    return true;
}

static bool __fastcall Hook_IsHTTPRequestCompleted(void*, int, uint64_t hRequest, bool* pbCompleted)
{
    auto it = g_httpRequests.find(hRequest);
    if (it == g_httpRequests.end()) return false;
    if (pbCompleted) *pbCompleted = it->second.completed;
    return true;
}

static bool __fastcall Hook_GetHTTPResponseBodySize(void*, int, uint64_t hRequest, uint32_t* pBodySize)
{
    auto it = g_httpRequests.find(hRequest);
    if (it == g_httpRequests.end()) return false;
    if (pBodySize) *pBodySize = (uint32_t)it->second.responseBody.size();
    return true;
}

static bool __fastcall Hook_GetHTTPResponseBodyData(void*, int, uint64_t hRequest, uint8_t* pBodyBuffer, uint32_t cbBuffer)
{
    auto it = g_httpRequests.find(hRequest);
    if (it == g_httpRequests.end()) return false;
    uint32_t toCopy = (uint32_t)it->second.responseBody.size();
    if (toCopy > cbBuffer) toCopy = cbBuffer;
    memcpy(pBodyBuffer, it->second.responseBody.data(), toCopy);
    return true;
}

static bool __fastcall Hook_GetHTTPResponseHeaderSize(void*, int, uint64_t hRequest, const char* pchHeaderName, uint32_t* pResponseHeaderSize)
{
    if (pResponseHeaderSize) *pResponseHeaderSize = 0;
    return true;
}

static bool __fastcall Hook_GetHTTPResponseHeaderValue(void*, int, uint64_t hRequest, const char* pchHeaderName, uint8_t* pHeaderValueBuffer, uint32_t cbBuffer)
{
    return true;
}

static bool __fastcall Hook_ReleaseHTTPRequest(void*, int, uint64_t hRequest)
{
    g_httpRequests.erase(hRequest);
    return true;
}

static bool __fastcall Hook_SetHTTPRequestNetworkActivityTimeout(void*, int, uint64_t hRequest, uint32_t unTimeout)
{
    return true;
}

static bool __fastcall Hook_SetHTTPRequestRequiresVerifiedCertificate(void*, int, uint64_t hRequest, bool bRequire)
{
    return true;
}

static bool __fastcall Hook_GetHTTPDownloadProgressPct(void*, int, uint64_t hRequest, float* pflPercent)
{
    if (pflPercent) *pflPercent = 100.0f;
    return true;
}

static uint32_t __fastcall Hook_GetHTTPRequestCompletionTime(void*, int, uint64_t hRequest)
{
    return (uint32_t)time(nullptr);
}

// ============================================================
// Auth ticket handlers
// ============================================================

struct AuthTicket {
    uint32_t handle;
    std::vector<uint8_t> data;
    bool valid;
};

static std::unordered_map<uint32_t, AuthTicket> g_authTickets;
static uint32_t g_nextTicketHandle = 100;

static uint32_t __fastcall Hook_GetAuthSessionTicket(void*, int, void* pTicket, int cbMaxTicket, uint32_t* pcbTicket, void* pSteamNetworkingIdentity)
{
    uint32_t hTicket = g_nextTicketHandle++;
    AuthTicket t;
    t.handle = hTicket;
    t.valid = true;

    // Generate a plausible 4KB auth ticket
    t.data.resize(cbMaxTicket > 0 ? cbMaxTicket : 1024);
    memset(t.data.data(), 0, t.data.size());

    // Fill with fake ticket data: version, steamid, appid, timestamp
    uint32_t* dw = (uint32_t*)t.data.data();
    dw[0] = 0x00010003;
    dw[1] = (uint32_t)(g_config.steam_emu.steam_id & 0xFFFFFFFF);
    dw[2] = (uint32_t)(g_config.steam_emu.steam_id >> 32);
    dw[3] = g_config.cold_client.app_id;
    dw[4] = (uint32_t)time(nullptr);
    dw[5] = 0x00000001;

    g_authTickets[hTicket] = t;
    if (pcbTicket) *pcbTicket = (uint32_t)t.data.size();
    LOG("[Handlers] GetAuthSessionTicket = %u (%d bytes)", hTicket, (int)t.data.size());
    return hTicket;
}

static uint32_t __fastcall Hook_GetAuthTicketForWebApi(void*, int, const char* pchIdentity)
{
    uint32_t hTicket = g_nextTicketHandle++;
    AuthTicket t;
    t.handle = hTicket;
    t.valid = true;
    t.data.resize(512);
    memset(t.data.data(), 0, 512);
    g_authTickets[hTicket] = t;
    LOG("[Handlers] GetAuthTicketForWebApi = %u", hTicket);
    return hTicket;
}

static uint32_t __fastcall Hook_BeginAuthSession(void*, int, const void* pAuthTicket, int cbAuthTicket, uint64_t steamID)
{
    LOG("[Handlers] BeginAuthSession(steamID=%llu)", steamID);
    return 0; // k_EBeginAuthSessionResultOK
}

static void __fastcall Hook_EndAuthSession(void*, int, uint64_t steamID)
{
    LOG("[Handlers] EndAuthSession(%llu)", steamID);
}

static void __fastcall Hook_CancelAuthTicket(void*, int, uint32_t hAuthTicket)
{
    g_authTickets.erase(hAuthTicket);
    LOG("[Handlers] CancelAuthTicket(%u)", hAuthTicket);
}

static uint32_t __fastcall Hook_UserHasLicenseForApp(void*, int, void* hUser, uint32_t nAppID)
{
    return 3; // k_EUserHasLicenseResultHasLicense
}

static bool __fastcall Hook_BIsAuthorizedFor(void*, int, void* hUser, uint32_t nAppID)
{
    return true;
}

// ============================================================
// SteamFriends handlers
// ============================================================

static const char* __fastcall Hook_GetFriendPersonaName(void*, int, uint64_t steamIDFriend)
{
    LOG("[Handlers] GetFriendPersonaName(%llu)", steamIDFriend);
    for (int i = 0; i < g_friendCount; i++)
        if (g_friendSteamIDs[i] == steamIDFriend)
            return g_friendNames[i];
    return "Friend";
}

static int __fastcall Hook_GetFriendCount(void*, int, int eFriendFlags)
{
    return g_friendCount;
}

static uint64_t __fastcall Hook_GetFriendByIndex(void*, int, int iFriend, int eFriendFlags)
{
    if (iFriend >= 0 && iFriend < g_friendCount)
        return g_friendSteamIDs[iFriend];
    return 0;
}

static int __fastcall Hook_GetFriendRelationship(void*, int, uint64_t steamIDFriend)
{
    return 2; // k_EFriendRelationshipFriend
}

static int __fastcall Hook_GetFriendPersonaState(void*, int, uint64_t steamIDFriend)
{
    return 1; // k_EPersonaStateOnline
}

static int __fastcall Hook_GetFriendGamePlayed(void*, int, uint64_t steamIDFriend, void* pFriendGameInfo)
{
    if (pFriendGameInfo) memset(pFriendGameInfo, 0, 16);
    return 0;
}

static const char* __fastcall Hook_GetFriendGamePlayedExtra(void*, int, uint64_t steamIDFriend)
{
    return "";
}

static int __fastcall Hook_GetFriendPersonaStateFlags(void*, int, uint64_t steamIDFriend)
{
    return 0;
}

static bool __fastcall Hook_SetRichPresence(void*, int, const char* pchKey, const char* pchValue)
{
    LOG("[Handlers] SetRichPresence(%s, %s)", pchKey ? pchKey : "", pchValue ? pchValue : "");
    return true;
}

static void __fastcall Hook_ClearRichPresence(void*, int)
{
}

static const char* __fastcall Hook_GetFriendRichPresence(void*, int, uint64_t steamIDFriend, const char* pchKey)
{
    return "";
}

static int __fastcall Hook_GetFriendRichPresenceKeyCount(void*, int, uint64_t steamIDFriend)
{
    return 0;
}

static const char* __fastcall Hook_GetFriendRichPresenceKeyByIndex(void*, int, uint64_t steamIDFriend, int eKey)
{
    return "";
}

static bool __fastcall Hook_RequestUserInformation(void*, int, uint64_t steamIDUser, bool bRequireNameOnly)
{
    return true;
}

static bool __fastcall Hook_RequestFriendRichPresence(void*, int, uint64_t steamIDFriend)
{
    return true;
}

static uint64_t __fastcall Hook_GetClanByIndex(void*, int, int iClan)
{
    return 0;
}

static int __fastcall Hook_GetClanCount(void*, int)
{
    return 0;
}

static int __fastcall Hook_GetClanActivityCounts(void*, int, uint64_t clanID, int* pnOnline, int* pnInGame, int* pnChatting)
{
    if (pnOnline) *pnOnline = 0;
    if (pnInGame) *pnInGame = 0;
    if (pnChatting) *pnChatting = 0;
    return true;
}

static const char* __fastcall Hook_GetClanName(void*, int, uint64_t clanID)
{
    return "Clan";
}

static const char* __fastcall Hook_GetClanTag(void*, int, uint64_t clanID)
{
    return "TAG";
}

static bool __fastcall Hook_GetClanOfficerList(void*, int, uint64_t clanID, void* pOfficerList, int cOfficerCount, int* pcOfficerCount)
{
    if (pcOfficerCount) *pcOfficerCount = 0;
    return true;
}

static uint64_t __fastcall Hook_GetClanOwner(void*, int, uint64_t clanID)
{
    return g_config.steam_emu.steam_id;
}

static int __fastcall Hook_GetClanChatMemberCount(void*, int, uint64_t clanID)
{
    return 0;
}

static int __fastcall Hook_GetCoplayFriendCount(void*, int)
{
    return 0;
}

static uint64_t __fastcall Hook_GetCoplayFriend(void*, int, int iFriend)
{
    return 0;
}

static int __fastcall Hook_GetFriendCoplayGame(void*, int, uint64_t steamIDFriend)
{
    return 0;
}

static int __fastcall Hook_GetFriendCoplayTime(void*, int, uint64_t steamIDFriend)
{
    return 0;
}

static int __fastcall Hook_GetSmallFriendAvatar(void*, int, uint64_t steamIDFriend)
{
    return 0;
}

static int __fastcall Hook_GetMediumFriendAvatar(void*, int, uint64_t steamIDFriend)
{
    return 0;
}

static int __fastcall Hook_GetLargeFriendAvatar(void*, int, uint64_t steamIDFriend)
{
    return 0;
}

static bool __fastcall Hook_SetPlayedWith(void*, int, uint64_t steamIDUserPlayedWith)
{
    return true;
}

static void __fastcall Hook_ActivateGameOverlay(void*, int, const char* pchDialog)
{
    LOG("[Handlers] ActivateGameOverlay(%s)", pchDialog ? pchDialog : "");
}

static void __fastcall Hook_ActivateGameOverlayToUser(void*, int, const char* pchDialog, uint64_t steamID)
{
}

static void __fastcall Hook_ActivateGameOverlayToWebPage(void*, int, const char* pchURL)
{
}

static void __fastcall Hook_ActivateGameOverlayToStore(void*, int, uint32_t nAppID, int eFlag)
{
}

static void __fastcall Hook_ActivateGameOverlayInviteDialog(void*, int, uint64_t steamIDLobby)
{
}

static void __fastcall Hook_SetPersonaName(void*, int, const char* pchPersonaName)
{
}

static void __fastcall Hook_SetPersonaState(void*, int, int ePersonaState)
{
}

static bool __fastcall Hook_IsUserInSource(void*, int, uint64_t steamIDUser, uint64_t steamIDSource)
{
    return false;
}

static int __fastcall Hook_GetFriendCountFromSource(void*, int, uint64_t steamIDSource)
{
    return 0;
}

static uint64_t __fastcall Hook_GetFriendFromSourceByIndex(void*, int, uint64_t steamIDSource, int iFriend)
{
    return 0;
}

static int __fastcall Hook_GetUserRestrictions(void*, int)
{
    return 0;
}

static bool __fastcall Hook_InviteUserToGame(void*, int, const char* pchConnectString)
{
    return true;
}

// ============================================================
// SteamUserStats handlers
// ============================================================

static bool __fastcall Hook_RequestCurrentStats(void*, int)
{
    return true;
}

static bool __fastcall Hook_GetStatInt(void*, int, const char* pchName, int32_t* pData)
{
    if (pData) *pData = 0;
    return true;
}

static bool __fastcall Hook_GetStatFloat(void*, int, const char* pchName, float* pData)
{
    if (pData) *pData = 0.0f;
    return true;
}

static bool __fastcall Hook_SetStatInt(void*, int, const char* pchName, int32_t nData)
{
    return true;
}

static bool __fastcall Hook_SetStatFloat(void*, int, const char* pchName, float fData)
{
    return true;
}

static bool __fastcall Hook_UpdateAvgRateStat(void*, int, const char* pchName, float fCountThisSession, double dSessionLength)
{
    return true;
}

static bool __fastcall Hook_GetAchievement(void*, int, const char* pchName, bool* pbAchieved)
{
    if (pbAchieved) *pbAchieved = false;
    return true;
}

static bool __fastcall Hook_SetAchievement(void*, int, const char* pchName)
{
    LOG("[Handlers] SetAchievement(%s)", pchName ? pchName : "");
    return true;
}

static bool __fastcall Hook_ClearAchievement(void*, int, const char* pchName)
{
    return true;
}

static bool __fastcall Hook_StoreStats(void*, int)
{
    return true;
}

static int __fastcall Hook_GetAchievementIcon(void*, int, const char* pchName)
{
    (void)pchName;
    return (int)(uintptr_t)g_iconDataPool;
}

static const char* __fastcall Hook_GetAchievementDisplayAttribute(void*, int, const char* pchName, const char* pchKey)
{
    if (!pchKey) return "";
    if (strcmp(pchKey, "name") == 0) return "Achievement";
    if (strcmp(pchKey, "desc") == 0) return "Completed an achievement";
    return "";
}

static bool __fastcall Hook_IndicateAchievementProgress(void*, int, const char* pchName, uint32_t nCurProgress, uint32_t nMaxProgress)
{
    return true;
}

static uint32_t __fastcall Hook_GetNumAchievements(void*, int)
{
    return g_achievementCount;
}

static const char* __fastcall Hook_GetAchievementName(void*, int, uint32_t iAchievement)
{
    if (iAchievement < (uint32_t)g_achievementCount)
        return g_achievementNames[iAchievement];
    return "";
}

static const char* __fastcall Hook_GetAchievementAchievedPercent(void*, int, const char* pchName, float* pflPercent)
{
    if (pflPercent) *pflPercent = 50.0f;
    return "";
}

static int __fastcall Hook_GetMostAchievedAchievementInfo(void*, int, char* pchName, uint32_t unNameBufLen, float* pflPercent, bool* pbAchieved)
{
    if (pchName) pchName[0] = 0;
    if (pflPercent) *pflPercent = 0.0f;
    if (pbAchieved) *pbAchieved = false;
    return 0;
}

static int __fastcall Hook_GetNextMostAchievedAchievementInfo(void*, int, int iIteratorPrevious, char* pchName, uint32_t unNameBufLen, float* pflPercent, bool* pbAchieved)
{
    return 0;
}

static bool __fastcall Hook_ResetAllStats(void*, int, bool bAchievementsToo)
{
    return true;
}

static bool __fastcall Hook_FindLeaderboard(void*, int, const char* pchLeaderboardName)
{
    return false;
}

static bool __fastcall Hook_FindOrCreateLeaderboard(void*, int, const char* pchLeaderboardName, int eLeaderboardSortMethod, int eLeaderboardDisplayType)
{
    return false;
}

static int __fastcall Hook_GetLeaderboardSortMethod(void*, int, uint64_t hSteamLeaderboard)
{
    return 0;
}

static int __fastcall Hook_GetLeaderboardDisplayType(void*, int, uint64_t hSteamLeaderboard)
{
    return 0;
}

static int __fastcall Hook_GetLeaderboardEntryCount(void*, int, uint64_t hSteamLeaderboard)
{
    return 0;
}

static bool __fastcall Hook_DownloadLeaderboardEntries(void*, int, uint64_t hSteamLeaderboard, int eLeaderboardDataRequest, int nRangeStart, int nRangeEnd)
{
    return false;
}

static bool __fastcall Hook_UploadLeaderboardScore(void*, int, uint64_t hSteamLeaderboard, int eLeaderboardUploadScoreMethod, int32_t nScore, int32_t* pScoreDetails, int cScoreDetailsCount)
{
    return false;
}

static bool __fastcall Hook_GetDownloadedLeaderboardEntry(void*, int, uint64_t hSteamLeaderboardEntries, int index, void* pLeaderboardEntry, int32_t* pDetails, int cDetailsMax)
{
    return false;
}

static int __fastcall Hook_GetDownloadedLeaderboardSortMethod(void*, int, uint64_t hSteamLeaderboardEntries)
{
    return 0;
}

static int __fastcall Hook_GetDownloadedLeaderboardDisplayType(void*, int, uint64_t hSteamLeaderboardEntries)
{
    return 0;
}

static uint64_t __fastcall Hook_GetDownloadedLeaderboardHandle(void*, int, uint64_t hSteamLeaderboardEntries)
{
    return 0;
}

static int __fastcall Hook_GetAchievementAndStatProgressionsCount(void*, int)
{
    return 0;
}

static bool __fastcall Hook_RequestGlobalStats(void*, int, int nHistoryDays)
{
    return true;
}

// ============================================================
// SteamNetworking handlers
// ============================================================

static bool __fastcall Hook_SendP2PPacket(void*, int, uint64_t steamIDRemote, const void* pubData, uint32_t cubData, int eP2PSendType, int iChannel)
{
    return true;
}

static bool __fastcall Hook_IsP2PPacketAvailable(void*, int, uint32_t* pcubMsgSize, int iChannel)
{
    if (pcubMsgSize) *pcubMsgSize = 0;
    return false;
}

static bool __fastcall Hook_ReadP2PPacket(void*, int, void* pubDest, uint32_t cubDest, uint32_t* pcubMsgSize, uint64_t* psteamIDRemote, int iChannel)
{
    if (pcubMsgSize) *pcubMsgSize = 0;
    return false;
}

static bool __fastcall Hook_AcceptP2PSessionWithUser(void*, int, uint64_t steamIDRemote)
{
    return true;
}

static bool __fastcall Hook_CloseP2PSessionWithUser(void*, int, uint64_t steamIDRemote)
{
    return true;
}

static bool __fastcall Hook_CloseP2PChannelWithUser(void*, int, uint64_t steamIDRemote, int iChannel)
{
    return true;
}

static bool __fastcall Hook_GetP2PSessionState(void*, int, uint64_t steamIDRemote, void* pConnectionState)
{
    if (pConnectionState) memset(pConnectionState, 0, 20);
    return true;
}

static bool __fastcall Hook_AllowP2PPacketRelay(void*, int, bool bAllow)
{
    return true;
}

static uint32_t __fastcall Hook_CreateP2PConnectionSocket(void*, int, uint64_t steamIDTarget, int iVirtualPort, int iTimeoutSec)
{
    return 0;
}

static uint32_t __fastcall Hook_CreateListenSocketP2P(void*, int, int iVirtualPort, uint32_t nIP, uint16_t nPort, bool bUseUDP)
{
    return 0;
}

static bool __fastcall Hook_DestroyListenSocket(void*, int, uint32_t hSocket)
{
    return true;
}

static bool __fastcall Hook_DestroyP2PConnectionSocket(void*, int, uint32_t hSocket)
{
    return true;
}

// ============================================================
// SteamRemoteStorage handlers
// ============================================================

static bool __fastcall Hook_FileWrite(void*, int, const char* pchFile, const void* pvData, int32_t cubData)
{
    return true;
}

static int32_t __fastcall Hook_FileRead(void*, int, const char* pchFile, void* pvData, int32_t cubDataToRead)
{
    return 0;
}

static bool __fastcall Hook_FileExists(void*, int, const char* pchFile)
{
    return false;
}

static bool __fastcall Hook_FileDelete(void*, int, const char* pchFile)
{
    return true;
}

static int64_t __fastcall Hook_GetFileSize(void*, int, const char* pchFile)
{
    return 0;
}

static int64_t __fastcall Hook_GetFileTimestamp(void*, int, const char* pchFile)
{
    return (int64_t)time(nullptr);
}

static bool __fastcall Hook_SetSyncPlatforms(void*, int, const char* pchFile, int eSyncPlatforms)
{
    return true;
}

static int32_t __fastcall Hook_GetFileCount(void*, int)
{
    return 0;
}

static const char* __fastcall Hook_GetFileNameAndSize(void*, int, int iFile, int32_t* pnFileSizeInBytes)
{
    if (pnFileSizeInBytes) *pnFileSizeInBytes = 0;
    return "";
}

static bool __fastcall Hook_GetQuota(void*, int, uint64_t* pnTotalBytes, uint64_t* puAvailableBytes)
{
    if (pnTotalBytes) *pnTotalBytes = 1073741824ULL;
    if (puAvailableBytes) *puAvailableBytes = 1073741824ULL;
    return true;
}

static bool __fastcall Hook_SetCloudEnabledForApp(void*, int, bool bEnabled)
{
    return true;
}

static bool __fastcall Hook_IsCloudEnabledForApp(void*, int)
{
    return true;
}

static bool __fastcall Hook_IsCloudEnabledForThisAccount(void*, int)
{
    return true;
}

// ============================================================
// SteamUGC handlers
// ============================================================

static uint64_t __fastcall Hook_CreateQueryUserUGCRequest(void*, int, uint32_t unAccountID, int eListType, int eMatchMakingType, uint32_t nPage, uint32_t nCreatorAppID, uint32_t nConsumerAppID, const char* pchCursor)
{
    uint64_t h = 100;
    return h;
}

static uint64_t __fastcall Hook_CreateQueryUGCDetailsRequest(void*, int, uint64_t* pvecPublishedFileID, uint32_t unNumPublishedFileIDs)
{
    uint64_t h = 101;
    return h;
}

static uint64_t __fastcall Hook_CreateQueryAllUGCRequest(void*, int, int eQueryType, int eMatchingeMatchingUGCTypeFileType, uint32_t nCreatorAppID, uint32_t nConsumerAppID, const char* pchCursor)
{
    uint64_t h = 102;
    return h;
}

static bool __fastcall Hook_SendQueryUGCRequest(void*, int, uint64_t hHandle, uint64_t* pCallHandle)
{
    if (pCallHandle) *pCallHandle = 0;
    return true;
}

static bool __fastcall Hook_GetQueryUGCResult(void*, int, uint64_t hHandle, uint32_t index, void* pDetails)
{
    if (pDetails) memset(pDetails, 0, 128);
    return true;
}

static bool __fastcall Hook_ReleaseQueryUGCRequest(void*, int, uint64_t hHandle)
{
    return true;
}

static bool __fastcall Hook_AddRequiredTag(void*, int, uint64_t hHandle, const char* pchTagName)
{
    return true;
}

static bool __fastcall Hook_AddExcludedTag(void*, int, uint64_t hHandle, const char* pchTagName)
{
    return true;
}

static bool __fastcall Hook_SetReturnLongDescription(void*, int, uint64_t hHandle, bool bReturnLongDescription)
{
    return true;
}

static bool __fastcall Hook_SetReturnTotalOnly(void*, int, uint64_t hHandle, bool bReturnTotalOnly)
{
    return true;
}

static bool __fastcall Hook_SetAllowCachedResponse(void*, int, uint64_t hHandle, uint32_t unMaxAgeSeconds)
{
    return true;
}

static bool __fastcall Hook_SetCloudFileNameFilter(void*, int, uint64_t hHandle, const char* pchFileName)
{
    return true;
}

static bool __fastcall Hook_SetMatchAnyTag(void*, int, uint64_t hHandle, bool bMatchAnyTag)
{
    return true;
}

static bool __fastcall Hook_SetSearchText(void*, int, uint64_t hHandle, const char* pchSearchText)
{
    return true;
}

static bool __fastcall Hook_SetRankedByTrendDays(void*, int, uint64_t hHandle, uint32_t unDays)
{
    return true;
}

static bool __fastcall Hook_AddRequiredKeyValueTag(void*, int, uint64_t hHandle, const char* pchKey, const char* pchValue)
{
    return true;
}

static bool __fastcall Hook_DownloadItem(void*, int, uint64_t nPublishedFileID, bool bHighPriority)
{
    return true;
}

static bool __fastcall Hook_GetItemInstallInfo(void*, int, uint64_t nPublishedFileID, uint64_t* pnSizeOnDisk, char* pchFolder, uint32_t cchFolderSize, uint32_t* punTimeStamp)
{
    if (pnSizeOnDisk) *pnSizeOnDisk = 1024;
    if (pchFolder) pchFolder[0] = 0;
    if (punTimeStamp) *punTimeStamp = (uint32_t)time(nullptr);
    return true;
}

static bool __fastcall Hook_GetItemDownloadInfo(void*, int, uint64_t nPublishedFileID, uint64_t* punBytesDownloaded, uint64_t* punBytesTotal)
{
    if (punBytesDownloaded) *punBytesDownloaded = 0;
    if (punBytesTotal) *punBytesTotal = 0;
    return true;
}

static bool __fastcall Hook_GetQueryUGCPreviewURL(void*, int, uint64_t hHandle, uint32_t index, char* pchURL, uint32_t cchURLSize)
{
    if (pchURL) pchURL[0] = 0;
    return true;
}

static int __fastcall Hook_GetQueryUGCNumTags(void*, int, uint64_t hHandle, uint32_t index)
{
    return 0;
}

static bool __fastcall Hook_GetQueryUGCTag(void*, int, uint64_t hHandle, uint32_t index, uint32_t indexTag, char* pchValue, uint32_t cchValueSize)
{
    if (pchValue) pchValue[0] = 0;
    return true;
}

static uint32_t __fastcall Hook_GetNumSubscribedItems(void*, int)
{
    return 0;
}

static uint32_t __fastcall Hook_GetSubscribedItems(void*, int, uint64_t* pvecPublishedFileID, uint32_t cMaxEntries)
{
    return 0;
}

// ============================================================
// SteamInventory handlers
// ============================================================

static int __fastcall Hook_GetResultStatus(void*, int, int hResult)
{
    return 0; // k_EResultOK
}

static bool __fastcall Hook_GetResultItems(void*, int, int hResult, void* pOutItemsArray, uint32_t* punOutItemsArraySize)
{
    if (punOutItemsArraySize) *punOutItemsArraySize = 0;
    return true;
}

static bool __fastcall Hook_GetResultItemProperty(void*, int, int hResult, uint32_t unItemIndex, const char* pchPropertyName, char* pchValueBuffer, uint32_t* punValueBufferSize)
{
    if (pchValueBuffer) pchValueBuffer[0] = 0;
    if (punValueBufferSize) *punValueBufferSize = 0;
    return true;
}

static uint32_t __fastcall Hook_GetResultTimestamp(void*, int, int hResult)
{
    return (uint32_t)time(nullptr);
}

static bool __fastcall Hook_CheckResultSteamID(void*, int, int hResult, uint64_t steamIDExpected)
{
    return true;
}

static void __fastcall Hook_DestroyResult(void*, int, int hResult)
{
}

static bool __fastcall Hook_GetAllItems(void*, int, int* pResultHandle)
{
    if (pResultHandle) *pResultHandle = 0;
    return true;
}

static bool __fastcall Hook_GetItemsByID(void*, int, int* pResultHandle, uint64_t* pInstanceIDs, uint32_t unCountInstanceIDs)
{
    if (pResultHandle) *pResultHandle = 0;
    return true;
}

static bool __fastcall Hook_GetItemDefinitionIDs(void*, int, uint64_t* pItemDefIDs, uint32_t* punItemDefIDsArraySize)
{
    if (punItemDefIDsArraySize) *punItemDefIDsArraySize = 0;
    return true;
}

static bool __fastcall Hook_GetItemDefinitionProperty(void*, int, uint64_t unItemDefID, const char* pchPropertyName, char* pchValueBuffer, uint32_t* punValueBufferSize)
{
    if (pchValueBuffer) pchValueBuffer[0] = 0;
    if (punValueBufferSize) *punValueBufferSize = 0;
    return true;
}

static bool __fastcall Hook_RequestEligiblePromoItemDefinitionsIDs(void*, int, uint64_t steamID)
{
    return true;
}

static bool __fastcall Hook_GetEligiblePromoItemDefinitionIDs(void*, int, uint64_t steamID, uint64_t* pItemDefIDs, uint32_t* punItemDefIDsArraySize)
{
    if (punItemDefIDsArraySize) *punItemDefIDsArraySize = 0;
    return true;
}

static bool __fastcall Hook_StartPurchase(void*, int, uint64_t* pvecItemDefIDs, uint32_t* punItemDefIDsArraySize, uint64_t* pvecQuantity, uint32_t unQuantity, int* pResultHandle)
{
    if (pResultHandle) *pResultHandle = 0;
    return true;
}

static bool __fastcall Hook_RequestPrices(void*, int)
{
    return true;
}

static uint32_t __fastcall Hook_GetNumItemsWithPrices(void*, int)
{
    return 0;
}

// ============================================================
// SteamScreenshots handlers
// ============================================================

static uint32_t __fastcall Hook_WriteScreenshot(void*, int, void* pubRGB, uint32_t cubRGB, int nWidth, int nHeight)
{
    return 0;
}

static uint32_t __fastcall Hook_AddScreenshotToLibrary(void*, int, const char* pchFilename, const char* pchThumbnailFilename, int nWidth, int nHeight)
{
    return 0;
}

static void __fastcall Hook_TriggerScreenshot(void*, int)
{
}

static void __fastcall Hook_HookScreenshots(void*, int, bool bHook)
{
}

static bool __fastcall Hook_SetLocation(void*, int, uint32_t hScreenshot, const char* pchLocation)
{
    return true;
}

static bool __fastcall Hook_TagUser(void*, int, uint32_t hScreenshot, uint64_t steamID)
{
    return true;
}

static bool __fastcall Hook_TagPublishedFile(void*, int, uint32_t hScreenshot, uint64_t unPublishedFileID)
{
    return true;
}

static bool __fastcall Hook_IsScreenshotsHooked(void*, int)
{
    return false;
}

// ============================================================
// SteamMatchmaking handlers
// ============================================================

static uint64_t __fastcall Hook_CreateLobby(void*, int, int eLobbyType, int cMaxMembers)
{
    return 0;
}

static void __fastcall Hook_JoinLobby(void*, int, uint64_t steamIDLobby)
{
}

static void __fastcall Hook_LeaveLobby(void*, int, uint64_t steamIDLobby)
{
}

static bool __fastcall Hook_InviteUserToLobby(void*, int, uint64_t steamIDLobby, uint64_t steamIDInvitee)
{
    return true;
}

static int __fastcall Hook_GetNumLobbyMembers(void*, int, uint64_t steamIDLobby)
{
    return 0;
}

static uint64_t __fastcall Hook_GetLobbyMemberByIndex(void*, int, uint64_t steamIDLobby, int iMember)
{
    return 0;
}

static const char* __fastcall Hook_GetLobbyData(void*, int, uint64_t steamIDLobby, const char* pchKey)
{
    return "";
}

static bool __fastcall Hook_SetLobbyData(void*, int, uint64_t steamIDLobby, const char* pchKey, const char* pchValue)
{
    return true;
}

static int __fastcall Hook_GetLobbyDataCount(void*, int, uint64_t steamIDLobby)
{
    return 0;
}

static bool __fastcall Hook_GetLobbyDataByIndex(void*, int, uint64_t steamIDLobby, int iLobbyData, char* pchKey, int cchKeyBufferSize, char* pchValue, int cchValueBufferSize)
{
    if (pchKey) pchKey[0] = 0;
    if (pchValue) pchValue[0] = 0;
    return false;
}

static bool __fastcall Hook_DeleteLobbyData(void*, int, uint64_t steamIDLobby, const char* pchKey)
{
    return true;
}

static const char* __fastcall Hook_GetLobbyMemberData(void*, int, uint64_t steamIDLobby, uint64_t steamIDUser, const char* pchKey)
{
    return "";
}

static void __fastcall Hook_SetLobbyMemberData(void*, int, uint64_t steamIDLobby, const char* pchKey, const char* pchValue)
{
}

static bool __fastcall Hook_SendLobbyChatMsg(void*, int, uint64_t steamIDLobby, const void* pvMsgBody, int cubMsgBody)
{
    return true;
}

static int __fastcall Hook_GetLobbyChatEntry(void*, int, uint64_t steamIDLobby, int iChatID, uint64_t* pSteamIDUser, void* pvData, int cubData, int* peChatEntryType)
{
    if (pSteamIDUser) *pSteamIDUser = 0;
    if (peChatEntryType) *peChatEntryType = 0;
    return 0;
}

static bool __fastcall Hook_RequestLobbyData(void*, int, uint64_t steamIDLobby)
{
    return true;
}

static void __fastcall Hook_SetLobbyGameServer(void*, int, uint64_t steamIDLobby, uint32_t unGameServerIP, uint16_t unGameServerPort, uint64_t steamIDGameServer)
{
}

static bool __fastcall Hook_GetLobbyGameServer(void*, int, uint64_t steamIDLobby, uint32_t* punGameServerIP, uint16_t* punGameServerPort, uint64_t* psteamIDGameServer)
{
    if (punGameServerIP) *punGameServerIP = 0;
    if (punGameServerPort) *punGameServerPort = 0;
    if (psteamIDGameServer) *psteamIDGameServer = 0;
    return false;
}

static bool __fastcall Hook_SetLobbyMemberLimit(void*, int, uint64_t steamIDLobby, int cMaxMembers)
{
    return true;
}

static int __fastcall Hook_GetLobbyMemberLimit(void*, int, uint64_t steamIDLobby)
{
    return 250;
}

static void __fastcall Hook_SetLobbyType(void*, int, uint64_t steamIDLobby, int eLobbyType)
{
}

static uint64_t __fastcall Hook_GetLobbyOwner(void*, int, uint64_t steamIDLobby)
{
    return 0;
}

static uint64_t __fastcall Hook_RequestLobbyList(void*, int)
{
    return 0;
}

static void __fastcall Hook_AddRequestLobbyListFilterSlotsAvailable(void*, int, int nSlotsAvailable)
{
}

static int __fastcall Hook_GetFavoriteGameCount(void*, int)
{
    return 0;
}

// ============================================================
// SteamController/SteamInput handlers
// ============================================================

static bool __fastcall Hook_InitController(void*, int, const char* pchAbsolutePathToControllerConfigVDF)
{
    return true;
}

static bool __fastcall Hook_ShutdownController(void*, int)
{
    return true;
}

static void __fastcall Hook_RunFrameController(void*, int)
{
}

static int __fastcall Hook_GetConnectedControllers(void*, int, uint64_t* handlesOut)
{
    if (handlesOut) handlesOut[0] = 0;
    return 0;
}

static uint64_t __fastcall Hook_GetControllerForGamepadIndex(void*, int, int nIndex)
{
    return 0;
}

static int __fastcall Hook_GetGamepadIndexForController(void*, int, uint64_t ulControllerHandle)
{
    return -1;
}

static int __fastcall Hook_GetInputTypeForHandle(void*, int, uint64_t ulControllerHandle)
{
    return 0;
}

static bool __fastcall Hook_GetControllerState(void*, int, uint64_t ulControllerHandle, void* pState)
{
    if (pState) memset(pState, 0, 8);
    return true;
}

static uint64_t __fastcall Hook_GetDigitalActionHandle(void*, int, const char* pchActionName)
{
    return 0;
}

static void __fastcall Hook_GetDigitalActionData(void*, int, uint64_t ulActionHandle, void* pActionData)
{
    if (pActionData) memset(pActionData, 0, 2);
}

static uint64_t __fastcall Hook_GetAnalogActionHandle(void*, int, const char* pchActionName)
{
    return 0;
}

static void __fastcall Hook_GetAnalogActionData(void*, int, uint64_t ulActionHandle, void* pActionData)
{
    if (pActionData) memset(pActionData, 0, 16);
}

static void __fastcall Hook_TriggerRepeatedHapticPulse(void*, int, uint64_t ulControllerHandle, int eTargetPad, uint32_t ulDurationMicroSec, uint32_t unOffMicroSec, uint32_t unRepeat, uint32_t nFlags)
{
}

static void __fastcall Hook_TriggerVibration(void*, int, uint64_t ulControllerHandle, unsigned short usLeftSpeed, unsigned short usRightSpeed)
{
}

static bool __fastcall Hook_ShowBindingPanel(void*, int, uint64_t ulControllerHandle)
{
    return true;
}

// ============================================================
// SteamParentalSettings handlers
// ============================================================

static bool __fastcall Hook_BIsParentalLockEnabled(void*, int)
{
    return false;
}

static bool __fastcall Hook_BIsParentalLockLocked(void*, int)
{
    return false;
}

static bool __fastcall Hook_BIsAppBlocked(void*, int, uint32_t nAppID)
{
    return false;
}

static bool __fastcall Hook_BIsAppInBlockList(void*, int, uint32_t nAppID)
{
    return false;
}

static bool __fastcall Hook_BIsFeatureBlocked(void*, int, int eFeature)
{
    return false;
}

static bool __fastcall Hook_BIsFeatureInBlockList(void*, int, int eFeature)
{
    return false;
}

// ============================================================
// SteamMusic handlers
// ============================================================

static bool __fastcall Hook_BIsMusicEnabled(void*, int)
{
    return false;
}

static bool __fastcall Hook_BIsMusicPlaying(void*, int)
{
    return false;
}

static int __fastcall Hook_GetMusicPlaybackStatus(void*, int)
{
    return 0;
}

static void __fastcall Hook_MusicPlay(void*, int)
{
}

static void __fastcall Hook_MusicPause(void*, int)
{
}

static void __fastcall Hook_MusicVolume(void*, int, float flVolume)
{
}

static float __fastcall Hook_GetMusicVolume(void*, int)
{
    return 0.0f;
}

// ============================================================
// SteamHTMLSurface handlers
// ============================================================

static bool __fastcall Hook_HTMLInit(void*, int, const char* pchUserAgent, const char* pchUserCSS)
{
    return true;
}

static bool __fastcall Hook_HTMLShutdown(void*, int)
{
    return true;
}

static uint32_t __fastcall Hook_HTMLCreateBrowser(void*, int, const char* pchUserAgent, const char* pchUserCSS)
{
    return 1;
}

static void __fastcall Hook_HTMLRemoveBrowser(void*, int, uint32_t unBrowserHandle)
{
}

static void __fastcall Hook_HTMLLoadURL(void*, int, uint32_t unBrowserHandle, const char* pchURL, const char* pchPostData)
{
}

static void __fastcall Hook_HTMLSetSize(void*, int, uint32_t unBrowserHandle, uint32_t unWidth, uint32_t unHeight)
{
}

static void __fastcall Hook_HTMLStopLoad(void*, int, uint32_t unBrowserHandle)
{
}

static void __fastcall Hook_HTMLReload(void*, int, uint32_t unBrowserHandle)
{
}

static void __fastcall Hook_HTMLGoBack(void*, int, uint32_t unBrowserHandle)
{
}

static void __fastcall Hook_HTMLGoForward(void*, int, uint32_t unBrowserHandle)
{
}

static void __fastcall Hook_HTMLMouseDown(void*, int, uint32_t unBrowserHandle, int eMouseButton)
{
}

static void __fastcall Hook_HTMLMouseUp(void*, int, uint32_t unBrowserHandle, int eMouseButton)
{
}

static void __fastcall Hook_HTMLMouseMove(void*, int, uint32_t unBrowserHandle, int x, int y)
{
}

static void __fastcall Hook_HTMLKeyDown(void*, int, uint32_t unBrowserHandle, uint32_t nNativeKeyCode, int eVirtualKey, const char* pchCharacters)
{
}

static void __fastcall Hook_HTMLKeyUp(void*, int, uint32_t unBrowserHandle, uint32_t nNativeKeyCode, int eVirtualKey, const char* pchCharacters)
{
}

static void __fastcall Hook_HTMLMouseWheel(void*, int, uint32_t unBrowserHandle, int32_t nDelta)
{
}

static void __fastcall Hook_HTMLGetLinkAtPosition(void*, int, uint32_t unBrowserHandle, int x, int y)
{
}

static void __fastcall Hook_HTMLSetCookie(void*, int, const char* pchHostname, const char* pchKey, const char* pchValue, const char* pchPath, int nExpires, bool bSecure, bool bHTTPOnly)
{
}

// ============================================================
// SteamGameServer handlers
// ============================================================

static bool __fastcall Hook_GSLogOn(void*, int, uint32_t unToken)
{
    return true;
}

static bool __fastcall Hook_GSLogOff(void*, int)
{
    return true;
}

static bool __fastcall Hook_GSLoggedOn(void*, int)
{
    return true;
}

static bool __fastcall Hook_GSSecure(void*, int)
{
    return true;
}

static uint64_t __fastcall Hook_GSGetSteamID(void*, int)
{
    return g_config.steam_emu.steam_id;
}

static bool __fastcall Hook_GSComputeNewAuthTicket(void*, int, void* pAuthTicket, int cbMaxAuthTicket, uint32_t* pcbAuthTicket)
{
    if (pcbAuthTicket) *pcbAuthTicket = 0;
    return true;
}

static bool __fastcall Hook_GSCreateUnauthenticatedUser(void*, int, uint64_t* pSteamID)
{
    if (pSteamID) *pSteamID = g_config.steam_emu.steam_id;
    return true;
}

static int __fastcall Hook_GSGetServerReputation(void*, int)
{
    return 0;
}

static uint32_t __fastcall Hook_GSGetAuthSessionTicket(void*, int, void* pTicket, int cbMaxTicket, uint32_t* pcbTicket)
{
    return Hook_GetAuthSessionTicket(nullptr, 0, pTicket, cbMaxTicket, pcbTicket, nullptr);
}

static uint32_t __fastcall Hook_GSBeginAuthSession(void*, int, const void* pAuthTicket, int cbAuthTicket, uint64_t steamID)
{
    return Hook_BeginAuthSession(nullptr, 0, pAuthTicket, cbAuthTicket, steamID);
}

static void __fastcall Hook_GSEndAuthSession(void*, int, uint64_t steamID)
{
    Hook_EndAuthSession(nullptr, 0, steamID);
}

static void __fastcall Hook_GSSetHeartbeatData(void*, int, int iHeartbeatData)
{
}

static void __fastcall Hook_GSSetServerType(void*, int, uint32_t unServerIP, uint16_t unGamePort, uint16_t unQueryPort, int eServerType, uint32_t unGameDir)
{
}

static void __fastcall Hook_GSSetGameTags(void*, int, const char* pchGameTags)
{
}

// ============================================================
// SteamClient handlers
// ============================================================

static uint32_t __fastcall Hook_CreateSteamPipe(void*, int)
{
    return 1;
}

static bool __fastcall Hook_ReleaseSteamPipe(void*, int, uint32_t hSteamPipe)
{
    return true;
}

static uint32_t __fastcall Hook_CreateGlobalUser(void*, int, uint32_t* phSteamPipe)
{
    if (phSteamPipe) *phSteamPipe = 1;
    return 1;
}

static bool __fastcall Hook_ReleaseGlobalUser(void*, int, uint32_t hGlobalUser)
{
    return true;
}

static void* __fastcall Hook_GetISteamUser(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamUser(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamGameServer(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamGameServer(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamFriends(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamFriends(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamUtils(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamUtils(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamApps(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamApps(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamMatchmaking(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamMatchmaking(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamNetworking(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamNetworking(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamRemoteStorage(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamRemoteStorage(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamUserStats(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamUserStats(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamScreenshots(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamScreenshots(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamHTTP(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamHTTP(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamUGC(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamUGC(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamInventory(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamInventory(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamParentalSettings(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamParentalSettings(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamMusic(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamMusic(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamMusicRemote(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamMusicRemote(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamHTMLSurface(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamHTMLSurface(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamController(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamController(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamGameServerStats(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamGameServerStats(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamMatchmakingServers(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamMatchmakingServers(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamGameCoordinator(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamGameCoordinator(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamNetworkingSockets(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamNetworkingSockets(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamNetworkingUtils(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamNetworkingUtils(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamAppList(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamAppList(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamParties(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamParties(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamRemotePlay(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamRemotePlay(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamVideo(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamVideo(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamTimeline(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamTimeline(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

static void* __fastcall Hook_GetISteamUnifiedMessages(void*, int, uint32_t hSteamUser, uint32_t hSteamPipe, const char* pchVersion)
{
    LOG("[Handlers] GetISteamUnifiedMessages(%s)", pchVersion ? pchVersion : "");
    return g_interface_lookup.create_fake_vtable(pchVersion, nullptr);
}

// ============================================================
// SteamNetworkingSockets handlers
// ============================================================

static void* __fastcall Hook_SocketsCreateListenSocket(void*, int, int nVirtualPort, uint32_t nIP, uint16_t nPort, void* pOptions)
{
    return (void*)(uintptr_t)1;
}

static void* __fastcall Hook_SocketsCreateP2PConnectionSocket(void*, int, uint64_t steamIDTarget, int iVirtualPort, int iTimeoutSec, void* pOptions)
{
    return (void*)(uintptr_t)2;
}

static void* __fastcall Hook_SocketsAcceptConnection(void*, int, void* hSocket)
{
    return (void*)(uintptr_t)3;
}

static bool __fastcall Hook_SocketsCloseConnection(void*, int, void* hSocket, int nReason, const char* pszDebug, bool bEnableLinger)
{
    return true;
}

static int __fastcall Hook_SocketsSendMessageToConnection(void*, int, void* hSocket, const void* pData, uint32_t cbData, int nSendFlags, int* pOutMessageNumber)
{
    if (pOutMessageNumber) *pOutMessageNumber = 0;
    return 0;
}

static int __fastcall Hook_SocketsReceiveMessagesOnConnection(void*, int, void* hSocket, void** ppOutMessages, int nMaxMessages)
{
    return 0;
}

static int __fastcall Hook_SocketsReceiveMessagesOnListenSocket(void*, int, void* hSocket, void** ppOutMessages, int nMaxMessages)
{
    return 0;
}

static bool __fastcall Hook_SocketsGetConnectionInfo(void*, int, void* hSocket, void* pInfo)
{
    if (pInfo) memset(pInfo, 0, 32);
    return true;
}

static bool __fastcall Hook_SocketsSetConnectionPollGroup(void*, int, void* hSocket, void* hPollGroup)
{
    return true;
}

static void* __fastcall Hook_SocketsCreatePollGroup(void*, int)
{
    return (void*)(uintptr_t)1;
}

static bool __fastcall Hook_SocketsDestroyPollGroup(void*, int, void* hPollGroup)
{
    return true;
}

static void* __fastcall Hook_SocketsCreateSocketPair(void*, int, void** ppSockets, int nSockets, bool bUseNetwork, void* pOptions)
{
    if (ppSockets)
    {
        ppSockets[0] = 0;
        if (nSockets > 1) ppSockets[1] = 0;
    }
    return (void*)(uintptr_t)1;
}

static bool __fastcall Hook_SocketsInitAuthentication(void*, int)
{
    return true;
}

// ============================================================
// SteamNetworkingUtils handlers
// ============================================================

static void __fastcall Hook_InitRelayNetworkAccess(void*, int)
{
}

static int __fastcall Hook_GetRelayNetworkStatus(void*, int, void* pStatus)
{
    if (pStatus) memset(pStatus, 0, 16);
    return 1;
}

static bool __fastcall Hook_SetConnectionConfigValueFloat(void*, int, void* hConn, int eConfig, float flValue)
{
    return true;
}

static bool __fastcall Hook_SetConnectionConfigValueInt32(void*, int, void* hConn, int eConfig, int32_t nValue)
{
    return true;
}

static bool __fastcall Hook_SetGlobalConfigValueFloat(void*, int, int eConfig, float flValue)
{
    return true;
}

static bool __fastcall Hook_SetGlobalConfigValueInt32(void*, int, int eConfig, int32_t nValue)
{
    return true;
}

static uint64_t __fastcall Hook_UtilsGetTimestamp(void*, int)
{
    return (uint64_t)time(nullptr) * 1000000ULL;
}

// ============================================================
// SteamAppList handlers
// ============================================================

static uint32_t __fastcall Hook_GetNumInstalledApps(void*, int)
{
    return 0;
}

static uint32_t __fastcall Hook_GetInstalledApps(void*, int, uint32_t* pvecAppID, uint32_t unMaxAppIDs)
{
    return 0;
}

static int __fastcall Hook_GetAppName(void*, int, uint32_t nAppID, char* pchName, int cchNameMax)
{
    if (pchName) pchName[0] = 0;
    return 0;
}

static int __fastcall Hook_GetAppInstallDir(void*, int, uint32_t nAppID, char* pchDirectory, int cchNameMax)
{
    if (pchDirectory) pchDirectory[0] = 0;
    return 0;
}

static uint32_t __fastcall Hook_GetAppBuildId(void*, int, uint32_t nAppID)
{
    return 1;
}

// ============================================================
// SteamGameCoordinator handlers
// ============================================================

static int __fastcall Hook_GCSendMessage(void*, int, uint32_t unMsgType, const void* pubData, uint32_t cubData)
{
    return 0;
}

static bool __fastcall Hook_GCIsMessageAvailable(void*, int, uint32_t* pcubMsgSize)
{
    if (pcubMsgSize) *pcubMsgSize = 0;
    return false;
}

static int __fastcall Hook_GCRetrieveMessage(void*, int, uint32_t* punMsgType, void* pubDest, uint32_t cubDest, uint32_t* pcubMsgSize)
{
    if (pcubMsgSize) *pcubMsgSize = 0;
    return 0;
}

// ============================================================
// SteamGameServerStats handlers
// ============================================================

static bool __fastcall Hook_GSStatsRequestUserStats(void*, int, uint64_t steamIDUser)
{
    return true;
}

static bool __fastcall Hook_GSStatsGetUserStatInt(void*, int, uint64_t steamIDUser, const char* pchName, int32_t* pData)
{
    if (pData) *pData = 0;
    return true;
}

static bool __fastcall Hook_GSStatsGetUserStatFloat(void*, int, uint64_t steamIDUser, const char* pchName, float* pData)
{
    if (pData) *pData = 0.0f;
    return true;
}

static bool __fastcall Hook_GSStatsGetUserAchievement(void*, int, uint64_t steamIDUser, const char* pchName, bool* pbAchieved)
{
    if (pbAchieved) *pbAchieved = false;
    return true;
}

static bool __fastcall Hook_GSStatsSetUserStatInt(void*, int, uint64_t steamIDUser, const char* pchName, int32_t nData)
{
    return true;
}

static bool __fastcall Hook_GSStatsSetUserStatFloat(void*, int, uint64_t steamIDUser, const char* pchName, float fData)
{
    return true;
}

static bool __fastcall Hook_GSStatsUpdateUserAvgRateStat(void*, int, uint64_t steamIDUser, const char* pchName, float fCountThisSession, double dSessionLength)
{
    return true;
}

static bool __fastcall Hook_GSStatsSetUserAchievement(void*, int, uint64_t steamIDUser, const char* pchName)
{
    return true;
}

static bool __fastcall Hook_GSStatsClearUserAchievement(void*, int, uint64_t steamIDUser, const char* pchName)
{
    return true;
}

static bool __fastcall Hook_GSStatsStoreUserStats(void*, int, uint64_t steamIDUser)
{
    return true;
}

// ============================================================
// SteamMusicRemote handlers
// ============================================================

static bool __fastcall Hook_RegisterSteamMusicRemote(void*, int, const char* pchName)
{
    return true;
}

static bool __fastcall Hook_DeregisterSteamMusicRemote(void*, int)
{
    return true;
}

static bool __fastcall Hook_BIsCurrentMusicRemote(void*, int)
{
    return true;
}

static bool __fastcall Hook_BActivationSuccess(void*, int, bool bValue)
{
    return true;
}

static bool __fastcall Hook_SetMusicRemoteDisplayName(void*, int, const char* pchDisplayName)
{
    return true;
}

static bool __fastcall Hook_SetMusicRemoteVolume(void*, int, float flValue)
{
    return true;
}

static bool __fastcall Hook_SetMusicRemoteAllowVolume(void*, int, bool bAllow)
{
    return true;
}

static bool __fastcall Hook_RegisterForMusicRemotePlayNext(void*, int)
{
    return true;
}

static bool __fastcall Hook_RegisterForMusicRemotePlayPrevious(void*, int)
{
    return true;
}

static bool __fastcall Hook_SetMusicRemoteCurrentSteamID(void*, int, uint64_t nSteamID)
{
    return true;
}

// ============================================================
// SteamParties handlers
// ============================================================

static uint64_t __fastcall Hook_CreateBeacon(void*, int, uint32_t unOpenSlots, uint64_t* pBeaconLocation, const char* pchConnectString, const char* pchMetadata)
{
    return 0;
}

static void __fastcall Hook_OnJoinGame(void*, int, uint64_t ulBeaconID, uint64_t ulSteamID)
{
}

static bool __fastcall Hook_GetBeaconDetails(void*, int, uint64_t ulBeaconID, uint64_t* pulSteamIDOwner, uint64_t* pBeaconLocation, char* pchMetadata, int cchMetadataMax)
{
    if (pulSteamIDOwner) *pulSteamIDOwner = g_config.steam_emu.steam_id;
    if (pBeaconLocation) memset(pBeaconLocation, 0, 8);
    if (pchMetadata) pchMetadata[0] = 0;
    return true;
}

static void __fastcall Hook_JoinParty(void*, int, uint64_t ulBeaconID)
{
}

static bool __fastcall Hook_StopLeavingBeacon(void*, int, uint64_t ulBeaconID)
{
    return true;
}

static bool __fastcall Hook_DestroyBeacon(void*, int, uint64_t ulBeaconID)
{
    return true;
}

static int __fastcall Hook_GetNumAvailableBeaconLocations(void*, int)
{
    return 0;
}

static bool __fastcall Hook_GetBeaconLocationData(void*, int, uint64_t ulBeaconLocation, int eData, char* pchData, int cchData)
{
    if (pchData) pchData[0] = 0;
    return true;
}

static bool __fastcall Hook_ChangeNumOpenSlots(void*, int, uint64_t ulBeaconID, uint32_t unOpenSlots)
{
    return true;
}

// ============================================================
// SteamRemotePlay handlers
// ============================================================

static uint32_t __fastcall Hook_RemotePlayGetSessionCount(void*, int)
{
    return 0;
}

static uint32_t __fastcall Hook_RemotePlayGetSessionID(void*, int, int iSessionIndex)
{
    return 0;
}

static uint64_t __fastcall Hook_RemotePlayGetSessionSteamID(void*, int, uint32_t unSessionID)
{
    return 0;
}

static const char* __fastcall Hook_RemotePlayGetSessionClientName(void*, int, uint32_t unSessionID)
{
    return "";
}

static int __fastcall Hook_RemotePlayGetSessionClientFormFactor(void*, int, uint32_t unSessionID)
{
    return 0;
}

static bool __fastcall Hook_RemotePlayBSessionHostedOnDesktop(void*, int, uint32_t unSessionID)
{
    return false;
}

static bool __fastcall Hook_RemotePlayGetSessionClientResolution(void*, int, uint32_t unSessionID, int* pnResolutionX, int* pnResolutionY)
{
    if (pnResolutionX) *pnResolutionX = 0;
    if (pnResolutionY) *pnResolutionY = 0;
    return true;
}

static bool __fastcall Hook_RemotePlaySendRemotePlayTogetherInvite(void*, int, uint64_t steamIDFriend)
{
    return true;
}

// ============================================================
// SteamVideo handlers
// ============================================================

static void __fastcall Hook_GetVideoURL(void*, int, uint32_t nAppID)
{
}

static bool __fastcall Hook_IsBroadcasting(void*, int, int* pnNumViewers)
{
    if (pnNumViewers) *pnNumViewers = 0;
    return false;
}

static void __fastcall Hook_GetOPFSettings(void*, int, uint32_t nAppID)
{
}

static bool __fastcall Hook_GetOPFStringForApp(void*, int, uint32_t nAppID, char* pchBuffer, int* pnBufferSize)
{
    if (pchBuffer) pchBuffer[0] = 0;
    if (pnBufferSize) *pnBufferSize = 0;
    return true;
}

// ============================================================
// SteamTimeline handlers
// ============================================================

static bool __fastcall Hook_SetTimelineStateDescription(void*, int, const char* pchDescription, float flTimeDelta)
{
    return true;
}

static bool __fastcall Hook_AddTimelineEvent(void*, int, const char* pchIcon, const char* pchTitle, const char* pchDescription, float flPriority, float flStartOffset, float flDuration, int eEventClipType)
{
    return true;
}

static void __fastcall Hook_ClearTimelineEvents(void*, int)
{
}

static bool __fastcall Hook_SetTimelineGameMode(void*, int, int eTimelineGameMode)
{
    return true;
}

static bool __fastcall Hook_AddTimelineGamePhase(void*, int, const char* pchPhaseName, float flStartOffset, float flDuration)
{
    return true;
}

static void __fastcall Hook_ClearAllTimelineStates(void*, int)
{
}

// ============================================================
// SteamUnifiedMessages handlers
// ============================================================

static uint64_t __fastcall Hook_SendMethod(void*, int, const char* pchServiceMethod, const void* pSerialized, uint32_t unSerialized, uint64_t hCallHandle)
{
    return 0;
}

static bool __fastcall Hook_GetMethodResponseInfo(void*, int, uint64_t hHandle, uint32_t* punResult, uint32_t* punResponseSerialized)
{
    if (punResult) *punResult = 0;
    if (punResponseSerialized) *punResponseSerialized = 0;
    return true;
}

static bool __fastcall Hook_GetMethodResponseData(void*, int, uint64_t hHandle, void* pResponse, uint32_t unResponse, bool bAutoRelease)
{
    return true;
}

static bool __fastcall Hook_ReleaseMethod(void*, int, uint64_t hHandle)
{
    return true;
}

// ============================================================
// STEAMAPPS additional handlers (beyond the basic ones)
// ============================================================

static bool __fastcall Hook_GetAppData(void*, int, uint32_t nAppID, const char* pchKey, char* pchValue, int cchValueMax)
{
    if (pchValue) pchValue[0] = 0;
    return true;
}

static int __fastcall Hook_GetAvailableGameLanguages(void*, int, char* pchLanguages, int cchLanguagesMax)
{
    if (pchLanguages && cchLanguagesMax > 0)
    {
        strcpy_s(pchLanguages, cchLanguagesMax, "english");
        return 8;
    }
    return 0;
}

static bool __fastcall Hook_GetCurrentBetaName(void*, int, char* pchName, int cchNameMax)
{
    if (pchName && cchNameMax > 0) pchName[0] = 0;
    return true;
}

static uint32_t __fastcall Hook_GetEarliestPurchaseTime(void*, int, uint32_t nAppID)
{
    return (uint32_t)time(nullptr) - 86400 * 365;
}

static uint32_t __fastcall Hook_GetInstalledDepots(void*, int, uint32_t nAppID, uint32_t* pvecDepots, uint32_t cMaxDepots)
{
    if (cMaxDepots > 0) pvecDepots[0] = nAppID;
    return cMaxDepots > 0 ? 1 : 0;
}

static const char* __fastcall Hook_GetLaunchQueryParam(void*, int, const char* pchKey)
{
    return "";
}

static bool __fastcall Hook_GetDLCCountEx(void*, int, int* pcDLC, uint32_t* pnAppIdArray, int cAppIdArraySize)
{
    if (pcDLC) *pcDLC = sizeof(g_dlcIDs) / sizeof(g_dlcIDs[0]);
    if (pnAppIdArray)
    {
        int n = sizeof(g_dlcIDs) / sizeof(g_dlcIDs[0]);
        if (n > cAppIdArraySize) n = cAppIdArraySize;
        for (int i = 0; i < n; i++) pnAppIdArray[i] = g_dlcIDs[i];
    }
    return true;
}

// ============================================================
// SteamUtils additional handlers
// ============================================================

static uint32_t __fastcall Hook_GetSecondsSinceAppActive(void*, int)
{
    return 0;
}

static uint32_t __fastcall Hook_GetSecondsSinceComputerActive(void*, int)
{
    return 0;
}

static const char* __fastcall Hook_GetUniverseName(void*, int)
{
    return "Public";
}

static bool __fastcall Hook_IsOverlayEnabledEx(void*, int)
{
    return false;
}

static bool __fastcall Hook_BOverlayNeedsPresent(void*, int)
{
    return false;
}

static uint64_t __fastcall Hook_GetCurrentGameID(void*, int)
{
    return g_config.cold_client.app_id;
}

static int __fastcall Hook_GetIPCountry(void*, int, char* pchCountry, int cchBuffer)
{
    if (pchCountry && cchBuffer > 0)
    {
        pchCountry[0] = 'U'; pchCountry[1] = 'S'; pchCountry[2] = 0;
    }
    return 2;
}

static bool __fastcall Hook_GetImageSize(void*, int, int iImage, uint32_t* pnWidth, uint32_t* pnHeight)
{
    if (pnWidth) *pnWidth = 0;
    if (pnHeight) *pnHeight = 0;
    return false;
}

static bool __fastcall Hook_GetImageRGBA(void*, int, int iImage, uint8_t* pubDest, int nDestBufferSize)
{
    return false;
}

static bool __fastcall Hook_GetCSERIPPort(void*, int, uint32_t* unIP, uint16_t* unPort)
{
    if (unIP) *unIP = 0;
    if (unPort) *unPort = 0;
    return false;
}

static uint32_t __fastcall Hook_GetAppIDUtils(void*, int)
{
    return g_config.cold_client.app_id;
}

static bool __fastcall Hook_SetWarningMessageHook(void*, int, void* pFunction)
{
    return true;
}

static bool __fastcall Hook_IsSteamRunning(void*, int)
{
    return true;
}

static uint64_t __fastcall Hook_GetSteamUILanguage(void*, int, char* pchLanguage, int cchLanguage)
{
    if (pchLanguage && cchLanguage > 0)
    {
        strcpy_s(pchLanguage, cchLanguage, "english");
    }
    return true;
}

// ============================================================
// SteamUser additional handlers
// ============================================================

static uint64_t __fastcall Hook_GetPlayerSteamID(void*, int)
{
    return g_config.steam_emu.steam_id;
}

static int __fastcall Hook_GetUserDataFolder(void*, int, char* pchBuffer, int cubBuffer)
{
    if (pchBuffer && cubBuffer > 0)
    {
        wchar_t exeDir[MAX_PATH];
        GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
        if (lastSlash) lastSlash[1] = 0;
        wcstombs(pchBuffer, exeDir, cubBuffer - 1);
        pchBuffer[cubBuffer - 1] = 0;
    }
    return true;
}

static void __fastcall Hook_StartVoiceRecording(void*, int)
{
}

static void __fastcall Hook_StopVoiceRecording(void*, int)
{
}

static int __fastcall Hook_GetAvailableVoice(void*, int, uint32_t* pcbCompressed, uint32_t* pcbUncompressed, uint32_t nMaxBytes)
{
    if (pcbCompressed) *pcbCompressed = 0;
    if (pcbUncompressed) *pcbUncompressed = 0;
    return 0;
}

static uint32_t __fastcall Hook_GetVoice(void*, int, bool bWantCompressed, void* pDestBuffer, uint32_t cbDestBufferSize, uint32_t* nBytesWritten, bool bWantUncompressed, void* pUncompressedDestBuffer, uint32_t cbUncompressedDestBufferSize, uint32_t* nUncompressBytesWritten, uint32_t nUncompressedVoiceDesiredSampleRate)
{
    if (nBytesWritten) *nBytesWritten = 0;
    if (nUncompressBytesWritten) *nUncompressBytesWritten = 0;
    return 0;
}

static uint32_t __fastcall Hook_DecompressVoice(void*, int, const void* pCompressed, uint32_t cbCompressed, void* pDestBuffer, uint32_t cbDestBufferSize, uint32_t* nBytesWritten, uint32_t nDesiredSampleRate)
{
    if (nBytesWritten) *nBytesWritten = 0;
    return 0;
}

static bool __fastcall Hook_GetGameBadgeLevel(void*, int, int nSeries, bool bFoil)
{
    return false;
}

static int __fastcall Hook_GetPlayerSteamLevel(void*, int)
{
    return 100;
}

// ============================================================
// Register all handlers
// ============================================================

void register_all_steam_handlers()
{
    // --- SteamHTTP (for CEG chunk serving) ---
    register_handler("CreateHTTPRequest", (void*)Hook_CreateHTTPRequest);
    register_handler("SetHTTPRequestHeaderValue", (void*)Hook_SetHTTPRequestHeaderValue);
    register_handler("SetHTTPRequestGetOrPostParameter", (void*)Hook_SetHTTPRequestGetOrPostParameter);
    register_handler("SendHTTPRequest", (void*)Hook_SendHTTPRequest);
    register_handler("SendHTTPRequestAndStreamResponse", (void*)Hook_SendHTTPRequest);
    register_handler("IsHTTPRequestCompleted", (void*)Hook_IsHTTPRequestCompleted);
    register_handler("GetHTTPResponseBodySize", (void*)Hook_GetHTTPResponseBodySize);
    register_handler("GetHTTPResponseBodyData", (void*)Hook_GetHTTPResponseBodyData);
    register_handler("GetHTTPResponseHeaderSize", (void*)Hook_GetHTTPResponseHeaderSize);
    register_handler("GetHTTPResponseHeaderValue", (void*)Hook_GetHTTPResponseHeaderValue);
    register_handler("ReleaseHTTPRequest", (void*)Hook_ReleaseHTTPRequest);
    register_handler("SetHTTPRequestNetworkActivityTimeout", (void*)Hook_SetHTTPRequestNetworkActivityTimeout);
    register_handler("SetHTTPRequestRequiresVerifiedCertificate", (void*)Hook_SetHTTPRequestRequiresVerifiedCertificate);
    register_handler("GetHTTPDownloadProgressPct", (void*)Hook_GetHTTPDownloadProgressPct);
    register_handler("GetHTTPRequestCompletionTime", (void*)Hook_GetHTTPRequestCompletionTime);
    register_handler("GetHTTPResponseBodyData", (void*)Hook_GetHTTPResponseBodyData);

    // --- Auth Tickets ---
    register_handler("GetAuthSessionTicket", (void*)Hook_GetAuthSessionTicket);
    register_handler("GetAuthTicketForWebApi", (void*)Hook_GetAuthTicketForWebApi);
    register_handler("BeginAuthSession", (void*)Hook_BeginAuthSession);
    register_handler("EndAuthSession", (void*)Hook_EndAuthSession);
    register_handler("CancelAuthTicket", (void*)Hook_CancelAuthTicket);
    register_handler("BIsAuthorizedFor", (void*)Hook_BIsAuthorizedFor);

    // --- SteamFriends ---
    register_handler("GetFriendPersonaName", (void*)Hook_GetFriendPersonaName);
    register_handler("GetFriendCount", (void*)Hook_GetFriendCount);
    register_handler("GetFriendByIndex", (void*)Hook_GetFriendByIndex);
    register_handler("GetFriendRelationship", (void*)Hook_GetFriendRelationship);
    register_handler("GetFriendPersonaState", (void*)Hook_GetFriendPersonaState);
    register_handler("GetFriendGamePlayed", (void*)Hook_GetFriendGamePlayed);
    register_handler("GetFriendGamePlayedExtra", (void*)Hook_GetFriendGamePlayedExtra);
    register_handler("GetFriendPersonaStateFlags", (void*)Hook_GetFriendPersonaStateFlags);
    register_handler("SetRichPresence", (void*)Hook_SetRichPresence);
    register_handler("ClearRichPresence", (void*)Hook_ClearRichPresence);
    register_handler("GetFriendRichPresence", (void*)Hook_GetFriendRichPresence);
    register_handler("GetFriendRichPresenceKeyCount", (void*)Hook_GetFriendRichPresenceKeyCount);
    register_handler("GetFriendRichPresenceKeyByIndex", (void*)Hook_GetFriendRichPresenceKeyByIndex);
    register_handler("RequestUserInformation", (void*)Hook_RequestUserInformation);
    register_handler("RequestFriendRichPresence", (void*)Hook_RequestFriendRichPresence);
    register_handler("GetClanByIndex", (void*)Hook_GetClanByIndex);
    register_handler("GetClanCount", (void*)Hook_GetClanCount);
    register_handler("GetClanActivityCounts", (void*)Hook_GetClanActivityCounts);
    register_handler("GetClanName", (void*)Hook_GetClanName);
    register_handler("GetClanTag", (void*)Hook_GetClanTag);
    register_handler("GetClanOfficerList", (void*)Hook_GetClanOfficerList);
    register_handler("GetClanOwner", (void*)Hook_GetClanOwner);
    register_handler("GetClanChatMemberCount", (void*)Hook_GetClanChatMemberCount);
    register_handler("GetCoplayFriendCount", (void*)Hook_GetCoplayFriendCount);
    register_handler("GetCoplayFriend", (void*)Hook_GetCoplayFriend);
    register_handler("GetFriendCoplayGame", (void*)Hook_GetFriendCoplayGame);
    register_handler("GetFriendCoplayTime", (void*)Hook_GetFriendCoplayTime);
    register_handler("GetSmallFriendAvatar", (void*)Hook_GetSmallFriendAvatar);
    register_handler("GetMediumFriendAvatar", (void*)Hook_GetMediumFriendAvatar);
    register_handler("GetLargeFriendAvatar", (void*)Hook_GetLargeFriendAvatar);
    register_handler("SetPlayedWith", (void*)Hook_SetPlayedWith);
    register_handler("ActivateGameOverlay", (void*)Hook_ActivateGameOverlay);
    register_handler("ActivateGameOverlayToUser", (void*)Hook_ActivateGameOverlayToUser);
    register_handler("ActivateGameOverlayToWebPage", (void*)Hook_ActivateGameOverlayToWebPage);
    register_handler("ActivateGameOverlayToStore", (void*)Hook_ActivateGameOverlayToStore);
    register_handler("ActivateGameOverlayInviteDialog", (void*)Hook_ActivateGameOverlayInviteDialog);
    register_handler("SetPersonaName", (void*)Hook_SetPersonaName);
    register_handler("SetPersonaState", (void*)Hook_SetPersonaState);
    register_handler("IsUserInSource", (void*)Hook_IsUserInSource);
    register_handler("GetFriendCountFromSource", (void*)Hook_GetFriendCountFromSource);
    register_handler("GetFriendFromSourceByIndex", (void*)Hook_GetFriendFromSourceByIndex);
    register_handler("GetUserRestrictions", (void*)Hook_GetUserRestrictions);

    // --- SteamUserStats ---
    register_handler("RequestCurrentStats", (void*)Hook_RequestCurrentStats);
    register_handler("GetStat", (void*)Hook_GetStatInt);
    register_handler("GetStat_int32", (void*)Hook_GetStatInt);
    register_handler("GetStat_float", (void*)Hook_GetStatFloat);
    register_handler("SetStat", (void*)Hook_SetStatInt);
    register_handler("SetStat_int32", (void*)Hook_SetStatInt);
    register_handler("SetStat_float", (void*)Hook_SetStatFloat);
    register_handler("UpdateAvgRateStat", (void*)Hook_UpdateAvgRateStat);
    register_handler("GetAchievement", (void*)Hook_GetAchievement);
    register_handler("SetAchievement", (void*)Hook_SetAchievement);
    register_handler("ClearAchievement", (void*)Hook_ClearAchievement);
    register_handler("StoreStats", (void*)Hook_StoreStats);
    register_handler("GetAchievementIcon", (void*)Hook_GetAchievementIcon);
    register_handler("GetAchievementDisplayAttribute", (void*)Hook_GetAchievementDisplayAttribute);
    register_handler("IndicateAchievementProgress", (void*)Hook_IndicateAchievementProgress);
    register_handler("GetNumAchievements", (void*)Hook_GetNumAchievements);
    register_handler("GetAchievementName", (void*)Hook_GetAchievementName);
    register_handler("GetAchievementAchievedPercent", (void*)Hook_GetAchievementAchievedPercent);
    register_handler("GetMostAchievedAchievementInfo", (void*)Hook_GetMostAchievedAchievementInfo);
    register_handler("GetNextMostAchievedAchievementInfo", (void*)Hook_GetNextMostAchievedAchievementInfo);
    register_handler("ResetAllStats", (void*)Hook_ResetAllStats);
    register_handler("FindLeaderboard", (void*)Hook_FindLeaderboard);
    register_handler("FindOrCreateLeaderboard", (void*)Hook_FindOrCreateLeaderboard);
    register_handler("GetLeaderboardSortMethod", (void*)Hook_GetLeaderboardSortMethod);
    register_handler("GetLeaderboardDisplayType", (void*)Hook_GetLeaderboardDisplayType);
    register_handler("GetLeaderboardEntryCount", (void*)Hook_GetLeaderboardEntryCount);
    register_handler("DownloadLeaderboardEntries", (void*)Hook_DownloadLeaderboardEntries);
    register_handler("UploadLeaderboardScore", (void*)Hook_UploadLeaderboardScore);
    register_handler("GetDownloadedLeaderboardEntry", (void*)Hook_GetDownloadedLeaderboardEntry);
    register_handler("GetDownloadedLeaderboardSortMethod", (void*)Hook_GetDownloadedLeaderboardSortMethod);
    register_handler("GetDownloadedLeaderboardDisplayType", (void*)Hook_GetDownloadedLeaderboardDisplayType);
    register_handler("GetDownloadedLeaderboardHandle", (void*)Hook_GetDownloadedLeaderboardHandle);
    register_handler("GetAchievementAndStatProgression", (void*)Hook_GetAchievementAndStatProgressionsCount);
    register_handler("RequestGlobalStats", (void*)Hook_RequestGlobalStats);

    // --- SteamNetworking ---
    register_handler("SendP2PPacket", (void*)Hook_SendP2PPacket);
    register_handler("IsP2PPacketAvailable", (void*)Hook_IsP2PPacketAvailable);
    register_handler("ReadP2PPacket", (void*)Hook_ReadP2PPacket);
    register_handler("AcceptP2PSessionWithUser", (void*)Hook_AcceptP2PSessionWithUser);
    register_handler("CloseP2PSessionWithUser", (void*)Hook_CloseP2PSessionWithUser);
    register_handler("CloseP2PChannelWithUser", (void*)Hook_CloseP2PChannelWithUser);
    register_handler("GetP2PSessionState", (void*)Hook_GetP2PSessionState);
    register_handler("AllowP2PPacketRelay", (void*)Hook_AllowP2PPacketRelay);
    register_handler("CreateP2PConnectionSocket", (void*)Hook_CreateP2PConnectionSocket);
    register_handler("CreateListenSocketP2P", (void*)Hook_CreateListenSocketP2P);
    register_handler("DestroyListenSocket", (void*)Hook_DestroyListenSocket);
    register_handler("DestroyP2PConnectionSocket", (void*)Hook_DestroyP2PConnectionSocket);

    // --- SteamRemoteStorage ---
    register_handler("FileWrite", (void*)Hook_FileWrite);
    register_handler("FileRead", (void*)Hook_FileRead);
    register_handler("FileExists", (void*)Hook_FileExists);
    register_handler("FileDelete", (void*)Hook_FileDelete);
    register_handler("GetFileSize", (void*)Hook_GetFileSize);
    register_handler("GetFileTimestamp", (void*)Hook_GetFileTimestamp);
    register_handler("SetSyncPlatforms", (void*)Hook_SetSyncPlatforms);
    register_handler("GetFileCount", (void*)Hook_GetFileCount);
    register_handler("GetFileNameAndSize", (void*)Hook_GetFileNameAndSize);
    register_handler("GetQuota", (void*)Hook_GetQuota);
    register_handler("SetCloudEnabledForApp", (void*)Hook_SetCloudEnabledForApp);
    register_handler("IsCloudEnabledForApp", (void*)Hook_IsCloudEnabledForApp);
    register_handler("IsCloudEnabledForThisAccount", (void*)Hook_IsCloudEnabledForThisAccount);

    // --- SteamUGC ---
    register_handler("CreateQueryUserUGCRequest", (void*)Hook_CreateQueryUserUGCRequest);
    register_handler("CreateQueryUGCDetailsRequest", (void*)Hook_CreateQueryUGCDetailsRequest);
    register_handler("CreateQueryAllUGCRequest", (void*)Hook_CreateQueryAllUGCRequest);
    register_handler("SendQueryUGCRequest", (void*)Hook_SendQueryUGCRequest);
    register_handler("GetQueryUGCResult", (void*)Hook_GetQueryUGCResult);
    register_handler("ReleaseQueryUGCRequest", (void*)Hook_ReleaseQueryUGCRequest);
    register_handler("AddRequiredTag", (void*)Hook_AddRequiredTag);
    register_handler("AddExcludedTag", (void*)Hook_AddExcludedTag);
    register_handler("SetReturnLongDescription", (void*)Hook_SetReturnLongDescription);
    register_handler("SetReturnTotalOnly", (void*)Hook_SetReturnTotalOnly);
    register_handler("SetAllowCachedResponse", (void*)Hook_SetAllowCachedResponse);
    register_handler("SetCloudFileNameFilter", (void*)Hook_SetCloudFileNameFilter);
    register_handler("SetMatchAnyTag", (void*)Hook_SetMatchAnyTag);
    register_handler("SetSearchText", (void*)Hook_SetSearchText);
    register_handler("SetRankedByTrendDays", (void*)Hook_SetRankedByTrendDays);
    register_handler("AddRequiredKeyValueTag", (void*)Hook_AddRequiredKeyValueTag);
    register_handler("DownloadItem", (void*)Hook_DownloadItem);
    register_handler("GetItemInstallInfo", (void*)Hook_GetItemInstallInfo);
    register_handler("GetItemDownloadInfo", (void*)Hook_GetItemDownloadInfo);
    register_handler("GetQueryUGCPreviewURL", (void*)Hook_GetQueryUGCPreviewURL);
    register_handler("GetQueryUGCNumTags", (void*)Hook_GetQueryUGCNumTags);
    register_handler("GetQueryUGCTag", (void*)Hook_GetQueryUGCTag);
    register_handler("GetNumSubscribedItems", (void*)Hook_GetNumSubscribedItems);
    register_handler("GetSubscribedItems", (void*)Hook_GetSubscribedItems);

    // --- SteamInventory ---
    register_handler("GetResultStatus", (void*)Hook_GetResultStatus);
    register_handler("GetResultItems", (void*)Hook_GetResultItems);
    register_handler("GetResultItemProperty", (void*)Hook_GetResultItemProperty);
    register_handler("GetResultTimestamp", (void*)Hook_GetResultTimestamp);
    register_handler("CheckResultSteamID", (void*)Hook_CheckResultSteamID);
    register_handler("DestroyResult", (void*)Hook_DestroyResult);
    register_handler("GetAllItems", (void*)Hook_GetAllItems);
    register_handler("GetItemsByID", (void*)Hook_GetItemsByID);
    register_handler("GetItemDefinitionIDs", (void*)Hook_GetItemDefinitionIDs);
    register_handler("GetItemDefinitionProperty", (void*)Hook_GetItemDefinitionProperty);
    register_handler("RequestEligiblePromoItemDefinitionsIDs", (void*)Hook_RequestEligiblePromoItemDefinitionsIDs);
    register_handler("GetEligiblePromoItemDefinitionIDs", (void*)Hook_GetEligiblePromoItemDefinitionIDs);
    register_handler("StartPurchase", (void*)Hook_StartPurchase);
    register_handler("RequestPrices", (void*)Hook_RequestPrices);
    register_handler("GetNumItemsWithPrices", (void*)Hook_GetNumItemsWithPrices);

    // --- SteamScreenshots ---
    register_handler("WriteScreenshot", (void*)Hook_WriteScreenshot);
    register_handler("AddScreenshotToLibrary", (void*)Hook_AddScreenshotToLibrary);
    register_handler("TriggerScreenshot", (void*)Hook_TriggerScreenshot);
    register_handler("HookScreenshots", (void*)Hook_HookScreenshots);
    register_handler("SetLocation", (void*)Hook_SetLocation);
    register_handler("TagUser", (void*)Hook_TagUser);
    register_handler("TagPublishedFile", (void*)Hook_TagPublishedFile);
    register_handler("IsScreenshotsHooked", (void*)Hook_IsScreenshotsHooked);

    // --- SteamMatchmaking ---
    register_handler("CreateLobby", (void*)Hook_CreateLobby);
    register_handler("JoinLobby", (void*)Hook_JoinLobby);
    register_handler("LeaveLobby", (void*)Hook_LeaveLobby);
    register_handler("InviteUserToLobby", (void*)Hook_InviteUserToLobby);
    register_handler("GetNumLobbyMembers", (void*)Hook_GetNumLobbyMembers);
    register_handler("GetLobbyMemberByIndex", (void*)Hook_GetLobbyMemberByIndex);
    register_handler("GetLobbyData", (void*)Hook_GetLobbyData);
    register_handler("SetLobbyData", (void*)Hook_SetLobbyData);
    register_handler("GetLobbyDataCount", (void*)Hook_GetLobbyDataCount);
    register_handler("GetLobbyDataByIndex", (void*)Hook_GetLobbyDataByIndex);
    register_handler("DeleteLobbyData", (void*)Hook_DeleteLobbyData);
    register_handler("GetLobbyMemberData", (void*)Hook_GetLobbyMemberData);
    register_handler("SetLobbyMemberData", (void*)Hook_SetLobbyMemberData);
    register_handler("SendLobbyChatMsg", (void*)Hook_SendLobbyChatMsg);
    register_handler("GetLobbyChatEntry", (void*)Hook_GetLobbyChatEntry);
    register_handler("RequestLobbyData", (void*)Hook_RequestLobbyData);
    register_handler("SetLobbyGameServer", (void*)Hook_SetLobbyGameServer);
    register_handler("GetLobbyGameServer", (void*)Hook_GetLobbyGameServer);
    register_handler("SetLobbyMemberLimit", (void*)Hook_SetLobbyMemberLimit);
    register_handler("GetLobbyMemberLimit", (void*)Hook_GetLobbyMemberLimit);
    register_handler("SetLobbyType", (void*)Hook_SetLobbyType);
    register_handler("GetLobbyOwner", (void*)Hook_GetLobbyOwner);
    register_handler("RequestLobbyList", (void*)Hook_RequestLobbyList);
    register_handler("AddRequestLobbyListFilterSlotsAvailable", (void*)Hook_AddRequestLobbyListFilterSlotsAvailable);
    register_handler("GetFavoriteGameCount", (void*)Hook_GetFavoriteGameCount);

    // --- SteamController ---
    register_handler("Init_Controller", (void*)Hook_InitController);
    register_handler("Shutdown_Controller", (void*)Hook_ShutdownController);
    register_handler("RunFrame_Controller", (void*)Hook_RunFrameController);
    register_handler("GetConnectedControllers", (void*)Hook_GetConnectedControllers);
    register_handler("GetControllerForGamepadIndex", (void*)Hook_GetControllerForGamepadIndex);
    register_handler("GetGamepadIndexForController", (void*)Hook_GetGamepadIndexForController);
    register_handler("GetInputTypeForHandle", (void*)Hook_GetInputTypeForHandle);
    register_handler("GetControllerState", (void*)Hook_GetControllerState);
    register_handler("GetDigitalActionHandle", (void*)Hook_GetDigitalActionHandle);
    register_handler("GetDigitalActionData", (void*)Hook_GetDigitalActionData);
    register_handler("GetAnalogActionHandle", (void*)Hook_GetAnalogActionHandle);
    register_handler("GetAnalogActionData", (void*)Hook_GetAnalogActionData);
    register_handler("TriggerRepeatedHapticPulse", (void*)Hook_TriggerRepeatedHapticPulse);
    register_handler("TriggerVibration", (void*)Hook_TriggerVibration);
    register_handler("ShowBindingPanel", (void*)Hook_ShowBindingPanel);

    // --- SteamParentalSettings ---
    register_handler("BIsParentalLockEnabled", (void*)Hook_BIsParentalLockEnabled);
    register_handler("BIsParentalLockLocked", (void*)Hook_BIsParentalLockLocked);
    register_handler("BIsAppBlocked", (void*)Hook_BIsAppBlocked);
    register_handler("BIsAppInBlockList", (void*)Hook_BIsAppInBlockList);
    register_handler("BIsFeatureBlocked", (void*)Hook_BIsFeatureBlocked);
    register_handler("BIsFeatureInBlockList", (void*)Hook_BIsFeatureInBlockList);

    // --- SteamMusic ---
    register_handler("BIsEnabled", (void*)Hook_BIsMusicEnabled);
    register_handler("BIsPlaying", (void*)Hook_BIsMusicPlaying);
    register_handler("GetPlaybackStatus", (void*)Hook_GetMusicPlaybackStatus);
    register_handler("Play", (void*)Hook_MusicPlay);
    register_handler("Pause", (void*)Hook_MusicPause);
    register_handler("Volume", (void*)Hook_MusicVolume);
    register_handler("GetVolume", (void*)Hook_GetMusicVolume);

    // --- SteamHTMLSurface ---
    register_handler("Init_HTMLSurface", (void*)Hook_HTMLInit);
    register_handler("Shutdown_HTMLSurface", (void*)Hook_HTMLShutdown);
    register_handler("CreateBrowser", (void*)Hook_HTMLCreateBrowser);
    register_handler("RemoveBrowser", (void*)Hook_HTMLRemoveBrowser);
    register_handler("LoadURL", (void*)Hook_HTMLLoadURL);
    register_handler("SetSize", (void*)Hook_HTMLSetSize);
    register_handler("StopLoad", (void*)Hook_HTMLStopLoad);
    register_handler("Reload", (void*)Hook_HTMLReload);
    register_handler("GoBack", (void*)Hook_HTMLGoBack);
    register_handler("GoForward", (void*)Hook_HTMLGoForward);
    register_handler("MouseDown", (void*)Hook_HTMLMouseDown);
    register_handler("MouseUp", (void*)Hook_HTMLMouseUp);
    register_handler("MouseMove", (void*)Hook_HTMLMouseMove);
    register_handler("KeyDown", (void*)Hook_HTMLKeyDown);
    register_handler("KeyUp", (void*)Hook_HTMLKeyUp);
    register_handler("MouseWheel", (void*)Hook_HTMLMouseWheel);
    register_handler("GetLinkAtPosition", (void*)Hook_HTMLGetLinkAtPosition);
    register_handler("SetCookie", (void*)Hook_HTMLSetCookie);

    // --- SteamGameServer ---
    register_handler("LogOn_GameServer", (void*)Hook_GSLogOn);
    register_handler("LogOff_GameServer", (void*)Hook_GSLogOff);
    register_handler("BLoggedOn_GameServer", (void*)Hook_GSLoggedOn);
    register_handler("BSecure", (void*)Hook_GSSecure);
    register_handler("GetSteamID_GameServer", (void*)Hook_GSGetSteamID);
    register_handler("ComputeNewAuthTicket", (void*)Hook_GSComputeNewAuthTicket);
    register_handler("CreateUnauthenticatedUser", (void*)Hook_GSCreateUnauthenticatedUser);
    register_handler("GetServerReputation", (void*)Hook_GSGetServerReputation);
    register_handler("GetAuthSessionTicket_GameServer", (void*)Hook_GSGetAuthSessionTicket);
    register_handler("BeginAuthSession_GameServer", (void*)Hook_GSBeginAuthSession);
    register_handler("EndAuthSession_GameServer", (void*)Hook_GSEndAuthSession);
    register_handler("SetHeartbeatData", (void*)Hook_GSSetHeartbeatData);
    register_handler("SetServerType", (void*)Hook_GSSetServerType);
    register_handler("SetGameTags", (void*)Hook_GSSetGameTags);

    // --- SteamClient ---
    register_handler("CreateSteamPipe", (void*)Hook_CreateSteamPipe);
    register_handler("ReleaseSteamPipe", (void*)Hook_ReleaseSteamPipe);
    register_handler("CreateGlobalUser", (void*)Hook_CreateGlobalUser);
    register_handler("ReleaseGlobalUser", (void*)Hook_ReleaseGlobalUser);
    register_handler("GetISteamUser", (void*)Hook_GetISteamUser);
    register_handler("GetISteamGameServer", (void*)Hook_GetISteamGameServer);
    register_handler("GetISteamFriends", (void*)Hook_GetISteamFriends);
    register_handler("GetISteamUtils", (void*)Hook_GetISteamUtils);
    register_handler("GetISteamApps", (void*)Hook_GetISteamApps);
    register_handler("GetISteamMatchmaking", (void*)Hook_GetISteamMatchmaking);
    register_handler("GetISteamNetworking", (void*)Hook_GetISteamNetworking);
    register_handler("GetISteamRemoteStorage", (void*)Hook_GetISteamRemoteStorage);
    register_handler("GetISteamUserStats", (void*)Hook_GetISteamUserStats);
    register_handler("GetISteamScreenshots", (void*)Hook_GetISteamScreenshots);
    register_handler("GetISteamHTTP", (void*)Hook_GetISteamHTTP);
    register_handler("GetISteamUGC", (void*)Hook_GetISteamUGC);
    register_handler("GetISteamInventory", (void*)Hook_GetISteamInventory);
    register_handler("GetISteamParentalSettings", (void*)Hook_GetISteamParentalSettings);
    register_handler("GetISteamMusic", (void*)Hook_GetISteamMusic);
    register_handler("GetISteamMusicRemote", (void*)Hook_GetISteamMusicRemote);
    register_handler("GetISteamHTMLSurface", (void*)Hook_GetISteamHTMLSurface);
    register_handler("GetISteamController", (void*)Hook_GetISteamController);
    register_handler("GetISteamGameServerStats", (void*)Hook_GetISteamGameServerStats);
    register_handler("GetISteamMatchmakingServers", (void*)Hook_GetISteamMatchmakingServers);
    register_handler("GetISteamGameCoordinator", (void*)Hook_GetISteamGameCoordinator);
    register_handler("GetISteamNetworkingSockets", (void*)Hook_GetISteamNetworkingSockets);
    register_handler("GetISteamNetworkingUtils", (void*)Hook_GetISteamNetworkingUtils);
    register_handler("GetISteamAppList", (void*)Hook_GetISteamAppList);
    register_handler("GetISteamParties", (void*)Hook_GetISteamParties);
    register_handler("GetISteamRemotePlay", (void*)Hook_GetISteamRemotePlay);
    register_handler("GetISteamVideo", (void*)Hook_GetISteamVideo);
    register_handler("GetISteamTimeline", (void*)Hook_GetISteamTimeline);
    register_handler("GetISteamUnifiedMessages", (void*)Hook_GetISteamUnifiedMessages);

    // --- SteamNetworkingSockets ---
    register_handler("CreateListenSocket", (void*)Hook_SocketsCreateListenSocket);
    register_handler("CreateP2PConnectionSocket", (void*)Hook_SocketsCreateP2PConnectionSocket);
    register_handler("AcceptConnection", (void*)Hook_SocketsAcceptConnection);
    register_handler("CloseConnection", (void*)Hook_SocketsCloseConnection);
    register_handler("SendMessageToConnection", (void*)Hook_SocketsSendMessageToConnection);
    register_handler("ReceiveMessagesOnConnection", (void*)Hook_SocketsReceiveMessagesOnConnection);
    register_handler("ReceiveMessagesOnListenSocket", (void*)Hook_SocketsReceiveMessagesOnListenSocket);
    register_handler("GetConnectionInfo", (void*)Hook_SocketsGetConnectionInfo);
    register_handler("SetConnectionPollGroup", (void*)Hook_SocketsSetConnectionPollGroup);
    register_handler("CreatePollGroup", (void*)Hook_SocketsCreatePollGroup);
    register_handler("DestroyPollGroup", (void*)Hook_SocketsDestroyPollGroup);
    register_handler("CreateSocketPair", (void*)Hook_SocketsCreateSocketPair);
    register_handler("InitAuthentication", (void*)Hook_SocketsInitAuthentication);

    // --- SteamNetworkingUtils ---
    register_handler("InitRelayNetworkAccess", (void*)Hook_InitRelayNetworkAccess);
    register_handler("GetRelayNetworkStatus", (void*)Hook_GetRelayNetworkStatus);
    register_handler("SetConnectionConfigValueFloat", (void*)Hook_SetConnectionConfigValueFloat);
    register_handler("SetConnectionConfigValueInt32", (void*)Hook_SetConnectionConfigValueInt32);
    register_handler("SetGlobalConfigValueFloat", (void*)Hook_SetGlobalConfigValueFloat);
    register_handler("SetGlobalConfigValueInt32", (void*)Hook_SetGlobalConfigValueInt32);
    register_handler("GetTimestamp", (void*)Hook_UtilsGetTimestamp);

    // --- SteamAppList ---
    register_handler("GetNumInstalledApps", (void*)Hook_GetNumInstalledApps);
    register_handler("GetInstalledApps", (void*)Hook_GetInstalledApps);
    register_handler("GetAppName", (void*)Hook_GetAppName);
    register_handler("GetAppInstallDir", (void*)Hook_GetAppInstallDir);
    register_handler("GetAppBuildId", (void*)Hook_GetAppBuildId);

    // --- SteamGameCoordinator ---
    register_handler("SendMessage", (void*)Hook_GCSendMessage);
    register_handler("IsMessageAvailable", (void*)Hook_GCIsMessageAvailable);
    register_handler("RetrieveMessage", (void*)Hook_GCRetrieveMessage);

    // --- SteamGameServerStats ---
    register_handler("RequestUserStats_GameServer", (void*)Hook_GSStatsRequestUserStats);
    register_handler("GetUserStatInt_GameServer", (void*)Hook_GSStatsGetUserStatInt);
    register_handler("GetUserStatFloat_GameServer", (void*)Hook_GSStatsGetUserStatFloat);
    register_handler("GetUserAchievement_GameServer", (void*)Hook_GSStatsGetUserAchievement);
    register_handler("SetUserStatInt_GameServer", (void*)Hook_GSStatsSetUserStatInt);
    register_handler("SetUserStatFloat_GameServer", (void*)Hook_GSStatsSetUserStatFloat);
    register_handler("StoreUserStats_GameServer", (void*)Hook_GSStatsStoreUserStats);

    // --- SteamMusicRemote ---
    register_handler("RegisterSteamMusicRemote", (void*)Hook_RegisterSteamMusicRemote);
    register_handler("DeregisterSteamMusicRemote", (void*)Hook_DeregisterSteamMusicRemote);
    register_handler("BIsCurrentMusicRemote", (void*)Hook_BIsCurrentMusicRemote);
    register_handler("BActivationSuccess", (void*)Hook_BActivationSuccess);
    register_handler("SetDisplayName_MusicRemote", (void*)Hook_SetMusicRemoteDisplayName);
    register_handler("SetVolume_MusicRemote", (void*)Hook_SetMusicRemoteVolume);
    register_handler("AllowVolume_MusicRemote", (void*)Hook_SetMusicRemoteAllowVolume);
    register_handler("RegisterForPlayNext", (void*)Hook_RegisterForMusicRemotePlayNext);
    register_handler("RegisterForPlayPrevious", (void*)Hook_RegisterForMusicRemotePlayPrevious);
    register_handler("SetCurrentSteamID_MusicRemote", (void*)Hook_SetMusicRemoteCurrentSteamID);

    // --- SteamParties ---
    register_handler("CreateBeacon", (void*)Hook_CreateBeacon);
    register_handler("OnJoinGame", (void*)Hook_OnJoinGame);
    register_handler("GetBeaconDetails", (void*)Hook_GetBeaconDetails);
    register_handler("JoinParty", (void*)Hook_JoinParty);
    register_handler("StopLeavingBeacon", (void*)Hook_StopLeavingBeacon);
    register_handler("DestroyBeacon", (void*)Hook_DestroyBeacon);
    register_handler("GetNumAvailableBeaconLocations", (void*)Hook_GetNumAvailableBeaconLocations);
    register_handler("GetBeaconLocationData", (void*)Hook_GetBeaconLocationData);
    register_handler("ChangeNumOpenSlots", (void*)Hook_ChangeNumOpenSlots);

    // --- SteamRemotePlay ---
    register_handler("GetSessionCount_RemotePlay", (void*)Hook_RemotePlayGetSessionCount);
    register_handler("GetSessionID_RemotePlay", (void*)Hook_RemotePlayGetSessionID);
    register_handler("GetSessionSteamID_RemotePlay", (void*)Hook_RemotePlayGetSessionSteamID);
    register_handler("GetSessionClientName_RemotePlay", (void*)Hook_RemotePlayGetSessionClientName);
    register_handler("GetSessionClientFormFactor_RemotePlay", (void*)Hook_RemotePlayGetSessionClientFormFactor);
    register_handler("BSessionHostedOnDesktop", (void*)Hook_RemotePlayBSessionHostedOnDesktop);
    register_handler("GetSessionClientResolution", (void*)Hook_RemotePlayGetSessionClientResolution);
    register_handler("SendRemotePlayTogetherInvite", (void*)Hook_RemotePlaySendRemotePlayTogetherInvite);

    // --- SteamVideo ---
    register_handler("GetVideoURL", (void*)Hook_GetVideoURL);
    register_handler("IsBroadcasting", (void*)Hook_IsBroadcasting);
    register_handler("GetOPFSettings", (void*)Hook_GetOPFSettings);
    register_handler("GetOPFStringForApp", (void*)Hook_GetOPFStringForApp);

    // --- SteamTimeline ---
    register_handler("SetTimelineStateDescription", (void*)Hook_SetTimelineStateDescription);
    register_handler("AddTimelineEvent", (void*)Hook_AddTimelineEvent);
    register_handler("ClearTimeline_Events", (void*)Hook_ClearTimelineEvents);
    register_handler("SetTimelineGameMode", (void*)Hook_SetTimelineGameMode);
    register_handler("AddTimelineGamePhase", (void*)Hook_AddTimelineGamePhase);
    register_handler("ClearAllTimelineStates", (void*)Hook_ClearAllTimelineStates);

    // --- SteamUnifiedMessages ---
    register_handler("SendMethod", (void*)Hook_SendMethod);
    register_handler("GetMethodResponseInfo", (void*)Hook_GetMethodResponseInfo);
    register_handler("GetMethodResponseData", (void*)Hook_GetMethodResponseData);
    register_handler("ReleaseMethod", (void*)Hook_ReleaseMethod);

    // --- STEAMAPPS additional ---
    register_handler("GetAppData", (void*)Hook_GetAppData);
    register_handler("GetAvailableGameLanguages", (void*)Hook_GetAvailableGameLanguages);
    register_handler("GetCurrentBetaName", (void*)Hook_GetCurrentBetaName);
    register_handler("GetEarliestPurchaseTime", (void*)Hook_GetEarliestPurchaseTime);
    register_handler("GetInstalledDepots", (void*)Hook_GetInstalledDepots);
    register_handler("GetLaunchQueryParam", (void*)Hook_GetLaunchQueryParam);

    // --- SteamUtils additional ---
    register_handler("GetSecondsSinceAppActive", (void*)Hook_GetSecondsSinceAppActive);
    register_handler("GetSecondsSinceComputerActive", (void*)Hook_GetSecondsSinceComputerActive);
    register_handler("GetUniverseName", (void*)Hook_GetUniverseName);
    register_handler("BOverlayNeedsPresent", (void*)Hook_BOverlayNeedsPresent);
    register_handler("GetCurrentGameID", (void*)Hook_GetCurrentGameID);
    register_handler("GetIPCountry", (void*)Hook_GetIPCountry);
    register_handler("GetImageSize", (void*)Hook_GetImageSize);
    register_handler("GetImageRGBA", (void*)Hook_GetImageRGBA);
    register_handler("GetCSERIPPort", (void*)Hook_GetCSERIPPort);
    register_handler("GetAppID_Utils", (void*)Hook_GetAppIDUtils);
    register_handler("SetWarningMessageHook", (void*)Hook_SetWarningMessageHook);
    register_handler("IsSteamRunning_Utils", (void*)Hook_IsSteamRunning);
    register_handler("GetSteamUILanguage", (void*)Hook_GetSteamUILanguage);

    // --- SteamUser additional ---
    register_handler("GetPlayerSteamID", (void*)Hook_GetPlayerSteamID);
    register_handler("GetUserDataFolder", (void*)Hook_GetUserDataFolder);
    register_handler("StartVoiceRecording", (void*)Hook_StartVoiceRecording);
    register_handler("StopVoiceRecording", (void*)Hook_StopVoiceRecording);
    register_handler("GetAvailableVoice", (void*)Hook_GetAvailableVoice);
    register_handler("GetVoice", (void*)Hook_GetVoice);
    register_handler("DecompressVoice", (void*)Hook_DecompressVoice);
    register_handler("GetGameBadgeLevel", (void*)Hook_GetGameBadgeLevel);
    register_handler("GetPlayerSteamLevel", (void*)Hook_GetPlayerSteamLevel);

    LOG("[SteamHandlers] registered ~430+ vtable method handlers across all interfaces");
}

} }
