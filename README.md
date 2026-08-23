# Disgaea Mayhem Modding

Ecossistema nativo C++ para carregar e gerenciar mods do Disgaea Mayhem.

## Arquitetura

- `dxgi.dll`: proxy DXGI e Mod Loader. Nao contem UI nem logica de um mod especifico.
- `mods/mod_menu/mod_menu.dll`: system mod obrigatorio com hooks DirectX 12 e interface ImGui.
- `mods/<id>/mod.json`: manifesto canonico `schema_version: 1` de cada mod.
- `enabled.txt`: estado persistido de mods `toggle` e `system`.
- `native/mod_loader/mod_loader_api.h`: ABI nativa v1 compartilhada.

O loader descobre manifestos, valida caminhos/ABI, carrega cada plugin uma vez e centraliza enable, disable, opcoes e actions. Plugins nao se autoativam em `DllMain` e nao sao injetados individualmente.

Consulte [docs/MOD_LOADER_ARQUITETURA.md](docs/MOD_LOADER_ARQUITETURA.md) para o diagnostico e o fluxo completo.

## Build

Com o jogo fechado:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/mod_menu_overlay/build.ps1
```

O script compila e implanta loader, Mod Menu e plugins ABI v1. O validador nativo executado ao final verifica todos os manifestos, arquivos declarados e exports.

## Uso

Inicie o jogo normalmente. Abra o Mod Menu por `F1`, `Insert`, `Home`, `L3 + R3` ou `Back`.

O rotulo integrado ao Main Menu ainda depende de um gravador NMPLTEX/YKCMP. O instalador atual valida os arquivos e falha explicitamente sem alterar o atlas.

## Mods atuais

- `chara_world`: toggle residente com hook validado do construtor de `CCharacterWorldInformation`.
- `safe_backup`: toggle residente com worker controlada pelo ciclo de vida.
- `item_world`: desativado; falta capturar de forma validada a instancia de `CItemWorldData`.
- `cheat_shop`, `dark_assembly` e `dlc_unlocker`: actions nativas com executavel explicito.

Os detalhes de engenharia reversa ficam em `docs/`. Os arquivos `.lub` de `data/script` pertencem a engine do jogo; nao sao fontes Lua de mods.
