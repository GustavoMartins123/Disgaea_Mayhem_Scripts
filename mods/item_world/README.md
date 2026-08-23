# Item World - Multiplicadores

Este mod controla três resultados do Item World:

- progresso usado para aumentar o nível do item;
- Item Points recebidos nas batalhas;
- raridade dos equipamentos obtidos.

Cada ganho possui um controle próprio para ativar ou desativar o multiplicador.
O valor `1.0` mantém o cálculo normal do jogo. Valores maiores aceleram o ganho e
podem produzir saltos grandes ao concluir uma exploração.

Opções:

- `level_exp_enabled` e `level_exp_multiplier`: progresso de nível entre `1x` e `20x`;
- `item_points_enabled` e `item_point_multiplier`: Item Points entre `1x` e `20x`;
- `rarity_enabled` e `minimum_rarity`: raridade mínima entre `0` e `100`.

Todas as alterações são feitas na memória. Desativar o mod ou um de seus
controles faz as próximas recompensas usarem o cálculo normal.

O controle de raridade define um valor mínimo entre `0` e `100`. Resultados que
já seriam melhores são mantidos. Ele atua somente nos equipamentos criados como
recompensa do Item World e não muda a quantidade de itens.

O mod não altera chance de equipamentos, escolha de salas ou Innocents. Essas
opções só serão incluídas quando houver uma rotina confirmada para cada uma.
O multiplicador de Item Points também não altera a quantidade de equipamentos.

As opções ficam em `config.json` e também aparecem no Mod Menu.

Se a versão do jogo ou as rotinas verificadas não corresponderem, o plugin não é
carregado e nenhuma alteração é aplicada.

Consulte `docs/SUBSISTEMA_ITEM_WORLD.md` para os dados confirmados do jogo.
