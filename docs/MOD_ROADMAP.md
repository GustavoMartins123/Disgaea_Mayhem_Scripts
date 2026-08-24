# Planejamento dos mods

Este arquivo separa o que já está implementado do que ainda depende de pesquisa
no executável. Um item só deve aparecer no Mod Menu depois que seu fluxo estiver
confirmado e isolado dos outros sistemas do jogo.

## Implementado

- Mod Loader: descoberta por `mod.json`, ABI v2, configuração persistente e
  ciclo de vida centralizado.
- Mod Menu: overlay DirectX 12, controle por teclado e controle, captura dos
  comandos do jogo e ajuste dos painéis ao espaço disponível.
- Chara World: energia configurável e multiplicador dos atributos ganhos nos
  tiles.
- Item World: multiplicadores separados de progresso de nível e Item Points,
  além de raridade mínima para os equipamentos obtidos.
- Cheat Shop: cinco valores mantidos em `5000%` na memória enquanto o mod está
  ativo, com restauração ao desativar e durante a gravação do save.
- Dark Assembly: aprovação garantida na memória da votação atual.
- DLC Unlocker: resgate reutilizável das definições `1` a `5` injetadas pelo
  SmokeAPI.
- Backup Seguro: backup inicial e novos arquivos quando `save.002` muda.

O atalho visual `Mods` dentro do Main Menu ainda não foi implementado. O
instalador existente apenas valida o atlas NMPLTEX/YKCMP e encerra com erro sem
alterar o arquivo.

## Item World pendente

### Chance e quantidade de equipamentos

É necessário localizar o cálculo específico das recompensas do Item World e
separá-lo do controle `Item Drops` da Cheat Shop. O multiplicador de Item Points
e a raridade mínima já estão isolados e não devem ser usados para quantidade.

### Salas misteriosas

Os dados de sala e de ondas foram encontrados, mas a rotina que escolhe entre
sala comum e sala misteriosa ainda não foi confirmada.

### Innocents

A classe e os bancos foram identificados. Ainda falta confirmar onde o jogo
registra encontro, derrota, ganho e subjugação.

## Outros mods pendentes

- Evilities e habilidades sem restrições de classe.
- Alcance e formato de magias e ataques em área.
- Velocidade de movimento na base.
- Paletas de cores e aparências alternativas.

Esses itens ainda não possuem pontos de alteração validados.
