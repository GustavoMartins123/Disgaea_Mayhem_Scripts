# Subsistema: Item World

Este documento registra apenas fatos confirmados por análise estática de
`Disgaea_Mayhem.exe`. Os endereços abaixo pertencem ao executável com SHA-256:

```text
13988368F66ADE40205C1D0D18157B6AE2D7736D67AC0C8734FE1DD4E62D5B41
```

Base preferencial do PE: `0x140000000`. Em runtime, todos os endereços devem ser
resolvidos como `GetModuleHandleA(NULL) + RVA`; o mod não deve depender da base
preferencial por causa de ASLR.

## Diagnóstico do mod existente

O mod anterior nunca chegou a operar por dois motivos independentes:

1. `g_cached_item_world` nunca era preenchido. A versão intermediária rejeitava
   o plugin explicitamente para não anunciar um mod residente e inerte.
2. O modelo de dados original está incorreto. O campo
   `CItemWorldData + 0x74` não significa "níveis por andar" e
   `CItemWorldData + 0x40` não é um ponteiro para `CItemStatus`.

Escrever continuamente o valor do slider em `CItemWorldData + 0x74` corromperia
um contador nativo de chefes. O jogo usa esse contador com peso 2 ao aplicar as
recompensas da sessão.

## Classes e ciclo de vida

### `CItemWorldData`

- VTable: RVA `0xA251F0`, VA preferencial `0x140A251F0`.
- Tamanho alocado: `0xD0` bytes.
- Construtor: RVA `0x1D7140`, VA preferencial `0x1401D7140`.
- Destrutor: RVA `0x1D7390`, VA preferencial `0x1401D7390`.
- Setup da sessão: RVA `0x1D7470`, VA preferencial `0x1401D7470`.
- Aplicação das recompensas: RVA `0x1D77E0`, VA preferencial
  `0x1401D77E0`.

O construtor é chamado durante a criação da exploração em `0x14034DD82`. Depois
do setup, a instância ativa é armazenada no campo `+0x31F0` do objeto global de
informações do jogo, em `0x14034DE0A`.

Campos confirmados:

| Offset | Nome/uso confirmado |
| --- | --- |
| `+0x18` | Campo serializado como `pItemStatus_`; o `CItemStatus*` efetivo está no objeto apontado, em `+0x28` |
| `+0x38` | `runDataList_` |
| `+0x40` | Quantidade de elementos da lista; não é o item |
| `+0x48` | `resultData_` |
| `+0x50/+0x58` | Gauge e valor calculado de resultado |
| `+0x60/+0x68` | Gauge e pontos de progressão de nível acumulados por derrotas |
| `+0x70` | Contador do chefe identificado pelo ID `1000100` |
| `+0x74` | Contador do chefe identificado pelo ID `1000200` |
| `+0x78` | Contador do chefe identificado pelo ID `1000300` |
| `+0x80/+0x88` | Gauge e acumulador de `itemPoint_` |
| `+0x90` | `waveMax_` |
| `+0x94` | `roomID_` |
| `+0xB0` | `pItemWorldRoomData_` |
| `+0xB8` | `dopingBuffs_` |
| `+0xBC` | `pointCorrection_` |
| `+0xC0` | `clearWaveNum_` |
| `+0xC4` | `isMakePlacement_` |
| `+0xC8` | Índice transitório inicializado com 1; o nome semântico ainda não foi confirmado |

O caminho correto até o item é:

```text
CItemWorldData + 0x18 -> contexto da sessão
contexto + 0x28       -> CItemStatus
```

### `CItemStatus`

- VTable: RVA `0xA252C0`, VA preferencial `0x140A252C0`.
- Construtor: RVA `0x1D1A60`, VA preferencial `0x1401D1A60`.
- Destrutor: RVA `0x1D1B80`, VA preferencial `0x1401D1B80`.
- Recálculo de atributos usado ao aplicar o resultado: RVA `0x1CEA80`, VA
  preferencial `0x1401CEA80`.

Campos confirmados pelos nomes do serializador:

