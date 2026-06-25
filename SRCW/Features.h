#pragma once

#include <Windows.h>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>

#include "Helpers.h"
#include "Reflect.h"
#include "CppSDK/SDK/Engine_classes.hpp"
#include "CppSDK/SDK/UnionSystem_classes.hpp"

struct SRCWConfig
{
    bool Console          = false;
    bool ClearOnly        = false;
    int  PhaseDelayMs     = 750;
    bool HotkeyEnabled    = false;  // false = autorun on menu load
    int  UnlockKey        = 0xC0;   // VK_OEM_3 (~)

    // When false (default), an ID resolved as "disabled" is left untouched (skipped).
    // When true, an ID resolved as "disabled" is actively re-locked in save data
    // wherever the game exposes a lock/clear primitive for that category.
    bool RemovalMode      = false;

    bool ClearCharaDLC    = true;
    bool ClearMachineDLC  = true;
    bool ClearHonorDLC    = true;
    bool ClearAlbumDLC    = true;
    bool ClearStickerDLC  = true;

    bool HonorTitles      = true;
    bool Drivers          = true;
    bool MachineCustomize = true;
    bool ColorPresets     = true;
    bool MirrorSpeed      = true;
    bool Music            = true;
    bool Gadgets          = true;
    bool GadgetPlate      = true;
    bool Challenges       = true;
    bool Achievements     = false;
//    bool SuperSonicAll    = false;

    bool StagesDLC        = true;
    bool StagesGPOpen     = true;
    bool StagesSecret     = true;

    bool NF_CompleteMachine = true;
    bool NF_Sticker       = true;
    bool NF_ColorPreset   = true;
    bool NF_Gadget        = true;
    bool NF_PartsSpeed    = true;
    bool NF_PartsAccel    = true;
    bool NF_PartsHandle   = true;
    bool NF_PartsPower    = true;
    bool NF_PartsDash     = true;
    bool NF_Horn          = true;
    bool NF_HonorTitles   = true;
    bool NF_Jukebox       = true;
    bool NF_Challenges    = true;
    bool NF_Rewards       = true;

    // Raw text keys parsed from [Drivers]/[HonorTitles]/[Albums]/[Tracks]/
    // [ColorPresets]/[Gadgets] INI sections. Keys may be a numeric ID or, for
    // enum-backed categories (Drivers/ColorPresets/Gadgets), the enum value's
    // name (e.g. "Axel"), resolved at runtime via Reflect::ResolveEnumValueByName
    // once the game's enums are loaded — see ResolveOverrides() in Features.cpp.
    std::unordered_map<std::string, bool> DriverOverridesRaw;
    std::unordered_map<std::string, bool> HonorTitleOverridesRaw;
    std::unordered_map<std::string, bool> AlbumOverridesRaw;
    std::unordered_map<std::string, bool> TrackOverridesRaw;
    std::unordered_map<std::string, bool> ColorPresetOverridesRaw;
    std::unordered_map<std::string, bool> GadgetOverridesRaw;

    // Resolved numeric-ID overrides, populated once by ResolveOverrides(). An ID
    // absent from its map falls back to the category master toggle above (so new
    // content introduced by a future game update still auto-unlocks unless
    // explicitly pinned here).
    std::unordered_map<int32_t, bool> DriverOverrides;
    std::unordered_map<int32_t, bool> HonorTitleOverrides;
    std::unordered_map<int32_t, bool> AlbumOverrides;
    std::unordered_map<int32_t, bool> TrackOverrides;
    std::unordered_map<int32_t, bool> ColorPresetOverrides;
    std::unordered_map<int32_t, bool> GadgetOverrides;
};

inline SRCWConfig cfg;

// Resolves whether `id` should be unlocked: explicit per-ID override if present,
// otherwise the category master toggle.
inline bool ResolveEnabled(const std::unordered_map<int32_t, bool>& overrides, int32_t id, bool categoryDefault)
{
    auto it = overrides.find(id);
    return it != overrides.end() ? it->second : categoryDefault;
}
inline const char ConfigFileName[] = ".\\UNION\\Binaries\\Win64\\SRCW.ini";

inline bool     bCleared = false;
inline int      unlockPhase = -1;
inline bool     bUnlockStarted = false;
inline bool     bUnlockDone = false;
inline bool     bKeyWasDown = false;
inline bool     bAutoRunReady = false;
inline ULONGLONG lastPhaseTime = 0;

void LoadConfig();
void WriteDefaultConfig();
void HookGame();
void Clear();
void ResolveOverrides();
bool RunUnlockPhase(int phase);
bool UnlockSteamAchievements();
void Cleanup();

SDK::UAppSaveGame* GetSaveGame();

void __fastcall hk_AActor_ProcessEvent(SDK::AActor* Class, SDK::UFunction* Function, void* Parms);
typedef void(__fastcall* AActor_ProcessEvent_t)(SDK::AActor* Class, SDK::UFunction* Function, void* Parms);
inline AActor_ProcessEvent_t Orig_AActor_ProcessEvent;
