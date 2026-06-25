#include "Features.h"
#include <cctype>

// =============================================================================
// Steam Achievement Unlocking
// =============================================================================
typedef void* (*SteamUserStats_t)();
typedef bool  (*RequestCurrentStats_t)(void*);
typedef uint32_t (*GetNumAchievements_t)(void*);
typedef const char* (*GetAchievementName_t)(void*, uint32_t);
typedef bool  (*SetAchievement_t)(void*, const char*);
typedef bool  (*StoreStats_t)(void*);

bool UnlockSteamAchievements()
{
    HMODULE steamApi = GetModuleHandleA("steam_api64.dll");
    if (!steamApi) return false;
    auto fnGetStats = (SteamUserStats_t)GetProcAddress(steamApi, "SteamAPI_SteamUserStats_v012");
    if (!fnGetStats) fnGetStats = (SteamUserStats_t)GetProcAddress(steamApi, "SteamAPI_SteamUserStats_v011");
    if (!fnGetStats) fnGetStats = (SteamUserStats_t)GetProcAddress(steamApi, "SteamUserStats");
    if (!fnGetStats) return false;
    void* userStats = fnGetStats();
    if (!userStats) return false;
    auto fnReq = (RequestCurrentStats_t)GetProcAddress(steamApi, "SteamAPI_ISteamUserStats_RequestCurrentStats");
    auto fnNum = (GetNumAchievements_t)GetProcAddress(steamApi, "SteamAPI_ISteamUserStats_GetNumAchievements");
    auto fnNam = (GetAchievementName_t)GetProcAddress(steamApi, "SteamAPI_ISteamUserStats_GetAchievementName");
    auto fnSet = (SetAchievement_t)GetProcAddress(steamApi, "SteamAPI_ISteamUserStats_SetAchievement");
    auto fnSto = (StoreStats_t)GetProcAddress(steamApi, "SteamAPI_ISteamUserStats_StoreStats");
    if (!fnReq || !fnNum || !fnNam || !fnSet || !fnSto) return false;
    fnReq(userStats); Sleep(500);
    uint32_t n = fnNum(userStats); if (n == 0) return false;
    int ok = 0;
    for (uint32_t i = 0; i < n; i++) { const char* a = fnNam(userStats, i); if (a && a[0] && fnSet(userStats, a)) ok++; }
    fnSto(userStats);
    std::cout << "[Achievements] " << ok << "/" << n << "\n";
    return true;
}

void Cleanup() { if (cfg.Console) FreeConsole(); }

// =============================================================================
// Save game access & per-ID override resolution
// =============================================================================
SDK::UAppSaveGame* GetSaveGame()
{
    return static_cast<SDK::UAppSaveGame*>(Reflect::FindInstance("AppSaveGame"));
}

static void ResolveOverrideMap(const std::unordered_map<std::string, bool>& raw, std::unordered_map<int32_t, bool>& resolved, const char* enumName)
{
    for (auto& [key, value] : raw) {
        bool numeric = !key.empty() && (isdigit((unsigned char)key[0]) || (key[0] == '-' && key.size() > 1));
        int32_t id = numeric ? std::stoi(key) : Reflect::ResolveEnumValueByName(enumName, key);
        if (id < 0) { std::cout << "[Config] Warning: could not resolve '" << key << "' against " << enumName << "\n"; continue; }
        resolved[id] = value;
    }
}

// Numeric-only categories (no enum backing these IDs in the SDK)
static void ResolveOverrideMapNumeric(const std::unordered_map<std::string, bool>& raw, std::unordered_map<int32_t, bool>& resolved)
{
    for (auto& [key, value] : raw) {
        try { resolved[std::stoi(key)] = value; }
        catch (...) { std::cout << "[Config] Warning: '" << key << "' is not a valid numeric ID for this section\n"; }
    }
}

void ResolveOverrides()
{
    ResolveOverrideMap(cfg.DriverOverridesRaw, cfg.DriverOverrides, "EDriverId");
    ResolveOverrideMap(cfg.ColorPresetOverridesRaw, cfg.ColorPresetOverrides, "EMachineId");
    ResolveOverrideMap(cfg.GadgetOverridesRaw, cfg.GadgetOverrides, "EGadgetId");
    ResolveOverrideMapNumeric(cfg.HonorTitleOverridesRaw, cfg.HonorTitleOverrides);
    ResolveOverrideMapNumeric(cfg.AlbumOverridesRaw, cfg.AlbumOverrides);
    ResolveOverrideMapNumeric(cfg.TrackOverridesRaw, cfg.TrackOverrides);
}

