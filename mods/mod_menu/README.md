# Disgaea Mayhem Mod Menu

System mod de interface do Mod Loader ABI v2.

- Plugin: `mod_menu.dll`
- Manifesto: `mod.json` (`type: "system"`, `required: true`)
- Atalhos: `F1`, `Insert`, `Home`, `L3 + R3` ou `Back`
- Fonte: `native/mod_menu_overlay/mod_menu_overlay.cpp`

A DLL contem somente hooks DirectX 12, entrada e UI ImGui. Descoberta, carga, persistencia e ciclo de vida pertencem ao `dxgi.dll`, implementado em `native/mod_loader`.

Enquanto a interface esta aberta, os comandos de teclado, mouse e controle sao
bloqueados para o jogo. O comando usado para fechar o menu so volta ao jogo
depois de ser solto.

Sliders alteram o valor em memoria durante o arraste e gravam `config.json` ao
serem soltos ou quando o menu e fechado.

`INSTALAR_MOD_MENU.exe` ainda nao grava o atlas NMPLTEX/YKCMP. Ele valida os arquivos e falha explicitamente sem alterar o atlas.
