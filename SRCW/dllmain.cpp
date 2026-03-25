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
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string key; int value;
        if (!(iss >> key >> value)) continue;
        bool b = (value != 0);

        if (key == "Console") cfg.Console = b;
        else if (key == "ClearOnly") cfg.ClearOnly = b;
        else if (key == "PhaseDelayMs") cfg.PhaseDelayMs = value;
        else if (key == "AutoRun") cfg.AutoRun = b;
        else if (key == "HotkeyEnabled") cfg.HotkeyEnabled = b;
        else if (key == "UnlockKey") cfg.UnlockKey = value;
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
        else if (key == "GadgetPlate") cfg.GadgetPlate = b;
        else if (key == "Challenges") cfg.Challenges = b;
        else if (key == "Achievements") cfg.Achievements = b;
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
    f << "; 1=enable, 0=disable — changes take effect on next launch\n\n";
    f << "; --- General ---\n";
    f << "Console 0\nClearOnly 0\nPhaseDelayMs 500\n\n";
    f << "; --- Trigger ---\n";
    f << "AutoRun 0\nHotkeyEnabled 1\n";
    f << "; 192=~, 120=F9, 45=Insert\nUnlockKey 192\n\n";
    f << "; --- DLC Gate Removal ---\n";
    f << "ClearCharaDLC 1\nClearMachineDLC 1\nClearHonorDLC 1\nClearAlbumDLC 1\nClearStickerDLC 1\n\n";
    f << "; --- Save Data Unlocks ---\n";
    f << "HonorTitles 1\nDrivers 1\nMachineCustomize 1\nColorPresets 1\nMirrorSpeed 1\nMusic 1\nGadgetPlate 1\nChallenges 1\n\n";
    f << "; --- Steam Achievements (OFF by default) ---\n";
    f << "; WARNING: This will permanently unlock all Steam achievements\n";
    f << "Achievements 0\n\n";
    f << "; --- Stage Unlocks ---\n";
    f << "StagesDLC 1\nStagesGPOpen 1\nStagesSecret 1\n\n";
    f << "; --- New Flag Clearing ---\n";
    f << "NF_CompleteMachine 1\nNF_Sticker 1\nNF_ColorPreset 1\nNF_Gadget 1\n";
    f << "NF_PartsSpeed 1\nNF_PartsAccel 1\nNF_PartsHandle 1\nNF_PartsPower 1\nNF_PartsDash 1\n";
    f << "NF_Horn 1\nNF_HonorTitles 1\nNF_Jukebox 1\nNF_Challenges 1\nNF_Rewards 1\n";
    f.close();
}

void Init()
{
    LoadConfig();
    if (cfg.Console) {
        FILE* io; AllocConsole();
        freopen_s(&io, "CONIN$", "r", stdin);
        freopen_s(&io, "CONOUT$", "w", stderr);
        freopen_s(&io, "CONOUT$", "w", stdout);
        SetConsoleTitleA("SRCW");
    }
    std::cout << "[*] SRCW Unlocker (Reflection Build)\n";
    std::cout << "[+] Phase delay: " << cfg.PhaseDelayMs << "ms\n";
    if (cfg.Achievements) std::cout << "[+] Steam achievements: ENABLED\n";
    if (cfg.AutoRun) std::cout << "[+] Mode: AutoRun\n";
    else if (cfg.HotkeyEnabled) std::cout << "[+] Mode: Hotkey 0x" << std::hex << cfg.UnlockKey << std::dec << "\n";
    else std::cout << "[!] Warning: AutoRun and Hotkey both off!\n";
    CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)HookGame, CurrentModule, 0, nullptr);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CurrentModule = hModule;
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)Init, CurrentModule, 0, nullptr);
    }
    return TRUE;
}
