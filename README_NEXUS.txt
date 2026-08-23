==================================================
Disgaea Mayhem - Native Mod Loader & Mod Suite
==================================================

A native C++ DirectX 12 Mod Loader and suite of gameplay mods for Disgaea Mayhem.

FEATURES:
- In-Game Mod Menu (Press F1, Insert, Home, or Gamepad L3+R3 / Back)
- Full keyboard, mouse, and controller input capture while the Mod Menu is open
- Item World Mod (Level progress, Item Points, and minimum equipment rarity)
- Chara World Mod (Configurable Energy & Stat Gain Multipliers)
- Cheat Shop (EXP, Mana, HL, Weapon Mastery, and Item Drops held at 5000%)
- Dark Assembly Approval (Guaranteed bill passing while enabled)
- Reusable SmokeAPI consumables (HL, Mana, and three Boost Tickets)
- Safe Backup (Initial backup and new backups when save.002 changes)

INSTALLATION:
1. Close the game.
2. Extract the complete archive to a normal folder.
3. Run INSTALAR_MOD.exe.
4. If more than one installation is found, run:
   INSTALAR_MOD.exe "path-to-the-folder-containing-Disgaea_Mayhem.exe"
5. Launch the game normally after the ABI validation succeeds.

The installer uses a transaction and restores the previous files if copying or validation fails. Existing config.json and enabled.txt files are preserved during an update. On a new installation, only the required Mod Menu starts enabled.

SMOKEAPI REQUIREMENT:
- The installer deploys SmokeAPI and keeps the original Steam library as steam_api64_o.dll, which SmokeAPI requires for forwarding.
- SmokeAPI.config.json enables auto_inject_inventory and lists definitions 1, 2, 3, 4, and 5 in extra_inventory_items.
- Without those injected entries, the five consumables are not displayed for redemption.

HOW TO USE IN-GAME:
- Press F1, Insert, Home on your keyboard or L3+R3 / Back on your controller to toggle the Mod Menu.
- Toggle mods on/off and adjust sliders in real-time.
- The Item Drops setting in Cheat Shop affects reward quantity and can generate unusually large result lists.
- Loader and plugin failures are recorded in mods/mod_loader.log.

UNINSTALLATION:
1. Close the game.
2. Remove dxgi.dll and the seven mod folders installed by this package.
3. Remove tools/mod_loader_validate.exe.
4. Remove the SmokeAPI steam_api64.dll and rename steam_api64_o.dll back to steam_api64.dll.
5. Remove SmokeAPI.config.json if no other installation uses it.
