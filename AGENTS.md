# 🤖 Diretrizes e Regras de Desenvolvimento (AGENTS.md)

Este documento estabelece as **regras arquiteturais e diretrizes obrigatórias** para qualquer Agente de IA ou desenvolvedor atuando no ecossistema de modificações de **Disgaea Mayhem**.

---

## 🏛️ Padrão Arquitetural de Inspiração: UE4SS Architecture

Nosso ecossistema segue a arquitetura modular e desacoplada inspirada no padrão de modding do **UE4SS** (`Echoes of Aincrad / UE4SS`):
* **Host / Loader Central:** O Mod Menu Overlay (`dxgi.dll` / `DisgaeaMayhemModMenu.dll`) atua puramente como orquestrador, gerenciador de UI (ImGui) e despachante de eventos.
* **Mods Desacoplados:** Cada mod é um subsistema independente, com seu próprio ciclo de vida, configurações e lógica de execução isolada (DLLs nativas em C++ ou scripts em Lua).
* **Comunicação por Bridge / Eventos:** A comunicação entre o Mod Menu e os mods ocorre exclusivamente via ciclo de vida genérico (`Mod_Enable()`, `Mod_Disable()`, `Mod_SetOption()`, `mod.json` ou `enabled.txt`).

---

## 🚫 1. Regra das Linguagens Permitidas (C++ e Lua ONLY)

* **PERMITIDO:**
  * **C++ Nativo:** Binários compilados para Windows x64 (`.exe`, `.dll`) utilizando MinGW GCC / G++ (`C:\TDM-GCC-64\bin\g++.exe`).
  * **Lua:** Scripts `.lua` para manipulação de rotinas do jogo e lógica interpretada leve.
  * **Batch (`.bat`):** Scripts simples apenas para acionamento direto de 1 clique pelo usuário.
* **ESTRITAMENTE PROIBIDO:**
  * **Python:** **NÃO use Python** em nenhuma ferramenta, mod, script de injeção ou patcher. Todos os scripts devem ser binários nativos C++ ou scripts Lua.

---

## 🧭 2. Proibição de Caminhos Absolutos Hardcoded (Auto-Discovery Obrigatório)

* **ESTRITAMENTE PROIBIDO:** **NUNCA** codifique caminhos absolutos de drives específicos da sua máquina (ex: `E:\Steam\...`, `C:\Disgaea\...`) dentro de arquivos `.cpp`, `.h`, `.exe`, `.dll` ou scripts.
* **OBRIGATÓRIO:** Todos os utilitários, executáveis e DLLs devem resolver seus caminhos via **Auto-Descobrimento Dinâmico (Auto-Discovery)**:
  1. Localizar arquivos relativos ao próprio executável (`GetModuleFileNameA(NULL, ...)`).
  2. Localizar o executável do jogo a partir do processo em execução via Win32 API (`QueryFullProcessImageNameA` / `GetModuleFileNameExA`).
  3. Varredura dinâmica de drives montados (`A:` a `Z:`) e pastas padrão do Steam.

---

## 🧩 3. Desacoplamento Total do Mod Menu

