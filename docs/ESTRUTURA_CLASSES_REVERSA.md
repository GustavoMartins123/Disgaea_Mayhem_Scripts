# 🏛️ Disgaea Mayhem - Estrutura de Classes & Engenharia Reversa

Este documento consolida a engenharia reversa completa do motor nativo (**Ngf / Nippon Ichi Game Framework**) do jogo **Disgaea Mayhem (PC / Steam / x64)**, detalhando a hierarquia de classes C++, VTables, offsets de memória e ciclo de vida do subsistema de **Itens e Item World**.

---

## 📌 1. Visão Geral do Motor (Ngf Engine)

* **Arquitetura:** 64-bit Windows PE (`Disgaea_Mayhem.exe`), Base Address `0x140000000`.
* **Renderizador:** DirectX 12 nativo (`d3d12.dll` / `dxgi.dll` proxy).
* **Gerenciamento de Memória & Objetos:** Ponteiros intrusivos com contagem atômica de referências (`Core::Nmpl::intrusive_ptr<T>`), utilizando instruções atômicas de barramento (`lock inc DWORD PTR [rcx+0x8]`, `lock xadd DWORD PTR [rcx+0x8], eax`).
* **Sistema de Estados e Tarefas:** Arquitetura desacoplada baseada em `CTask` (gerenciador de tarefas) e `CState` (máquina de estados finitos).

---

## 🗡️ 2. Subsistema de Itens (`CItemStatus`)

### 📍 VTable & Localização
* **RTTI Symbol:** `.?AVCItemStatus@@` (TypeDescriptor RVA: `0xCA4318`, VA: `0x140CA4318`)
* **VTable RVA:** `0xA252C0` (VA: `0x140A252C0`)
* **Função de Nível (`setLevel`):** `0x1401CFB00`
* **Função de Recálculo de Atributos (`recalculateStats`):** `0x1401CEA80`

### 📐 Layout de Memória do Objeto Item (`CItemStatus` / `ItemData`)

| Offset (Hex) | Tipo | Descrição / Função |
| :--- | :--- | :--- |
| `+0x00` | `void*` | Ponteiro para VTable de `CItemStatus` (`0x140A252C0`) |
| `+0x08` | `uint32_t` | Contador Atômico de Referências (Ref Count) |
| `+0x10` | `ItemData*` | Ponteiro para definição estática do item (`item.dat`) |
| `+0x18` | `void*` | Metadados de Tipo e Propriedades de Equipamento |
| `+0x28` | `ItemStatusInner*` | Estrutura interna de status dinâmico do item |
| `+0x5C` | `int32_t` | Limite Máximo de Nível Base (Level Cap) |
| `+0xA0` | `int32_t` | Categoria de Equipamento / Slot (Espada, Machado, Armadura, etc.) |
| `+0xA8` | `StatContainer` | Contêiner do Atributo HP |
| `+0xB0` | `int64_t` | Valor Base / Atual de HP |
| `+0xB8` | `StatContainer` | Contêiner do Atributo SP |
| `+0xC0` | `int64_t` | Valor Base / Atual de SP |
| `+0xC8` | `StatContainer` | Contêiner do Atributo ATK |
| `+0xD0` | `int64_t` | Valor Base / Atual de ATK |
| `+0xD8` | `StatContainer` | Contêiner do Atributo DEF |
| `+0xE0` | `int64_t` | Valor Base / Atual de DEF |
| `+0xE8` | `StatContainer` | Contêiner do Atributo INT |
| `+0xF0` | `int64_t` | Valor Base / Atual de INT |
| `+0xF8` | `StatContainer` | Contêiner do Atributo RES |
| `+0x100` | `int64_t` | Valor Base / Atual de RES |
| `+0x108` | `StatContainer` | Contêiner do Atributo HIT |
| `+0x110` | `int64_t` | Valor Base / Atual de HIT |
| `+0x118` | `StatContainer` | Contêiner do Atributo SPD |
| `+0x120` | `int64_t` | Valor Base / Atual de SPD |
| `+0x260` | `void*` | Objeto de Controle de Nível |
| `+0x268` | `uint16_t` | **Nível Base do Equipamento (`Base Level`)** |
| `+0x278` | `int64_t` | **Acumulador de EXP / Kill Bonus do Item** |
| `+0x328` | `uint16_t` | **Níveis Adquiridos no Item World (`IW Floor Level Bonus`)** |
| `+0x358` | `Innocent*` | Início do Array de Inocentes (`std::vector<intrusive_ptr<CInnocentStatus>>`) |
| `+0x360` | `Innocent*` | Fim do Array de Inocentes |
| `+0x368` | `Innocent*` | Capacidade do Array de Inocentes |
| `+0x378` | `uint32_t` | Contador de Andares / Ondas Concluídas no Item World |

