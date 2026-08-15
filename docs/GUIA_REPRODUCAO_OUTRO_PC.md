# 🎮 Guia de Instalação e Reprodução em Outro Computador
### Disgaea Mayhem - Pacote Completo de Mods, In-Game Mod Menu & DLC Unlocker

Este documento contém todas as instruções necessárias para instalar, configurar e reproduzir exatamente toda a estrutura de mods do **Disgaea Mayhem (PC / Steam)** em uma máquina limpa ou após reinstalação do jogo.

---

## 📌 1. Visão Geral da Arquitetura

O ecossistema de modificação foi arquitetado para ser **100% automatizado, nativo e in-process**:

1. **Proxy DirectX 12 (`dxgi.dll`):**
   * Atua como auto-loader nativo ao iniciar o executável do jogo.
   * Não requer abrir nenhum script `.bat`, console externo ou Python durante as partidas.
   * Injeta o 5º slot (**"Mods"**) no Main Menu e renderiza o **Mod Manager Overlay (Dear ImGui)** no backbuffer DirectX 12.
   * Permite injeção e sincronização instantânea de **Boost Tickets 900%** diretamente pela interface in-game.

2. **Desbloqueio de DLCs e Itens Consumíveis (`SmokeAPI` + `data/database/`):**
   * Emula a camada Steamworks (`steam_api64.dll` e `SmokeAPI.config.json`).
   * Converte itens de microtransação online (Tipo 2) para DLCs offline (Tipo 1) em `DLC_information.dat`.
   * Permite resgates infinitos de Boost Tickets 900%, Pacotes de HL (1M) e Mana (100k) no NPC **Carlbunch** na base do jogo.

---

## 📁 2. Estrutura de Pastas Padronizada

A raiz do jogo contém apenas os arquivos essenciais de runtime. Todos os mods e ferramentas ficam organizados em seus respectivos diretórios:

```text
E:\Steam\steamapps\common\Disgaea Mayhem\
├── Disgaea_Mayhem.exe                # Executável do jogo
├── dxgi.dll                          # Auto-loader DirectX 12 + Mod Menu Nativo
├── steam_api64.dll                   # SmokeAPI (Wrapper da Steamworks)
├── steam_api64_o.dll                 # DLL original da Steam (Backup de segurança)
├── SmokeAPI.config.json              # Configuração de inventário e DLCs
├── lz4.dll, NmplDLL.dll, etc.        # Bibliotecas nativas do motor da NIS
│
├── data/                             # Dados do jogo (bancos de dados, mapas, modelos)
│   ├── database/                     # Tabelas (.dat) modificadas com multiplicadores e flags
│   └── fairy/                        # Atlas de animação do menu (AnmDat_1_00_EN.fad)
│
├── mods/                             # Diretório isolado de mods
│   ├── registry.json                 # Registro de sub-mods e configurações
│   ├── mod_menu/                     # Mod Menu In-Game (DirectX 12 + ImGui)
│   │   ├── INSTALAR_MOD_MENU.bat     # Instalador do rótulo "Mods" no atlas FAD
│   │   ├── INSTALAR_MOD_MENU.py
│   │   └── main_menu/mods_slot.dds   # Textura do 5º slot
│   └── dlc_unlocker/                 # Unlocker de DLCs e Boost Tickets 900%
│       ├── APLICAR_MOD_DLC.bat       # Script de reaplicação das tabelas
│       ├── APLICAR_MOD_DLC.py
│       └── INSTRUCOES_RESGATES.txt
│
├── docs/                             # Documentação técnica e guias
│   ├── GUIA_REPRODUCAO_OUTRO_PC.md   # Este guia
│   ├── MOD_MENU_README.md            # Documentação detalhada do overlay
│   └── MOD_ROADMAP.md                # Roadmap de novos sub-mods
│
├── native/                           # Código-fonte C++ do Overlay DirectX 12
│   └── mod_menu_overlay/
│       ├── build.ps1                 # Script de compilação com TDM-GCC 64-bit
│       ├── mod_menu_overlay.cpp      # Fonte com proxy DXGI e ImGui DX12
│       └── vendor/                   # ImGui 1.92.6 + MinHook 1.3.4
│
└── tools/                            # Ferramentas de engenharia reversa
    └── fad_texture_tool.py           # Parser e compressor NMPLTEX/LZ4
```

---

## 🚀 3. Passo a Passo de Instalação em um Novo PC

### Pré-requisitos:
* **Disgaea Mayhem** instalado via Steam (64-bit).
* **Python 3.10+** instalado (marcar a opção *"Add python.exe to PATH"* no instalador).
* *(Opcional, apenas para recompilar o C++)*: Compilador **TDM-GCC-64** (ou GCC 10+ x64).

### Procedimento de Instalação Rápida (Menos de 2 minutos):

1. **Copiar os Arquivos Base para a Pasta do Jogo:**
   * Copie o `dxgi.dll`, `steam_api64.dll` e `SmokeAPI.config.json` para a raiz do jogo (`E:\Steam\steamapps\common\Disgaea Mayhem\`).
   * Copie a pasta `mods/` e `tools/` para dentro do diretório do jogo.

2. **Instalar o Rótulo "Mods" no Menu Principal:**
   * Entre em `mods\mod_menu\` e execute **`INSTALAR_MOD_MENU.bat`**.
   * Ele aplicará o slot de textura no atlas `AnmDat_1_00_EN.fad` de forma segura, criando um backup automático de rollback.

3. **Reaplicar as Tabelas de DLCs & Multiplicadores:**
   * Entre em `mods\dlc_unlocker\` e execute **`APLICAR_MOD_DLC.bat`**.
   * Ele configurará o multiplicador de +900% em todos os tickets e liberará o resgate offline.

4. **Pronto!**
   * Inicie o jogo normalmente pela Steam.
   * Carregue seu save game.
   * Abra o **Main Menu** e clique em **Mods** para gerenciar recursos ou pressione **B** / **Esc** para fechar.

---

## 🎮 4. Como Usar os Recursos no Jogo

1. **Resgatar Tickets Ilimitados no NPC:**
   * Fale com o coelho **Carlbunch** (*DLC Special Content Shop*) na base.
   * Selecione **Apply DLC** e clique nos Boost Tickets, Sacos de HL ou Mana para receber os pacotes instantaneamente.

2. **Ativar o Multiplicador 900% nas Batalhas:**
   * Abra o menu do jogo -> **Settings (Configurações)**.
   * Ative a opção **`Use Boost Tickets`** para **`[On]`**.
   * Ao vencer batalhas, o ganho de EXP e Mana será multiplicado por **+900%**!

3. **Mod Manager Overlay:**
   * Abra o Main Menu a qualquer momento e entre na opção **Mods** para visualizar o status do engine, injetar tickets na RAM ou conferir os módulos ativos.
