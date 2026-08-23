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

## Instalacao

Extraia o pacote e, com o jogo fechado, execute `INSTALAR_MOD.exe`. O instalador
localiza o jogo ou aceita a pasta como argumento, valida o pacote, instala o
loader e os sete mods, configura o SmokeAPI e executa o validador ABI v1.

A instalacao e transacional. Em uma atualizacao, `config.json` e `enabled.txt`
existentes sao preservados. Se uma copia ou a validacao final falhar, os arquivos
anteriores sao restaurados. Instalacoes novas iniciam apenas o Mod Menu ativo.

## Build

Com o jogo fechado:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/mod_menu_overlay/build.ps1
```

O script compila e implanta loader, Mod Menu e plugins ABI v1. O validador nativo executado ao final verifica todos os manifestos, arquivos declarados e exports.
Ele tambem compila e testa o instalador em uma pasta isolada e, somente depois,
gera `Disgaea_Mayhem_Mod_Loader_Nexus.zip` com uma lista fechada de arquivos.

## Uso

Inicie o jogo normalmente. Abra o Mod Menu por `F1`, `Insert`, `Home`, `L3 + R3` ou `Back`.

Falhas de manifesto, configuração, versão do jogo ou inicialização aparecem no
estado do mod e são registradas em `mods/mod_loader.log`.

O rotulo integrado ao Main Menu ainda depende de um gravador NMPLTEX/YKCMP. O instalador atual valida os arquivos e falha explicitamente sem alterar o atlas.

## Mods atuais

- `chara_world`: energia configurável e multiplicador dos atributos ganhos nos tiles.
- `safe_backup`: backup inicial e a cada gravação de `save.002`, mantendo os N mais recentes por slot (`max_backups`).
- `item_world`: progresso de nível (até 20x), Item Points (até 200x) e raridade mínima. A raridade cobre apenas um dos seis call sites de `GenerateRarity`; ver `docs/SUBSISTEMA_ITEM_WORLD.md`.
- `cheat_shop`: mantém EXP, Mana, HL, Weapon Mastery e Item Drops em `5000%` enquanto estiver ativo. O valor de Item Drops pode aumentar muito a quantidade de recompensas.
- `dark_assembly`: aprovação garantida em memória, sem substituir `wish.dat`.
- `dlc_unlocker`: toggle residente que confirma em memória o consumo dos cinco itens simulados pelo SmokeAPI.

Os detalhes de engenharia reversa ficam em `docs/`. Os arquivos `.lub` de `data/script` pertencem a engine do jogo; nao sao fontes Lua de mods.
