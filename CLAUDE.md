# SRCWUnlocker — Project Context

DLL-based unlocker for **Sonic Racing CrossWorlds** ("UNION", UE4/5 — confirm engine version against
`CppSDK`). Reflection-based: resolves all game classes/functions/properties by string name at
runtime, so it survives game updates without recompiling against new SDK headers — only
`Reflect`-name lookups and pattern scans need revisiting after an update.

Built as `umpdc.dll` (DLL hijack/proxy). Drops into `SonicRacingCrossWorlds\UNION\Binaries\Win64`.

## What it Unlocks

Everything: drivers (characters), machine customization (parts, color presets, stickers, gadgets,
horns, aura), honor titles, music (albums/tracks), grand prix/stage completion flags, challenges,
mirror mode, Super Sonic speed, and "new" UI flags — without legitimate progression.

## Build

- **Toolchain:** MSVC, x64
- **Solution:** `SRCW.sln` → `SRCW/SRCW.vcxproj`
- **Output:** `umpdc.dll`
- **Config:** `SRCW.ini`, expected at `.\UNION\Binaries\Win64\SRCW.ini` relative to the game exe
- **CppSDK:** Dumper-7 output, currently from `5.4.3-0+TERRA5044TA.1-UNION`. Replace
  `SRCW/CppSDK/` wholesale when the game updates — don't hand-edit generated SDK headers.

## File Layout

```
SRCW/
├── dllmain.cpp       — entry, INI parse/write, console alloc, spawns HookGame
├── Features.h/.cpp   — SRCWConfig, phased unlock logic, ProcessEvent hook, Super Sonic hack
├── Reflect.h/.cpp    — runtime UE reflection API (find objects/functions/properties by name)
├── Helpers.h/.cpp    — PatternScan, TrampHook64
├── resource.h, SRCW.rc
└── CppSDK/           — Dumper-7 SDK dump (SDK/, Mappings if present, GObjects dumps if present)
```

## Architecture

`DllMain(PROCESS_ATTACH)` → spawn `HookGame()` thread.

`HookGame()` polls until `UWorld` + `UEngine` + `GameViewport` exist, pattern-scans for
`AActor::ProcessEvent`, and installs a `TrampHook64` trampoline (`hk_AActor_ProcessEvent`).
**No hardcoded RVA/offset** for ProcessEvent — found via byte pattern, so it should self-heal
across most updates unless the function prologue pattern itself changes.

**ProcessEvent hook flow (`hk_AActor_ProcessEvent`):**
1. `Clear()` — one-time: copies all `ContentDataAsset.PackageDataList[].P_Id` into
   `UnionContentManager.m_packageIds` (VirtualAlloc-grow if needed), then calls
   `UnionContentUtils::RequestCheckContent(true)`. This is what makes DLC/bundled content packages
   visible to the content system in the first place.
2. AutoRun trigger: waits for `Function->GetName() == "OnInitStateSelectPlayMode"`, then starts the
   phased unlock. (Hotkey mode is an alternative trigger, config-gated.)
3. Phased unlock timer (`GetTickCount64` + `cfg.PhaseDelayMs`, default 750ms) — advances
   `unlockPhase` by calling `RunUnlockPhase(phase)` each tick until it returns `false`.
4. `CALL ORIGINAL Orig_AActor_ProcessEvent`

**Phases (`RunUnlockPhase`, Features.cpp):** 0 honor titles, 1 drivers
(`AppSaveGameHelper::SetDriverSelectable` / `ClearDriverNew` per `EDriverId`), 2 machine customize
(`MachineCustomizeUtilityLibrary::StoreAll*` + `UnlockGadgetAll`), 3 color presets per `EMachineId`,
4 mirror/SSS, 5 music (`SetAlbumAvailable`/`SetTrackAvailable` loops, hardcoded upper bounds 200/500 —
**bump these if a content update adds more albums/tracks than the loop covers**), 6 DLC stages
(`RequestCheckContent`), 7 GP opened, 8 secret stages, 9 gadget plate, 10 challenges. Several
"new-flag clearing" phases (11+) are present but commented out.

**Super Sonic unlock** is a separate mechanism from the phased save-data unlock: it swaps the
native `ExecFunction` pointer (at `UFunction + 0xD8`) on `CharaSelectUtilityLibrary::IsDriverSelectable`
and `DriverDataUtilityLibrary::GetCharaSelectIndexByDriverId`, because these are called via native
C++ dispatch and never pass through the `ProcessEvent` hook. Currently disabled
(`InstallSuperSonicHooks()` call commented out in the hook).

## Known Update-Sensitive Spots

- **Hardcoded loop bounds:** Phase 0 (honor titles, 500), Phase 5 (albums 200 / tracks 500). If new
  content lands above these bounds it silently won't unlock — this is the first thing to check
  when "some new content doesn't unlock but old content does."
- **Enum-driven loops** (`EDriverId`, `EMachineId`, `EGrandPrixId`, `EGrandPrixEventFlag`,
  `EMachineHornType`) self-size via `Reflect::GetEnumNum`, so new entries there are picked up
  automatically — these are NOT where missing-content bugs usually come from.
- **`AActor::ProcessEvent` pattern scan** in `HookGame()` — if the function prologue changes
  enough across an engine/game update, the scan fails and the DLL spins forever waiting to hook.
