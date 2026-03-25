# 🔓 SonicUnlocker

> Unlock everything. Instantly. Cleanly. Efficiently.

A lightweight DLL-based unlocker for **Sonic Racing CrossWorlds** that removes progression restrictions, clears warning flags, and enables full access to in-game content.

---

## ✨ Features

- 🧩 **Full Unlock System**
  - Unlock all characters, items, and content
- 🚫 **Clear Red Exclamation Flags**
  - Removes all “new/unread” indicators
- ⚡ **Optimized Execution**
  - Built-in timer system to reduce redundant unlock calls
- 🧱 **Modular Design**
  - Easily extend or modify functionality
- 💉 **DLL Injection Ready**
  - Simple drop-in usage, no complicated setup

---

## 📦 Installation

1. Download the latest release from the **Releases** tab  
2. Place `umpc.dll` into:

SonicRacingCrossWorlds\UNION\Binaries\Win64

3. Launch the game  
4. Done — everything should now be unlocked

---

## 🚀 Usage

No configuration required.

The unlocker runs automatically when the game loads and:
- Applies all unlocks
- Clears UI flags
- Maintains performance via timed execution

---

## 🛠️ Building

> Requires Visual Studio (C++)

1. Clone the repository:
git clone https://github.com/Glitxhhh/SonicUnlocker.git

2. Open the solution in Visual Studio  
3. Build in **Release x64**  
4. Output DLL will be located in:

/x64/Release/umpc.dll

---

## ⚠️ Disclaimer

- This project is intended for **educational and research purposes only**
- Use at your own risk
- Not affiliated with SEGA or Sonic Team
- Online use may result in restrictions or bans

---

## 📸 Preview

> *(Add screenshots or GIFs here for better presentation)*

---

## 🧠 How It Works

- Hooks into game memory at runtime  
- Overrides progression checks  
- Injects unlock states directly  
- Uses a timer system to prevent excessive writes  

---

## 📌 Roadmap

- [ ] Config system (enable/disable features)  
- [ ] UI overlay / debug menu  
- [ ] Pattern scanning for version independence  
- [ ] Expanded modular unlock controls  

---

## 🤝 Contributing

Pull requests are welcome.

If you're adding features:
- Keep modules isolated  
- Follow existing structure  
- Document new functionality  

---

## ⭐ Support

If you find this useful:
- Star the repo ⭐  
- Share it with others  

---

## 📜 License

MIT License (or your preferred license)

---

## 👤 Author

**Glitxhhh**
