# Subsistema de IA de combate

Este documento descreve a IA usada durante a exploração e o combate em tempo
real de Disgaea Mayhem. Ele cobre inimigos e companions, pois ambos passam pelo
mesmo conjunto de tabelas táticas.

Build analisada:

- timestamp PE: `0x6A6AB373`;
- `SizeOfImage`: `0x00E01000`;
- image base preferencial: `0x140000000`;
- banco: 230 arquivos em `data/database/`.

Os endereços e layouts abaixo pertencem somente a essa build. Um mod deve
validar o fingerprint antes de acessar memória e falhar de forma explícita se a
build, o registro ou a capacidade esperada não corresponder.

## 1. Resultado da análise

A IA não usa o modelo por turnos dos Disgaea principais. Mayhem executa duas
camadas de estado em tempo real:

1. a camada tática escolhe o estado, verifica condições e sorteia uma task;
2. a camada de execução procura o alvo, movimenta a unidade e executa a ação.

A cadeia completa confirmada é:

```text
placementParameter_ExploreEnemy.dat
    tacticsStrategyID
             |
             v
enemyTacticsStrategy.dat
    estado inicial + transições por resultado
             |
             v
enemyTacticsStatus.dat
    tipo do estado + lista de orders
             |
             v
enemyTacticsOrder.dat
    condições de reavaliação + loteria + tabelas de tasks
             |
             v
enemyTacticsTaskTable.dat
    tasks candidatas + pesos + condições
             |
             v
enemyTacticsTask.dat
    intenção + prioridade + modo de alvo + movimento
             |
             v
enemyTaskActionList.dat  <--- enemy.dat::taskActionListID
    task abstrata -> ação concreta daquele inimigo
             |
             v
action_Enemy.dat / scripts/attack_Enemy*.lub
             |
             v
CEnemyStateMachine
    busca, movimento, espera e ataque em tempo real
```

Essa separação é importante: alterar apenas a ação não melhora a decisão, e
alterar apenas a cadência não corrige uma tabela que continua sorteando espera
ou intimidação com peso alto.

## 2. Formato externo dos `.dat`

O formato comum confirmado é apenas o enquadramento dos registros:

```text
uint32 recordCount
repeat recordCount vezes:
    uint32 payloadSize
    byte payload[payloadSize]
```

Dentro do payload, várias tabelas começam com um identificador numérico, uma
chave e um rótulo. Strings dimensionadas usam:

```text
uint32 byteCount
byte text[byteCount]       // inclui o terminador NUL
```

O restante do payload não possui um formato universal. Cada tabela tem seu
próprio serializador, campos e vetores de capacidade fixa. Portanto, procurar
uma chave ASCII é útil para identificar um registro, mas não substitui a
decodificação da tabela.

## 3. Inventário das tabelas de IA

| Arquivo | Tamanho | Registros | Função confirmada |
| --- | ---: | ---: | --- |
| `enemyTacticsOrderDefine.dat` | 4.459 B | 51 | Enumerações de reavaliação, condições, ordenação e resultados |
| `enemyTacticsStrategy.dat` | 10.146 B | 81 | Estado inicial e transições |
| `enemyTacticsStatus.dat` | 10.914 B | 76 | Estados táticos e orders ativas |
| `enemyTacticsOrder.dat` | 16.484 B | 71 | Condições e sorteio de tabelas de tasks |
| `enemyTacticsTask.dat` | 40.387 B | 261 | Intenções de movimento, espera e ataque |
| `enemyTacticsTaskTable.dat` | 45.781 B | 250 | Sorteio das tasks candidatas |
| `enemyTaskActionList.dat` | 41.660 B | 83 | Conversão de task em ação específica |
| `enemyActionSet.dat` | 1.314 B | 11 | Conjuntos especiais de ação |
| `enemy.dat` | 514.159 B | 2.444 | Cadastro, alcance de busca e lista de ações por inimigo |
| `placementParameter_ExploreEnemy.dat` | 204.719 B | 1.059 | Estratégia escolhida para cada inimigo colocado no mapa |
| `act_Enemy.dat` | 274.476 B | — | Dados de atuação e animação |
| `action_Enemy.dat` | 910.362 B | — | Ações concretas |

