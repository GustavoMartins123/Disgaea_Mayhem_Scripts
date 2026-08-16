# 🗡️ Subsistema: Itens & Mundo dos Itens (`Item World`)

Este documento consolida a engenharia reversa completa do subsistema de **Itens, Equipamentos e Mundo dos Itens** em `Disgaea_Mayhem.exe`.

---

## 🗡️ 1. Estrutura do Objeto de Item (`CItemStatus`)

### 📍 VTable & Funções-Chave
* **RTTI Symbol:** `.?AVCItemStatus@@` (TypeDescriptor RVA: `0xCA4318`, VA: `0x140CA4318`)
* **VTable RVA:** `0xA252C0` (VA: `0x140A252C0`)
* **Função de Nível (`setLevel`):** `0x1401CFB00`
* **Função de Recálculo de Atributos (`recalculateStats`):** `0x1401CEA80`

### 📐 Layout de Memória Completo do Objeto Item (`CItemStatus`)

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
| `+0x280` | `int32_t` | **Grau de Raridade / Rarity** (Common, Rare, Legendary, Epic) |
| `+0x328` | `uint16_t` | **Níveis Adquiridos no Item World (`IW Floor Level Bonus`)** |
| `+0x358` | `Innocent*` | Início do Array de Inocentes (`std::vector<intrusive_ptr<CInnocentStatus>>`) |
| `+0x360` | `Innocent*` | Fim do Array de Inocentes |
| `+0x368` | `Innocent*` | Capacidade do Array de Inocentes |
| `+0x378` | `uint32_t` | Contador de Andares / Ondas Concluídas no Item World |

---

## 🌀 2. Subsistema do Mundo dos Itens (`Item World`)

### 📍 Classes Principais e VTables

```text
CItemWorldData (0x140A251F0)
  ├── Gerencia a sessao ativa do Item World
  ├── +0x40: Ponteiro para o CItemStatus em evolucao
  ├── +0x74: Niveis acumulados na sessao atual (incremento por andar)
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

## 🧮 3. Fórmula Nativa de Cálculo de Atributos (`0x1401CEA80`)

O nível efetivo do item é a soma de:
$$\text{Nível Total} = \text{BaseLevel (Offset } 0x268) + \text{ItemWorldLevelBonus (Offset } 0x328)$$

### Multiplicadores de Tier por Nível:
* **Nível $\ge$ 100:** Multiplicador Base de **200%** (`edi = 0xC8`)
* **Nível $\ge$ 50:** Multiplicador Base de **150%** (`edi = 0x96`)
* **Nível $\ge$ 25:** Multiplicador Base de **125%** (`edi = 0x7D`)
* **Nível < 25:** Multiplicador Base de **100%** (`edi = 0x64`)

Além do nível, a função aplica o fator de escala de Kill Bonus (`Offset +0x278`) com interpolação de ponto flutuante SSE2/AVX (`cvtsi2sd`, `mulsd`, `divsd`).

---

## 👾 4. Estrutura dos Inocentes (`InnocentStatus`)

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

## 🛠️ 5. Resolução do Hook Residente no Item World

1. **Problema Original:** O mod menu inicial apenas declarava sliders (`levels_per_floor`, `auto_subdue`) na UI sem injetar hooks na memória de `Disgaea_Mayhem.exe`.
2. **Implementação Aplicada ([`mods/item_world/item_world.dll`](file:///C:/Disgaea_Mayhem_Scripts/mods/item_world/item_world.dll)):**
   * Hook residente em C++ que intercepta o objeto `CItemWorldData` (`0x140A251F0`) e `CItemStatus` (`0x140A252C0`).
   * Mantém `[CItemWorldData + 0x74] = levels_per_floor` (+5 Lv padrão).
   * Varre o vetor de Inocentes `[item + 0x358]` e marca `subdued = 1` duplicando o poder do inocente automaticamente ao concluir andares.