// =============================================================================
// Super Sonic — Native ExecFunction pointer swap
//
// IsDriverSelectable and GetCharaSelectIndexByDriverId are called as native
// C++ calls that bypass ProcessEvent. Our ProcessEvent hook never sees them.
//
// However, UE4 stores a function pointer (ExecFunction) at UFunction+0xD8.
// When ProcessEvent dispatches a native function, it calls this pointer.
// We REPLACE it with our own function. Our function calls the original,
// then overrides the return value for Super Sonic (driver 46).
//
// ExecFunction signature: void (*)(void* Context, void* TheStack, void* Result)
//   Context = UObject* (the CDO for BPFL statics)
//   TheStack = FFrame& (stack frame, we don't need to parse it)
//   Result = RESULT_DECL = pointer to ReturnValue in the params buffer
//
// IsDriverSelectable params layout:
//   [0x00] UObject* WorldContextObject (8 bytes)
//   [0x08] uint8    InDriverId         (1 byte)
//   [0x09] bool     ReturnValue        (1 byte)
//   Result = &Params[0x09], so InDriverId = *(Result - 1)
//
// GetCharaSelectIndexByDriverId params layout:
//   [0x00] uint8    InDriverId         (1 byte)
//   [0x01] pad      (3 bytes)
//   [0x04] int32    ReturnValue        (4 bytes)
//   Result = &Params[0x04], so InDriverId = *(Result - 4)
// =============================================================================

using FNativeFuncPtr = void (*)(void* Context, void* TheStack, void* Result);

static constexpr uint8_t kSuperSonicDriverId = 46;
static constexpr int32_t kSSCharaSelectIndex = 46;  // SS's grid slot from DT_DriverData01

 //Original function pointers (saved before swap)
static FNativeFuncPtr s_origExecIsDriverSelectable = nullptr;
static FNativeFuncPtr s_origExecGetCSIndex         = nullptr;

 //UFunction objects (kept for re-swap on CSS entry)
static SDK::UFunction* s_ufnIsDriverSelectable = nullptr;
static SDK::UFunction* s_ufnGetCSIndex         = nullptr;

static bool s_ssHooksInstalled = false;
static bool s_ssLoggedOnce = false;

// --- Hook: IsDriverSelectable ---
// After calling original, if driver is Super Sonic, force return true
void ExecIsDriverSelectableHook(void* Context, void* TheStack, void* Result)
{
    s_origExecIsDriverSelectable(Context, TheStack, Result);

    if (Result) {
        uint8_t* retPtr = static_cast<uint8_t*>(Result);
        // InDriverId is 1 byte before ReturnValue in the params buffer
        uint8_t driverId = *(retPtr - 1);

        if (driverId == kSuperSonicDriverId) {
            *retPtr = 1; // force true
            if (!s_ssLoggedOnce) {
                std::cout << "[SS] ExecIsDriverSelectable: driver 46 -> true\n";
            }
        }
    }
}

// --- Hook: GetCharaSelectIndexByDriverId ---
// After calling original, if driver is Super Sonic and index is -1, force valid index
void ExecGetCSIndexHook(void* Context, void* TheStack, void* Result)
{
    s_origExecGetCSIndex(Context, TheStack, Result);

    if (Result) {
        int32_t* retPtr = static_cast<int32_t*>(Result);
        // InDriverId is 4 bytes before ReturnValue in the params buffer
        uint8_t driverId = *(reinterpret_cast<uint8_t*>(Result) - 4);

        if (driverId == kSuperSonicDriverId && *retPtr < 0) {
            *retPtr = kSSCharaSelectIndex;
            if (!s_ssLoggedOnce) {
                std::cout << "[SS] ExecGetCSIndex: driver 46 -> " << kSSCharaSelectIndex << "\n";
                s_ssLoggedOnce = true;
            }
        }
    }
}