* O Mod Menu ([`mod_menu_overlay.cpp`](file:///C:/Disgaea_Mayhem_Scripts/native/mod_menu_overlay/mod_menu_overlay.cpp) / `DisgaeaMayhemModMenu.dll` / `dxgi.dll`) é **apenas um host genérico de UI (Overlay DirectX 12 com Dear ImGui) e despachante**.
* **NUNCA** adicione lógica de jogo, regras de negócio ou tratamentos específicos de mods no código-fonte do Mod Menu (`mod_menu_overlay.cpp`).
* Para mods do tipo **Toggle (`type: "toggle"`)**:
  * Ao ativar (`ON`): o host despacha `NotifyModToggle` / `Mod_Enable()`.
  * Ao desativar (`OFF`): o host despacha `NotifyModToggle` / `Mod_Disable()`.
  * Ao alterar sliders/opções: o host despacha `NotifyModOptionChanged` / `Mod_SetOption()`.
* Para mods do tipo **Action (`type: "action"`)**:
  * O Mod Menu despacha de forma puramente genérica através de `ExecuteModActionGeneric(ModItem& mod)`, executando `APLICAR_MOD_*.exe`, `APLICAR_MOD_*.bat` ou scripts Lua na pasta do mod.

---

## 📁 4. Estrutura Autônoma de Cada Mod (`mods/<mod_id>/`)

Cada mod deve ser completamente autônomo (*standalone*), independente e auto-contido em sua pasta:

```text
mods/<mod_id>/
├── mod.json        # Manifesto canonico schema_version 1 (imutavel: id, tipo, limites)
├── config.json     # Valores correntes das options (obrigatorio p/ toggle e system)
├── enabled.txt     # Estado persistido: exatamente "0" ou "1"
├── <mod_id>.dll    # Plugin ABI v1 (toggle/system)
└── README.md       # Documentacao tecnica do mod
```

Mods `type: "action"` trocam o `plugin` por um `executable` declarado no manifesto e
não usam `config.json` nem `enabled.txt`.

**Autoridade única:** `mod.json` e `config.json` pertencem ao loader. Um plugin nunca
os escreve, nunca se auto-ativa e nunca chama `LoadLibrary` para outro mod.

**Reúso:** helpers de hot path (validação de ponteiro, guarda de chamadas em voo,
escalonamento) vêm de `native/mod_loader/dm_mod_common.h`, header-only. Estado único do
processo — configuração, ciclo de vida, identidade da build do jogo — vem do loader via
`DmModHostContext`/`DmModLoaderApi`.

> Para escrever um mod novo, siga [`docs/GUIA_CRIAR_MOD.md`](docs/GUIA_CRIAR_MOD.md):
> contrato da ABI, esqueleto pronto, o que o loader faz por você e os erros que o
> validador rejeita.

---

## 📚 5. Documentação Modular de Engenharia Reversa na Pasta `docs/`

Qualquer estrutura de classe, VTable, TypeDescriptor RTTI, offset de struct ou fluxo de desmontagem de `Disgaea_Mayhem.exe` descoberto deve ser catalogado de forma modular em `docs/`:
* [`docs/ESTRUTURA_CLASSES_REVERSA.md`](file:///C:/Disgaea_Mayhem_Scripts/docs/ESTRUTURA_CLASSES_REVERSA.md) (Índice mestre consolidado)
* [`docs/MOTOR_NGF_ARQUITETURA.md`](file:///C:/Disgaea_Mayhem_Scripts/docs/MOTOR_NGF_ARQUITETURA.md)
* [`docs/SUBSISTEMA_ITEM_WORLD.md`](file:///C:/Disgaea_Mayhem_Scripts/docs/SUBSISTEMA_ITEM_WORLD.md)
* [`docs/SUBSISTEMA_CHARA_WORLD.md`](file:///C:/Disgaea_Mayhem_Scripts/docs/SUBSISTEMA_CHARA_WORLD.md)

---

## ⚙️ 6. Compilação e Ferramentas Nativas

* **Compilador C++:** MinGW GCC x64 (`C:\TDM-GCC-64\bin\g++.exe` / `gcc.exe`).
* **Compilação de DLLs de Hook / Plugin:**
  ```powershell
  g++ -O2 -shared -static -s chara_world.cpp -o chara_world.dll
  ```
* **Compilação de Utilitários Standalone:**
  ```powershell
  g++ -O2 -static -s apply_chara_world.cpp -o APLICAR_MOD_CHARA_WORLD.exe -lpsapi
  ```
* **Compilação do Mod Menu Overlay:**
  ```powershell
  powershell -ExecutionPolicy Bypass -File native/mod_menu_overlay/build.ps1
  ```