`enemyParametersTable.dat` possui 80 registros, mas os registros iniciais são
parâmetros de rank do Item World. O nome do arquivo, sozinho, não permite
classificá-lo como tabela geral de IA.

## 4. Estratégia e transições

### 4.1 Layout carregado

O serializador de `EnemyTacticsStrategyData` confirma:

| Offset | Campo |
| ---: | --- |
| `+0x58` | `aiType` |
| `+0x5C` | `firstStateID` |
| `+0x60` | vetor de transições, capacidade 32 |

Cada transição contém o estado atual, um resultado e o próximo estado. Exemplos
decodificados:

```text
ENEMY_TACTICS_STRATEGY_WAIT_GENERIC
  aiType = 1
  firstStateID = 1000000                  // SEARCHING
  1000000 + FIND_PLAYER -> 1000100        // ATTACK

ENEMY_TACTICS_STRATEGY_BOSS_GENERIC
  aiType = 2
  firstStateID = 5000000
  5000000 + FIND_PLAYER -> 5000100
```

A mesma tabela contém estratégias de companion e Item World. Exemplo da cadeia
de companion:

```text
estado de busca
  FIND_ENEMY -> estado de ataque

estado de ataque
  TASKFINISH ou DEAD -> estado de busca
```

Isso confirma que a base para perfis de companion já existe no banco. Não
confirma, por si só, que qualquer personagem do jogador possa ser colocado sob
controle dessa IA.

Famílias observadas:

| Faixa ou ID | Uso |
| --- | --- |
| `1000000` | espera e busca genéricas |
| `1000100` / `1000101` | ataque e combate genéricos |
| `5000000` | estratégia genérica de boss |
| `7000000` em diante | espera, busca, ataque, retorno e fuga de companion |
| `11000000` | entrada genérica do Item World |
| `12010000` a `12160000` | estratégias únicas usadas no Item World |

Também existem estratégias específicas para Sword, Doppelganger, Afterimage,
Mushroom e Kids. Uma faixa numérica não deve ser tratada como compatível com
outra sem verificar as transições e ações disponíveis.

### 4.2 Resultados de transição

Os identificadores de resultado em `enemyTacticsOrderDefine.dat` são:

| ID | Identificador original |
| ---: | --- |
| 1000 | `UNCONDITIONAL` |
| 1001 | `WAIT_DIRECTION` |
| 1002 | `WAIT_DIRECTION_BOSS` |
| 1003 | `WAIT_DIRECTION_ITEM_WORLD` |
| 1004 | `END_DIRECTION` |
| 1005 | `WAIT_FACILITY` |
| 1006 | `START_FACILITY` |
| 1007 | `START_FACILITY_COMPANION` |
| 1008 | `START_ITEMWORLD` |
| 1009 | `GAMEFINISH` |
| 1010 | `NO_ALLOCATE` |
| 1011 | `FIND` |
| 1012 | `FORCE_RETURN` |
| 1013 | `TASKFINISH` |
| 1014 | `DEAD` |
| 1015 | `FIND_PLAYER` |
| 1016 | `FIND_COMPANION` |
| 1017 | `FIND_ENEMY` |

## 5. Status tático

O serializador de `EnemyTacticsStatusData` confirma:

| Offset | Campo |
| ---: | --- |
| `+0x84` | `statusType` |
| `+0x88` | vetor de IDs de `enemyTacticsOrder` |

Exemplos decodificados:

| Status | Tipo | Orders |
| --- | ---: | --- |
| `SEARCHING`, ID `1000000` | 2 | `1000000`, `1000100` |
| `ATTACK`, ID `1000100` | 4 | `1000200`, `1000300`, `1000101` |
| `CONBAT`, ID `1000101` | 4 | `1000201`, `1000300`, `1000101` |
| `ATTACK_RANDOM_GENERIC` | 4 | `1000202` |
| `ATTACK_ItemWorld` | 4 | `1000203`, `1000300`, `1000101` |

