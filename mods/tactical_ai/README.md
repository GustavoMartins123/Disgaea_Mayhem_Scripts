# IA - Perfis de Combate

Ajusta a IA de combate em tempo real. Inimigos e parceiros usam controles
separados, mesmo que o jogo compartilhe as mesmas classes entre as duas equipes.

## Como ler os valores

`100%` mantém o valor original.

- Intervalo de ataque, duração das pausas e intervalo de busca: valores menores
  deixam a ação mais rápida.
- Velocidade de movimento, alcance de busca e frequência de nova decisão:
  valores maiores aumentam o efeito.

O perfil inicial usa diferenças fortes o bastante para serem percebidas durante
um combate. Para comparar com o jogo sem ajustes, coloque todos os controles em
`100%`.

## Controles

Cada equipe possui dois controles de ativação e seis parâmetros:

- **IA Ativa**: ligada, o jogo atualiza normalmente o gerenciador daquela
  equipe. Desligada, a atualização completa do gerenciador é bloqueada; a
  equipe deixa de procurar alvos, mover e atacar até o controle ser ligado
  novamente.

- **Ativar Ajustes**: aplica ou restaura apenas o perfil daquela equipe. O
  toggle principal do mod continua ativando ou desativando o conjunto inteiro.

- **Intervalo de Ataque**: altera a espera carregada pelo jogo antes do próximo
  ataque.
- **Duração das Pausas**: altera o tempo dos estados de espera.
- **Intervalo de Busca**: altera a demora antes de uma nova procura por alvo.
- **Velocidade de Movimento**: altera deslocamento lateral, corrida, mergulho e
  investida.
- **Alcance de Busca**: altera a área usada para encontrar alvos.
- **Frequência de Nova Decisão**: altera os temporizadores das ordens `SEC` e
  `TASKSHORT_SEC`. `200%` faz essas ordens vencerem em metade do tempo original.

## Separação entre inimigos e parceiros

O jogo mantém um gerenciador para os inimigos e outro para os parceiros. O
plugin registra os dois durante a atualização da fase. Os estados táticos
apontam diretamente para um desses gerenciadores; os estados de movimento e
ataque continuam ligados à unidade criada. Não há classificação por nome,
posição ou alvo.

## Ativação e desativação

Os valores são mantidos apenas na sessão. O plugin guarda o valor original de
cada estado ativo, recalcula a partir desse valor quando uma opção muda e o
restaura ao desativar.

Desmarcar **Ativar Ajustes dos Inimigos** ou **Ativar Ajustes dos Parceiros**
restaura somente a equipe escolhida. O jogo continua controlando normalmente a
equipe; apenas os multiplicadores do mod deixam de ser aplicados.

Desmarcar **IA Ativa** é diferente: o plugin deixa de chamar a rotina que
atualiza todos os integrantes daquela equipe. Marcar novamente retoma a rotina
original sem reiniciar a fase.

Se a equipe, a estrutura ou um valor não corresponder ao esperado, o perfil é
interrompido e os valores já alterados são restaurados. O mod não altera `.dat`,
scripts, banco local ou save.

As primeiras alterações de cada grupo são registradas em
`mods/mod_loader.log`.

## Build suportada

- timestamp PE: `0x6A6AB373`;
- `SizeOfImage`: `0x00E01000`.

Outra build é recusada na inicialização.