- **Reflect:: name lookups** (class/function/property names) — these break if TT/Sega renames a
  UFunction or UClass. Confirm against the fresh `CppSDK` dump before assuming a logic bug.

## Reflection API (Reflect:: namespace)

All game objects resolved by name at runtime:
- `FindClass/FindCDO/FindInstance(name)` — cached GObjects scan
- `FindFunction/FindProperty/FindPropertyOffset` — walks the UStruct/FField chain
- `CallStatic*`/`CallInstance*` variants for different param signatures
- `ReflectRaw::RawTArray` + helpers for direct memory array manipulation (`Data`/`NumElements`/`MaxElements`)

## INI Format (SRCW.ini)

Key-value, one setting per line, matching the `SRCWConfig` fields in `Features.h` (e.g.
`Console`, `PhaseDelayMs`, `HotkeyEnabled`, `UnlockKey`, and the per-feature `Clear*`/feature
toggles). `LoadConfig()`/`WriteDefaultConfig()` in `dllmain.cpp`/`Features.cpp` define exact keys —
check there before assuming a flag name. A static copy lives at the repo root (`SRCW.ini`) purely
so the release CI has something to bundle — keep it in sync with `WriteDefaultConfig()`'s output.

### Per-ID overrides + RemovalMode (v3.2)

Six categories support sparse per-ID overrides via INI sections: `[Drivers]`, `[HonorTitles]`,
`[Albums]`, `[Tracks]`, `[ColorPresets]`, `[Gadgets]`. Each line is `<key> <0|1>`. For
**Drivers/ColorPresets/Gadgets** the key may be a numeric ID *or* an enum value name (e.g.
`Axel`) — resolved at runtime against the live `EDriverId`/`EMachineId`/`EGadgetId` `UEnum` via
`Reflect::ResolveEnumValueByName` (see `Reflect.cpp`), not the compiled SDK constants. This means
a name added by a future game update resolves correctly without rebuilding the DLL. HonorTitles/
Albums/Tracks have no enum backing their IDs, so those sections are numeric-only.

An ID absent from its section falls back to the category's master toggle (`cfg.Drivers`,
`cfg.Music`, etc.) — this preserves the "new content auto-unlocks" property; explicit overrides
are for pinning specific items, not for replacing the master switch.

Resolution happens once, lazily, via `ResolveOverrides()` (`Features.cpp`), called from
`RunUnlockPhase()` on its first invocation — **not** from `LoadConfig()`, because `LoadConfig()`
runs at `DLL_PROCESS_ATTACH` before `GObjects`/enums are populated. `LoadConfig()` only stores the
raw string keys into `cfg.*OverridesRaw`.

`RemovalMode 1` turns a resolved-disabled ID from "skip" into "actively re-lock in save data."
This is **not** implemented by removing TMap entries — the lightweight `TMap`/`TSet` reflection
containers in `UnrealContainers.hpp` can mutate existing values in place (proven safe; used
throughout) but cannot safely insert/remove keys (no hash-table sync with native code), so doing
that would desync the live save object. Verified safe relock primitives, found by tracing each
category's actual save-data field on `UAppSaveGame` (`UnionSystem_classes.hpp`):
- **Drivers** → flip `FUserDriverProperty.bIsSelectable` (`_UserDriverData.UserDriverList`)
- **Albums** → flip `FAlbumCondition.bUnlocked`/`bAvailable` (`_UserJukeboxData.AlbumCondition`)
- **Tracks** → flip `FTrackCondition.bAvailable` (`_UserJukeboxData.TrackCondition`)
- **Honor Titles** → remove the ID from `_UserHonorTitleData.UnlockedHonorTitleList`, a flat
  `TArray<int32>` (safe — no hash table involved, just a `TArray::Remove(Index)` shift)
- **Gadgets** → real `MachineCustomizeUtilityLibrary::LockGadget(EGadgetId)` UFunction (safest —
  goes through the game's own logic, not a raw field write)

**Color Presets has no verified relock path.** Its unlock state is presence-in-`TMap`
(`FUserMachineAssemblyData.ColorPresetList`, keyed by `EMachineColorPresetId`/`EMachineId`), and
there's no dedicated lock UFunction for it (unlike Gadgets/Horns). Don't add one without first
confirming a safe mechanism — under `RemovalMode` it currently just skips and logs a notice.
Stickers/Horns/Auras/MachineParts are in the same "TMap-presence = unlocked" boat and are
currently only category-toggle-controlled (no per-ID override, no relock) — extending them needs
the same kind of save-field tracing done above, not a guess.

## Coding Conventions

- Standard C++ stdint types (`int32_t`, `uint8_t`, etc.)
- File-static helpers and phase logic live in `Features.cpp`; config/state as `inline` globals in `Features.h`
- Diagnostics printed via `std::cout` when `cfg.Console` is set (console alloc'd conditionally — unlike
  the LEGO Batman fork, this one is config-gated, not always-on)
- `VirtualAlloc` for growing native arrays in place (see `Clear()`'s `m_packageIds` growth) — same
  pattern used for any other TArray that needs to grow beyond `MaxElements`
- `TrampHook64` for inline hooks; `PatternScan` for locating functions without fixed RVAs
