# Guia de instalacao em outro computador

## Pre-requisitos

- Disgaea Mayhem x64 instalado pela Steam.
- TDM-GCC-64 no `PATH` ou em uma unica pasta `TDM-GCC-64\bin` na raiz de um
  drive montado.
- O jogo fechado durante build/implantacao.

Python nao faz parte do runtime nem do fluxo de build deste projeto.

## Estrutura canonica

```text
<pasta-do-jogo>/
|-- Disgaea_Mayhem.exe
|-- dxgi.dll                         # proxy DXGI + Mod Loader
|-- SmokeAPI.config.json             # exigido apenas pelos consumiveis Steam
|-- mods/
|   |-- mod_menu/
|   |   |-- mod.json
|   |   |-- config.json
|   |   |-- enabled.txt
|   |   `-- mod_menu.dll             # system mod de UI
|   |-- chara_world/                 # chara_world.dll + manifesto/configuracao
|   |-- item_world/                  # item_world.dll + manifesto/configuracao
|   |-- cheat_shop/                  # cheat_shop.dll + manifesto/configuracao
|   |-- dark_assembly/               # dark_assembly.dll + manifesto/configuracao
|   |-- dlc_unlocker/                # dlc_unlocker.dll + manifesto/configuracao
|   `-- safe_backup/                 # safe_backup.dll + manifesto/configuracao
`-- native/
    |-- mod_loader/
    `-- mod_menu_overlay/
```

Nao existe `mods/registry.json` nem uma DLL agregada em `mods/native`. Cada pasta com `mod.json` e a unidade canonica de descoberta.

Todo plugin `toggle` ou `system` exige `config.json` com `schema_version`, `mod_id` exato e todas as opcoes declaradas no manifesto. Campo ausente, extra, duplicado, tipo incorreto ou valor fora do intervalo rejeita o mod. Sliders sao aplicados em RAM durante o ajuste e gravados atomicamente ao encerrar a edicao ou fechar o menu. Uma falha de gravacao restaura o ultimo valor persistido.

O `dlc_unlocker` nao cria entradas no inventario. O pacote inclui o SmokeAPI e a
configuracao com `auto_inject_inventory: true` e as definicoes `1`, `2`, `3`, `4`
e `5` em `extra_inventory_items`. O instalador aplica esses arquivos e conserva a
biblioteca Steam original em `steam_api64_o.dll`. O plugin recusa qualquer pedido
fora dessas definicoes ou com uma quantidade diferente de uma unidade.

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

O build gera `Disgaea_Mayhem_Mod_Loader_Nexus.zip`. Em outro computador:

1. extraia o pacote;
2. feche o jogo;
3. execute `INSTALAR_MOD.exe`;
4. se a busca encontrar nenhuma ou mais de uma instalacao, informe como
   argumento a pasta que contem `Disgaea_Mayhem.exe`;
5. inicie o jogo somente depois da validacao ABI terminar sem erro.

O instalador preserva `config.json` e `enabled.txt` existentes. Uma instalacao
nova deixa apenas o Mod Menu ativado. Copias e remocoes conhecidas da arquitetura
antiga fazem parte de uma transacao; qualquer falha restaura o estado anterior.

Abra o Mod Menu por `F1`, `Insert`, `Home`, `L3 + R3` ou `Back`.

O rotulo integrado ao Main Menu depende de escrita NMPLTEX/YKCMP ainda nao implementada no instalador C++. O instalador valida os arquivos e retorna erro sem alterar o atlas; use os atalhos ate esse servico ser concluido.

Consulte `docs/MOD_LOADER_ARQUITETURA.md` para a arquitetura e as limitacoes atuais de cada mod.
