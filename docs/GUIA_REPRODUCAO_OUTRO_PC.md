# Guia de instalacao em outro computador

## Pre-requisitos

- Disgaea Mayhem x64 instalado pela Steam.
- TDM-GCC-64 instalado em `C:\TDM-GCC-64\bin` para recompilar.
- O jogo fechado durante build/implantacao.

Python nao faz parte do runtime nem do fluxo de build deste projeto.

## Estrutura canonica

```text
<pasta-do-jogo>/
|-- Disgaea_Mayhem.exe
|-- dxgi.dll                         # proxy DXGI + Mod Loader
|-- mods/
|   |-- mod_menu/
|   |   |-- mod.json
|   |   |-- enabled.txt
|   |   `-- mod_menu.dll             # system mod de UI
|   |-- chara_world/
|   |   |-- mod.json
|   |   |-- enabled.txt
|   |   `-- chara_world.dll
|   |-- item_world/
|   |   |-- mod.json
|   |   |-- enabled.txt
|   |   `-- item_world.dll
|   `-- safe_backup/
|       |-- mod.json
|       |-- enabled.txt
|       `-- safe_backup.dll
`-- native/
    |-- mod_loader/
    `-- mod_menu_overlay/
```

Nao existe `mods/registry.json` nem uma DLL agregada em `mods/native`. Cada pasta com `mod.json` e a unidade canonica de descoberta.

## Build e implantacao

A partir da pasta do jogo:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/mod_menu_overlay/build.ps1
```

O script:

1. compila `dxgi.dll` como loader sem UI;
2. compila `mods/mod_menu/mod_menu.dll` como system mod;
3. compila os plugins residentes ABI v1;
4. implanta cada artefato no caminho declarado pelo manifesto;
5. executa o validador nativo de schema, arquivos e exports;
6. executa um smoke test isolado do proxy, loader e system mod, sem actions.

Qualquer falha encerra o build. Nao ha escolha de binario alternativo nem injecao manual.

## Uso

Inicie o jogo normalmente. Abra o Mod Menu por `F1`, `Insert`, `Home`, `L3 + R3` ou `Back`.

O rotulo integrado ao Main Menu depende de escrita NMPLTEX/YKCMP ainda nao implementada no instalador C++. O instalador valida os arquivos e retorna erro sem alterar o atlas; use os atalhos ate esse servico ser concluido.

Consulte `docs/MOD_LOADER_ARQUITETURA.md` para a arquitetura e as limitacoes atuais de cada mod.