Os companions possuem estados separados para busca próxima, busca distante,
distância do jogador, ataque curto, ataque longo, retorno, fuga e espera. Esses
estados usam o mesmo formato dos inimigos.

## 6. Orders: reavaliação, condições e sorteio

### 6.1 Layout carregado

O serializador de `EnemyTacticsOrderData` confirma:

| Offset | Campo |
| ---: | --- |
| `+0x84` | `orderType` |
| `+0x88` | condições de reavaliação AND, capacidade 32 |
| `+0x198` | condições de reavaliação OR, capacidade 32 |
| `+0x2A8` | limites de contagem |
| `+0x2B0` | tabela de tasks padrão |
| `+0x2B8` | entradas de loteria, capacidade 32 |

Cada entrada de loteria referencia uma tabela de tasks, possui um peso e pode
conter até quatro condições adicionais.

### 6.2 Modos de reavaliação

| ID | Identificador original | Rótulo observado |
| ---: | --- | --- |
| 1 | `EMEMY_TACTICS_ORDER_RELOTTERY_SEC` | N segundos após o sorteio |
| 2 | `EMEMY_TACTICS_ORDER_RELOTTERY_TASKSHORT` | quantidade insuficiente de unidades |
| 3 | `EMEMY_TACTICS_ORDER_RELOTTERY_TASKSHORT_SEC` | combinação dos dois casos |
| 4 | `EMEMY_TACTICS_ORDER_RELOTTERY_TASKFINISH` | conclusão da task |
| 5 | `EMEMY_TACTICS_ORDER_RELOTTERY_MAX` | sentinela |

O identificador `EMEMY` está grafado assim no dado original. A interpretação de
`TASKFINISH` é confirmada pelo rótulo. O efeito perceptível de tasks longas sobre
a demora da IA é uma conclusão provável, mas precisa ser medido durante a
execução antes de definir novos argumentos temporais.

Exemplo do ataque genérico:

```text
order 1000200
  reavalia ao terminar a task
  também possui condições alternativas por tempo/encurtamento
  permite até duas seleções
  sorteia table 1000301 com peso 75
  sorteia table 1000302 com peso 100
```

As tabelas escolhidas contêm ataques curtos, longos e esperas. Portanto, a
sensação de lentidão pode nascer tanto da reavaliação tardia quanto dos pesos
das tasks passivas.

### 6.3 Condições sobre a unidade

Há 25 entradas no grupo, mas `MAX` é sentinela. As 24 condições utilizáveis são:

| ID | Condição | ID | Condição |
| ---: | --- | ---: | --- |
| 100 | `ATTACKAREA` | 112 | `HASNOT_KIDS` |
| 101 | `OVER_HP` | 113 | `HAS_MUSHROOM` |
| 102 | `UNDER_HP` | 114 | `UNDER_MUSHROOM` |
| 103 | `OVER_DISTANCE` | 115 | `IS_FLY` |
| 104 | `UNDER_DISTANCE` | 116 | `IS_IN_WATER` |
| 105 | `PREV_TASK` | 117 | `IS_DIVING` |
| 106 | `NOPREV_TASK` | 118 | `IS_BLACKHOLE` |
| 107 | `NOSAME_TASK` | 119 | `NO_BLACKHOLE` |
| 108 | `HAS_FEATURE` | 120 | `HAS_SIMULACRA` |
| 109 | `TARGET_FIND` | 121 | `HASNOT_SIMULACRA` |
| 110 | `BEHIND_PLAYER` | 122 | `IS_NEEDLE` |
| 111 | `HAS_KIDS` | 123 | `NO_NEEDLE` |

Os modos de ordenação são `NEARTARGET` (200), `FARTARGET` (201) e
`SEARCHWIDE` (202).

## 7. Tabelas de tasks

`enemyTacticsTaskTable.dat` contém 250 loterias. Cada registro possui até 32
entradas. Uma entrada liga uma task a um peso e a até quatro condições.

Exemplos:

