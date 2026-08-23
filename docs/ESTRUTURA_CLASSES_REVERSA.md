# Disgaea Mayhem - Estrutura de Classes e Engenharia Reversa

Índice mestre dos subsistemas nativos do jogo para PC/Steam x64.

## Documentação modular

1. [MOTOR_NGF_ARQUITETURA.md](MOTOR_NGF_ARQUITETURA.md): engine Ngf, DirectX 12,
   ponteiros intrusivos, `CTask` e `CState`.
2. [SUBSISTEMA_ITEM_WORLD.md](SUBSISTEMA_ITEM_WORLD.md): ciclo de vida,
   progressão de nível por derrotas, Item Points e offsets confirmados.
3. [SUBSISTEMA_CHARA_WORLD.md](SUBSISTEMA_CHARA_WORLD.md): energia, lógica/UI e
   pontos de hook do Chara World.
4. [SUBSISTEMA_CHEAT_SHOP.md](SUBSISTEMA_CHEAT_SHOP.md): schema do banco,
   offsets dos limites e patch transacional do Cheat Shop.

## VTables e classes RTTI catalogadas

| Classe C++ | VTable VA | VTable RVA | TypeDescriptor RVA | Descrição |
| --- | --- | --- | --- | --- |
| `CItemWorldData` | `0x140A251F0` | `0xA251F0` | `0xCA4120` | Sessão ativa do Item World |
| `CItemStatus` | `0x140A252C0` | `0xA252C0` | `0xCA4318` | Status de item/equipamento |
| `CInnocentStatus` | `0x140A253A0` | `0xA253A0` | `0xCA44F0` | Status de Inocente |
| `CTask_Explore_ItemWorldClear` | `0x140A4E320` | `0xA4E320` | `0xCE4610` | Conclusão da exploração |
| `CState_Main@CTask_Explore_ItemWorldClear` | `0x140A4E1D0` | `0xA4E1D0` | `0xCE44E0` | Distribuição das recompensas |
| `CState_Item@CTask_Explore_ItemWorldClear` | `0x140A4DFA0` | `0xA4DFA0` | `0xCE4260` | Comparação do item antes/depois |
| `CCharacterWorldInformation` | `0x140A57610` | `0xA57610` | `0xCED6B0` | Gerenciador do Chara World |
| `CCharacterWorldBonus` | `0x140A57620` | `0xA57620` | `0xCED720` | Bônus do Chara World |
| `CUIUnion_CharacterWorld_Energy` | `0x140A71728` | `0xA71728` | `0xCFA260` | UI de energia |
| `CUIUnion_CharacterWorldBattle_Energy` | `0x140A710F8` | `0xA710F8` | `0xCF94F8` | UI de energia em batalha |
| `CTask_CharacterWorldGame_Move` | `0x140A53D88` | `0xA53D88` | `0xCE1FB0` | Movimento no Chara World |
| `CTask_CharacterWorldGame_TurnStart` | `0x140A52E18` | `0xA52E18` | `0xCE0780` | Início de turno no Chara World |

## Resumo do Item World

- `CItemStatus + 0xA0`: valor atual de `lv_`.
- `CItemStatus + 0x268`: valor atual de `purity_`.
- `CItemStatus + 0x278`: valor atual de `defeatCount_`.
- `CItemStatus + 0x288`: valor atual de `itemPoint_`.
- `CItemStatus + 0x328`: valor atual de `purityAdd_`.
- `CItemWorldData + 0x68`: pontos de progressão de nível acumulados por derrotas.
- `CItemWorldData + 0x88`: acumulador separado de Item Points.
- `CItemWorldData + 0x74`: contador ponderado com peso 2; não representa nível por andar.

Consulte o documento do subsistema antes de criar hooks: a antiga associação de
`+0x268` a nível, `+0x328` a bônus de nível e `+0x74` a níveis por andar estava
incorreta.

## Resumo do Chara World

- `CCharacterWorldInformation + 0x174`: parte de um ponteiro interno; não é energia.
- `CCharacterWorldInformation + 0x178`: energia atual da lógica.
- `CUIUnion_CharacterWorld_Energy + 0x70`: energia exibida.
- `CUIUnion_CharacterWorld_Energy + 0x78`: barra de progresso.
- `CUIUnion_CharacterWorld_Energy + 0x7C`: valor alvo da transição.
- `CUIUnion_CharacterWorld_Energy + 0x80`: texto formatado.
