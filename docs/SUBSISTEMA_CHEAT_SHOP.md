# Subsistema: Cheat Shop

## Registro nativo

O executável contém o caminho `data/database/cheatSetting.dat` na VA preferencial
`0x1409F68E0`. A referência em `0x14002FB14` encaminha esse banco para a rotina
de registro em `0x14079F570`.

Também estão presentes os RTTIs/templates de `CheatSettingData`, incluindo
`Ngf::CDatabase<CheatSettingData>` e o vetor de
`CheatSettingData::ItemInfo`.

Para a build analisada, o banco possui `68279` bytes. Cada um dos cinco primeiros
registros de porcentagem termina com a sequência estrutural:

```text
enabled/default marker = 1
base percentage        = 100
step/cost field        = 90
maximum percentage     = 500
reserved[7]            = 0
next numeric ID
```

## Registros e offsets confirmados

| Registro | String no arquivo | Offset do máximo | Máximo original | Máximo do mod |
| --- | --- | ---: | ---: | ---: |
| EXP | `CHEAT_SETTING_EXP` | `0x0B1B` | `500` | `5000` |
| Mana | `CHEAT_SETTING_MANA` | `0x17E2` | `500` | `5000` |
| HL | `CHEAT_SETTING_HL` | `0x24C2` | `500` | `5000` |
| Weapon Mastery | `CHEAT_SETTING_WM` | `0x30C8` | `500` | `5000` |
| Item Drops | `CHEAT_SETTING_ITEM_DROPS` | `0x3C00` | `500` | `5000` |

O registro seguinte, `CHEAT_SETTING_ENEMY_LV`, é usado como limite estrutural do
último registro modificado e não tem seu próprio máximo alterado.

## Diagnóstico da versão antiga

O `cheatSetting.dat` antigo da pasta do mod tinha somente quatro bytes diferentes
do banco original:

- `0x30C8`: Weapon Mastery, `500 -> 5000`;
- `0x3C00`: Item Drops, `500 -> 5000`.

EXP, Mana e HL nunca foram modificados, embora o manifesto prometesse os cinco
efeitos. A cópia cega também não verificava versão, schema ou se o banco havia
sido carregado antes da action.

## Patcher 2.0

`APLICAR_MOD_CHEAT_SHOP.exe` não contém uma cópia alternativa do banco. Ele:

1. descobre a raiz do jogo relativamente ao próprio executável;
2. exige o tamanho exato da build documentada;
3. localiza uma única ocorrência de cada ID e valida a ordem;
4. valida campos adjacentes e o ID numérico do próximo registro;
5. aceita somente o máximo original `500` ou o estado canônico `5000`;
6. altera os cinco máximos em RAM;
7. grava `cheatSetting.dat.tmp`, força o flush e substitui o banco atomicamente;
8. relê o arquivo e exige igualdade byte a byte.

Qualquer divergência encerra a action com erro antes de substituir o banco. Não
há busca por outro arquivo, cópia de backup ou compatibilidade silenciosa com
outro schema.

## Confirmação no Mod Menu

Depois da execução, o loader mostra quais cinco limites foram confirmados em
`5000%`. A indicação `[OK]` e essa mensagem vêm somente depois da releitura do
arquivo e da comparação final.
