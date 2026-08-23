# Chara World - Energia Infinita

Este mod mantém a energia do Chara World no valor configurado.

Ele aplica o valor ao preparar os dados da sessão e novamente quando uma ação do
tabuleiro altera a energia. Assim, o valor também é corrigido ao entrar com uma
sessão que já estava abaixo de `100`.

Opções:

- `locked_energy`: valor entre `10` e `100`;
- `freeze_energy`: ativa ou desativa a trava.

O plugin é carregado pelo Mod Loader quando `enabled.txt` contém `1`. As opções
ficam em `config.json` e também podem ser alteradas pelo Mod Menu.

O campo usado pelo mod é somente o valor atual da energia. O campo que causava o
crash na versão anterior não é mais alterado.

Consulte `docs/SUBSISTEMA_CHARA_WORLD.md` para os dados confirmados do jogo.