---

## 🌀 3. Subsistema do Mundo dos Itens (`Item World`)

### 📍 Classes Principais e VTables

```text
CItemWorldData (0x140A251F0)
  ├── Gerencia a sessao ativa do Item World
  ├── +0x40: Ponteiro para o CItemStatus em evolucao
  ├── +0x74: Niveis acumulados na sessao atual
  └── +0xC8: Indice do andar / onda atual

CTask_Explore_ItemWorldClear (0x140A4E320)
  ├── Tarefa raiz disparada na conclusao de um andar do Item World
  ├── CState_Main@CTask_Explore_ItemWorldClear (0x140A4E1D0) - Logica de conclusao e distribuicao de recompensas
  ├── CState_Item@CTask_Explore_ItemWorldClear (0x140A4DFA0) - Atualizacao e exibicao de niveis do item (0x1403FDCC0)
  └── CState_Performance@CTask_Explore_ItemWorldClear (0x140A4E0E0) - Animacoes visuais e fanfarra de vitoria

CTask_Explore_WaveClear (0x140A4D400)
  └── Disparado a cada onda de monstros derrotada dentro do andar

CTask_Explore_WaveRewards (0x140A4D2F0)
  └── Concede baus, buffs de andar e drops adicionais de ondas

CTask_ItemStrengthen (0x140A44608)
  └── Assembleia / Aprimoramento de Equipamentos na base
```

---

## 🧮 4. Fórmula Nativa de Cálculo de Atributos (`0x1401CEA80`)

O nível efetivo do item é a soma de:
$$\text{Nível Total} = \text{BaseLevel (Offset } 0x268) + \text{ItemWorldLevelBonus (Offset } 0x328)$$

### Multiplicadores de Tier por Nível:
* **Nível $\ge$ 100:** Multiplicador Base de **200%** (`edi = 0xC8`)
* **Nível $\ge$ 50:** Multiplicador Base de **150%** (`edi = 0x96`)
* **Nível $\ge$ 25:** Multiplicador Base de **125%** (`edi = 0x7D`)
* **Nível < 25:** Multiplicador Base de **100%** (`edi = 0x64`)

Além do nível, a função aplica o fator de escala de Kill Bonus (`Offset +0x278`) com interpolação de ponto flutuante SSE2/AVX (`cvtsi2sd`, `mulsd`, `divsd`).

---

## 👾 5. Estrutura dos Inocentes (`InnocentStatus`)

Cada slot de especialista dentro do item possui a seguinte estrutura:

| Offset (Hex) | Tipo | Descrição |
| :--- | :--- | :--- |
| `+0x00` | `void*` | VTable de `CInnocentStatus` |
| `+0x08` | `uint32_t` | RefCount intrusivo |
| `+0x10` | `uint32_t` | ID do Tipo de Inocente (ex: Gladiator, Dietician, Statistician, etc.) |
| `+0x14` | `uint32_t` | **Flag de Subjugação (`0 = Não Subjugado / Vermelho`, `1 = Subjugado / Amarelo`)** |
| `+0x18` | `int32_t` | **Poder / Valor Numérico do Bônus** (dobrado automaticamente quando `Subdued = 1`) |
| `+0x1C` | `int32_t` | Limite Máximo de Fusão |

---

## 🛠️ 6. Por que o Multiplicador não Funcionava Originalmente?

1. **Ausência de Injeção / Hook no Mod Menu:** O arquivo `mods/registry.json` e `mods/item_world/mod.json` declaravam a interface gráfica e sliders (`levels_per_floor`, `auto_subdue`), mas o tratador no código C++ (`mod_menu_overlay.cpp`) apenas alterava a flag `mod.action_applied = true` sem aplicar nenhuma modificação na memória RAM ou no fluxo de execução de `Disgaea_Mayhem.exe`.
2. **Hardcoded Floor Clear:** O código nativo do jogo incrementa o andar de 1 em 1 por padrão no método `0x1403FE056` e `0x1401D7711`.

### 💡 Solução Aplicada:
Implementação de:
* **Hook Nativo & Sincronizador de Memória:** Ao ativar ou aplicar no Mod Menu, o módulo varre os blocos `CItemStatus` e a sessão ativa do `CItemWorldData`, injetando diretamente o multiplicador selecionado (ex: +5 Lv, +10 Lv ou turbo 9999), subjuga todos os inocentes instantaneamente para 100% Subdued (com valores duplicados) e atualiza os atributos chamando `CItemStatus::recalculateStats`.