// Install the ExecFunction swaps on both UFunctions
static void InstallSuperSonicHooks()
{
    if (s_ssHooksInstalled) return;

    // Find UFunctions
    s_ufnIsDriverSelectable = Reflect::FindFunction("CharaSelectUtilityLibrary", "IsDriverSelectable");
    s_ufnGetCSIndex = Reflect::FindFunction("DriverDataUtilityLibrary", "GetCharaSelectIndexByDriverId");

    if (!s_ufnIsDriverSelectable) {
        std::cout << "[SS] ERROR: IsDriverSelectable UFunction not found\n";
        return;
    }
    if (!s_ufnGetCSIndex) {
        std::cout << "[SS] ERROR: GetCharaSelectIndexByDriverId UFunction not found\n";
        return;
    }

    // Read ExecFunction pointers at UFunction + 0xD8
    FNativeFuncPtr* pExecIDS = reinterpret_cast<FNativeFuncPtr*>(
        reinterpret_cast<uint8_t*>(s_ufnIsDriverSelectable) + 0xD8);
    FNativeFuncPtr* pExecGCS = reinterpret_cast<FNativeFuncPtr*>(
        reinterpret_cast<uint8_t*>(s_ufnGetCSIndex) + 0xD8);

    s_origExecIsDriverSelectable = *pExecIDS;
    s_origExecGetCSIndex = *pExecGCS;

    std::cout << "[SS] IsDriverSelectable ExecFunc @ 0x"
              << std::hex << reinterpret_cast<uintptr_t>(s_origExecIsDriverSelectable) << std::dec << "\n";
    std::cout << "[SS] GetCSIndex ExecFunc @ 0x"
              << std::hex << reinterpret_cast<uintptr_t>(s_origExecGetCSIndex) << std::dec << "\n";

    if (!s_origExecIsDriverSelectable || !s_origExecGetCSIndex) {
        std::cout << "[SS] ERROR: ExecFunction is null — function may be Blueprint-only\n";
        return;
    }

    // Swap the ExecFunction pointers
    // UFunction memory is typically read-only, so we may need VirtualProtect
    DWORD oldProtect;

    VirtualProtect(pExecIDS, sizeof(FNativeFuncPtr), PAGE_READWRITE, &oldProtect);
    *pExecIDS = &ExecIsDriverSelectableHook;
    VirtualProtect(pExecIDS, sizeof(FNativeFuncPtr), oldProtect, &oldProtect);

    VirtualProtect(pExecGCS, sizeof(FNativeFuncPtr), PAGE_READWRITE, &oldProtect);
    *pExecGCS = &ExecGetCSIndexHook;
    VirtualProtect(pExecGCS, sizeof(FNativeFuncPtr), oldProtect, &oldProtect);

    s_ssHooksInstalled = true;
    std::cout << "[SS] ExecFunction swaps installed!\n";
}

// =============================================================================
// Hook & Clear
// =============================================================================
void HookGame()
{
    bool bHooked = false;
    while (!bHooked) {
        auto World = SDK::UWorld::GetWorld();
        auto Engine = SDK::UEngine::GetEngine();
        if (!World || !Engine || !Engine->GameViewport) continue;
        Orig_AActor_ProcessEvent = reinterpret_cast<AActor_ProcessEvent_t>(
            PatternScan("48 89 5C 24 10 48 89 6C 24 18 57 48 83 EC ? F7 82 B0 00 00 00 ? ? ? ?"));
        if (!Orig_AActor_ProcessEvent) continue;
        Orig_AActor_ProcessEvent = reinterpret_cast<AActor_ProcessEvent_t>(
            TrampHook64((BYTE*)Orig_AActor_ProcessEvent, (BYTE*)hk_AActor_ProcessEvent, 15));
        bHooked = true;
        if (!cfg.HotkeyEnabled) std::cout << "[SRCW] Hooked! AutoRun mode.\n";
        else std::cout << "[SRCW] Hooked! Hotkey 0x" << std::hex << cfg.UnlockKey << std::dec << "\n";
    }
}