| Tabela | Conteúdo observado |
| ---: | --- |
| `1000101` | busca genérica: task `2000000`, peso 1 |
| `1000102` | aviso genérico: espera, intimidação e busca com pesos distintos |
| `1000301` | ataque curto com peso 90 e espera com peso 10 |
| `1000302` | ataque longo com peso 90 e espera com peso 10 |
| `1000303` | ataque aleatório |
| `1000400` | movimentação de cerco seguida de ataque longo |

Os três valores de cada condição interna são estruturalmente confirmados, mas
os nomes e a semântica exata dos três ainda não estão resolvidos. Strings dos
serializadores sugerem campos de ação, peso e execução imediata. Isso não deve
ser usado como contrato até a validação no processo.

## 8. Tasks, movimento e seleção de alvo

### 8.1 Layout carregado

O serializador de `EnemyTacticsTaskData` confirma:

| Offset | Campo |
| ---: | --- |
| `+0x58` | `refID` |
| `+0x5C` | `priority` |
| `+0x60` | `type` |
| `+0x64` | `targetMode` |
| `+0x68` | informações de movimento, capacidade 8 |
| `+0xB8` | comentário |

Existem 261 tasks, e não 247. Os tipos observados incluem:

| Tipo | Uso observado |
| ---: | --- |
| 0 | livre e sem alocação |
| 1 | espera |
| 2 | espera especial e recuo |
| 3 | busca e perseguição |
| 4 | ataque |
| 5 | movimento e retorno |

### 8.2 Modos de alvo

Os dados e `CEnemyState_Searching::isTargetFind()` confirmam:

| `targetMode` | Uso observado |
| ---: | --- |
| 0 | modo padrão ou aleatório conforme a task |
| 1 | jogador |
| 2 | companion |
| 3 | candidato de menor distância |
| 4 | busca distante usada por companion |
| 5 | busca distante do jogador usada por companion |

O código compara `targetMode` no offset `+0x194` do estado de busca. O modo 3
percorre os candidatos e conserva a menor distância. Os modos 4 e 5 seguem o
ramo de maior distância; a distinção entre eles também é indicada pelos nomes
das tasks de companion.

Os grupos `ATTACK_SHORT`, `ATTACK_LONG`, `ATTACK_RUSH` e `ATTACK_RANDOM` já
possuem variantes `_PLAYER`, `_COMPANION`, `_LENGTH` e `_RANDOM`. As variantes
referenciam uma task base por `refID`, em vez de duplicar toda a definição.

## 9. Conversão da intenção em ataque

`enemyTaskActionList.dat` não decide quando atacar. Ele converte a task abstrata
em uma ação que existe para o inimigo atual.

- existem 83 listas;
- cada lista comporta até 64 mapeamentos;
- cada mapeamento comporta até 10 ações;
- `enemy.dat::taskActionListID` escolhe a lista usada pelo inimigo;
- as tasks `ATTACK_1` a `ATTACK_10` são associadas a slots concretos, como
  `700` a `709`, conforme a lista.

Uma lista de inimigo pode referenciar a lista base e substituir apenas alguns
mapeamentos. Isso permite que a mesma intenção tática produza ataques diferentes
em inimigos diferentes.

Os quatro inteiros de cada ação foram decodificados estruturalmente. O primeiro
é registrado como `isUse`; a função exata dos três restantes ainda requer
observação dinâmica.

## 10. Cadastro e colocação dos inimigos

Campos confirmados no objeto carregado de `EnemyData`:

| Offset | Campo |
| ---: | --- |
| `+0x80` | `refID` |
| `+0x84` | `characterID` |
| `+0x88` | `charaID` |
| `+0x8C` | correção de HP |
| `+0x90` | correção de parâmetros |
| `+0x98` | armadura |
| `+0x9C` | espera após dano |
| `+0xA0` | espera de ataque |
| `+0xA4` | área de busca |
| `+0x148` | `taskActionListID` |
| `+0x154` | taxa de caminhada lateral |
| `+0x158` | taxa de corrida |
| `+0x15C` | taxa de investida |
| `+0x160` | taxa de mergulho |

O serializador de `PlacementParameterData_ExploreEnemy` confirma o
`tacticsStrategyID` em `+0x128`. A estrutura usada em runtime também expõe
`enemyStrategyID_` em `+0x40` e `tacticsStrategyID_` em `+0x48`.

