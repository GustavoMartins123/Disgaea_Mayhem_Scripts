# Cheat Shop - Valores em 5000%

Enquanto este mod estiver ativado, os cinco valores principais da Cheat Shop
ficam em `5000%`:

- EXP;
- Mana;
- HL;
- Weapon Mastery;
- Item Drops.

Os cinco valores são aplicados juntos e não possuem controles separados. Item
Drops também influencia a quantidade de recompensas; em `5000%`, algumas telas
podem receber listas muito maiores que no jogo normal.

Os valores anteriores ficam guardados em memória e são restaurados quando o mod
é desativado.
Se a tela da Cheat Shop já estiver aberta, a cópia mostrada na lista também é
atualizada imediatamente.

Durante a leitura do save, o mod espera o jogo terminar e então aplica `5000%`.
Durante a gravação, entrega os valores anteriores ao jogo e reaplica `5000%`
somente depois. Assim, a alteração não fica gravada no save.

O plugin aceita somente a versão verificada do executável e os cinco registros
esperados da Cheat Shop. Se algum deles não corresponder, nenhum valor é
alterado.