void Clear()
{
    auto* contentData = Reflect::FindInstance("ContentDataAsset");
    auto* contentMgr = Reflect::FindInstance("UnionContentManager");
    if (!contentData || !contentMgr) { bCleared = true; return; }

    SDK::FProperty* pkgListProp = Reflect::FindProperty(contentData->Class, "PackageDataList");
    if (!pkgListProp) { bCleared = true; return; }
    auto* arrProp = static_cast<SDK::FArrayProperty*>(pkgListProp);
    auto* innerProp = arrProp->InnerProperty;
    auto* structProp = static_cast<SDK::FStructProperty*>(innerProp);
    int32_t pidOffset = Reflect::FindPropertyOffset(structProp->Struct, "P_Id");
    int32_t elemSize = innerProp->ElementSize;
    if (pidOffset < 0 || elemSize <= 0) { bCleared = true; return; }

    void* pkgListAddr = reinterpret_cast<uint8_t*>(contentData) + pkgListProp->Offset;
    int32_t pkgCount = ReflectRaw::TArrayNum(pkgListAddr);
    std::vector<int32_t> allPkgIds;
    for (int i = 0; i < pkgCount; i++) {
        void* elem = ReflectRaw::TArrayAt(pkgListAddr, i, elemSize);
        allPkgIds.push_back(ReflectRaw::ReadInt32(elem, pidOffset));
    }

    SDK::FProperty* mPkgProp = Reflect::FindProperty(contentMgr->Class, "m_packageIds");
    if (!mPkgProp || allPkgIds.empty()) { bCleared = true; return; }
    void* mPkgAddr = reinterpret_cast<uint8_t*>(contentMgr) + mPkgProp->Offset;
    auto* mPkgArr = reinterpret_cast<ReflectRaw::RawTArray*>(mPkgAddr);

    std::vector<int32_t> existing;
    for (int32_t i = 0; i < mPkgArr->NumElements; i++)
        existing.push_back(reinterpret_cast<int32_t*>(mPkgArr->Data)[i]);

    int32_t added = 0;
    for (auto pid : allPkgIds) {
        bool found = false;
        for (auto eid : existing) if (eid == pid) { found = true; break; }
        if (!found) { existing.push_back(pid); added++; }
    }

    if (added > 0) {
        int32_t nc = (int32_t)existing.size();
        if (nc > mPkgArr->MaxElements) {
            int32_t* nd = (int32_t*)VirtualAlloc(nullptr, nc * sizeof(int32_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (nd) { memcpy(nd, existing.data(), nc * sizeof(int32_t)); mPkgArr->Data = nd; mPkgArr->NumElements = nc; mPkgArr->MaxElements = nc; }
        } else {
            for (int32_t j = mPkgArr->NumElements; j < nc; j++)
                reinterpret_cast<int32_t*>(mPkgArr->Data)[j] = existing[j];
            mPkgArr->NumElements = nc;
        }
        Reflect::CallStaticBool("UnionContentUtils", "RequestCheckContent", true);
        std::cout << "[Clear] Injected " << added << " package IDs\n";
    }
    bCleared = true;
}

// =============================================================================
// Phased unlock
// =============================================================================
static bool bOverridesResolved = false;

bool RunUnlockPhase(int phase)
{
    if (!bOverridesResolved) { ResolveOverrides(); bOverridesResolved = true; }

    switch (phase) {
    case 0:  {
        if (cfg.HonorTitles || !cfg.HonorTitleOverrides.empty()) {
            int unlocked = 0, relocked = 0;
            auto* honorAsset = static_cast<SDK::UHonorTitleListDataAsset*>(Reflect::FindInstance("HonorTitleListDataAsset"));
            if (honorAsset) {
                Reflect::CallInstance(honorAsset, "HonorTitleListDataAsset", "Update");
                for (auto& pair : honorAsset->HonorTitleTableDataMap) {
                    int32_t id = pair.Key();
                    if (ResolveEnabled(cfg.HonorTitleOverrides, id, cfg.HonorTitles)) {
                        Reflect::CallStaticInt32("AppSaveGameHelper", "UnlockHonorTitle", id);
                        unlocked++;
                    } else if (cfg.RemovalMode) {
                        auto* save = GetSaveGame();
                        if (save) {
                            auto& list = save->_UserHonorTitleData.UnlockedHonorTitleList;
                            for (int i = list.Num() - 1; i >= 0; i--) if (list[i] == id) list.Remove(i);
                            relocked++;
                        }
                    }
                }
            }
            std::cout << "[Phase 0] Honor titles (" << unlocked << " unlocked, " << relocked << " relocked)\n";
        }
        return true; }
    case 1:  {
        if (cfg.Drivers || !cfg.DriverOverrides.empty()) {
            int n = Reflect::GetEnumNum("EDriverId");
            int unlocked = 0, relocked = 0;
            auto* save = cfg.RemovalMode ? GetSaveGame() : nullptr;
            for (int i = 0; i < n; i++) {
                if (ResolveEnabled(cfg.DriverOverrides, i, cfg.Drivers)) {
                    Reflect::CallStaticUInt8("AppSaveGameHelper", "SetDriverSelectable", (uint8_t)i);
                    Reflect::CallStaticUInt8("AppSaveGameHelper", "ClearDriverNew", (uint8_t)i);
                    unlocked++;
                } else if (save) {
                    for (auto& pair : save->_UserDriverData.UserDriverList) {
                        if ((int32_t)pair.Key() == i) { pair.Value().bIsSelectable = false; relocked++; }
                    }
                }
            }
            std::cout << "[Phase 1] Drivers (" << unlocked << " unlocked, " << relocked << " relocked)\n";
        }
        return true; }
    case 2:  {
        if (cfg.MachineCustomize) { Reflect::CallStatic("MachineCustomizeUtilityLibrary", "StoreAllAura"); Reflect::CallStatic("MachineCustomizeUtilityLibrary", "StoreAllHorn"); Reflect::CallStatic("MachineCustomizeUtilityLibrary", "StoreAllMachineAssembly"); Reflect::CallStatic("MachineCustomizeUtilityLibrary", "StoreAllMachineParts"); Reflect::CallStatic("MachineCustomizeUtilityLibrary", "StoreAllSticker"); std::cout << "[Phase 2] Machine customize\n"; }
        if (cfg.Gadgets || !cfg.GadgetOverrides.empty()) {
            int n = Reflect::GetEnumNum("EGadgetId");
            int given = 0, locked = 0;
            for (int i = 0; i < n; i++) {
                if (ResolveEnabled(cfg.GadgetOverrides, i, cfg.Gadgets)) { Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "GiveGadget", (uint8_t)i); given++; }
                else if (cfg.RemovalMode) { Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "LockGadget", (uint8_t)i); locked++; }
            }
            std::cout << "[Phase 2] Gadgets (" << given << " given, " << locked << " locked)\n";
        }
        return true; }
    case 3:  {
        if (cfg.ColorPresets || !cfg.ColorPresetOverrides.empty()) {
            int n = Reflect::GetEnumNum("EMachineId");
            int unlocked = 0, skipped = 0;
            for (int i = 0; i < n; i++) {
                if (ResolveEnabled(cfg.ColorPresetOverrides, i, cfg.ColorPresets)) { Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "UnlockMachinePresetColor", (uint8_t)i); unlocked++; }
                else skipped++;
            }
            if (cfg.RemovalMode && skipped > 0) std::cout << "[Phase 3] Note: RemovalMode can't relock color presets yet (no safe lock primitive found) - " << skipped << " skipped\n";
            std::cout << "[Phase 3] Color presets (" << unlocked << ")\n";
        }
        return true; }
    case 4:  { if (cfg.MirrorSpeed) { Reflect::CallStaticBool("AppSaveGameHelper", "SetOpenMirror", true); Reflect::CallStaticBool("AppSaveGameHelper", "SetOpenSuperSonicSpeed", true); std::cout << "[Phase 4] Mirror & SSS\n"; } return true; }
    case 5:  {
        if (cfg.Music || !cfg.AlbumOverrides.empty() || !cfg.TrackOverrides.empty()) {
            int albums = 0, tracks = 0, albumsRelocked = 0, tracksRelocked = 0;
            auto* jukebox = static_cast<SDK::UJukeboxDataAsset*>(Reflect::FindInstance("JukeboxDataAsset"));
            auto* save = cfg.RemovalMode ? GetSaveGame() : nullptr;
            if (jukebox) {
                Reflect::CallInstance(jukebox, "JukeboxDataAsset", "Update");
                for (auto& pair : jukebox->AlbumDataMap) {
                    int32_t id = pair.Key();
                    if (ResolveEnabled(cfg.AlbumOverrides, id, cfg.Music)) { Reflect::CallStaticInt32("AppSaveGameHelper", "SetAlbumAvailable", id); albums++; }
                    else if (save) {
                        for (auto& cond : save->_UserJukeboxData.AlbumCondition) {
                            if (cond.Key() == id) { cond.Value().bUnlocked = false; cond.Value().bAvailable = false; albumsRelocked++; }
                        }
                    }
                }
                for (auto& pair : jukebox->TrackDataMap) {
                    int32_t id = pair.Key();
                    if (ResolveEnabled(cfg.TrackOverrides, id, cfg.Music)) { Reflect::CallStaticInt32("AppSaveGameHelper", "SetTrackAvailable", id); tracks++; }
                    else if (save) {
                        for (auto& cond : save->_UserJukeboxData.TrackCondition) {
                            if (cond.Key() == id) { cond.Value().bAvailable = false; tracksRelocked++; }
                        }
                    }
                }
            }
            std::cout << "[Phase 5] Music (" << albums << " albums, " << tracks << " tracks unlocked; "
                      << albumsRelocked << "/" << tracksRelocked << " albums/tracks relocked)\n";
        }
        return true; }
    case 6:  { if (cfg.StagesDLC) { Reflect::CallStaticBool("UnionContentUtils", "RequestCheckContent", true); std::cout << "[Phase 6] DLC stages\n"; } return true; }
    case 7:  {
        if (cfg.StagesGPOpen) {
            auto* gp = Reflect::FindCDO("CheatGrandPrix");
            if (gp) { Reflect::CallInstanceBool(gp, "CheatGrandPrix", "SetGrandPrixOpenedAll", true); Reflect::CallInstanceBool(gp, "CheatGrandPrix", "SetSetGrandPrixLeast1Play", true); }
            int n = Reflect::GetEnumNum("EGrandPrixId"); for (int i = 1; i < n; i++) Reflect::CallStaticUInt8Bool("AppSaveGameHelper", "SetGrandPrixLeast1PlayEachGrandPrix", (uint8_t)i, true);
            int ne = Reflect::GetEnumNum("EGrandPrixEventFlag"); for (int i = 0; i < ne; i++) Reflect::CallStaticUInt8Bool("AppSaveGameHelper", "SetDodonpaEventCompleteFlag", (uint8_t)i, true);
            Reflect::CallStaticBool("AppSaveGameHelper", "SetGrandPrixLeast1Play", true);
            std::cout << "[Phase 7] GP opened\n"; }
        return true; }
    case 8:  {
        if (cfg.StagesSecret) {
            auto* gp = Reflect::FindCDO("CheatGrandPrix");
            if (gp) {
                Reflect::CallInstance(gp, "CheatGrandPrix", "SetPlayedAnotherStageAll");
                int ne = Reflect::GetEnumNum("EGrandPrixEndingId");
                for (int i = 0; i < ne; i++) Reflect::CallInstanceUInt8Bool(gp, "CheatGrandPrix", "SetClearedGrandPrixEnding", (uint8_t)i, true);
            }
            std::cout << "[Phase 8] Secret stages\n"; }
        return true; }
    case 9:  {
        if (cfg.GadgetPlate) {
            int n = Reflect::GetEnumNum("EGadgetPlateId");
            if (n > 0) Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "SetCurrentGadgetPlateIdUseId", (uint8_t)(n - 1));
            Reflect::CallStatic("MachineCustomizeUtilityLibrary", "UpdateGadgetSlotNumInUserData");
            std::cout << "[Phase 9] Gadget plate (highest id " << (n - 1) << ")\n";
        }
        return true; }
    case 10: { if (cfg.Challenges) { Reflect::CallStatic("CheatChallenge", "AllChallengeClear"); Reflect::CallStaticBool("AppSaveGameHelper", "SetCompleteMainChallenge", true); Reflect::CallStaticBool("AppSaveGameHelper", "SetCompleteSpecialChallenge", true); int32_t pc = Reflect::CallStaticRetInt32("ChallengeStatsUtility", "GetChallengeProgressCount"); Reflect::CallStaticInt32("AppSaveGameHelper", "SetChallengeShowProgress", pc); Reflect::CallStaticFloat("AppSaveGameHelper", "SetChallengeLastShowProgress", 1.0f); std::cout << "[Phase 10] Challenges\n"; } return true; }

    // Phase 11: Super Sonic — save-data + ExecFunction hooks
