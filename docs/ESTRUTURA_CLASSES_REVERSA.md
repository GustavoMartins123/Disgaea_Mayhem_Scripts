# Disgaea Mayhem - Estrutura de Classes e Engenharia Reversa

Índice mestre dos subsistemas nativos do jogo para PC/Steam x64.

## Documentação modular

1. [MOTOR_NGF_ARQUITETURA.md](MOTOR_NGF_ARQUITETURA.md): engine Ngf, DirectX 12,
   ponteiros intrusivos, `CTask` e `CState`.
2. [SUBSISTEMA_ITEM_WORLD.md](SUBSISTEMA_ITEM_WORLD.md): ciclo de vida,
   progressão de nível por derrotas, Item Points, raridade e offsets confirmados.
3. [SUBSISTEMA_CHARA_WORLD.md](SUBSISTEMA_CHARA_WORLD.md): energia, lógica/UI e
   pontos de hook do Chara World.
4. [SUBSISTEMA_CHEAT_SHOP.md](SUBSISTEMA_CHEAT_SHOP.md): controles, limites e
   aplicação temporária dos valores da Cheat Shop.
5. [SUBSISTEMA_DARK_ASSEMBLY.md](SUBSISTEMA_DARK_ASSEMBLY.md): decisão das
   votações e aprovação temporária em memória.
6. [SUBSISTEMA_DLC_UNLOCKER.md](SUBSISTEMA_DLC_UNLOCKER.md): DLCs, consumíveis
   Steam e resgate reutilizável.
7. [SUBSISTEMA_IA_COMBATE.md](SUBSISTEMA_IA_COMBATE.md): decisão em tempo real,
   máquinas de estado, estratégias, orders, tasks, seleção de alvo e ações.

## VTables e classes RTTI catalogadas

| Classe C++ | VTable VA | VTable RVA | TypeDescriptor RVA | Descrição |
| --- | --- | --- | --- | --- |
| `CItemWorldData` | `0x140A251F0` | `0xA251F0` | `0xCA4120` | Sessão ativa do Item World |
| `CItemStatus` | `0x140A252C0` | `0xA252C0` | `0xCA4318` | Status de item/equipamento |
| `CInnocentStatus` | `0x140A253A0` | `0xA253A0` | `0xCA44F0` | Status de Inocente |
| `CTask_Explore_ItemWorldClear` | `0x140A4E320` | `0xA4E320` | `0xCE4610` | Conclusão da exploração |
| `CState_Main@CTask_Explore_ItemWorldClear` | `0x140A4E1D0` | `0xA4E1D0` | `0xCE44E0` | Distribuição das recompensas |
| `CState_Item@CTask_Explore_ItemWorldClear` | `0x140A4DFA0` | `0xA4DFA0` | `0xCE4260` | Comparação do item antes/depois |
| `CCharacterWorldInformation` | `0x140A57610` | `0xA57610` | `0xCE45F8` | Gerenciador do Chara World |
| `CCharacterWorldBonus` | `0x140A57620` | `0xA57620` | `0xCE43A8` | Bônus do Chara World |
| `CUIUnion_CharacterWorld_Energy` | `0x140A71728` | `0xA71728` | `0xCFA260` | UI de energia |
| `CUIUnion_CharacterWorldBattle_Energy` | `0x140A710F8` | `0xA710F8` | `0xCF94F8` | UI de energia em batalha |
| `CTask_CharacterWorldGame_Move` | `0x140A53D88` | `0xA53D88` | `0xCE1FB0` | Movimento no Chara World |
| `CTask_CharacterWorldGame_TurnStart` | `0x140A52E18` | `0xA52E18` | `0xCE0780` | Início de turno no Chara World |
| `CCheatInformation` | `0x140A25B60` | `0xA25B60` | `0xCA3098` | Lista de controles da Cheat Shop |
| `CCheatData_Gauge` | `0x140A25B70` | `0xA25B70` | `0xCB3838` | Valor e limites de um controle da Cheat Shop |
| `CListItemData_Cheat` | `0x140A67950` | `0xA67950` | `0xCEF620` | Cópia mostrada na lista da Cheat Shop |
| `CTask_Vote_Voting` | `0x140A59960` | `0xA59960` | `0xCE7EB8` | Dados e resultado da votação |
| `CState_Vote@CTask_Vote_Voting` | `0x140A59850` | `0xA59850` | `0xCE7D58` | Estado da votação |
| `CSteamInventoryService` | `0x140A85BE8` | `0xA85BE8` | — | Operações do inventário Steam |
| `CEnemyController` | `0x140A1BF00` | `0xA1BF00` | `0xCA0B70` | Controlador da unidade inimiga |
| `CEnemyStateMachine` | `0x140A1C178` | `0xA1C178` | `0xC9F218` | Execução de busca, movimento e ataque |
| `CEnemyTacticsStateMachine` | `0x140A1BF20` | `0xA1BF20` | `0xC9F0E8` | Estados da decisão tática |
| `CEnemyTacticsManagement` | `0x140A1BEE0` | `0xA1BEE0` | `0xC9FDB0` | Atualização e avaliação da tática |
| `CEnemyTactics_OrderData` | `0x140A1C0B0` | `0xA1C0B0` | `0xC9B130` | Condições e loteria de tasks |
| `CEnemyTacticsTask` | `0x140A1BE98` | `0xA1BE98` | `0xC9F248` | Task tática selecionada |
| `CEnemyTacticsStatus` | `0x140A1BEA8` | `0xA1BEA8` | `0xC9F170` | Estado tático carregado |

