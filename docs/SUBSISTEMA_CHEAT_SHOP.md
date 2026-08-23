# Subsistema: Cheat Shop

## Valores principais

A Cheat Shop mantém cinco controles de porcentagem:

| ID | Controle |
| ---: | --- |
| `10101` | EXP |
| `10102` | Mana |
| `10103` | HL |
| `10104` | Weapon Mastery |
| `10105` | Item Drops |

Cada controle usa um objeto `CCheatData_Gauge`. O valor atual fica em `+0x28`,
o limite inferior em `+0x30` e o limite superior em `+0x34`.

`CCheatInformation` guarda esses objetos no mapa em `+0x68`. Na versão
verificada, o mapa também contém os controles `20102` e `20103`, que não são
alterados pelo mod.

## Motivo da falha anterior

O patch anterior alterava cinco campos de `data/database/cheatSetting.dat` de
`500` para `5000`. O jogo lia esses campos, mas depois carregava do save um
limite separado para cada controle.

O `500` desse arquivo é um valor-base do banco. Ele não representa o limite
atual da partida e não substitui os valores do save.

Na sessão analisada, os registros do banco estavam em `5000`, porém os cinco
controles em memória continuavam limitados a `1100`. Os valores mostrados na
tela eram `720`, `100`, `80`, `100` e `100`. Esses valores e o limite carregado
do save são o estado que o plugin guarda e restaura. Por isso a ação antiga
terminava com sucesso sem produzir o resultado esperado.

## Funcionamento atual

O mod é um plugin ativável. Depois que a Cheat Shop e o save são carregados, ele
confirma os IDs dos cinco controles e mantém o valor atual e o limite superior
em `5000`.

O controle `Item Drops` não representa raridade. Ele atua no fluxo geral de
recompensas e pode aumentar bastante a quantidade de itens recebidos. A raridade
mínima do Item World usa uma rotina separada no plugin `item_world`.

Ao desativar, os valores anteriores são restaurados. Durante uma gravação, o
plugin entrega os valores anteriores ao jogo e reaplica `5000` somente depois.
O banco `cheatSetting.dat` e o save permanecem sem a alteração do mod.

A tela mantém uma segunda cópia dos valores em objetos `CListItemData_Cheat`.
O plugin acompanha a criação desses itens e atualiza essa cópia junto com os
controles principais. Assim, ativar ou desativar o mod também muda uma tela que
já esteja aberta.

Se a versão do jogo, a lista de controles ou os registros não corresponderem ao
que foi verificado, o plugin não altera nenhum valor.

## Pontos confirmados

| Uso | RVA |
| --- | ---: |
| criação da lista da Cheat Shop | `0x001B3920` |
| leitura e gravação de cada controle | `0x001B0500` |
| criação dos itens mostrados na lista | `0x00543610` |
| VTable de `CCheatInformation` | `0x00A25B60` |
| VTable de `CCheatData_Gauge` | `0x00A25B70` |
| VTable de `CListItemData_Cheat` | `0x00A67950` |
