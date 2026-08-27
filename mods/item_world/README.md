# Item World - Multiplicadores

Este mod oferece ajustes para três partes do Item World:

- progresso usado para aumentar o nível do item;
- Item Points recebidos durante a exploração;
- raridade mínima dos equipamentos gerados pelo jogo.

## Progresso de nível

Ative **Multiplicar Progresso de Nível** e escolha um multiplicador entre `1x` e `20x`.

`1x` mantém o comportamento normal do jogo. Valores maiores aceleram o progresso aplicado ao item ao concluir a exploração.

## Item Points

Ative **Multiplicar Item Points** e escolha um multiplicador entre `1x` e `200x`.

Esse ajuste aumenta os Item Points recebidos, mas não aumenta a quantidade de equipamentos obtidos e não controla a raridade deles.

## Raridade mínima — experimental

A parte de raridade **funciona apenas parcialmente e ainda está sendo investigada**.

O jogo possui vários caminhos diferentes para criar equipamentos. O mod já consegue interceptar alguns desses caminhos, mas a cobertura ainda não é completa e o resultado pode variar dependendo de onde o item foi gerado.

Existem duas opções:

- **Raridade Mínima - somente Item World**: tenta aplicar o valor mínimo aos caminhos identificados como pertencentes ao Item World;
- **Estender Raridade a TODO o jogo**: também tenta aplicar o mínimo a outros caminhos de geração de equipamentos.

O valor de **Raridade Mínima** pode ser configurado entre `0` e `100`. Quando o mod consegue atuar naquele equipamento, uma raridade que já seria maior que o mínimo é mantida.

Não considere a opção global como garantia de que todo item do jogo terá a raridade escolhida. Essa funcionalidade continua experimental enquanto os demais caminhos de geração são identificados e testados.

## O que este mod não altera

O mod não controla atualmente:

- chance ou quantidade de equipamentos recebidos;
- escolha entre salas comuns e salas misteriosas;
- encontro, ganho ou subjugação de Innocents.

Alguns arquivos `.dat` do jogo relacionados ao Item World, recompensas, ondas e Innocents são mencionados na documentação técnica porque foram úteis para entender esses sistemas. **O plugin não modifica esses arquivos diretamente.** Os recursos implementados atualmente funcionam por alterações em memória durante a execução do jogo.

As opções são salvas em `config.json` e podem ser alteradas pelo Mod Menu.

Se uma atualização do jogo tornar as rotinas conhecidas incompatíveis, o plugin pode recusar o carregamento para evitar aplicar alterações em locais incorretos.

Para detalhes de engenharia reversa, consulte `docs/SUBSISTEMA_ITEM_WORLD.md`.