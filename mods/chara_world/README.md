# Chara World - Energia e bônus

Este mod mantém a energia do Chara World no valor configurado ao entrar e
durante o tabuleiro.

O multiplicador de atributos atua sobre os ganhos de `HP`, `ATK`, `DEF`, `MOVE`
e `CRITICAL` recebidos nos tiles. O valor ganho é multiplicado uma vez antes de
ser somado ao quadro de bônus.

Opções:

- `locked_energy`: valor entre `10` e `100`;
- `freeze_energy`: ativa ou desativa a trava;
- `tile_status_multiplier`: multiplicador entre `1x` e `20x`.

As opções ficam em `config.json` e também podem ser alteradas pelo Mod Menu.
Se a versão do jogo não corresponder à versão verificada, o plugin não é
carregado.
