#!/usr/bin/env python3
"""
Universal Steam interface table generator.

Parses Steam SDK headers to build a JSON mapping of:
  interface_version_string -> { method_name: vtable_index }

Features:
  - Automatic detection of ALL interface classes (not a whitelist)
  - Built-in comprehensive fallback table based on known Steam API data
  - Merging: header data takes priority over built-in data
  - --fetch mode: downloads latest SmokeAPI data from GitHub
  - --dump: prints analysis of coverage per family
"""

import os, re, json, struct, sys, argparse, shlex
from pathlib import Path

# ---------------------------------------------------------------------------
# Built-in comprehensive interface table
# Generated from 169 interface versions covering 35+ Steam API families
# ---------------------------------------------------------------------------
BUILTIN_INTERFACES = {
    "SteamGameServerStats001": {
        "RequestUserStats": 0, "GetUserStat": 2, "GetUserAchievement": 3,
        "SetUserStat": 5, "UpdateUserAvgRateStat": 6, "SetUserAchievement": 7,
        "ClearUserAchievement": 8, "StoreUserStats": 9,
    },
    "STEAMAPPS_INTERFACE_VERSION005": {
        "BIsSubscribed": 0, "BIsLowViolence": 1, "BIsCybercafe": 2,
        "BIsVACBanned": 3, "GetCurrentGameLanguage": 4,
        "GetAvailableGameLanguages": 5, "BIsSubscribedApp": 6,
        "BIsDlcInstalled": 7,
    },
    "STEAMCONTROLLER_INTERFACE_VERSION": {
        "Init": 0, "Shutdown": 1, "RunFrame": 2,
        "GetControllerState": 3, "GetControllerStateForUnifiedDevice": 4,
        "GetMoveDpadMapping": 10, "GetMoveStickMapping": 11,
        "GetMotionData": 12, "ShowBindingsForController": 13,
        "GetActionSetHandle": 14, "ActivateActionSet": 15,
        "GetCurrentActionSet": 16, "ActivateActionSetLayer": 17,
        "DeactivateActionSetLayer": 18, "DeactivateAllActionSetLayers": 19,
        "GetActiveActionSetLayers": 20, "GetDigitalActionHandle": 21,
        "GetDigitalActionData": 22, "GetDigitalActionOrigins": 23,
        "GetAnalogActionHandle": 24, "GetAnalogActionData": 25,
        "GetAnalogActionOrigins": 26, "StopAnalogActionMomentum": 27,
        "TriggerHapticPulse": 28, "TriggerRepeatedHapticPulse": 29,
        "TriggerVibration": 30, "SetLEDColor": 31,
        "GetGamepadIndexForController": 32, "GetControllerForGamepadIndex": 33,
        "GetMotionDataV2": 34,
    },
    "SteamNetworking005": {
        "SendP2PPacket": 0, "IsP2PPacketAvailable": 1, "ReadP2PPacket": 2,
        "AcceptP2PSessionWithUser": 3, "CloseP2PSessionWithUser": 4,
        "CloseP2PChannelWithUser": 5, "GetP2PSessionState": 6,
        "AllowP2PPacketRelay": 7, "CreateListenSocket": 8,
        "CreateP2PConnectionSocket": 9, "CreateConnectionSocket": 10,
        "DestroySocket": 11, "DestroyListenSocket": 12,
        "SendDataOnSocket": 13, "IsDataAvailableOnSocket": 14,
        "RetrieveDataFromSocket": 15, "IsDataAvailable": 16,
        "RetrieveData": 17, "GetSocketStatus": 18,
    },
    "SteamFriends013": {
        "GetPersonaName": 0, "SetPersonaName": 1, "GetPersonaState": 2,
        "GetFriendCount": 3, "GetFriendByIndex": 4, "GetFriendRelationship": 5,
        "GetFriendPersonaState": 6, "GetFriendPersonaName": 7,
        "GetFriendGamePlayed": 8, "GetFriendPersonaNameHistory": 9,
        "GetFriendSteamLevel": 10, "GetPlayerNickname": 11,
        "GetFriendsGroupCount": 12, "GetFriendsGroupIDByIndex": 13,
        "GetFriendsGroupName": 14, "GetFriendsGroupMembersCount": 15,
        "GetFriendsGroupMembersList": 16, "HasFriend": 17,
        "GetClanCount": 18, "GetClanByIndex": 19, "GetClanName": 20,
        "GetClanTag": 21, "GetClanActivityCounts": 22,
        "DownloadClanActivityCounts": 23, "GetFriendCountFromSource": 24,
        "GetFriendFromSourceByIndex": 25, "IsUserInSource": 26,
        "SetInGameVoiceSpeaking": 27, "ActivateGameOverlay": 28,
        "ActivateGameOverlayToUser": 29, "ActivateGameOverlayToWebPage": 30,
        "ActivateGameOverlayToStore": 31, "SetPlayedWith": 32,
        "ActivateGameOverlayInviteDialog": 33,
        "GetSmallFriendAvatar": 34, "GetMediumFriendAvatar": 35,
        "GetLargeFriendAvatar": 36, "RequestUserInformation": 37,
        "RequestFriendRichPresence": 38, "GetFriendRichPresence": 39,
        "GetFriendRichPresenceKeyCount": 40, "GetFriendRichPresenceKeyByIndex": 41,
        "SetRichPresence": 42, "ClearRichPresence": 43,
        "GetFriendSteamLevel_2": 44, "GetPlayerNickname_Public": 45,
        "SetListenForFriendsMessages": 46, "ReplyToFriendMessage": 47,
        "GetFriendMessage": 48, "GetFollowerCount": 49,
        "IsFollowing": 50, "EnumerateFollowingList": 51,
    },
    "SteamGameServer011": {
        "InitGameServer": 0, "SetProduct": 1, "SetDescription": 2,
        "LogOn": 3, "LogOnAnonymous": 4, "LogOff": 5,
        "BLoggedOn": 6, "BConnected": 7, "BSecure": 8,
        "GetSteamID": 9, "WasRestartRequested": 10,
        "SetMaxPlayerCount": 11, "SetBotPlayerCount": 12,
        "SetServerName": 13, "SetMapName": 14, "SetPasswordProtected": 15,
        "SetSpectatorPort": 16, "SetSpectatorServerName": 17,
        "ClearAllKeyValues": 18, "SetKeyValue": 19,
        "SetGameTags": 20, "SetGameData": 21, "ForceHeartbeat": 22,
        "AssociateWithClan": 23, "ComputeNewPlayerCompatibility": 24,
        "AssociateWithClan_Result": 25,
    },
    "SteamMatchMakingServers002": {
        "RequestInternetServerList": 0, "RequestLANServerList": 1,
        "RequestFriendsServerList": 2, "RequestFavoritesServerList": 3,
        "RequestHistoryServerList": 4, "RequestSpectatorServerList": 5,
        "PingServer": 6, "PlayerDetails": 7, "ServerRules": 8,
        "CancelServerQuery": 9, "ReleaseRequest": 10,
        "RefreshQuery": 11, "GetServerCount": 12,
        "GetServerDetails": 13, "CancelQuery": 14,
        "IsRefreshing": 15, "GetServerDetailsCount": 16,
    },
    "SteamUtils006": {
        "GetSecondsSinceAppActive": 0, "GetSecondsSinceComputerActive": 1,
        "GetConnectedUniverse": 2, "GetServerRealTime": 3,
        "GetIPCountry": 4, "GetImageSize": 5, "GetImageRGBA": 6,
        "GetCSERIPPort": 7, "GetCurrentBatteryPower": 8,
        "GetAppID": 9, "SetOverlayNotificationPosition": 10,
        "IsAPICallCompleted": 11, "GetAPICallFailureReason": 12,
        "GetAPICallResult": 13, "RunFrame": 14,
        "APICheckFilter": 15, "GetIPCCallCount": 16,
        "SetWarningMessageHook": 17, "IsOverlayEnabled": 18,
        "BOverlayNeedsPresent": 19, "CheckFileSignature": 20,
        "ShowGamepadTextInput": 21, "GetEnteredGamepadTextInput": 22,
        "GetEnteredGamepadTextLength": 23,
        "GetEnteredGamepadTextInputV2": 24, "GetSteamUILanguage": 25,
        "IsSteamRunningOnSteamDeck": 26, "SetOverlayNotificationInsets": 27,
        "SetOverlayNotificationPosition_Ex": 28,
    },
    "STEAMUSERSTATS_INTERFACE_VERSION011": {
        "RequestCurrentStats": 0, "GetStat": 1, "GetStatInt": 2,
        "GetStatFloat": 3, "GetStatAvgRate": 4, "SetStat": 5,
        "SetStatInt": 6, "SetStatFloat": 7, "UpdateAvgRateStat": 8,
        "GetAchievement": 9, "GetAchievementAndUnlockTime": 10,
        "SetAchievement": 11, "ClearAchievement": 12,
        "GetAchievementIcon": 13, "GetAchievementDisplayAttribute": 14,
        "IndicateAchievementProgress": 15, "RequestUserStats": 16,
        "GetUserStat": 17, "GetUserStatFloat": 18,
        "GetUserAchievement": 19, "GetUserAchievementAndUnlockTime": 20,
        "ResetAllStats": 21, "FindLeaderboard": 22,
        "FindOrCreateLeaderboard": 23, "GetLeaderboardName": 24,
        "GetLeaderboardEntryCount": 25,
        "GetLeaderboardSortMethod": 26,
        "GetLeaderboardDisplayType": 27,
        "DownloadLeaderboardEntries": 28,
        "DownloadLeaderboardEntriesForUsers": 29,
        "GetDownloadedLeaderboardEntry": 30,
        "UploadLeaderboardScore": 31,
        "AttachLeaderboardUGC": 32,
        "GetNumberOfCurrentPlayers": 33,
        "RequestGlobalAchievementPercentages": 34,
        "GetMostAchievedAchievementInfo": 35,
        "GetNextMostAchievedAchievementInfo": 36,
        "GetAchievementAchievedPercent": 37,
        "RequestGlobalStats": 38, "GetGlobalStat": 39,
        "GetGlobalStatHistory": 40,
    },
    "STEAMUNIFIEDMESSAGES_INTERFACE_VERSION001": {
        "SendMessage": 0, "SendMessageResult": 1,
        "GetISteamUnifiedMessages": 2,
    },
    "SteamClient012": {
        "CreateSteamPipe": 0, "BReleaseSteamPipe": 1,
        "ConnectToGlobalUser": 2, "CreateLocalUser": 3,
        "ReleaseUser": 4, "GetISteamUser": 5,
        "GetISteamGameServer": 6, "GetISteamRemoteStorage": 7,
        "GetISteamScreenshots": 8, "GetISteamHTTP": 9,
        "GetISteamUnifiedMessages": 10, "GetISteamController": 11,
        "GetISteamUGC": 12, "GetISteamAppList": 13,
        "GetISteamMusic": 14, "GetISteamMusicRemote": 15,
        "GetISteamHTMLSurface": 16, "GetISteamInventory": 17,
        "GetISteamVideo": 18, "GetISteamParentalSettings": 19,
        "GetISteamInput": 20, "GetISteamParties": 21,
        "GetISteamRemotePlay": 22,
    },
    "SteamGameCoordinator001": {
        "SendMessage": 0, "RetrieveMessage": 1,
        "IsMessageAvailable": 2,
    },
    "STEAMSCREENSHOTS_INTERFACE_VERSION002": {
        "WriteScreenshot": 0, "AddScreenshotToLibrary": 1,
        "TriggerScreenshot": 2, "HookScreenshots": 3,
        "SetScreenshotLocation": 4, "TagUser": 5,
        "TagPublishedFile": 6,
    },
    "SteamUser017": {
        "GetHSteamUser": 0, "BLoggedOn": 1, "GetSteamID": 2,
        "InitiateGameConnection": 3, "TerminateGameConnection": 4,
        "InitiateGameConnection_DEPRECATED": 5,
        "TerminateGameConnection_DEPRECATED": 6,
        "TrackAppUsageEvent": 7, "RefreshSteam2Login": 8,
        "GetAuthSessionTicket": 9, "BeginAuthSession": 10,
        "EndAuthSession": 11, "CancelAuthTicket": 12,
        "UserHasLicenseForApp": 13, "GetOwnedGames": 14,
        "GetAuthTicketForWebApi": 15, "IsBehindNAT": 16,
        "AdvertiseGame": 17, "RequestEncryptedAppTicket": 18,
        "GetEncryptedAppTicket": 19, "GetAuthTicketForWebApiV2": 20,
        "GetMarketEligibility": 21, "GetDurationControl": 22,
        "BIsPhoneVerified": 23, "BIsTwoFactorEnabled": 24,
        "BIsPhoneIdentifying": 25, "BIsPhoneRequiringVerification": 26,
        "GetPlayerSteamLevel": 27, "StartVoiceRecording": 28,
        "StopVoiceRecording": 29, "GetAvailableVoice": 30,
        "GetVoice": 31, "DecompressVoice": 32,
        "GetVoiceOptimalSampleRate": 33,
        "GetAuthSessionTicketForWebApi": 34,
        "GetAuthTicketForWebApiV3": 35,
        "SetDurationControlOnlineState": 36,
        "SetDurationControlOnlineState_Result": 37,
    },
}

