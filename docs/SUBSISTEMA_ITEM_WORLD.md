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

## Multiplicadores do plugin

O plugin oferece dois controles independentes. Cada controle possui uma chave
para ativar o cálculo e um valor para definir a multiplicação:

| Opção | Onde atua |
| --- | --- |
| `level_exp_enabled` | ativa ou desativa a multiplicação do progresso de nível |
| `level_exp_multiplier` | pontos de nível aplicados pela rotina RVA `0x001D77E0` |
| `item_points_enabled` | ativa ou desativa a multiplicação de Item Points |
| `item_point_multiplier` | entradas recebidas pela rotina RVA `0x001D7BD0` |

O padrão é `1.0`, que mantém o cálculo original. O plugin não altera os contadores de
chefes nem o resultado já salvo. Se a versão do executável ou os dados da sessão
não corresponderem ao esperado, o plugin não aplica a mudança.

## Outros dados encontrados

O executável registra bancos separados para:

- `itemWorldRewards.dat` e `itemWorldRewardsTable.dat`;
- `itemWorldWave.dat`, `itemWorldWaveTable.dat` e
  `itemWorldWavePlacement.dat`;
- `innocent.dat` e `innocentAffinity.dat`.

Também existem os campos `roomID_`, `pItemWorldRoomData_`, `defeatInnocent_` e a
classe `CInnocentStatus`. Eles confirmam que salas e Innocents usam fluxos
separados dos dois ganhos já implementados.

Ainda falta localizar com segurança:

- a chance de drop específica do Item World;
- a escolha entre sala comum e sala misteriosa;
- o ganho e a subjugação de Innocents.

Esses controles não devem ser adicionados ao Mod Menu antes de o cálculo
correspondente ser confirmado no executável.