//    case 11: {
//        if (cfg.SuperSonicAll) {
//            Reflect::CallStaticUInt8("AppSaveGameHelper", "SetDriverSelectable", kSuperSonicDriverId);
//            Reflect::CallStaticUInt8("AppSaveGameHelper", "ClearDriverNew", kSuperSonicDriverId);
//            Reflect::CallStaticBool("AppSaveGameHelper", "SetOpenSuperSonicSpeed", true);
//            std::cout << "[Phase 11] Super Sonic save-data set\n";
//        }
//        return true; }
//
//    case 12: { if (cfg.NF_CompleteMachine) Reflect::CallStatic("MachineCustomizeUtilityLibrary", "DisableCompleteMachineNewFlags"); if (cfg.NF_Sticker) Reflect::CallStatic("MachineCustomizeUtilityLibrary", "DisableStickerNewFlags"); if (cfg.NF_ColorPreset) Reflect::CallStatic("MachineCustomizeUtilityLibrary", "DisableMachineColorPresetNewFlags"); if (cfg.NF_Gadget) Reflect::CallStatic("MachineCustomizeUtilityLibrary", "DisableDisplayedGadgetNewFlags"); std::cout << "[Phase 12] Machine NF\n"; return true; }
//    case 13: { if (cfg.NF_PartsSpeed)  Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "DisablePartsListNewFlagByType", 0); return true; }
//    case 14: { if (cfg.NF_PartsAccel)  Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "DisablePartsListNewFlagByType", 1); return true; }
//    case 15: { if (cfg.NF_PartsHandle) Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "DisablePartsListNewFlagByType", 2); return true; }
//    case 16: { if (cfg.NF_PartsPower)  Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "DisablePartsListNewFlagByType", 3); return true; }
//    case 17: { if (cfg.NF_PartsDash)   Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "DisablePartsListNewFlagByType", 4); return true; }
//    case 18: { if (cfg.NF_Horn) { int n = Reflect::GetEnumNum("EMachineHornType"); for (int i = 0; i < n; i++) Reflect::CallStaticUInt8Bool("MachineCustomizeUtilityLibrary", "SetCustomMachineHornNew", (uint8_t)i, false); } return true; }
//    case 19: { if (cfg.NF_HonorTitles) { for (int i = 0; i < 500; i++) Reflect::CallStaticInt32("AppSaveGameHelper", "ResetNewHonorTitle", i); } return true; }
//    case 20: { return true; }
//    case 21: { if (cfg.NF_Challenges) { int32_t pc = Reflect::CallStaticRetInt32("ChallengeStatsUtility", "GetChallengeProgressCount"); Reflect::CallStaticInt32("AppSaveGameHelper", "SetChallengeShowProgress", pc); Reflect::CallStaticFloat("AppSaveGameHelper", "SetChallengeLastShowProgress", 1.0f); } if (cfg.NF_Rewards) Reflect::CallStatic("AppSaveGameHelper", "ClearRewardGetDisplayRequestDataAll"); return true; }
//    case 22: { if (cfg.Achievements) { UnlockSteamAchievements(); std::cout << "[Phase 22] Achievements\n"; } return true; }
//    default: return false;
  }
