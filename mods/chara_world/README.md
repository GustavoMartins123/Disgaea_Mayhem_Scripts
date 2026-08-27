# Chara World - Energia e bônus

Este mod possui dois recursos para o Chara World: travar a energia durante o tabuleiro e multiplicar os atributos recebidos em determinados tiles.

## Energia

Ative **Congelar Energia Automaticamente** e escolha um valor entre `10` e `100`.

Enquanto a opção estiver ativa, o mod tenta manter a energia atual do Chara World no valor configurado durante o tabuleiro.

## Multiplicador de atributos — experimental

A opção **Multiplicador dos Atributos Ganhos nos Tiles** permite escolher um valor entre `1x` e `20x` para ganhos de:

- HP;
- ATK;
- DEF;
- MOVE;
- CRITICAL.

**Esse recurso ainda está em investigação e o comportamento pode ser bastante irregular.** Nem todo tile ou situação entrega os atributos exatamente pelo mesmo caminho, então alguns ganhos podem ser multiplicados como esperado enquanto outros podem não apresentar o resultado esperado.

Use `1x` para manter o ganho normal. Valores maiores devem ser tratados como experimentais até que todos os tipos de ganho do Chara World estejam melhor identificados e testados.

As alterações são feitas em memória durante a execução do jogo. O mod não depende de editar arquivos `.dat` do Chara World. Caso esses arquivos sejam mencionados na documentação técnica, eles servem como referência para entender os dados internos do jogo, não como arquivos modificados pelo plugin.

As opções são salvas em `config.json` e podem ser alteradas pelo Mod Menu.

Se uma atualização do jogo tornar as rotinas conhecidas incompatíveis, o plugin pode recusar o carregamento para evitar alterações incorretas.