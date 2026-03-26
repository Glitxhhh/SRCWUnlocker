#include "Features.h"
#include <iostream>

typedef void* (*SteamUserStats_t)();
typedef bool  (*RequestCurrentStats_t)(void*);
typedef uint32_t (*GetNumAchievements_t)(void*);
typedef const char* (*GetAchievementName_t)(void*, uint32_t);
typedef bool  (*SetAchievement_t)(void*, const char*);
typedef bool  (*StoreStats_t)(void*);

bool UnlockSteamAchievements()
{
    HMODULE steamApi = GetModuleHandleA("steam_api64.dll");
    if (!steamApi) { std::cout << "[Achievements] steam_api64.dll not loaded\n"; return false; }
    auto fnGetStats = (SteamUserStats_t)GetProcAddress(steamApi, "SteamAPI_SteamUserStats_v012");
    if (!fnGetStats) fnGetStats = (SteamUserStats_t)GetProcAddress(steamApi, "SteamAPI_SteamUserStats_v011");
    if (!fnGetStats) { std::cout << "[Achievements] SteamUserStats not found\n"; return false; }
    void* userStats = fnGetStats();
    if (!userStats) { std::cout << "[Achievements] SteamUserStats null\n"; return false; }
    auto fnReq  = (RequestCurrentStats_t)GetProcAddress(steamApi, "SteamAPI_ISteamUserStats_RequestCurrentStats");
    auto fnNum  = (GetNumAchievements_t) GetProcAddress(steamApi, "SteamAPI_ISteamUserStats_GetNumAchievements");
    auto fnName = (GetAchievementName_t) GetProcAddress(steamApi, "SteamAPI_ISteamUserStats_GetAchievementName");
    auto fnSet  = (SetAchievement_t)     GetProcAddress(steamApi, "SteamAPI_ISteamUserStats_SetAchievement");
    auto fnStore= (StoreStats_t)         GetProcAddress(steamApi, "SteamAPI_ISteamUserStats_StoreStats");
    if (!fnReq || !fnNum || !fnName || !fnSet || !fnStore) { std::cout << "[Achievements] Missing API funcs\n"; return false; }
    fnReq(userStats); Sleep(500);
    uint32_t numAch = fnNum(userStats);
    if (!numAch) { std::cout << "[Achievements] 0 achievements found\n"; return false; }
    int unlocked = 0;
    for (uint32_t i = 0; i < numAch; i++) {
        const char* n = fnName(userStats, i);
        if (n && n[0] && fnSet(userStats, n)) unlocked++;
    }
    fnStore(userStats);
    std::cout << "[Achievements] Unlocked " << unlocked << "/" << numAch << "\n";
    return true;
}

void HookGame()
{
    bool bHooked = false;
    while (!bHooked) {
        auto World  = SDK::UWorld::GetWorld();
        auto Engine = SDK::UEngine::GetEngine();
        if (!World || !Engine || !Engine->GameViewport) continue;
        Orig_AActor_ProcessEvent = reinterpret_cast<AActor_ProcessEvent_t>(
            PatternScan("48 89 5C 24 10 48 89 6C 24 18 57 48 83 EC ? F7 82 B0 00 00 00 ? ? ? ?"));
        if (!Orig_AActor_ProcessEvent) continue;
        Orig_AActor_ProcessEvent = reinterpret_cast<AActor_ProcessEvent_t>(
            TrampHook64((BYTE*)Orig_AActor_ProcessEvent, (BYTE*)hk_AActor_ProcessEvent, 15));
        bHooked = true;
        if (cfg.HotkeyEnabled)
            std::cout << "[SRCW] Hooked! Press hotkey (0x" << std::hex << cfg.UnlockKey << std::dec << ") to unlock.\n";
        else
            std::cout << "[SRCW] Hooked! Will unlock on menu load.\n";
    }
}

