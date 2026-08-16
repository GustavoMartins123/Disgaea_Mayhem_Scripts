# 🏛️ Disgaea Mayhem - Estrutura de Classes & Engenharia Reversa

Este documento consolida a engenharia reversa completa do motor nativo (**Ngf / Nippon Ichi Game Framework**) do jogo **Disgaea Mayhem (PC / Steam / x64)**, detalhando a hierarquia de classes C++, VTables, offsets de memória e ciclo de vida dos subsistemas do jogo.

---

## 📚 Índice Modular de Documentação

Para aprofundamento específico em cada subsistema, consulte os documentos dedicados na pasta `docs/`:

1. [**MOTOR_NGF_ARQUITETURA.md**](file:///C:/Disgaea_Mayhem_Scripts/docs/MOTOR_NGF_ARQUITETURA.md): Visão geral da engine Ngf, DirectX 12, Nmpl intrusive pointers, CTask e CState.
2. [**SUBSISTEMA_ITEM_WORLD.md**](file:///C:/Disgaea_Mayhem_Scripts/docs/SUBSISTEMA_ITEM_WORLD.md): Estrutura de `CItemStatus`, tabela completa de offsets, fórmulas de status, Inocentes e hook do Item World.
3. [**SUBSISTEMA_CHARA_WORLD.md**](file:///C:/Disgaea_Mayhem_Scripts/docs/SUBSISTEMA_CHARA_WORLD.md): Estrutura de `CCharacterWorldInformation`, widgets de energia, offsets de lógica/UI e hook do Chara World.

---

## 📌 1. Tabela Geral de VTables e Classes RTTI Catalogadas

| Classe C++ | VTable VA | VTable RVA | TypeDescriptor RVA | Descrição |
| :--- | :--- | :--- | :--- | :--- |
| `CItemWorldData` | `0x140A251F0` | `0xA251F0` | `0xCA4120` | Sessão ativa de exploração do Mundo dos Itens |
| `CItemStatus` | `0x140A252C0` | `0xA252C0` | `0xCA4318` | Estrutura de status de um item/equipamento |
| `CInnocentStatus` | `0x140A253A0` | `0xA253A0` | `0xCA44F0` | Slot de especialista/Inocente dentro de um item |
| `CTask_Explore_ItemWorldClear` | `0x140A4E320` | `0xA4E320` | `0xCE4610` | Tarefa de conclusão de andar do Item World |
| `CState_Main@CTask_Explore_ItemWorldClear` | `0x140A4E1D0` | `0xA4E1D0` | `0xCE44E0` | Distribuição de recompensas de andar do Item World |
| `CState_Item@CTask_Explore_ItemWorldClear` | `0x140A4DFA0` | `0xA4DFA0` | `0xCE4260` | Atualização e aplicação de níveis ao item |
| `CCharacterWorldInformation` | `0x140A57610` | `0xA57610` | `0xCED6B0` | Gerenciador mestre do Mundo dos Personagens (Chara World) |
| `CCharacterWorldBonus` | `0x140A57620` | `0xA57620` | `0xCED720` | Tabela de bônus acumulados no Chara World |
| `CUIUnion_CharacterWorld_Energy` | `0x140A71728` | `0xA71728` | `0xCFA260` | Widget de UI de controle e exibição de Energia |
| `CUIUnion_CharacterWorldBattle_Energy` | `0x140A710F8` | `0xA710F8` | `0xCF94F8` | Widget de UI de energia durante batalhas de clones |
| `CTask_CharacterWorldGame_Move` | `0x140A53D88` | `0xA53D88` | `0xCE1FB0` | Tarefa de movimentação e passos no Chara World |
| `CTask_CharacterWorldGame_TurnStart` | `0x140A52E18` | `0xA52E18` | `0xCE0780` | Tarefa de início de turno no Chara World |

---

## 🗡️ 2. Resumo Rápido: Subsistema de Itens (`CItemStatus`)

* **Nível Efetivo:** $\text{Nível Total} = \text{BaseLevel (Offset } 0x268) + \text{ItemWorldLevelBonus (Offset } 0x328)$
* **Offsets Principais:**
  * `+0x00`: VTable (`0x140A252C0`)
  * `+0x268`: Nível Base (`uint16_t`)
  * `+0x278`: Kill Bonus / EXP (`int64_t`)
  * `+0x328`: Bônus de Nível do Item World (`uint16_t`)
  * `+0x358` a `+0x360`: Vetor de Inocentes (`std::vector<intrusive_ptr<CInnocentStatus>>`)

---

## 🎲 3. Resumo Rápido: Subsistema de Chara World

* **`CCharacterWorldInformation` (VTable `0x140A57610`):**
  * `+0x174`: Energia Máxima (100)
  * `+0x178`: **Energia Atual da Lógica de Jogo (Master Energy)**
* **`CUIUnion_CharacterWorld_Energy` (VTable `0x140A71728`):**
  * `+0x70`: Energia de Exibição
  * `+0x78`: Barra de Progresso
  * `+0x7C`: Valor Alvo de Transição
  * `+0x80`: Texto Formatado de Energia
