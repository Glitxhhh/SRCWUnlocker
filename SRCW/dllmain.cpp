#include "Features.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

inline HMODULE CurrentModule = nullptr;

void LoadConfig()
{
    std::ifstream file(ConfigFileName);
    if (!file.is_open()) { WriteDefaultConfig(); file.open(ConfigFileName); if (!file.is_open()) return; }
    std::string line;
    std::unordered_map<std::string, bool>* activeSection = nullptr;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            std::string section = line.substr(1, line.find(']') - 1);
            if      (section == "Drivers")      activeSection = &cfg.DriverOverridesRaw;
            else if (section == "HonorTitles")  activeSection = &cfg.HonorTitleOverridesRaw;
            else if (section == "Albums")       activeSection = &cfg.AlbumOverridesRaw;
            else if (section == "Tracks")       activeSection = &cfg.TrackOverridesRaw;
            else if (section == "ColorPresets") activeSection = &cfg.ColorPresetOverridesRaw;
            else if (section == "Gadgets")      activeSection = &cfg.GadgetOverridesRaw;
            else if (section == "Machines")     activeSection = &cfg.MachineOverridesRaw;
            else activeSection = nullptr;
            continue;
        }

        std::istringstream iss(line); std::string key; int value;
        if (!(iss >> key >> value)) continue;
        bool b = (value != 0);

        if (activeSection) { (*activeSection)[key] = b; continue; }

        if      (key == "Console") cfg.Console = b;
        else if (key == "ClearOnly") cfg.ClearOnly = b;
        else if (key == "PhaseDelayMs") cfg.PhaseDelayMs = value;
        else if (key == "HotkeyEnabled") cfg.HotkeyEnabled = b;
        else if (key == "UnlockKey") cfg.UnlockKey = value;
        else if (key == "RemovalMode") cfg.RemovalMode = b;
        else if (key == "FestivalOnlyMode") cfg.FestivalOnlyMode = b;
        else if (key == "ClearCharaDLC") cfg.ClearCharaDLC = b;
        else if (key == "ClearMachineDLC") cfg.ClearMachineDLC = b;
        else if (key == "ClearHonorDLC") cfg.ClearHonorDLC = b;
        else if (key == "ClearAlbumDLC") cfg.ClearAlbumDLC = b;
        else if (key == "ClearStickerDLC") cfg.ClearStickerDLC = b;
        else if (key == "HonorTitles") cfg.HonorTitles = b;
        else if (key == "Drivers") cfg.Drivers = b;
        else if (key == "MachineCustomize") cfg.MachineCustomize = b;
        else if (key == "ColorPresets") cfg.ColorPresets = b;
        else if (key == "MirrorSpeed") cfg.MirrorSpeed = b;
        else if (key == "Music") cfg.Music = b;
        else if (key == "Gadgets") cfg.Gadgets = b;
        else if (key == "Machines") cfg.Machines = b;
        else if (key == "GadgetPlate") cfg.GadgetPlate = b;
        else if (key == "Challenges") cfg.Challenges = b;
        else if (key == "Achievements") cfg.Achievements = b;
        //else if (key == "SuperSonicAll") cfg.SuperSonicAll = b;
        else if (key == "StagesDLC") cfg.StagesDLC = b;
        else if (key == "StagesGPOpen") cfg.StagesGPOpen = b;
        else if (key == "StagesSecret") cfg.StagesSecret = b;
        else if (key == "NF_CompleteMachine") cfg.NF_CompleteMachine = b;
        else if (key == "NF_Sticker") cfg.NF_Sticker = b;
        else if (key == "NF_ColorPreset") cfg.NF_ColorPreset = b;
        else if (key == "NF_Gadget") cfg.NF_Gadget = b;
        else if (key == "NF_PartsSpeed") cfg.NF_PartsSpeed = b;
        else if (key == "NF_PartsAccel") cfg.NF_PartsAccel = b;
        else if (key == "NF_PartsHandle") cfg.NF_PartsHandle = b;
        else if (key == "NF_PartsPower") cfg.NF_PartsPower = b;
        else if (key == "NF_PartsDash") cfg.NF_PartsDash = b;
        else if (key == "NF_Horn") cfg.NF_Horn = b;
        else if (key == "NF_HonorTitles") cfg.NF_HonorTitles = b;
        else if (key == "NF_Jukebox") cfg.NF_Jukebox = b;
        else if (key == "NF_Challenges") cfg.NF_Challenges = b;
        else if (key == "NF_Rewards") cfg.NF_Rewards = b;
    }
    file.close();
    if (cfg.ClearOnly) bUnlockDone = true;
}