| Offset | Campo serializado | Valor atual |
| --- | --- | --- |
| `+0x98` | `lv_` | `+0xA0` (`int32_t`) |
| `+0x260` | `purity_` | `+0x268` (`uint16_t`) |
| `+0x270` | `defeatCount_` | `+0x278` (`int64_t`) |
| `+0x280` | `itemPoint_` | `+0x288` (`int64_t`) |
| `+0x320` | `purityAdd_` | `+0x328` (`uint16_t`) |

A função em RVA `0x1CFB00` altera `purity_`, e não `lv_`. Portanto, o nome
anterior `setLevel` para essa função estava errado.

## Fluxo nativo de progressão

O manipulador de derrota em torno de `0x1403C6180` adiciona diretamente pontos
ao gauge `CItemWorldData + 0x68`. Os valores observados são:

| Caminho de recompensa | Pontos em `+0x68` | Contador ponderado | Entrada de Item Points |
| --- | ---: | ---: | ---: |
| Derrota comum | `50` | `0` | `1` |
| Derrota especial (condição interna `200`) | `1000` | `0` | `4` |
| ID `1000100` | `3000` | `+1` em `+0x70` | `10` |
| ID `1000200` | `5000` | `+1` em `+0x74` | `30` |
| ID `1000300` | `10000` | `+1` em `+0x78` | `100` |

Os nomes apresentados ao jogador para esses três IDs ainda precisam de uma
confirmação dinâmica/localizada; a ponderação e os IDs estão confirmados no
código nativo.

Na aplicação das recompensas, `0x1401D77E0` executa três cálculos separados:

```text
level_target = clamp(floor(level_progress_points / 100))
weighted_defeats = count_1000100 + 2 * count_1000200 + 3 * count_1000300
item_points = floor(item_point_accumulator * (100 + pointCorrection_) / 100)
```

- `level_target` é comparado com `CItemStatus.lv_` (`+0xA0`) e só pode elevá-lo.
- `weighted_defeats` é acrescentado a `purity_` (`+0x268`) e
  `defeatCount_` (`+0x278`).
- `item_points` é acrescentado a `itemPoint_` (`+0x288`).
- Ao final, o jogo chama o recálculo nativo em RVA `0x1CEA80`.

A função em RVA `0x1D7BD0` não é o ganho de nível. Ela recebe os valores
`1/4/10/30/100`, aplica fatores do andar/item e acumula o resultado em
`CItemWorldData + 0x88`, que depois alimenta `itemPoint_`.

## Implementação do mod residente

A opção canônica agora é `level_exp_multiplier`, com `1.0` significando o
comportamento original. A opção `levels_per_floor` foi removida porque o jogo não
concede um incremento fixo por andar.

O plugin instala MinHook na função de aplicação em RVA `0x1D77E0`:

1. valida `TimeDateStamp`, `SizeOfImage`, arquitetura PE e o prólogo completo do
   ponto de hook;
2. recebe `CItemWorldData*` diretamente da chamada nativa e confirma sua vtable;
3. multiplica temporariamente `+0x68` antes da divisão nativa por 100;
4. deixa o jogo executar clamps, atualização do item e recálculo;
5. restaura o valor transitório depois da chamada;
6. mantém `+0x70/+0x74/+0x78` intactos.

Se houver uma opção separada `item_point_multiplier`, ela deve atuar no fluxo de
RVA `0x1D7BD0`. Misturar os dois multiplicadores produziria um efeito diferente
do descrito na interface.

O hook falha fechado se assinatura, vtable, fingerprint ou ponteiros não
corresponderem à versão documentada. Não há polling nem thread residente.

## Pontos ainda não validados dinamicamente

- nomes localizados dos IDs `1000100`, `1000200` e `1000300`;
- limites mínimo/máximo concretos dos gauges em cada tipo de item;
- comportamento visual da barra quando um multiplicador fracionário é usado;
- layout do vetor de Inocentes e o ponto de subjugação automática.

Esses itens não devem ser tratados como implementados ou estáveis sem uma sessão
instrumentada no jogo.
