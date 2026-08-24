# IA - Perfil Agressivo

Reduz a demora da IA durante o combate em tempo real. Inimigos e parceiros usam
a mesma base do jogo, mas possuem controles separados neste mod.

## Controles

- **Inimigos - Tempo Antes do Ataque**: reduz a espera antes de um ataque.
- **Inimigos - Duração das Pausas**: reduz pausas entre decisões.
- **Inimigos - Intervalo de Busca**: procura um alvo novamente mais cedo.
- **Parceiros - Tempo Antes do Ataque**: reduz a espera antes de um ataque.
- **Parceiros - Duração das Pausas**: reduz pausas entre decisões.
- **Parceiros - Intervalo de Busca**: procura um alvo novamente mais cedo.

`1.0` mantém o tempo original. Valores menores tornam a IA mais rápida. A
configuração inicial dos inimigos usa `0.55`, `0.35` e `0.60`. A dos parceiros
usa `0.40`, `0.20` e `0.40`.

## Funcionamento

O plugin intercepta a inicialização de `CEnemyState_AttackWait`,
`CEnemyState_Wait` e `CEnemyState_Searching`. Somente os temporizadores criados
por esses estados são alterados. O nome interno `CEnemyState` não limita o uso a
inimigos: as estratégias de parceiros também entram nessa máquina de estados.

O jogo cria o mesmo `CEnemyController` para os dois lados. O plugin registra a
origem da unidade no momento em que esse controller é criado: os caminhos
`makeCompanionPlayerUnit` e `makeCompanionKidsUnit` identificam parceiros; os
caminhos `makeEnemyUnit` e `makeEnemyKidsUnit` identificam inimigos. O estado de
combate aponta para o controller, que aponta para a unidade registrada. Nenhuma
classificação é feita por distância, alvo ou nome da estratégia.

O mod não altera dano, HP, velocidade de animação, arquivos `.dat` ou save. Ao
desativar, os temporizadores monitorados são restaurados e novos estados voltam
ao comportamento original.

Se a equipe não estiver registrada, um chamador da fábrica for desconhecido, um
estado não corresponder à estrutura esperada ou o limite monitorado for
atingido, o perfil inteiro é interrompido e os valores já alterados são
restaurados.

As primeiras amostras e a quantidade de estados ajustados por lado são
registradas em `mods/mod_loader.log`.

## Build suportada

- timestamp PE: `0x6A6AB373`;
- `SizeOfImage`: `0x00E01000`.

Se a build ou os estados esperados não corresponderem, o plugin rejeita a
inicialização.