// 
}

// =============================================================================
// ProcessEvent Hook
// =============================================================================
void __fastcall hk_AActor_ProcessEvent(SDK::AActor* Class, SDK::UFunction* Function, void* Parms)
{
    // ===== EARLIEST: Install SS ExecFunction hooks on first call =====
    //if (!s_ssHooksInstalled && cfg.SuperSonicAll) {
    //    InstallSuperSonicHooks();
    //}

    // ===== PRE-CALL =====
    if (!bCleared)
        Clear();

    // AutoRun
    if (!cfg.HotkeyEnabled && !bUnlockStarted && !bUnlockDone) {
        if (!bAutoRunReady) { if (!Function->GetName().compare("OnInitStateSelectPlayMode")) bAutoRunReady = true; }
        else { bUnlockStarted = true; unlockPhase = 0; lastPhaseTime = GetTickCount64(); std::cout << "[SRCW] AutoRun starting...\n"; }
    }

    // Hotkey
    if (cfg.HotkeyEnabled && !bUnlockStarted && !bUnlockDone) {
        bool kd = (GetAsyncKeyState(cfg.UnlockKey) & 0x8000) != 0;
        if (kd && !bKeyWasDown) { bUnlockStarted = true; unlockPhase = 0; lastPhaseTime = GetTickCount64(); std::cout << "[SRCW] Hotkey pressed...\n"; }
        bKeyWasDown = kd;
    }

    // Phased unlock timer
    if (bUnlockStarted && !bUnlockDone) {
        ULONGLONG now = GetTickCount64();
        if (now - lastPhaseTime >= (ULONGLONG)cfg.PhaseDelayMs) {
            if (RunUnlockPhase(unlockPhase)) { unlockPhase++; lastPhaseTime = now; }
            else { bUnlockDone = true; std::cout << "All phases complete!\n"; }
        }
    }

    // ===== CALL ORIGINAL =====
    Orig_AActor_ProcessEvent(Class, Function, Parms);
}
