# Disgaea Mayhem Mod Menu

System mod de interface do Mod Loader ABI v1.

- Plugin: `mod_menu.dll`
- Manifesto: `mod.json` (`type: "system"`, `required: true`)
- Atalhos: `F1`, `Insert`, `Home`, `L3 + R3` ou `Back`
- Fonte: `native/mod_menu_overlay/mod_menu_overlay.cpp`

A DLL contem somente hooks DirectX 12, entrada e UI ImGui. Descoberta, carga, persistencia e ciclo de vida pertencem ao `dxgi.dll`, implementado em `native/mod_loader`.

`INSTALAR_MOD_MENU.exe` ainda nao grava o atlas NMPLTEX/YKCMP. Ele valida os arquivos e falha explicitamente sem alterar o atlas.
