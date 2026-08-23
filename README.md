# Disgaea Mayhem Modding

Ecossistema nativo C++ para carregar e gerenciar mods do Disgaea Mayhem.

## Arquitetura

- `dxgi.dll`: proxy DXGI e Mod Loader. Nao contem UI nem logica de um mod especifico.
- `mods/mod_menu/mod_menu.dll`: system mod obrigatorio com hooks DirectX 12 e interface ImGui.
- `mods/<id>/mod.json`: manifesto canonico `schema_version: 1` de cada mod.
- `mods/<id>/config.json`: valores persistidos das opcoes de cada plugin residente.
- `enabled.txt`: estado persistido de mods `toggle` e `system`.
- `native/mod_loader/mod_loader_api.h`: ABI nativa v1 compartilhada.

O loader descobre manifestos, valida caminhos/ABI/configuracoes, carrega cada plugin uma vez e centraliza enable, disable, opcoes e actions. `mod.json` define os tipos e limites; `config.json` contem os valores correntes. Plugins nao se autoativam em `DllMain` e nao sao injetados individualmente.

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

- `chara_world`: energia configurável e multiplicador dos atributos ganhos nos tiles.
- `safe_backup`: toggle residente com worker controlada pelo ciclo de vida.
- `item_world`: multiplicadores separados de nível e Item Points.
- `cheat_shop`: confirma e aplica os cinco limites de `5000%`.
- `dark_assembly`: aprovação garantida em memória, sem substituir `wish.dat`.
- `dlc_unlocker`: action nativa com executável explícito.

Os detalhes de engenharia reversa ficam em `docs/`. Os arquivos `.lub` de `data/script` pertencem a engine do jogo; nao sao fontes Lua de mods.