Assim, a colocação no mapa pode trocar o comportamento de uma instância sem
alterar globalmente o cadastro do tipo de inimigo.

## 11. Classes em runtime

### 11.1 Controle e máquinas de estado

| Classe | TypeDescriptor RVA | VTable RVA | Função |
| --- | ---: | ---: | --- |
| `CEnemyController` | `0xCA0B70` | `0xA1BF00` | Controlador da unidade inimiga |
| `CEnemyStateMachine` | `0xC9F218` | `0xA1C178` | Estados concretos de execução |
| `CEnemyTacticsStateMachine` | `0xC9F0E8` | `0xA1BF20` | Estados da camada tática |
| `CEnemyTacticsManagement` | `0xC9FDB0` | `0xA1BEE0` | Atualização e avaliação tática |
| `CEnemyTactics_OrderData` | `0xC9B130` | `0xA1C0B0` | Avaliação de uma order |
| `CEnemyTacticsTask` | `0xC9F248` | `0xA1BE98` | Task tática em execução |
| `CEnemyTacticsStatus` | `0xC9F170` | `0xA1BEA8` | Estado tático carregado |
| `CEnemyState` | `0xC9E1E8` | `0xA1CAA8` | Base dos estados de execução |

O construtor de `CEnemyController`, iniciado no RVA `0x18DDC0`, cria um
`CEnemyStateMachine` e o armazena em `CEnemyController + 0x20`. A relação direta
de propriedade do `CEnemyTacticsStateMachine` não foi confirmada nesse
construtor; não deve ser representada como um segundo filho direto sem localizar
o ponteiro responsável.

### 11.2 Estados de execução

| Classe | TypeDescriptor RVA | VTable RVA |
| --- | ---: | ---: |
| `CEnemyState_Free` | `0xC9E178` | `0xA1C9F0` |
| `CEnemyState_NoAllocate` | `0xC9E100` | `0xA1C938` |
| `CEnemyState_Wait_AfterDamage` | `0xC9E078` | `0xA1C880` |
| `CEnemyState_Wait` | `0xC9E008` | `0xA1C7C8` |
| `CEnemyState_Wait_Motion` | `0xC9DF88` | `0xA1C710` |
| `CEnemyState_AttackWait` | `0xC9DF08` | `0xA1C540` |
| `CEnemyState_Attack` | `0xC9DE88` | `0xA1C480` |
| `CEnemyState_Attack_Set` | `0xC9DE08` | `0xA1C3C0` |
| `CEnemyState_Attack_Rush` | `0xC9DD90` | `0xA1C300` |
| `CEnemyState_Attack_Counter` | `0xC9DD08` | `0xA1C240` |
| `CEnemyState_Searching` | `0xC9DC88` | `0xA1C188` |
| `CEnemyState_Move` | `0xC9DC18` | `0xA1C0C0` |

`Attack_Rush` e `Attack_Counter` derivam de `Attack_Set`, que deriva de
`Attack`. `CEnemyStateMachine` deriva de `CSingleStateMachine`; os estados
concretos derivam de `CEnemyState` e, por fim, de `CSingleState`.

### 11.3 Estados táticos

| Classe | TypeDescriptor RVA | VTable RVA |
| --- | ---: | ---: |
| `CEnemyTacticsState` | `0xC9B0B8` | `0xA1C030` |
| `CEnemyTacticsState_Default` | `0xC9B028` | `0xA1BFB0` |
| `CEnemyTacticsState_Direction` | `0xC9AFA0` | `0xA1BF30` |

### 11.4 Métodos confirmados

Símbolos presentes no executável:

```text
CEnemyTacticsManagement::update_Normal(UpdateInfo const&)
CEnemyTactics_OrderData::isCondition(
    ConditionInfo,
    intrusive_ptr<CEnemyController> const&,
    int)
CEnemyState_Searching::isTargetFind()
CEnemyState_AttackWait::setAttackTarget()
CEnemyState::makeSurroundUnitList()
```