void Clear()
{
    auto* contentData = Reflect::FindInstance("ContentDataAsset");
    auto* contentMgr  = Reflect::FindInstance("UnionContentManager");
    if (!contentData || !contentMgr) { bCleared = true; return; }

    SDK::FProperty* pkgListProp = Reflect::FindProperty(contentData->Class, "PackageDataList");
    if (!pkgListProp) { bCleared = true; return; }

    auto* arrProp    = static_cast<SDK::FArrayProperty*>(pkgListProp);
    auto* structProp = static_cast<SDK::FStructProperty*>(arrProp->InnerProperty);
    int32_t pidOffset = Reflect::FindPropertyOffset(structProp->Struct, "P_Id");
    int32_t elemSize  = arrProp->InnerProperty->ElementSize;
    if (pidOffset < 0 || elemSize <= 0) { bCleared = true; return; }

    void*    pkgListAddr = reinterpret_cast<uint8_t*>(contentData) + pkgListProp->Offset;
    int32_t  pkgCount    = ReflectRaw::TArrayNum(pkgListAddr);

    std::vector<int32_t> allPkgIds;
    for (int i = 0; i < pkgCount; i++)
        allPkgIds.push_back(ReflectRaw::ReadInt32(ReflectRaw::TArrayAt(pkgListAddr, i, elemSize), pidOffset));

    SDK::FProperty* mPkgProp = Reflect::FindProperty(contentMgr->Class, "m_packageIds");
    if (!mPkgProp || allPkgIds.empty()) { bCleared = true; return; }

    void*   mPkgAddr = reinterpret_cast<uint8_t*>(contentMgr) + mPkgProp->Offset;
    auto*   mPkgArr  = reinterpret_cast<ReflectRaw::RawTArray*>(mPkgAddr);

    std::vector<int32_t> existing;
    for (int i = 0; i < mPkgArr->NumElements; i++)
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

// ---------------------------------------------------------------------------
// Super Sonic DataTable patch
//
// ROOT CAUSE (final, confirmed):
//   UCharaSelectWindow::Setup() is a NATIVE C++ function. When it builds the
//   CSS grid it calls IsDriverSelectable() and GetCharaSelectIndexByDriverId()
//   directly as C++ calls — NOT through ProcessEvent. Our ProcessEvent hook is
//   completely blind to all of this.
//
//   The CSS bundle mod (DT_DriverData01-04 pak replacement) works because it
//   patches the data BEFORE Setup() reads it. We replicate that at runtime.
//
// FDriverDataCore memory layout (from UNION_structs.hpp Dumper-7 dump):
//   Struct starts at row pointer (FTableRowBase base = 8 byte vtable/pad)
//   +0x20  bool IsEnabled          — whether this driver appears in any CSS
//   +0x21  bool IsDefaultSelect    — whether pre-selected (keep false for SS)
//   +0x2C  EDriverId DriverId      — the driver enum value (uint8)
//   +0x34  int32 CharaSelectIndex  — grid slot index (-1 or absent = hidden)
//
// UDataTable memory layout (from Engine_classes.hpp):
//   +0x28  UScriptStruct* RowStruct
//   +0x30  TMap<FName, uint8*> RowMap  (0x50 bytes)
//
// TMap<FName,uint8*> raw iteration:
//   The map is a TSet<TPair<FName,uint8*>> internally (see UnrealContainers.hpp).
//   TSparseArray layout:
//     +0x00  void*  Data.Data        (ptr to element array)
//     +0x08  int32  Data.NumElements
//     +0x0C  int32  Data.MaxElements
//     +0x10  FBitArray AllocationFlags
//              TInlineAllocator<4> = 16 bytes inline uint32[4]
//              +int32 NumBits (+0x20), +int32 MaxBits (+0x24)
//     +0x28  int32  FirstFreeIndex
//     +0x2C  int32  NumFreeIndices
//   Hash follows at +0x30 (8 bytes), HashSize at +0x38.
//
//   Each element in Data array is 0x18 (24) bytes:
//     union { struct { FName Key (8); uint8* Value (8); int32 HashNextId (4); int32 HashIndex (4); }; FreeListLink; }
//     Valid elements have their bit set in AllocationFlags.
//
//   Bit i in AllocationFlags (inline path, NumBits <= 128):
//     uint32* words = (uint32*)(mapBase + 0x10)
//     set = (words[i >> 5] >> (i & 31)) & 1
// ---------------------------------------------------------------------------

// Offsets within FDriverDataCore row pointer (row ptr already points past FTableRowBase)
static constexpr int32_t kDDC_IsEnabled        = 0x20;
static constexpr int32_t kDDC_IsDefaultSelect  = 0x21;
static constexpr int32_t kDDC_DriverId         = 0x2C;
static constexpr int32_t kDDC_CharaSelectIndex = 0x34;
static constexpr uint8_t kSuperSonicDriverId   = 46;

// TMap raw layout offsets (relative to start of TMap = start of TSet = start of TSparseArray)
static constexpr int32_t kMap_DataPtr    = 0x00; // void*  (ptr to element array)
static constexpr int32_t kMap_DataNum    = 0x08; // int32  number of allocated slots
static constexpr int32_t kMap_AllocFlags = 0x10; // uint32[4] inline bit array

// Size of each element slot in the sparse array
static constexpr int32_t kMap_ElemStride = 0x18; // FName(8)+ptr(8)+int32(4)+int32(4) = 24
// Offsets within one element slot
static constexpr int32_t kElem_KeyFName  = 0x00; // FName (8 bytes)
static constexpr int32_t kElem_ValuePtr  = 0x08; // uint8* (row data pointer)

static bool s_DTPatched = false;

// Iterate TMap<FName,uint8*> at mapAddr, calling fn(rowDataPtr) for every valid entry
// where DriverId at rowDataPtr+kDDC_DriverId == targetDriverId.
static int PatchDriverDataTable(SDK::UObject* tableObj, uint8_t targetDriverId, int32_t csIndex)
{
    if (!tableObj) return 0;

    // RowMap is at offset 0x30 in UDataTable
    uint8_t* mapBase = reinterpret_cast<uint8_t*>(tableObj) + 0x30;

    void*   dataPtr  = *reinterpret_cast<void**>  (mapBase + kMap_DataPtr);
    int32_t dataNum  = *reinterpret_cast<int32_t*>(mapBase + kMap_DataNum);

    if (!dataPtr || dataNum <= 0 || dataNum > 4096) return 0;

    // AllocationFlags inline words (up to 4 uint32s = 128 bits)
    const uint32_t* allocWords = reinterpret_cast<const uint32_t*>(mapBase + kMap_AllocFlags);

    int patched = 0;
    for (int32_t i = 0; i < dataNum; i++) {
        // Check allocation bit
        uint32_t word = allocWords[i >> 5];
        if (!(word & (1u << (i & 31)))) continue; // free slot

        uint8_t* elem    = reinterpret_cast<uint8_t*>(dataPtr) + i * kMap_ElemStride;
        uint8_t* rowData = *reinterpret_cast<uint8_t**>(elem + kElem_ValuePtr);

        if (!rowData) continue;

        // Check DriverId
        uint8_t driverId = rowData[kDDC_DriverId];
        if (driverId != targetDriverId) continue;

        // Patch the row
        bool wasEnabled = rowData[kDDC_IsEnabled] != 0;
        int32_t wasCsIdx = *reinterpret_cast<int32_t*>(rowData + kDDC_CharaSelectIndex);

        rowData[kDDC_IsEnabled]       = 1;
        rowData[kDDC_IsDefaultSelect] = 0;
        if (*reinterpret_cast<int32_t*>(rowData + kDDC_CharaSelectIndex) < 0)
            *reinterpret_cast<int32_t*>(rowData + kDDC_CharaSelectIndex) = csIndex;

        int32_t newCsIdx = *reinterpret_cast<int32_t*>(rowData + kDDC_CharaSelectIndex);
        std::cout << "[DTpatch] Table 0x" << std::hex << reinterpret_cast<uintptr_t>(tableObj)
                  << " row " << i
                  << " driver=" << std::dec << (int)driverId
                  << " enabled: " << wasEnabled << "->" << true
                  << " csIdx: " << wasCsIdx << "->" << newCsIdx << "\n";
        patched++;
    }
    return patched;
}

// Scan GObjects for all UDataTable instances whose name contains "DT_DriverData"
// and patch Super Sonic's row in each. Also reads SS's CharaSelectIndex from the
// first GP-valid table to use as the fallback value for others.
static void PatchSuperSonicDataTables()
{
    if (s_DTPatched) return;

    SDK::UClass* dtClass = Reflect::FindClass("DataTable");
    if (!dtClass) {
        std::cout << "[SSPatch] UDataTable class not found\n";
        return;
    }

    // First pass: find SS's existing CharaSelectIndex from any table that has it enabled.
    // This is the GP-valid index we'll propagate to other tables.
    int32_t ssCSIndex = -1;
    int     tablesFound = 0;

    for (int i = 0; i < SDK::UObject::GObjects->Num(); i++) {
        SDK::UObject* obj = SDK::UObject::GObjects->GetByIndex(i);
        if (!obj || obj->IsDefaultObject()) continue;
        if (!obj->IsA(dtClass)) continue;

        std::string name = obj->GetName();
        if (name.find("DT_DriverData") == std::string::npos) continue;

        tablesFound++;

        // Read SS's current CharaSelectIndex from this table (to find the GP-mode value)
        if (ssCSIndex < 0) {
            uint8_t* mapBase = reinterpret_cast<uint8_t*>(obj) + 0x30;
            void*   dataPtr  = *reinterpret_cast<void**>(mapBase + kMap_DataPtr);
            int32_t dataNum  = *reinterpret_cast<int32_t*>(mapBase + kMap_DataNum);
            const uint32_t* allocWords = reinterpret_cast<const uint32_t*>(mapBase + kMap_AllocFlags);

            if (dataPtr && dataNum > 0 && dataNum <= 4096) {
                for (int32_t j = 0; j < dataNum; j++) {
                    if (!(allocWords[j >> 5] & (1u << (j & 31)))) continue;
                    uint8_t* elem    = reinterpret_cast<uint8_t*>(dataPtr) + j * kMap_ElemStride;
                    uint8_t* rowData = *reinterpret_cast<uint8_t**>(elem + kElem_ValuePtr);
                    if (!rowData) continue;
                    if (rowData[kDDC_DriverId] != kSuperSonicDriverId) continue;
                    int32_t idx = *reinterpret_cast<int32_t*>(rowData + kDDC_CharaSelectIndex);
                    if (idx >= 0) { ssCSIndex = idx; break; }
                }
            }
        }
    }

    if (tablesFound == 0) {
        std::cout << "[SSPatch] No DT_DriverData tables found in GObjects\n";
        return;
    }

    // If SS has no valid CharaSelectIndex anywhere, assign it after the last normal driver.
    // Normal roster is ~16 drivers, GP adds SS at slot 16. Use 16 as a safe default.
    if (ssCSIndex < 0) {
        ssCSIndex = 16;
        std::cout << "[SSPatch] SS has no existing CSIndex, defaulting to " << ssCSIndex << "\n";
    } else {
        std::cout << "[SSPatch] SS existing CSIndex = " << ssCSIndex << "\n";
    }

    // Second pass: patch all DT_DriverData tables
    int totalPatched = 0;
    for (int i = 0; i < SDK::UObject::GObjects->Num(); i++) {
        SDK::UObject* obj = SDK::UObject::GObjects->GetByIndex(i);
        if (!obj || obj->IsDefaultObject()) continue;
        if (!obj->IsA(dtClass)) continue;

        std::string name = obj->GetName();
        if (name.find("DT_DriverData") == std::string::npos) continue;

        std::cout << "[SSPatch] Patching table: " << name << "\n";
        totalPatched += PatchDriverDataTable(obj, kSuperSonicDriverId, ssCSIndex);
    }

    std::cout << "[SSPatch] Done. Patched " << totalPatched << " rows across " << tablesFound << " tables\n";
    s_DTPatched = true;
}

bool RunUnlockPhase(int phase)
{
    switch (phase) {
    case 0:  { if (cfg.HonorTitles)      { for (int i = 0; i < 500; i++) Reflect::CallStaticInt32("AppSaveGameHelper", "UnlockHonorTitle", i); std::cout << "[Phase 0] Honor titles\n"; } return true; }
    case 1:  { if (cfg.Drivers)          { int n = Reflect::GetEnumNum("EDriverId"); for (int i = 0; i < n; i++) { Reflect::CallStaticUInt8("AppSaveGameHelper", "SetDriverSelectable", (uint8_t)i); Reflect::CallStaticUInt8("AppSaveGameHelper", "ClearDriverNew", (uint8_t)i); } std::cout << "[Phase 1] Drivers (" << n << ")\n"; } return true; }
    case 2:  { if (cfg.MachineCustomize) { Reflect::CallStatic("MachineCustomizeUtilityLibrary", "StoreAllAura"); Reflect::CallStatic("MachineCustomizeUtilityLibrary", "StoreAllHorn"); Reflect::CallStatic("MachineCustomizeUtilityLibrary", "StoreAllMachineAssembly"); Reflect::CallStatic("MachineCustomizeUtilityLibrary", "StoreAllMachineParts"); Reflect::CallStatic("MachineCustomizeUtilityLibrary", "StoreAllSticker"); Reflect::CallStatic("MachineCustomizeUtilityLibrary", "UnlockGadgetAll"); std::cout << "[Phase 2] Machine customize\n"; } return true; }
    case 3:  { if (cfg.ColorPresets)     { int n = Reflect::GetEnumNum("EMachineId"); for (int i = 0; i < n; i++) Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "UnlockMachinePresetColor", (uint8_t)i); std::cout << "[Phase 3] Color presets\n"; } return true; }
    case 4:  { if (cfg.MirrorSpeed)      { Reflect::CallStaticBool("AppSaveGameHelper", "SetOpenMirror", true); Reflect::CallStaticBool("AppSaveGameHelper", "SetOpenSuperSonicSpeed", true); std::cout << "[Phase 4] Mirror & SSS\n"; } return true; }
    case 5:  { if (cfg.Music)            { for (int i = 0; i < 200; i++) Reflect::CallStaticInt32("AppSaveGameHelper", "SetAlbumAvailable", i); for (int i = 0; i < 500; i++) Reflect::CallStaticInt32("AppSaveGameHelper", "SetTrackAvailable", i); std::cout << "[Phase 5] Music\n"; } return true; }
    case 6:  { if (cfg.StagesDLC)        { Reflect::CallStaticBool("UnionContentUtils", "RequestCheckContent", true); std::cout << "[Phase 6] DLC stages\n"; } return true; }
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
            if (gp) { Reflect::CallInstance(gp, "CheatGrandPrix", "SetPlayedAnotherStageAll"); Reflect::CallInstanceUInt8Bool(gp, "CheatGrandPrix", "SetClearedGrandPrixEnding", 0, true); Reflect::CallInstanceUInt8Bool(gp, "CheatGrandPrix", "SetClearedGrandPrixEnding", 1, true); }
            std::cout << "[Phase 8] Secret stages\n"; }
        return true; }
    case 9:  { if (cfg.GadgetPlate)  { Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "SetCurrentGadgetPlateIdUseId", 7); Reflect::CallStatic("MachineCustomizeUtilityLibrary", "UpdateGadgetSlotNumInUserData"); std::cout << "[Phase 9] Gadget plate\n"; } return true; }
    case 10: { if (cfg.Challenges)   { Reflect::CallStatic("CheatChallenge", "AllChallengeClear"); Reflect::CallStaticBool("AppSaveGameHelper", "SetCompleteMainChallenge", true); Reflect::CallStaticBool("AppSaveGameHelper", "SetCompleteSpecialChallenge", true); int32_t pc = Reflect::CallStaticRetInt32("ChallengeStatsUtility", "GetChallengeProgressCount"); Reflect::CallStaticInt32("AppSaveGameHelper", "SetChallengeShowProgress", pc); Reflect::CallStaticFloat("AppSaveGameHelper", "SetChallengeLastShowProgress", 1.0f); std::cout << "[Phase 10] Challenges\n"; } return true; }
    case 11: {
        if (cfg.SuperSonicAll) {
            // Save-data layer: mark driver 46 selectable + open prerequisites
            Reflect::CallStaticUInt8("AppSaveGameHelper", "SetDriverSelectable", 46);
            Reflect::CallStaticBool("AppSaveGameHelper", "SetOpenSuperSonicSpeed", true);
            auto* gp = Reflect::FindCDO("CheatGrandPrix");
            if (gp) Reflect::CallInstanceBool(gp, "CheatGrandPrix", "SetOpenFever", true);

            // DataTable layer: patch DT_DriverData tables so SS appears in all CSS grids
            PatchSuperSonicDataTables();

            std::cout << "[Phase 11] Super Sonic all modes\n"; }
        return true; }
    case 12: { if (cfg.NF_CompleteMachine) { Reflect::CallStatic("MachineCustomizeUtilityLibrary", "DisableCompleteMachineNewFlags"); std::cout << "[Phase 12] NF_CompleteMachine\n"; } return true; }
    case 13: { if (cfg.NF_Sticker)         { Reflect::CallStatic("MachineCustomizeUtilityLibrary", "DisableStickerNewFlags");             std::cout << "[Phase 13] NF_Sticker\n";         } return true; }
    case 14: { if (cfg.NF_ColorPreset)     { Reflect::CallStatic("MachineCustomizeUtilityLibrary", "DisableMachineColorPresetNewFlags");   std::cout << "[Phase 14] NF_ColorPreset\n";     } return true; }
    case 15: { if (cfg.NF_Gadget)          { Reflect::CallStatic("MachineCustomizeUtilityLibrary", "DisableDisplayedGadgetNewFlags");      std::cout << "[Phase 15] NF_Gadget\n";          } return true; }
    case 16: { if (cfg.NF_PartsSpeed)      { Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "DisablePartsListNewFlagByType", 0); std::cout << "[Phase 16] NF_PartsSpeed\n";    } return true; }
    case 17: { if (cfg.NF_PartsAccel)      { Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "DisablePartsListNewFlagByType", 1); std::cout << "[Phase 17] NF_PartsAccel\n";    } return true; }
    case 18: { if (cfg.NF_PartsHandle)     { Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "DisablePartsListNewFlagByType", 2); std::cout << "[Phase 18] NF_PartsHandle\n";   } return true; }
    case 19: { if (cfg.NF_PartsPower)      { Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "DisablePartsListNewFlagByType", 3); std::cout << "[Phase 19] NF_PartsPower\n";    } return true; }
    case 20: { if (cfg.NF_PartsDash)       { Reflect::CallStaticUInt8("MachineCustomizeUtilityLibrary", "DisablePartsListNewFlagByType", 4); std::cout << "[Phase 20] NF_PartsDash\n";     } return true; }
    case 21: { if (cfg.NF_Horn) { int n = Reflect::GetEnumNum("EMachineHornType"); for (int i = 0; i < n; i++) Reflect::CallStaticUInt8Bool("MachineCustomizeUtilityLibrary", "SetCustomMachineHornNew", (uint8_t)i, false); std::cout << "[Phase 21] NF_Horn\n"; } return true; }
    case 22: { if (cfg.NF_HonorTitles) { for (int i = 0; i < 500; i++) Reflect::CallStaticInt32("AppSaveGameHelper", "ResetNewHonorTitle", i); std::cout << "[Phase 22] NF_HonorTitles\n"; } return true; }
    case 23: { if (cfg.NF_Challenges) { int32_t pc = Reflect::CallStaticRetInt32("ChallengeStatsUtility", "GetChallengeProgressCount"); Reflect::CallStaticInt32("AppSaveGameHelper", "SetChallengeShowProgress", pc); Reflect::CallStaticFloat("AppSaveGameHelper", "SetChallengeLastShowProgress", 1.0f); std::cout << "[Phase 23] NF_Challenges\n"; } return true; }
    case 24: { if (cfg.NF_Rewards) { Reflect::CallStatic("AppSaveGameHelper", "ClearRewardGetDisplayRequestDataAll"); std::cout << "[Phase 24] NF_Rewards\n"; } return true; }
    case 25: { if (cfg.Achievements) { UnlockSteamAchievements(); std::cout << "[Phase 25] Achievements\n"; } return true; }
    default: return false;
    }
}