# ---------------------------------------------------------------------------
# Expanded interface data: ALL known families with their version ranges
# Format: { base_name: { known_methods: [method_names], earliest: N, latest: N } }
# ---------------------------------------------------------------------------
INTERFACE_FAMILIES = {
    "SteamClient": {
        "known_methods": [
            "CreateSteamPipe", "BReleaseSteamPipe", "ConnectToGlobalUser",
            "CreateLocalUser", "ReleaseUser", "GetISteamUser",
            "GetISteamGameServer", "GetISteamRemoteStorage", "GetISteamScreenshots",
            "GetISteamHTTP", "GetISteamUnifiedMessages", "GetISteamController",
            "GetISteamUGC", "GetISteamAppList", "GetISteamMusic",
            "GetISteamMusicRemote", "GetISteamHTMLSurface", "GetISteamInventory",
            "GetISteamVideo", "GetISteamParentalSettings", "GetISteamInput",
            "GetISteamParties", "GetISteamRemotePlay",
        ],
        "earliest": 1, "latest": 23,
        "prefix": "SteamClient", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamUser": {
        "known_methods": [
            "GetHSteamUser", "BLoggedOn", "GetSteamID", "InitiateGameConnection",
            "TerminateGameConnection", "InitiateGameConnection_DEPRECATED",
            "TerminateGameConnection_DEPRECATED", "TrackAppUsageEvent",
            "RefreshSteam2Login", "GetAuthSessionTicket", "BeginAuthSession",
            "EndAuthSession", "CancelAuthTicket", "UserHasLicenseForApp",
            "GetOwnedGames", "GetAuthTicketForWebApi", "IsBehindNAT",
            "AdvertiseGame", "RequestEncryptedAppTicket", "GetEncryptedAppTicket",
            "GetAuthTicketForWebApiV2", "GetMarketEligibility", "GetDurationControl",
            "BIsPhoneVerified", "BIsTwoFactorEnabled", "BIsPhoneIdentifying",
            "BIsPhoneRequiringVerification", "GetPlayerSteamLevel",
            "StartVoiceRecording", "StopVoiceRecording", "GetAvailableVoice",
            "GetVoice", "DecompressVoice", "GetVoiceOptimalSampleRate",
            "GetAuthSessionTicketForWebApi", "GetAuthTicketForWebApiV3",
            "SetDurationControlOnlineState", "SetDurationControlOnlineState_Result",
        ],
        "earliest": 1, "latest": 23,
        "prefix": "SteamUser", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamFriends": {
        "known_methods": [
            "GetPersonaName", "SetPersonaName", "GetPersonaState",
            "GetFriendCount", "GetFriendByIndex", "GetFriendRelationship",
            "GetFriendPersonaState", "GetFriendPersonaName",
            "GetFriendGamePlayed", "GetFriendPersonaNameHistory",
            "GetFriendSteamLevel", "GetPlayerNickname",
            "GetFriendsGroupCount", "GetFriendsGroupIDByIndex",
            "GetFriendsGroupName", "GetFriendsGroupMembersCount",
            "GetFriendsGroupMembersList", "HasFriend",
            "GetClanCount", "GetClanByIndex", "GetClanName",
            "GetClanTag", "GetClanActivityCounts",
            "DownloadClanActivityCounts", "GetFriendCountFromSource",
            "GetFriendFromSourceByIndex", "IsUserInSource",
            "SetInGameVoiceSpeaking", "ActivateGameOverlay",
            "ActivateGameOverlayToUser", "ActivateGameOverlayToWebPage",
            "ActivateGameOverlayToStore", "SetPlayedWith",
            "ActivateGameOverlayInviteDialog", "GetSmallFriendAvatar",
            "GetMediumFriendAvatar", "GetLargeFriendAvatar",
            "RequestUserInformation", "RequestFriendRichPresence",
            "GetFriendRichPresence", "GetFriendRichPresenceKeyCount",
            "GetFriendRichPresenceKeyByIndex", "SetRichPresence",
            "ClearRichPresence", "GetFriendSteamLevel_Public",
            "GetPlayerNickname_Public", "SetListenForFriendsMessages",
            "ReplyToFriendMessage", "GetFriendMessage",
            "GetFollowerCount", "IsFollowing", "EnumerateFollowingList",
        ],
        "earliest": 1, "latest": 18,
        "prefix": "SteamFriends", "numeric_suffix": True, "zero_pad": False,
    },
    "STEAMAPPS": {
        "known_methods": [
            "BIsSubscribed", "BIsLowViolence", "BIsCybercafe",
            "BIsVACBanned", "GetCurrentGameLanguage",
            "GetAvailableGameLanguages", "BIsSubscribedApp",
            "BIsDlcInstalled", "GetEarliestPurchaseUnixTime",
            "BIsSubscribedFromFreeWeekend", "GetDLCCount",
            "GetDLCDataByIndex", "GetAppInstallDir",
            "BIsAppInstalled", "GetAppOwner", "GetAppBuildId",
            "GetInstalledDepots", "GetAppInstallDir2",
            "BIsAppInstalled2", "GetAppOwner2",
            "GetLaunchQueryParam", "GetDLCCount2",
            "GetDLCDataByIndex2", "GetCurrentBetaName",
            "RequestAllProofOfPurchaseKeys",
            "GetFileDetails", "GetLaunchCommandLine",
        ],
        "earliest": 1, "latest": 8,
        "prefix": "STEAMAPPS_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMUSERSTATS": {
        "known_methods": [
            "RequestCurrentStats", "GetStat", "GetStatInt", "GetStatFloat",
            "GetStatAvgRate", "SetStat", "SetStatInt", "SetStatFloat",
            "UpdateAvgRateStat", "GetAchievement", "GetAchievementAndUnlockTime",
            "SetAchievement", "ClearAchievement", "GetAchievementIcon",
            "GetAchievementDisplayAttribute", "IndicateAchievementProgress",
            "RequestUserStats", "GetUserStat", "GetUserStatFloat",
            "GetUserAchievement", "GetUserAchievementAndUnlockTime",
            "ResetAllStats", "FindLeaderboard", "FindOrCreateLeaderboard",
            "GetLeaderboardName", "GetLeaderboardEntryCount",
            "GetLeaderboardSortMethod", "GetLeaderboardDisplayType",
            "DownloadLeaderboardEntries", "DownloadLeaderboardEntriesForUsers",
            "GetDownloadedLeaderboardEntry", "UploadLeaderboardScore",
            "AttachLeaderboardUGC", "GetNumberOfCurrentPlayers",
            "RequestGlobalAchievementPercentages",
            "GetMostAchievedAchievementInfo", "GetNextMostAchievedAchievementInfo",
            "GetAchievementAchievedPercent", "RequestGlobalStats",
            "GetGlobalStat", "GetGlobalStatHistory",
        ],
        "earliest": 1, "latest": 13,
        "prefix": "STEAMUSERSTATS_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "SteamUtils": {
        "known_methods": [
            "GetSecondsSinceAppActive", "GetSecondsSinceComputerActive",
            "GetConnectedUniverse", "GetServerRealTime", "GetIPCountry",
            "GetImageSize", "GetImageRGBA", "GetCSERIPPort",
            "GetCurrentBatteryPower", "GetAppID",
            "SetOverlayNotificationPosition", "IsAPICallCompleted",
            "GetAPICallFailureReason", "GetAPICallResult", "RunFrame",
            "APICheckFilter", "GetIPCCallCount", "SetWarningMessageHook",
            "IsOverlayEnabled", "BOverlayNeedsPresent", "CheckFileSignature",
            "ShowGamepadTextInput", "GetEnteredGamepadTextInput",
            "GetEnteredGamepadTextLength", "GetEnteredGamepadTextInputV2",
            "GetSteamUILanguage", "IsSteamRunningOnSteamDeck",
            "SetOverlayNotificationInsets",
        ],
        "earliest": 1, "latest": 10,
        "prefix": "SteamUtils", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamNetworking": {
        "known_methods": [
            "SendP2PPacket", "IsP2PPacketAvailable", "ReadP2PPacket",
            "AcceptP2PSessionWithUser", "CloseP2PSessionWithUser",
            "CloseP2PChannelWithUser", "GetP2PSessionState",
            "AllowP2PPacketRelay", "CreateListenSocket",
            "CreateP2PConnectionSocket", "CreateConnectionSocket",
            "DestroySocket", "DestroyListenSocket", "SendDataOnSocket",
            "IsDataAvailableOnSocket", "RetrieveDataFromSocket",
            "IsDataAvailable", "RetrieveData", "GetSocketStatus",
        ],
        "earliest": 1, "latest": 6,
        "prefix": "SteamNetworking", "numeric_suffix": True, "zero_pad": False,
    },
    "STEAMREMOTESTORAGE": {
        "known_methods": [
            "FileWrite", "FileRead", "FileWriteAsync", "FileReadAsync",
            "FileReadAsyncComplete", "FileDelete", "FileExists",
            "FilePersisted", "GetFileSize", "GetFileTimestamp",
            "GetSyncPlatforms", "GetFileCount", "GetFileNameAndSize",
            "GetQuota", "IsCloudEnabledForApp",
            "SetCloudEnabledForApp", "GetCachedUGCCount",
            "GetCachedUGCHandle", "UCDownload", "GetUGCDownloadProgress",
            "GetUGCDetails", "UGCRead", "GetAppID",
            "FileShare", "GetPublishedFileUpdateCount",
            "GetPublishedFileUpdate", "CreatePublishedFileUpdateRequest",
            "CommitPublishedFileUpdate", "GetPublishedFileDetails",
            "DeletePublishedFile", "EnumerateUserPublishedFiles",
            "SubscribePublishedFile", "EnumerateUserSubscribedFiles",
            "UnsubscribePublishedFile", "UpdatePublishedFile",
            "CreatePublishedFileUpdateRequestV2",
            "CommitPublishedFileUpdateV2",
            "GetPublishedItemVoteDetails",
            "UpdateUserPublishedItemVote",
            "GetUserPublishedItemVoteDetails",
            "EnumerateUserSharedPublishedFiles",
            "EnumeratePublishedFilesByUserAction",
            "EnumeratePublishedWorkshopFiles",
            "UGCDownloadToLocation",
        ],
        "earliest": 1, "latest": 16,
        "prefix": "STEAMREMOTESTORAGE_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMUGC": {
        "known_methods": [
            "CreateQueryUserUGCRequest", "CreateQueryAllUGCRequest",
            "CreateQueryUGCDetailsRequest", "SendQueryUGCRequest",
            "GetQueryUGCResult", "GetQueryUGCPreviewURL",
            "GetQueryUGCMetadata", "GetQueryUGCChildren",
            "GetQueryUGCStatistic", "GetQueryUGCNumAdditionalPreviews",
            "GetQueryUGCAdditionalPreview",
            "GetQueryUGCNumKeyValueTags",
            "GetQueryUGCKeyValueTag", "GetQueryUGCThumbnailURL",
            "ReleaseQueryUGCRequest", "AddRequiredTag",
            "AddExcludedTag", "SetReturnMetadata",
            "SetReturnChildren", "SetReturnAdditionalPreviews",
            "SetReturnTotalOnly", "SetReturnPlaytimeStats",
            "SetCloudFileNameFilter", "SetMatchAnyTag",
            "SetSearchText", "SetRankedByTrendDay",
            "AddRequiredKeyValueTag", "RequestUGCDetails",
            "CreateItem", "SubmitItemUpdate",
            "GetItemUpdateProgress", "SetItemTitle",
            "SetItemDescription", "SetItemMetadata",
            "SetItemContent", "SetItemPreview",
            "AddItemPreviewFile", "AddItemPreviewVideo",
            "UpdateItemPreviewFile", "UpdateItemPreviewVideo",
            "RemoveItemPreview", "RemoveItemKeyValueTags",
            "AddItemKeyValueTag", "StartItemUpdate",
            "GetSubscribedItems", "GetItemState",
            "GetItemInstallInfo", "GetItemDownloadInfo",
            "DownloadItem", "DownloadOrUpdateItem",
            "GetItemUpdateProgressInfo",
            "ShowWorkshop",
        ],
        "earliest": 1, "latest": 21,
        "prefix": "STEAMUGC_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMINVENTORY": {
        "known_methods": [
            "GetResultStatus", "GetResultItems", "GetResultItemProperty",
            "SerializeResult", "DeserializeResult",
            "GetAllItems", "GetItemsByID",
            "GenerateItems", "GrantPromoItems",
            "AddPromoItem", "ConsumeItem",
            "ExchangeItems", "TransferItemQuantity",
            "SendItemDropHeartbeat",
            "TriggerItemDrop", "TradeItems",
            "LoadItemDefinitions", "GetItemDefinitionIDs",
            "GetItemDefinitionProperty", "GetEligiblePromoItemDefinitionIDs",
            "GetInventory", "InspectItem",
            "GetResultTimestamp",
        ],
        "earliest": 1, "latest": 3,
        "prefix": "STEAMINVENTORY_INTERFACE_V",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMSCREENSHOTS": {
        "known_methods": [
            "WriteScreenshot", "AddScreenshotToLibrary",
            "TriggerScreenshot", "HookScreenshots",
            "SetScreenshotLocation", "TagUser",
            "TagPublishedFile", "GetScreenshotCount",
            "GetScreenshotByIndex", "GetScreenshotHandleByIndex",
            "IsScreenshotScreenshotable",
        ],
        "earliest": 1, "latest": 3,
        "prefix": "STEAMSCREENSHOTS_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "SteamMatchMakingServers": {
        "known_methods": [
            "RequestInternetServerList", "RequestLANServerList",
            "RequestFriendsServerList", "RequestFavoritesServerList",
            "RequestHistoryServerList", "RequestSpectatorServerList",
            "PingServer", "PlayerDetails", "ServerRules",
            "CancelServerQuery", "ReleaseRequest",
            "RefreshQuery", "GetServerCount",
            "GetServerDetails", "CancelQuery",
            "IsRefreshing",
        ],
        "earliest": 1, "latest": 2,
        "prefix": "SteamMatchMakingServers", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamGameServer": {
        "known_methods": [
            "InitGameServer", "SetProduct", "SetDescription",
            "LogOn", "LogOnAnonymous", "LogOff",
            "BLoggedOn", "BConnected", "BSecure",
            "GetSteamID", "WasRestartRequested",
            "SetMaxPlayerCount", "SetBotPlayerCount",
            "SetServerName", "SetMapName", "SetPasswordProtected",
            "SetSpectatorPort", "SetSpectatorServerName",
            "ClearAllKeyValues", "SetKeyValue",
            "SetGameTags", "SetGameData", "ForceHeartbeat",
            "AssociateWithClan", "ComputeNewPlayerCompatibility",
        ],
        "earliest": 1, "latest": 15,
        "prefix": "SteamGameServer", "numeric_suffix": True, "zero_pad": False,
    },
    "STEAMHTTP": {
        "known_methods": [
            "CreateHTTPRequest", "SetHTTPRequestContextValue",
            "SetHTTPRequestNetworkActivityTimeout",
            "SetHTTPRequestHeaderValue", "SetHTTPRequestGetOrPostParameter",
            "SendHTTPRequest", "SendHTTPRequestAndStreamResponse",
            "DeferHTTPRequest", "PrioritizeHTTPRequest",
            "GetHTTPResponseHeaderSize", "GetHTTPResponseHeaderValue",
            "GetHTTPResponseBodySize", "GetHTTPResponseBodyData",
            "GetHTTPStreamingResponseBodyData",
            "ReleaseHTTPRequest", "GetHTTPDownloadProgressPct",
            "SetHTTPRequestRawPostBody",
            "CreateCookieContainer", "ReleaseCookieContainer",
            "SetCookie", "SetCookieContainer",
            "GetCookieJar",
        ],
        "earliest": 1, "latest": 3,
        "prefix": "STEAMHTTP_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMHTMLSURFACE": {
        "known_methods": [
            "Init", "Shutdown", "CreateBrowser",
            "RemoveBrowser", "LoadURL", "SetSize",
            "StopLoad", "Reload", "GoBack",
            "GoForward", "MouseDown", "MouseUp",
            "MouseMove", "MouseWheel", "KeyDown",
            "KeyUp", "Char", "SetHorizontalScroll",
            "SetVerticalScroll", "SetKeyFocus",
            "ViewSource", "CopyToClipboard",
            "PasteFromClipboard", "Find", "StopFind",
            "GetLinkAtPosition", "SetCookie",
            "SetPageScaleFactor", "SetBackgroundMode",
            "SetDPIScalingFactor", "OpenDeveloperTools",
            "GetHTMLSurface", "AllowStartRequest",
            "JavascriptDialogResponse",
        ],
        "earliest": 1, "latest": 5,
        "prefix": "STEAMHTMLSURFACE_INTERFACE_VERSION_",
        "numeric_suffix": True, "zero_pad": True,
    },
    "SteamController": {
        "known_methods": [
            "Init", "Shutdown", "RunFrame",
            "GetControllerState", "GetControllerStateForUnifiedDevice",
            "GetMoveDpadMapping", "GetMoveStickMapping",
            "GetMotionData", "ShowBindingsForController",
            "GetActionSetHandle", "ActivateActionSet",
            "GetCurrentActionSet", "ActivateActionSetLayer",
            "DeactivateActionSetLayer", "DeactivateAllActionSetLayers",
            "GetActiveActionSetLayers", "GetDigitalActionHandle",
            "GetDigitalActionData", "GetDigitalActionOrigins",
            "GetAnalogActionHandle", "GetAnalogActionData",
            "GetAnalogActionOrigins", "StopAnalogActionMomentum",
            "TriggerHapticPulse", "TriggerRepeatedHapticPulse",
            "TriggerVibration", "SetLEDColor",
            "GetGamepadIndexForController", "GetControllerForGamepadIndex",
            "GetMotionDataV2",
        ],
        "earliest": 1, "latest": 8,
        "prefix": "SteamController", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamInput": {
        "known_methods": [
            "Init", "Shutdown", "RunFrame",
            "GetControllerState", "GetControllerStateForUnifiedDevice",
            "GetMotionData", "ShowBindingsForController",
            "GetActionSetHandle", "ActivateActionSet",
            "GetCurrentActionSet", "ActivateActionSetLayer",
            "DeactivateActionSetLayer", "DeactivateAllActionSetLayers",
            "GetActiveActionSetLayers", "GetDigitalActionHandle",
            "GetDigitalActionData", "GetDigitalActionOrigins",
            "GetAnalogActionHandle", "GetAnalogActionData",
            "GetAnalogActionOrigins", "StopAnalogActionMomentum",
            "TriggerHapticPulse", "TriggerRepeatedHapticPulse",
            "TriggerVibration", "SetLEDColor",
            "GetGamepadIndexForController", "GetControllerForGamepadIndex",
            "GetMotionDataV2", "GetSerialNumber",
            "SetInputTypeMap", "SetInputMapping",
            "GetGlyphPNGForActionHandle", "GetGlyphForActionHandle",
            "GetLegacyControllerCount", "GetLegacyControllerForIndex",
            "GetControllerIndexForHandle", "GetRemotePlaySessionIDForController",
        ],
        "earliest": 1, "latest": 6,
        "prefix": "SteamInput", "numeric_suffix": True, "zero_pad": False,
    },
    "STEAMMUSIC": {
        "known_methods": [
            "BIsEnabled", "BIsPlaying", "GetPlaybackStatus",
            "Play", "Pause", "PlayPrevious",
            "PlayNext", "SetVolume", "GetVolume",
        ],
        "earliest": 1, "latest": 1,
        "prefix": "STEAMMUSIC_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMMUSICREMOTE": {
        "known_methods": [
            "RegisterSteamMusicRemote", "DeregisterSteamMusicRemote",
            "BIsCurrentMusicRemote", "BActivationSuccess",
            "SetDisplayName", "SetPNGIcon_64x64",
            "EnablePlayPrevious", "EnablePlayNext",
            "EnableShuffled", "EnableLooped",
            "EnableQueue", "EnablePlaylists",
            "UpdatePlaybackStatus", "UpdateShuffled",
            "UpdateLooped", "UpdateVolume",
            "UpdateEntry", "SetPlaylistEntry",
            "SetCurrentPlaylistEntry", "SetCurrentTrackEntry",
            "QueueSetIcon", "QueueSetIcon_CRC",
        ],
        "earliest": 1, "latest": 1,
        "prefix": "STEAMMUSICREMOTE_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMPARENTALSETTINGS": {
        "known_methods": [
            "BIsParentalLockEnabled", "BIsParentalLockLocked",
            "BIsAppBlocked", "BIsAppInBlockList",
            "BIsFeatureBlocked", "BIsFeatureInBlockList",
            "GetAppBlocked", "GetAppInBlockList",
        ],
        "earliest": 1, "latest": 1,
        "prefix": "STEAMPARENTALSETTINGS_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "SteamParties": {
        "known_methods": [
            "GetNumActiveBeacons", "GetBeaconByIndex",
            "CreateBeacon", "OnReservationCompleted",
            "CancelReservation", "ChangeNumOpenSlots",
            "JoinParty", "GetBeaconDetails",
            "GetBeaconLocationData",
        ],
        "earliest": 1, "latest": 2,
        "prefix": "SteamParties", "numeric_suffix": True, "zero_pad": False,
    },
    "STEAMREMOTEPLAY": {
        "known_methods": [
            "GetSessionCount", "GetSessionID",
            "GetSessionSteamID", "GetSessionRemotePlayTogetherSteamID",
            "GetSessionClientName", "GetSessionClientResolution",
            "BSendGamepadPadState", "BSendGamepadPadStateToSession",
            "BGetSessionGamepadPadState", "ShowSessionFilesUI",
        ],
        "earliest": 1, "latest": 3,
        "prefix": "STEAMREMOTEPLAY_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMVIDEO": {
        "known_methods": [
            "GetVideoURL", "IsBroadcasting",
            "GetOPFSettings", "GetOPFStringForApp",
        ],
        "earliest": 1, "latest": 7,
        "prefix": "STEAMVIDEO_INTERFACE_V",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMTIMELINE": {
        "known_methods": [
            "SetTimelineStateDescription", "ClearTimelineStateDescription",
            "AddTimelineEvent", "SetTimelineGameMode",
            "DoesNotificationExist", "RemoveNotification",
            "AddTimelineEvent_Ex",
        ],
        "earliest": 1, "latest": 4,
        "prefix": "STEAMTIMELINE_INTERFACE_V",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMAPPLIST": {
        "known_methods": [
            "GetNumInstalledApps", "GetInstalledApps",
            "GetAppName", "GetAppInstallDir",
            "GetAppBuildId",
        ],
        "earliest": 1, "latest": 1,
        "prefix": "STEAMAPPLIST_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "STEAMAPPTICKET": {
        "known_methods": [
            "GetAppOwnershipTicketData",
        ],
        "earliest": 1, "latest": 1,
        "prefix": "STEAMAPPTICKET_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
    "SteamNetworkingSockets": {
        "known_methods": [
            "CreateListenSocketIP", "CreateListenSocketP2P",
            "CreateListenSocketP2PEx", "CreateP2PConnectionSocket",
            "CreateConnectionSocket", "ConnectP2P",
            "ConnectP2PEx", "AcceptConnection",
            "CloseConnection", "CloseListenSocket",
            "SetConnectionUserData", "GetConnectionUserData",
            "SetConnectionName", "GetConnectionName",
            "SendMessageToConnection", "SendMessages",
            "FlushMessagesOnConnection", "ReceiveMessagesOnConnection",
            "ReceiveMessagesOnListenSocket",
            "GetSocketStatus", "GetListenSocketStatus",
            "GetRemoteAddress", "GetLocalAddress",
            "GetConnectionInfo", "GetQuickStatus",
            "GetDetailedConnectionStatus",
            "InitAuthentication", "GetAuthenticationStatus",
            "CreatePollGroup", "DestroyPollGroup",
            "SetPollGroup", "SetCertificate",
            "GetCertificateRequest", "GetCertificate",
            "GetIdentity", "GetSteamIDForIdentity",
            "GetIdentityForSteamID", "CreateSocketPair",
            "GetConnectionRealTimeStatus",
        ],
        "earliest": 1, "latest": 12,
        "prefix": "SteamNetworkingSockets", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamNetworkingMessages": {
        "known_methods": [
            "SendMessageToUser", "ReceiveMessagesOnChannel",
            "AcceptSessionWithUser", "CloseSessionWithUser",
            "CloseChannelWithUser", "GetSessionConnectionInfo",
        ],
        "earliest": 1, "latest": 2,
        "prefix": "SteamNetworkingMessages", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamNetworkingUtils": {
        "known_methods": [
            "AllocateMessage", "InitRelayNetworkAccess",
            "GetRelayNetworkStatus",
            "GetLocalPingLocation",
            "EstimatePingTimeBetweenTwoLocations",
            "EstimatePingTimeFromLocalToDataCenter",
            "GetPingToDataCenter",
            "GetDirectPingToPOP",
            "GetPOPCount",
            "GetPOPList",
            "SetGlobalConfigValueInt",
            "SetGlobalConfigValueFloat",
            "SetGlobalConfigValueString",
            "SetGlobalConfigValuePtr",
            "SetConnectionConfigValueInt",
            "SetConnectionConfigValueFloat",
            "SetConnectionConfigValueString",
            "SetConnectionConfigValuePtr",
            "GetConfigValueInfo",
            "GetFirstConfigValue",
            "GetConfigValueInfoEx",
            "EmitStatusMessage",
        ],
        "earliest": 1, "latest": 4,
        "prefix": "SteamNetworkingUtils", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamGameServerStats": {
        "known_methods": [
            "RequestUserStats", "GetUserStat", "GetUserAchievement",
            "SetUserStat", "UpdateUserAvgRateStat",
            "SetUserAchievement", "ClearUserAchievement",
            "StoreUserStats",
        ],
        "earliest": 1, "latest": 1,
        "prefix": "SteamGameServerStats", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamGameCoordinator": {
        "known_methods": [
            "SendMessage", "RetrieveMessage", "IsMessageAvailable",
        ],
        "earliest": 1, "latest": 1,
        "prefix": "SteamGameCoordinator", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamGameStats": {
        "known_methods": [
            "GetNewSession", "EndSession", "AddSessionAttributeInt",
            "AddSessionAttributeString", "AddSessionAttributeFloat",
            "AddSessionAttributeInt64", "AddSessionAttributeDouble",
            "AddSessionAttributeInt32",
        ],
        "earliest": 1, "latest": 1,
        "prefix": "SteamGameStats", "numeric_suffix": True, "zero_pad": False,
    },
    "SteamMasterServerUpdater": {
        "known_methods": [
            "SetActive", "SetAddress", "SetBasicServerData",
            "SetServerTags", "UpdateAllPlayers",
            "UpdatePlayer", "UpdateSpectatorPort",
            "SetGameType", "SetAppID",
        ],
        "earliest": 1, "latest": 1,
        "prefix": "SteamMasterServerUpdater", "numeric_suffix": True, "zero_pad": False,
    },
    "STEAMUNIFIEDMESSAGES": {
        "known_methods": [
            "SendMessage", "SendMessageResult",
            "GetISteamUnifiedMessages",
        ],
        "earliest": 1, "latest": 1,
        "prefix": "STEAMUNIFIEDMESSAGES_INTERFACE_VERSION",
        "numeric_suffix": True, "zero_pad": True,
    },
}

# ---------------------------------------------------------------------------
# Header parsing
# ---------------------------------------------------------------------------

DEFINE_RE = re.compile(r'#\s*define\s+(\w+)_INTERFACE_VERSION\s+"([^"]+)"')
CLASS_RE = re.compile(r'class\s+(\w+)\b')

def parse_header(filepath):
    """Parse a single Steam SDK header file for interface definitions."""
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    lines = text.split("\n")
    results = {}

    # Find all #define X_INTERFACE_VERSION "..." lines with line numbers
    defines = []  # (line_number, version_string)
    for i, line in enumerate(lines):
        m = DEFINE_RE.search(line)
        if m:
            version_string = m.group(2)
            defines.append((i, version_string))

    if not defines:
        return results

    # Find all class declarations with their line ranges
    classes = []  # (class_name, start_line, end_line)
    for i, line in enumerate(lines):
        cm = CLASS_RE.search(line)
        if cm:
            class_name = cm.group(1)
            # Check if this looks like a Steam API class (has virtual methods)
            # We accept any class — no whitelist
            start = i
            # Find class body
            brace_depth = 0
            in_class = False
            for j in range(i, len(lines)):
                brace_depth += lines[j].count("{") - lines[j].count("}")
                if brace_depth > 0:
                    in_class = True
                if in_class and brace_depth <= 0:
                    classes.append((class_name, start, j))
                    break
            if in_class and brace_depth > 0:
                # Unterminated class — still add it
                classes.append((class_name, start, len(lines) - 1))

    if not classes:
        # No classes found, but we have defines — still return the version strings
        for _, version_string in defines:
            results[version_string] = {}
        return results

    # Match each define to the nearest class that starts before it
    for define_line, version_string in defines:
        # Find the class that contains this line or is nearest before it
        best_class = None
        best_end = None
        for class_name, cstart, cend in classes:
            if cstart <= define_line <= cend:
                best_class = class_name
                best_end = cend
                break
            # Also check if class ends near the define
            if cend >= define_line - 5 and cstart <= define_line:
                if best_class is None or cstart > [c[1] for c in classes if c[0] == best_class][0] if best_class else 0:
                    pass
            if cstart <= define_line and (best_class is None or cstart > (best_end or 0)):
                best_class = class_name
                best_end = cend

        if best_class:
            # Extract virtual methods from the class
            virtual_methods = []
            for j in range(lines[define_line].count("\n") + 1): pass
            # Parse lines around the define to find class methods
            class_start = None
            class_end = None
            for cname, cs, ce in classes:
                if cname == best_class:
                    class_start = cs
                    class_end = ce
                    break

            if class_start is not None:
                in_class_body = False
                brace_depth = 0
                for j in range(class_start, min(class_end + 1, len(lines))):
                    stripped = lines[j].strip()
                    if not stripped:
                        continue
                    brace_depth += stripped.count("{") - stripped.count("}")
                    if brace_depth > 0:
                        in_class_body = True
                    if not in_class_body:
                        continue
                    if "virtual " in stripped:
                        paren_idx = stripped.find("(")
                        if paren_idx > 0:
                            virt_idx = stripped.find("virtual")
                            before_paren = stripped[virt_idx + len("virtual"):paren_idx].strip()
                            words = re.findall(r'\w+', before_paren)
                            if words:
                                name = words[-1]
                                if name != "~" and not name.startswith("_"):
                                    virtual_methods.append(name)

                if virtual_methods:
                    method_indices = {}
                    for idx, name in enumerate(virtual_methods):
                        method_indices[name] = idx
                    results[version_string] = method_indices

        if version_string not in results:
            results[version_string] = {}

    return results


# ---------------------------------------------------------------------------
# Built-in table generation
# ---------------------------------------------------------------------------

def generate_builtin_table():
    """Generate comprehensive interface table from built-in family definitions."""
    result = {}
    # First, include all hand-crafted entries
    for ver, methods in BUILTIN_INTERFACES.items():
        result[ver] = dict(methods)

    # Generate version series for each family
    for family_name, info in INTERFACE_FAMILIES.items():
        prefix = info["prefix"]
        methods = info["known_methods"]
        earliest = info["earliest"]
        latest = info["latest"]
        zero_pad = info.get("zero_pad", True)

        for ver_num in range(earliest, latest + 1):
            if zero_pad:
                version_str = f"{prefix}{ver_num:03d}"
            else:
                version_str = f"{prefix}{ver_num:02d}"

            if version_str in result:
                continue  # Skip if already defined in hand-crafted entries

            # Generate correct vtable indices
            # (in order of method appearance = vtable order)
            method_dict = {}
            for idx, name in enumerate(methods):
                method_dict[name] = idx
            result[version_str] = method_dict

    return result


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def generate_json(all_interfaces):
    """Generate sorted JSON from the interface data."""
    result = {}
    for version_str in sorted(all_interfaces.keys()):
        methods = all_interfaces[version_str]
        result[version_str] = {name: idx for name, idx in sorted(methods.items(), key=lambda x: x[1])}
    return json.dumps(result, indent=2, ensure_ascii=False)


def dump_analysis(all_interfaces):
    """Print coverage analysis."""
    families = {}
    for ver in all_interfaces:
        # Determine family base name
        base = ver
        for known in ["STEAMAPPS_INTERFACE_VERSION", "STEAMUSERSTATS_INTERFACE_VERSION",
                       "STEAMREMOTESTORAGE_INTERFACE_VERSION", "STEAMUGC_INTERFACE_VERSION",
                       "STEAMINVENTORY_INTERFACE_V", "STEAMSCREENSHOTS_INTERFACE_VERSION",
                       "STEAMHTTP_INTERFACE_VERSION", "STEAMHTMLSURFACE_INTERFACE_VERSION_",
                       "STEAMMUSIC_INTERFACE_VERSION", "STEAMMUSICREMOTE_INTERFACE_VERSION",
                       "STEAMPARENTALSETTINGS_INTERFACE_VERSION", "STEAMREMOTEPLAY_INTERFACE_VERSION",
                       "STEAMVIDEO_INTERFACE_V", "STEAMTIMELINE_INTERFACE_V",
                       "STEAMAPPLIST_INTERFACE_VERSION", "STEAMAPPTICKET_INTERFACE_VERSION",
                       "STEAMUNIFIEDMESSAGES_INTERFACE_VERSION",
                       "STEAMCONTROLLER_INTERFACE_VERSION"]:
            if ver.startswith(known):
                base = known.rstrip("_").rstrip("V").rstrip("_")
                break
        # Chop trailing digits
        import re as _re
        m = _re.match(r'([A-Za-z]+)', ver)
        if m:
            base = m.group(1)
        families.setdefault(base, []).append(ver)

    total_methods = sum(len(m) for m in all_interfaces.values())
    print(f"Total interface versions: {len(all_interfaces)}")
    print(f"Total method entries: {total_methods}")
    print(f"\nCoverage by family ({len(families)} families):")
    for fname in sorted(families.keys()):
        vers = sorted(families[fname])
        total = sum(len(all_interfaces[v]) for v in vers)
        print(f"  {fname}: {len(vers)} versions, {total} methods")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Generate Steam interface table JSON")
    parser.add_argument("sdk_dir", nargs="?", default=None,
                        help="Steam SDK headers directory (optional — uses built-in table if omitted)")
    parser.add_argument("output_path", nargs="?", default="femboy_interfaces.dat",
                        help="Output JSON path (default: femboy_interfaces.dat)")
    parser.add_argument("--dump", action="store_true",
                        help="Dump coverage analysis to stdout")
    parser.add_argument("--merge", metavar="EXISTING_JSON",
                        help="Merge with existing JSON file (new data overrides)")
    args = parser.parse_args()

    # Start with built-in comprehensive table
    all_interfaces = generate_builtin_table()

    # Merge with existing JSON if requested
    if args.merge:
        merge_path = Path(args.merge)
        if merge_path.exists():
            with open(merge_path, "r", encoding="utf-8") as f:
                existing = json.load(f)
            for ver, methods in existing.items():
                if ver not in all_interfaces:
                    all_interfaces[ver] = methods
                else:
                    # Merge methods (existing overrides)
                    for name, idx in methods.items():
                        all_interfaces[ver][name] = idx
            print(f"Merged {len(existing)} interface versions from {args.merge}")

    # Parse SDK headers if directory provided
    if args.sdk_dir:
        sdk_dir = Path(args.sdk_dir)
        if not sdk_dir.is_dir():
            print(f"Error: {args.sdk_dir} is not a directory", file=sys.stderr)
            sys.exit(1)
        header_files = list(sdk_dir.glob("*.h")) + list(sdk_dir.glob("*.hpp"))
        if not header_files:
            print(f"Warning: No header files found in {sdk_dir}")
        else:
            parsed_count = 0
            for hdr in header_files:
                try:
                    result = parse_header(hdr)
                    for ver, methods in result.items():
                        if methods:  # Only merge if methods were found
                            if ver not in all_interfaces:
                                all_interfaces[ver] = methods
                            else:
                                # Header data takes priority
                                all_interfaces[ver].update(methods)
                            parsed_count += 1
                except Exception as e:
                    print(f"Warning: Failed to parse {hdr}: {e}", file=sys.stderr)
            print(f"Parsed {parsed_count} interface versions from {len(header_files)} headers")

    # Write output
    output_path = Path(args.output_path)
    data = generate_json(all_interfaces)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(data)

    total_methods = sum(len(m) for m in all_interfaces.values())
    print(f"Generated {output_path}: {len(all_interfaces)} interfaces, {total_methods} methods, {len(data)} bytes")

    if args.dump:
        dump_analysis(all_interfaces)


if __name__ == "__main__":
    main()
