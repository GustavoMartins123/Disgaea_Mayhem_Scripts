# 🤖 Diretrizes e Regras de Desenvolvimento (AGENTS.md)

Este documento estabelece as **regras arquiteturais e diretrizes obrigatórias** para qualquer Agente de IA ou desenvolvedor atuando no ecossistema de modificações de **Disgaea Mayhem**.

---

## 🚫 1. Regra das Linguagens Permitidas (C++ e Lua ONLY)

* **PERMITIDO:**
  * **C++ Nativo:** Binários compilados para Windows x64 (`.exe`, `.dll`) utilizando MinGW GCC / G++ (`C:\TDM-GCC-64\bin\g++.exe`).
  * **Lua:** Scripts `.lua` para manipulação de rotinas do jogo e lógica interpretada leve.
  * **Batch (`.bat`):** Scripts simples apenas para acionamento direto de 1 clique pelo usuário.
* **ESTRITAMENTE PROIBIDO:**
  * **Python:** **NÃO use Python** em nenhuma ferramenta, mod, script de injeção ou patcher. Todos os scripts devem ser binários nativos C++ ou scripts Lua.

---

## 🧩 2. Desacoplamento Total do Mod Menu

* O Mod Menu ([`mod_menu_overlay.cpp`](file:///C:/Disgaea_Mayhem_Scripts/native/mod_menu_overlay/mod_menu_overlay.cpp) / `DisgaeaMayhemModMenu.dll` / `dxgi.dll`) é **apenas um host genérico de UI (Overlay DirectX 12 com Dear ImGui) e despachante**.
* **NUNCA** adicione lógica de jogo, regras de negócio ou tratamentos específicos de mods no código-fonte do Mod Menu (`mod_menu_overlay.cpp`).
* O Mod Menu despacha ações de forma puramente genérica através de `ExecuteModActionGeneric(ModItem& mod)`, procurando por executáveis nativos `APLICAR_MOD_*.exe`, scripts `.bat` ou scripts Lua na pasta do respectivo mod.

---

## 📁 3. Estrutura Autônoma de Cada Mod (`mods/<mod_id>/`)

Cada mod deve ser completamente autônomo (*standalone*), independente e auto-contido em sua pasta:

```text
mods/<nome_do_mod>/
├── APLICAR_MOD_<nome>.exe   # Binário nativo compilado em C++
├── APLICAR_MOD_<nome>.bat   # Lançador de 1 clique
├── mod.json                 # Metadados, categoria, tipo (action/toggle) e opções
└── README.md                # Instruções e documentação específica do mod
```

---

## 🔄 4. Sincronização Obrigatória com o Repositório

Todas as modificações de código, scripts, documentação, metadados JSON e binários compilados **DEVEM** ser mantidas 100% sincronizadas entre:
1. **Repositório Central:** `C:\Disgaea_Mayhem_Scripts`
2. **Pasta do Jogo Instalado:** `E:\Steam\steamapps\common\Disgaea Mayhem`

---

## 📚 5. Documentação de Engenharia Reversa na Pasta `docs/`

Qualquer estrutura de classe, VTable, TypeDescriptor RTTI, offset de struct ou fluxo de desmontagem de `Disgaea_Mayhem.exe` descoberto durante as investigações deve ser imediatamente catalogado em:
* [`docs/ESTRUTURA_CLASSES_REVERSA.md`](file:///C:/Disgaea_Mayhem_Scripts/docs/ESTRUTURA_CLASSES_REVERSA.md)

---

## ⚙️ 6. Compilação e Ferramentas Nativas

* **Compilador C++:** MinGW GCC x64 (`C:\TDM-GCC-64\bin\g++.exe` / `gcc.exe`).
* **Compilação de Utilitários Standalone:**
  ```powershell
  g++ -O2 -static -s apply_mod.cpp -o APLICAR_MOD.exe
  ```
* **Compilação do Mod Menu Overlay:**
  ```powershell
  powershell -ExecutionPolicy Bypass -File native/mod_menu_overlay/build.ps1
  ```