// ---------------------------------------------------------------------------
// ProcessEvent hook
//
// The DataTable patch in Phase 11 handles the CSS grid appearance.
// Setup() reads from the DataTable natively — once the row has IsEnabled=true
// and a valid CharaSelectIndex, Super Sonic will appear in all CSS grids
// including Race Park (PartyRace) and Time Trial without any ProcessEvent
// interception needed.
//
// We keep one lightweight ProcessEvent role: detect when the player navigates
// BACK to a CSS screen AFTER we've already patched, to ensure the patch is
// still live (DataTable might be reloaded from disk on level transitions).
// ---------------------------------------------------------------------------
static bool s_SSCheckPending = false;

void __fastcall hk_AActor_ProcessEvent(SDK::AActor* Class, SDK::UFunction* Function, void* Parms)
{
    if (!bCleared) Clear();

    if (!cfg.HotkeyEnabled && !bUnlockStarted && !bUnlockDone) {
        if (!bAutoRunReady) { if (!Function->GetName().compare("OnInitStateSelectPlayMode")) bAutoRunReady = true; }
        else { bUnlockStarted = true; unlockPhase = 0; lastPhaseTime = GetTickCount64(); std::cout << "[SRCW] AutoRun starting...\n"; }
    }

    if (cfg.HotkeyEnabled && !bUnlockStarted && !bUnlockDone) {
        bool kd = (GetAsyncKeyState(cfg.UnlockKey) & 0x8000) != 0;
        if (kd && !bKeyWasDown) { bUnlockStarted = true; unlockPhase = 0; lastPhaseTime = GetTickCount64(); std::cout << "[SRCW] Hotkey pressed...\n"; }
        bKeyWasDown = kd;
    }

    if (bUnlockStarted && !bUnlockDone) {
        ULONGLONG now = GetTickCount64();
        if (now - lastPhaseTime >= (ULONGLONG)cfg.PhaseDelayMs) {
            if (RunUnlockPhase(unlockPhase)) { unlockPhase++; lastPhaseTime = now; }
            else { bUnlockDone = true; std::cout << "All phases complete!\n"; }
        }
    }

    // Re-check DataTable patch on every CSS screen entry in case tables were reloaded.
    // OnInitState fires for BPC_CharaSelectState_C in GP, Race Park, and Time Trial.
    if (cfg.SuperSonicAll && bUnlockDone && s_DTPatched) {
        std::string fname = Function->GetName();
        if (fname == "OnInitState" || fname == "Construct") {
            SDK::UObject* obj = reinterpret_cast<SDK::UObject*>(Class);
            if (obj && obj->Class) {
                std::string cname = obj->Class->GetName();
                if (cname.find("CharaSelect") != std::string::npos) {
                    // Reset patch flag so PatchSuperSonicDataTables re-runs
                    s_DTPatched = false;
                    s_SSCheckPending = true;
                }
            }
        }
    }

    Orig_AActor_ProcessEvent(Class, Function, Parms);

    // Run re-patch after Orig so we're not blocking the event
    if (s_SSCheckPending) {
        s_SSCheckPending = false;
        PatchSuperSonicDataTables();
    }
}