## Resumo da IA de combate

- A IA de exploração e combate é atualizada em tempo real; não usa ordem de
  turnos ou decisões baseadas em SP.
- A colocação do inimigo escolhe uma estratégia, que transita entre status.
- Cada status ativa orders; as orders sorteiam tabelas de tasks.
- A task define a intenção, o movimento e o modo de seleção de alvo.
- A lista indicada por `enemy.dat::taskActionListID` converte a intenção em uma
  ação que o inimigo possui.
- Inimigos e companions compartilham essa infraestrutura, com estratégias e
  estados próprios.

Consulte `SUBSISTEMA_IA_COMBATE.md` antes de alterar cadência, busca, alvo ou
pesos. Vários vetores têm capacidade fixa e não podem receber novas entradas
sem validar o layout carregado.

## Resumo do Item World

- `CItemStatus + 0xA0`: valor atual de `lv_`.
- `CItemStatus + 0x268`: valor atual de `purity_`.
- `CItemStatus + 0x278`: valor atual de `defeatCount_`.
- `CItemStatus + 0x288`: valor atual de `itemPoint_`.
- `CItemStatus + 0x328`: valor atual de `purityAdd_`.
- `CItemWorldData + 0x68`: pontos de progressão de nível acumulados por derrotas.
- `CItemWorldData + 0x88`: acumulador separado de Item Points.
- `CItemWorldData + 0x74`: contador ponderado com peso 2; não representa nível por andar.
- RVA `0x001D58A0`: geração de raridade; a chamada de recompensas do Item World ocorre em RVA `0x003C64CE`.

Consulte o documento do subsistema antes de criar hooks: a antiga associação de
`+0x268` a nível, `+0x328` a bônus de nível e `+0x74` a níveis por andar estava
incorreta.

## Resumo do Chara World

- `CCharacterWorldInformation + 0x174`: parte de um ponteiro interno; não é energia.
- `CCharacterWorldInformation + 0x178`: energia atual da lógica.
- `CCharacterWorldInformation + 0x180`: objeto `CCharacterWorldBonus`.
- `CCharacterWorldBonus + 0x18/+0x28/+0x38`: HP, ATK e DEF ganhos.
- `CCharacterWorldBonus + 0x48/+0x58`: MOVE e CRITICAL ganhos.
- `CUIUnion_CharacterWorld_Energy + 0x70`: energia exibida.
- `CUIUnion_CharacterWorld_Energy + 0x78`: barra de progresso.
- `CUIUnion_CharacterWorld_Energy + 0x7C`: valor alvo da transição.
- `CUIUnion_CharacterWorld_Energy + 0x80`: texto formatado.
