# Item World - Multiplicadores

Este mod altera dois ganhos do Item World:

- `level_exp_multiplier`: pontos usados para aumentar o nível do item;
- `item_point_multiplier`: Item Points recebidos por derrotas e conclusão de ondas.

O valor `1.0` mantém o cálculo original. Cada opção pode ser configurada entre
`1.0` e `20.0`.

O jogo continua aplicando seus limites, bônus de chefes e atualização dos
atributos do item. O mod não altera a chance de drops, a escolha de salas ou os
Innocents, pois esses caminhos ainda não foram confirmados no executável.

O plugin é carregado pelo Mod Loader quando `enabled.txt` contém `1`. As opções
ficam em `config.json` e também podem ser alteradas pelo Mod Menu.

Consulte `docs/SUBSISTEMA_ITEM_WORLD.md` para os dados confirmados do jogo.