As lambdas associadas percorrem objetos `CCom_ExploreUnit`. A atualização opera
sobre unidades vivas e estados espaciais da exploração, e não sobre uma fila de
turnos.

Referências úteis encontradas no código, apresentadas como locais de chamada e
não como início garantido de função:

| Método | RVA de referência observado |
| --- | ---: |
| `makeSurroundUnitList` | próximo de `0x1783AB` |
| `setAttackTarget` | próximos de `0x17D49E` e `0x17D87B` |
| `isTargetFind` | próximo de `0x184B98` |
| `isCondition` | próximo de `0x189D8C` |
| `update_Normal` | próximo de `0x194FCA` |

## 12. Scripts de ataque

Há 374 arquivos cujo nome corresponde a `attack_Enemy*.lub`. O cabeçalho
`1B 4C 75 61 54` identifica bytecode Lua versão `0x54`, isto é, Lua 5.4.

Strings recuperadas de scripts de boss incluem chamadas como:

```text
ParticleCreate
ObjectSetPos
VoicePlayRandom_Enemy
SePlay
callHitEffect
```

O executável também contém o nome
`makeActionExecution_CallScript_Attack`. A evidência indica que os scripts
orquestram movimento, efeitos, som e acerto da ação já escolhida. A decisão de
alto nível continua nas tabelas de estratégia, status, order e task.

## 13. Perfil proposto: IA agressiva

O perfil deve ser um mod independente, por exemplo `mods/tactical_ai/`. O Mod
Menu apenas envia ativação e opções genéricas. A DLL mantém snapshot dos valores
originais, altera somente a memória e restaura o snapshot em `Mod_Disable()`.

### 13.1 Objetivo

Reduzir o tempo entre localizar um alvo e iniciar um ataque, sem acelerar
animações, aumentar atributos ou trocar os golpes próprios de cada inimigo.

### 13.2 Alterações da primeira versão

| Camada | Alteração | Motivo |
| --- | --- | --- |
| `EnemyData` | reduzir `attackWaitTime` com piso configurável | remove espera explícita antes do próximo ataque |
| `EnemyData` | ampliar `searchArea` de forma limitada | inicia a perseguição antes |
| `enemyTacticsTaskTable` | reduzir o peso das tasks de espera nas tabelas de ataque | evita sortear pausa quando já há alvo |
| `enemyTacticsTaskTable` | aumentar o peso relativo de ataque curto e investida | favorece ações que entram em alcance rapidamente |
| `enemyTacticsOrder` | reavaliar quando a task termina e, depois de validado, em intervalo curto | reduz permanência em uma escolha obsoleta |
| `enemyTacticsTask` | conservar os modos de alvo originais na versão inicial | evita transformar agressividade em foco artificial no jogador |

O perfil não deve remover todas as esperas. Algumas delas fecham animações,
recuperação após dano e transições de estado. A primeira versão altera apenas
tasks passivas dentro das loterias táticas, nunca os estados internos
`Wait_Motion` ou `Wait_AfterDamage`.

### 13.3 Níveis do perfil

As opções podem ser expressas como multiplicadores, aplicados sempre sobre o
snapshot original:

| Opção | Moderado | Agressivo | Implacável |
| --- | ---: | ---: | ---: |
| espera de ataque | 0,80x | 0,55x | 0,35x |
| peso de espera tática | 0,70x | 0,35x | 0,10x |
| peso de ataque | 1,15x | 1,40x | 1,80x |
| área de busca | 1,10x | 1,25x | 1,50x |

Esses números são uma proposta inicial, não valores extraídos do jogo. Pesos
inteiros devem ser normalizados sem ultrapassar o tipo do campo e sem criar
novas entradas nos vetores.

### 13.4 Segunda etapa, após telemetria

Registrar em memória, sem persistência:

- tempo entre `FIND_PLAYER` e entrada em `CEnemyState_Attack`;
- quantidade de tasks de espera sorteadas com alvo válido;
- quantidade de reavaliações por segundo;
- tempo gasto perseguindo um alvo fora do alcance;
- diferença por estratégia, boss e Item World.

