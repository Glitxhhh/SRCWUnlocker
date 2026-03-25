#pragma once
#include <vector>
#include <string>

#include "Helpers.h"
#include "Reflect.h"

// Only engine-level SDK includes needed — NO game-specific headers
#include "CppSDK/SDK/Engine_classes.hpp"

// =============================================================================
// RUNTIME CONFIG — loaded from SRCW.ini, no recompile needed
// =============================================================================
struct SRCWConfig
{
    bool Console          = false;
    bool ClearOnly        = false;
    int  PhaseDelayMs     = 500;
    bool AutoRun          = false;
    bool HotkeyEnabled    = true;
    int  UnlockKey        = 0xC0;

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
    bool GadgetPlate      = true;
    bool Challenges       = true;

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
};

inline SRCWConfig cfg;

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
bool RunUnlockPhase(int phase);

void __fastcall hk_AActor_ProcessEvent(SDK::AActor* Class, SDK::UFunction* Function, void* Parms);
typedef void(__fastcall* AActor_ProcessEvent_t)(SDK::AActor* Class, SDK::UFunction* Function, void* Parms);
inline AActor_ProcessEvent_t Orig_AActor_ProcessEvent;
