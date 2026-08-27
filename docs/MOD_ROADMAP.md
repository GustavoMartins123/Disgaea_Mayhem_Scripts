# Estado e planejamento dos mods

Este documento resume o que já está disponível no pacote e quais recursos ainda estão em desenvolvimento ou investigação.

## Disponível atualmente

- **Mod Menu**: interface dentro do jogo para ativar mods e alterar opções em tempo real.
- **Chara World**: trava de energia configurável. O multiplicador de atributos dos tiles está disponível, mas ainda é experimental e pode apresentar resultados irregulares.
- **Item World**: multiplicadores de progresso de nível e Item Points. A raridade mínima está disponível de forma experimental e ainda não cobre de maneira confiável todos os caminhos usados pelo jogo para gerar equipamentos.
- **Cheat Shop**: mantém EXP, Mana, HL, Weapon Mastery e Item Drops em `5000%` enquanto estiver ativado.
- **Dark Assembly**: aprovação garantida das propostas enquanto o mod estiver ativo.
- **DLC Unlocker**: permite reutilizar os cinco consumíveis fornecidos pelo SmokeAPI incluído no pacote.
- **Tactical AI**: ajustes separados para inimigos e parceiros, incluindo ataque, pausas, busca, movimento, alcance e frequência de novas decisões.
- **Safe Backup**: cria backups automáticos dos saves e mantém uma quantidade configurável de cópias recentes por slot.

## Recursos que ainda estão sendo investigados

### Item World

A geração de equipamentos possui vários caminhos internos. A raridade mínima já funciona em parte deles, mas a cobertura ainda não é completa. A opção global deve ser considerada experimental.

Também continuam em investigação:

- chance e quantidade específica de equipamentos no Item World;
- escolha entre salas comuns e salas misteriosas;
- encontro, ganho e subjugação de Innocents.

Os arquivos `.dat` relacionados a recompensas, ondas, salas e Innocents podem aparecer nas anotações técnicas porque ajudam a identificar os sistemas internos do jogo. Eles **não são modificados diretamente pelos mods atuais**.

### Chara World

A trava de energia está implementada. O multiplicador dos atributos recebidos nos tiles ainda apresenta comportamento variável dependendo do tipo de ganho e continua sendo investigado.

## Ideias para mods futuros

- Evilities e habilidades sem restrições de classe;
- alcance e formato de magias e ataques em área;
- velocidade de movimento na base;
- paletas de cores e aparências alternativas.

Esses itens são ideias ou pesquisas em andamento e não devem ser considerados recursos disponíveis no pacote atual.