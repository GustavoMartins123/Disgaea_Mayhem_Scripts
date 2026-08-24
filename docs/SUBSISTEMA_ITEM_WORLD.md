# Subsistema: Item World

Este documento registra os dados confirmados do Item World na versão atual de
`Disgaea_Mayhem.exe`.

## Dados da sessão

`CItemWorldData` representa a exploração em andamento. O objeto tem `0xD0` bytes
e usa a identificação interna em RVA `0xA251F0`.

| Posição | Uso confirmado |
| --- | --- |
| `+0x18` | referência do item da exploração |
| `+0x38` | lista de ondas da sessão |
| `+0x48` | resultados acumulados |
| `+0x68` | pontos usados para aumentar o nível do item |
| `+0x70` | contador do inimigo `1000100` |
| `+0x74` | contador do inimigo `1000200` |
| `+0x78` | contador do inimigo `1000300` |
| `+0x88` | Item Points acumulados |
| `+0x90` | quantidade máxima de ondas |
| `+0x94` | lista de níveis dos bônus da exploração |
| `+0xB0` | dados da sala atual |
| `+0xBC` | correção aplicada aos Item Points |
| `+0xC0` | ondas concluídas |

`+0x74` não representa níveis por andar. A versão antiga do mod escrevia nesse
contador e por isso não alterava o nível do item.

## Dados do item

O item da sessão é um `CItemStatus`. Os valores usados na conclusão são:

| Posição | Valor |
| --- | --- |
| `+0xA0` | nível atual |
| `+0x268` | pureza |
| `+0x278` | total de derrotas |
| `+0x288` | Item Points |
| `+0x328` | acréscimo de pureza |

## Ganhos confirmados

O jogo acumula os ganhos durante as derrotas e aplica o resultado ao encerrar a
exploração.

| Evento | Pontos de nível | Entrada de Item Points |
| --- | ---: | ---: |
| derrota comum | `50` | `1` |
| derrota especial | `1000` | `4` |
| inimigo `1000100` | `3000` | `10` |
| inimigo `1000200` | `5000` | `30` |
| inimigo `1000300` | `10000` | `100` |

A conclusão de uma onda também chama o cálculo de Item Points com valor `2`.

Na conclusão, o jogo faz três operações independentes:

- converte os pontos de `+0x68` em nível;
- usa os três contadores para pureza e total de derrotas;
- converte `+0x88` em Item Points, incluindo a correção de `+0xBC`.

Depois disso, o próprio jogo atualiza os atributos do item.

## Raridade dos equipamentos

A rotina RVA `0x001D58A0` gera a raridade entre `0` e `100`. O jogo separa esse
valor nas faixas `0–24`, `25–49`, `50–99` e `100`.

O Item World usa três dos seis call sites. O plugin separa o escopo em duas
options: `rarity_enabled` cobre só o Item World, `rarity_global` estende a
qualquer origem.

### Call sites de `0x001D58A0`

Varredura de `call`/`jmp rel32` no executável (build `0x6A6AB373`,
`SizeOfImage 0x00E01000`), com o contexto obtido pela cadeia de chamada e pelas
vtables referenciadas:

| Call site | Retorno | Contexto | Coberto por |
| --- | --- | --- | --- |
| `0x003C64CE` | `0x003C64D3` | Item World, recompensa | `rarity_enabled` |
| `0x003F71C6` | `0x003F71CB` | Item World, `CState_Performance@CTask_Explore_ItemWorldClear` | `rarity_enabled` |
| `0x003F75EE` | `0x003F75F3` | Item World, mesmo ancestral `0x003F61D0` | `rarity_enabled` |
| `0x001D5C01` | `0x001D5C06` | Tesouro: ancestral `0x001C9420` referencia `CTreasureData` e `CTreasureInformation` | `rarity_global` |
| `0x00465739` | `0x0046573E` | Geração de item; a mesma função chama o caminho de tesouro | `rarity_global` |
| `0x0042649F` | `0x004264A4` | Não identificado | `rarity_global` |

As seis funções que contêm os call sites referenciam `CItemStatusMakeData`.

Limite do levantamento: `0x003C5F90`, `0x00424B40` e `0x00464A50` não têm caller
por `call rel32` nem referência por `lea`. O início de função obtido por
varredura de padding `CC` pode estar incorreto nesses três.

## Multiplicadores do plugin

O plugin oferece três grupos independentes: progresso de nível, Item Points e
raridade mínima. Cada grupo possui uma chave de ativação e um valor:

| Opção | Onde atua |
| --- | --- |
| `level_exp_enabled` | ativa ou desativa a multiplicação do progresso de nível |
| `level_exp_multiplier` | pontos de nível aplicados pela rotina RVA `0x001D77E0` |
| `item_points_enabled` | ativa ou desativa a multiplicação de Item Points |
| `item_point_multiplier` | entradas recebidas pela rotina RVA `0x001D7BD0` |
| `rarity_enabled` | raridade mínima nos três caminhos do Item World |
| `rarity_global` | estende a raridade mínima a qualquer origem do jogo |
| `minimum_rarity` | valor mínimo entre `0` e `100` para equipamentos obtidos |

O padrão é `1.0`, que mantém o cálculo original. O plugin não altera os contadores de
chefes nem o resultado já salvo. Se a versão do executável ou os dados da sessão
não corresponderem ao esperado, o plugin não aplica a mudança.

Os Item Points e a quantidade de equipamentos usam caminhos separados. O
multiplicador de Item Points não altera a quantidade nem a raridade dos drops.

## Outros dados encontrados

O executável registra bancos separados para:

- `itemWorldRewards.dat` e `itemWorldRewardsTable.dat`;
- `itemWorldWave.dat`, `itemWorldWaveTable.dat` e
  `itemWorldWavePlacement.dat`;
- `innocent.dat` e `innocentAffinity.dat`.

Também existem os campos `roomID_`, `pItemWorldRoomData_`, `defeatInnocent_` e a
classe `CInnocentStatus`. Eles confirmam que salas e Innocents usam fluxos
separados dos recursos já implementados.

Ainda falta localizar com segurança:

- a chance de drop específica do Item World;
- a escolha entre sala comum e sala misteriosa;
- o ganho e a subjugação de Innocents.

Esses controles não devem ser adicionados ao Mod Menu antes de o cálculo
correspondente ser confirmado no executável.