Com esses dados será possível ajustar os argumentos de `RELOTTERY_SEC`, separar
inimigos comuns de bosses e evitar reavaliação excessiva. O mod deve parar com
erro explícito se não encontrar exatamente os registros esperados.

### 13.5 Limites da primeira versão

- não altera arquivos `.dat` no disco;
- não grava no save;
- não adiciona entradas a vetores de capacidade fixa;
- não substitui scripts `.lub`;
- não muda bosses que usam uma estratégia especial sem uma regra específica;
- não promete melhorar posicionamento coletivo, desvio ou coordenação entre
  inimigos; essas funções exigem análise adicional de `SURROUND`, boids e
  colisão.

### 13.6 Opções propostas no Mod Menu

O manifesto continuaria genérico; nenhuma regra da IA seria adicionada ao código
do Mod Menu. Esboço das opções:

```json
{
  "schema_version": 1,
  "id": "tactical_ai",
  "name": "IA - Perfil Agressivo",
  "category": "Combate",
  "version": "0.1.0",
  "type": "toggle",
  "plugin": "tactical_ai.dll",
  "options": [
    {
      "id": "attack_wait_multiplier",
      "name": "Multiplicador da Espera entre Ataques",
      "type": "slider_float",
      "min": 0.25,
      "max": 1.0
    },
    {
      "id": "search_area_multiplier",
      "name": "Multiplicador da Área de Busca",
      "type": "slider_float",
      "min": 1.0,
      "max": 1.5
    },
    {
      "id": "passive_weight_multiplier",
      "name": "Peso das Pausas Táticas",
      "type": "slider_float",
      "min": 0.1,
      "max": 1.0
    },
    {
      "id": "attack_weight_multiplier",
      "name": "Peso das Tasks de Ataque",
      "type": "slider_float",
      "min": 1.0,
      "max": 2.0
    },
    {
      "id": "affect_bosses",
      "name": "Aplicar Também aos Bosses",
      "type": "toggle"
    }
  ]
}
```

`Mod_SetOption()` recalcula todo o conjunto a partir do snapshot original. Ele
não multiplica um valor já alterado. `affect_bosses` começa desativado no
`config.json`, porque as estratégias especiais precisam ser verificadas antes de
receber o mesmo perfil dos inimigos comuns.

## 14. Estrutura recomendada do mod

```text
mods/tactical_ai/
├── enabled.txt
├── mod.json
├── README.md
└── tactical_ai.dll
```

Responsabilidades da DLL:

1. validar o fingerprint da build;
2. localizar o repositório carregado de dados por uma chamada válida do jogo;
3. validar IDs, quantidades e capacidades;
4. capturar um snapshot imutável antes da primeira alteração;
5. recalcular os valores sempre a partir do snapshot;
6. restaurar exatamente o snapshot ao desativar;
7. rejeitar a ativação se qualquer validação falhar.

Não deve haver varredura alternativa, offset substituto ou aplicação parcial.

## 15. Estado das evidências

### Confirmado

- formato externo dos registros `.dat`;
- contagens das tabelas descritas;
- layouts carregados e capacidades indicadas;
- cadeia placement -> estratégia -> status -> order -> task table -> task;
- ligação `enemy.dat::taskActionListID` -> ações;
- classes, heranças, RTTI e VTables listadas;
- seleção por menor distância no `targetMode` 3;
- uso compartilhado das tabelas por inimigos e companions;
- atualização sobre unidades da exploração em tempo real.

### Inferência apoiada por evidência

- scripts `attack_Enemy*.lub` concentram a coreografia da ação concreta;
- tasks longas combinadas com `TASKFINISH` aumentam a demora para reagir;
- os modos 4 e 5 especializam o ramo de maior distância conforme os nomes das
  tasks de companion.

### Pendente

- unidade e escala exatas de todos os argumentos de `RELOTTERY`;
- nomes definitivos dos campos internos de condição da task table;
- semântica dos três campos restantes das ações em `enemyTaskActionList`;
- ponteiro proprietário da máquina de estado tática;
- papel completo de boids, cerco e colisão na coordenação coletiva;
- limite de companions simultâneos e efeitos sobre HUD, câmera e alocação.
