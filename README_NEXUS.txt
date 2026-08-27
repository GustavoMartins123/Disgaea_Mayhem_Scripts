==================================================
Disgaea Mayhem - Native Mod Loader & Mod Suite
==================================================

A native C++ DirectX 12 Mod Loader and gameplay mod suite for Disgaea Mayhem.

FEATURES:
- In-Game Mod Menu (F1, Insert, Home, or Gamepad L3+R3 / Back)
- Chara World configurable energy lock
- Experimental Chara World stat gain multiplier (behavior is still being investigated and may vary between tiles)
- Item World level progress multiplier up to 20x
- Item World Item Points multiplier up to 200x
- Experimental minimum equipment rarity (partial coverage; not every item-generation path is handled yet)
- Cheat Shop EXP, Mana, HL, Weapon Mastery, and Item Drops held at 5000%
- Dark Assembly guaranteed approval while enabled
- Reusable SmokeAPI consumables (HL, Mana, and three Boost Tickets)
- Tactical AI profiles for enemies and companions
- Safe Backup with configurable backup retention per save slot

INSTALLATION:
1. Close the game.
2. Extract the complete archive to a normal folder.
3. Run INSTALAR_MOD.exe.
4. If more than one installation is found, run:
   INSTALAR_MOD.exe "path-to-the-folder-containing-Disgaea_Mayhem.exe"
5. Launch the game normally after validation succeeds.

Existing config.json and enabled.txt files are preserved during an update. On a new installation, only the required Mod Menu starts enabled.

SMOKEAPI REQUIREMENT:
- The installer deploys SmokeAPI and keeps the original Steam library as steam_api64_o.dll for forwarding.
- SmokeAPI.config.json injects definitions 1, 2, 3, 4, and 5 used by the reusable consumables.
- Without those injected entries, the five consumables are not displayed for redemption.

NOTES ABOUT GAME DATA:
Some technical documentation in the repository references the game's .dat files because they were useful while researching how its systems work. The current gameplay plugins do not rely on directly editing those .dat files; their implemented gameplay changes are applied in memory unless explicitly documented otherwise.

HOW TO USE IN-GAME:
- Press F1, Insert, Home on your keyboard or L3+R3 / Back on your controller to toggle the Mod Menu.
- Toggle mods on/off and adjust their options in real time.
- Item Drops in Cheat Shop can generate unusually large reward lists at 5000%.
- Loader and plugin failures are recorded in mods/mod_loader.log.

KNOWN EXPERIMENTAL FEATURES:
- Item World rarity modification currently works only on part of the game's item-generation paths and is still under investigation.
- Chara World stat gain multiplication can behave inconsistently between different tiles or gain paths and is still under investigation.

UNINSTALLATION:
1. Close the game.
2. Remove dxgi.dll and the eight plugin folders installed by this package (Mod Menu + seven gameplay/QoL mods).
3. Remove tools/mod_loader_validate.exe.
4. Remove the SmokeAPI steam_api64.dll and rename steam_api64_o.dll back to steam_api64.dll.
5. Remove SmokeAPI.config.json if no other installation uses it.