void WriteDefaultConfig()
{
    std::ofstream f(ConfigFileName, std::ofstream::out | std::ofstream::trunc);
    if (!f.is_open()) return;
    f << "; SRCW Unlocker Config (Reflection Build)\n";
    f << "; 1=enable, 0=disable — changes on next launch\n\n";
    f << "; --- General ---\n";
    f << "Console 0\nClearOnly 0\nPhaseDelayMs 500\n\n";
    f << "; --- Trigger ---\n";
    f << "; HotkeyEnabled 0 = autorun on menu load (default)\n";
    f << "; HotkeyEnabled 1 = wait for hotkey press\n";
    f << "HotkeyEnabled 0\n";
    f << "; 192=~, 120=F9, 45=Insert\nUnlockKey 192\n\n";
    f << "; RemovalMode 0 (default) = anything disabled below is left untouched (skipped)\n";
    f << "; RemovalMode 1 = anything disabled below is actively re-locked in save data,\n";
    f << ";   wherever the game exposes a lock function for that category (currently: Gadgets only).\n";
    f << ";   Categories without a lock primitive will just be skipped and a notice logged.\n";
    f << "RemovalMode 0\n";
    f << "; FestivalOnlyMode 1 forces Drivers/Music/MachineCustomize/ColorPresets/HonorTitles/\n";
    f << ";   Gadgets/Machines OFF regardless of their settings below, and unlocks ONLY the\n";
    f << ";   drivers/machines/albums tied to past limited-time festival content (read live\n";
    f << ";   from the game). Use this if you want missed festival content without unlocking\n";
    f << ";   the full permanent roster.\n";
    f << "FestivalOnlyMode 0\n\n";
    f << "; --- DLC Gate Removal ---\n";
    f << "ClearCharaDLC 1\nClearMachineDLC 1\nClearHonorDLC 1\nClearAlbumDLC 1\nClearStickerDLC 1\n\n";
    f << "; --- Save Data Unlocks (category master switches) ---\n";
    f << "; Each of these can be fine-tuned per-ID with the [Drivers]/[HonorTitles]/[Albums]/\n";
    f << "; [Tracks]/[ColorPresets]/[Gadgets] sections below. An ID not listed in its section\n";
    f << "; falls back to the master switch here, so new content from a future update still\n";
    f << "; auto-unlocks unless you explicitly pin it.\n";
    f << "HonorTitles 1\nDrivers 1\nMachineCustomize 1\nColorPresets 1\nMirrorSpeed 1\nMusic 1\nGadgets 1\nMachines 1\nGadgetPlate 1\nChallenges 1\n\n";
    f << "; --- Optional (OFF by default) ---\n";
    f << "; WARNING: Achievements will permanently unlock on Steam\nAchievements 0\n";
    f << "; Super Sonic selectable + Fever mode in Race Park/Time Trial\nSuperSonicAll 1\n\n";
    f << "; --- Stage Unlocks ---\n";
    f << "StagesDLC 1\nStagesGPOpen 1\nStagesSecret 1\n\n";
    f << "; --- New Flag Clearing ---\n";
    f << "NF_CompleteMachine 1\nNF_Sticker 1\nNF_ColorPreset 1\nNF_Gadget 1\n";
    f << "NF_PartsSpeed 1\nNF_PartsAccel 1\nNF_PartsHandle 1\nNF_PartsPower 1\nNF_PartsDash 1\n";
    f << "NF_Horn 1\nNF_HonorTitles 1\nNF_Jukebox 1\nNF_Challenges 1\nNF_Rewards 1\n\n";
    f << "; --- Per-item overrides (optional, sparse) ---\n";
    f << "; Format: <key> <0 or 1>, one per line.\n";
    f << "; [Drivers]/[ColorPresets]/[Gadgets]/[Machines] keys may be the enum name (e.g. Axel) OR a\n";
    f << "; numeric ID -- names are resolved against the running game, so this works for\n";
    f << "; content added by future updates without needing a new DLL build.\n";
    f << "; [HonorTitles]/[Albums]/[Tracks] only support numeric IDs (no names exposed).\n";
    f << ";\n";
    f << "; Example to unlock ONLY Axel and lock every other driver:\n";
    f << ";   Drivers 0\n";
    f << ";   [Drivers]\n";
    f << ";   Axel 1\n";
    f << ";\n";
    f << "; RemovalMode 1 will actively re-lock anything disabled here, but only for\n";
    f << "; categories the game exposes a real lock primitive for: Drivers, Albums,\n";
    f << "; Tracks, HonorTitles, and Gadgets. ColorPresets has no safe lock path yet,\n";
    f << "; so disabling a color preset there only skips it -- it will not be removed\n";
    f << "; from an existing save.\n";
    f << "[Drivers]\n\n";
    f << "[HonorTitles]\n\n";
    f << "[Albums]\n\n";
    f << "[Tracks]\n\n";
    f << "[ColorPresets]\n\n";
    f << "[Gadgets]\n\n";
    f << "[Machines]\n";
    f.close();
}

void Init()
{
    LoadConfig();
    if (cfg.Console) { FILE* io; AllocConsole(); freopen_s(&io, "CONIN$", "r", stdin); freopen_s(&io, "CONOUT$", "w", stderr); freopen_s(&io, "CONOUT$", "w", stdout); SetConsoleTitleA("SRCW"); }
    std::cout << "[*] SRCW Unlocker (Reflection Build)\n[+] Delay: " << cfg.PhaseDelayMs << "ms\n";
    if (cfg.HotkeyEnabled) std::cout << "[+] Mode: Hotkey 0x" << std::hex << cfg.UnlockKey << std::dec << "\n";
    else std::cout << "[+] Mode: AutoRun\n";
    CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)HookGame, CurrentModule, 0, nullptr);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(hModule); CurrentModule = hModule; CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)Init, CurrentModule, 0, nullptr); }
    else if (reason == DLL_PROCESS_DETACH) { if (cfg.Console) FreeConsole(); }
    return TRUE;
}
