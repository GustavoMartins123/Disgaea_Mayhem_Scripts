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
| `CEnemyTactics` | `0xC9F1F0` | `0xA1BEF0` | Proprietário dos estados táticos |
| `CEnemyStatus` | `0xC9F270` | `0xA1BF10` | Status mantido pelo controller |
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

### 11.5 Uso da mesma IA pelos parceiros

Os companions de combate também recebem `CEnemyController`. A cadeia foi
confirmada pelas VTables das funções internas criadas pelos dois métodos de
spawn:

```text
CExploreInformation::makeCompanionPlayerUnit
  lambda VTable 0xA496A0, referência em 0x3CA5C6
  chamada em 0x3CA9B5 -> 0x192950

CExploreInformation::makeCompanionKidsUnit
  lambda VTable 0xA49668, referência em 0x3CD134
  chamada em 0x3CD48C -> 0x192950

0x192950
  cria CEnemyTactics
  chama 0x1925F0

0x1925F0
  cria CEnemyTacticsTask
  chamada em 0x19272D -> CEnemyController::CEnemyController, 0x18DDC0
```

Isso confirma que `CEnemyStateMachine` e seus estados não são exclusivos da
equipe inimiga, apesar dos nomes internos. Um hook nas entradas desses estados
atinge tanto inimigos quanto parceiros.

### 11.6 Unidade proprietária e identificação da equipe

A cadeia do estado até a unidade foi confirmada:

```text
CEnemyState + 0x28
  -> CEnemyController

CEnemyController + 0x28
  -> CCom_ExploreUnit
```

O construtor `CEnemyController::CEnemyController`, em `0x18DDC0`, recebe a
`CCom_ExploreUnit` e a conserva em `+0x28`. A fábrica compartilhada em
`0x192950` possui seis chamadores estáticos nesta build:

| Retorno após a chamada | Origem |
| ---: | --- |
| `0x3CA9BA` | `makeCompanionPlayerUnit` |
| `0x3CD491` | `makeCompanionKidsUnit` |
| `0x3CF0AF` | `makeEnemyKidsUnit` |
| `0x3D2C50` | `makeEnemyUnit` |
| `0x13A077` | recriação do controller de unidade existente |
| `0x13A1A1` | recriação do controller de unidade existente |

Os quatro criadores nomeados fornecem a identificação canônica de equipe. Os
dois caminhos de recriação não definem uma equipe nova: eles só são aceitos para
uma `CCom_ExploreUnit` que já esteja registrada. O destrutor virtual principal
de `CCom_ExploreUnit`, em `0x163D60`, encerra esse registro.

Existe ainda um marcador usado durante a criação do corpo da unidade. Em
`0x3D4220`, o jogo lê o byte `+0x40` de um objeto apontado pelo campo `+0x110`
da definição. Esse byte escolhe as máscaras `0x1100` e `0x1900` e é copiado para
`+0x359` do corpo criado. Ele reforça que o jogo diferencia os lados durante o
spawn, mas o nome definitivo do campo ainda não foi recuperado. O mod não usa
esse byte como classificador.

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

## 13. Perfis de combate separados

O perfil foi implementado em `mods/tactical_ai/` como plugin ABI v2 independente.
O Mod Menu apenas apresenta as opções e despacha o ciclo de vida genérico.

### 13.1 Campos alterados

O perfil atua sobre valores locais de cada estado. O cadastro compartilhado não
é modificado, portanto inimigos e parceiros podem receber valores diferentes.

| Controle | Origem confirmada | Cópia local alterada |
| --- | --- | --- |
| intervalo de ataque | `EnemyData + 0xA0` | `CEnemyState_AttackWait + 0x1A8/+0x1B0` |
| duração das pausas | entrada `0x17A5E0` | `CEnemyState_Wait + 0x40/+0x48/+0x70/+0x78` |
| intervalo de busca | entrada `0x183FF0` | `CEnemyState_Searching + 0x70/+0x78` |
| deslocamento lateral | `EnemyData + 0x154` | estado de execução `+0x90` |
| corrida | `EnemyData + 0x158` | estado de execução `+0x98` |
| mergulho | `EnemyData + 0x160` | estado de execução `+0x9C` |
| investida | `EnemyData + 0x15C` | `CEnemyState_Attack_Rush + 0x1F0` |
| alcance de busca | `EnemyData + 0xA4` | `CEnemyState_Searching + 0x1B4` |

O carregamento comum de movimento começa em `0x175E60`. Ele combina o valor
base da unidade com as taxas de `EnemyData` e grava os três campos locais. A
inicialização de investida em `0x180E20` grava `+0x1F0`; a inicialização de busca
em `0x183C50` grava `+0x1B4`. Os hooks são executados depois dessas rotinas.

O alcance em `EnemyData + 0xA4` e `CEnemyState_Searching + 0x1B4` é `float`.
A rotina de busca carrega o campo com `movss` em `0x184AB2` e o usa como extensão
espacial. A versão 3.1.0 do plugin tratava os mesmos quatro bytes como inteiro;
por isso um alcance normal falhava na validação e restaurava todas as alterações
logo depois da primeira amostra de movimento. A versão 3.2.0 valida e multiplica
esse campo como `float`.

Na fase inspecionada, um `CEnemyState_Searching` ativo continha `2,460` em
`+0x90`, `9,840` em `+0x98` e `24,600` em `+0x9C`. Estados `Free` ainda não
inicializados para movimento mantinham `1,0`. Isso confirma que o carregamento
comum preenche valores reais e diferentes conforme o estado.

No intervalo de ataque, apenas o par `+0x1A8/+0x1B0` é tratado como o valor
derivado de `EnemyData + 0xA0`. O par `+0x70/+0x78` da mesma rotina não é mais
apresentado como intervalo real de ataque.

### 13.2 Inimigos e parceiros

As rotinas de criação de companions chamam a mesma fábrica que cria
`CEnemyController`, conforme a cadeia da seção 11.5. Na camada de execução, a
origem descrita na seção 11.6 registra cada `CCom_ExploreUnit` como inimigo ou
parceiro. Quando um estado de movimento ou ataque é iniciado, o plugin resolve
`estado -> controller -> unidade`.

O nome interno da classe não é usado como filtro. Também não há classificação
por posição, alvo, estratégia ou outro sinal indireto.

Depois que a fábrica `0x192950` retorna, o vínculo confirmado da execução é:

```text
CCom_ExploreUnit registrada
  -> CEnemyController
```

`CEnemyController + 0x18` aponta para `CEnemyStatus`, VTable `0xA1BF10`. A
classificação anterior desse objeto como `CEnemyTactics` estava errada e fazia o
perfil interromper a operação ao entrar em uma fase.

Na camada tática, o vínculo correto é independente do controller:

```text
CEnemyTacticsState + 0x28
  -> CEnemyTactics, VTable 0xA1BEF0
  -> CEnemyTacticsManagement, em CEnemyTactics + 0xB0
```

Há ainda dois gerenciadores táticos separados no objeto da exploração:

| Campo | Equipe | Chamada de `update_Normal`, retorno |
| ---: | --- | ---: |
| `+0x400` | inimigos | `0x41D2DD` |
| `+0x408` | parceiros | `0x41D2F3` |

O fluxo `makeEnemyUnit` usa explicitamente `+0x400`; o fluxo
`makeCompanionPlayerUnit` usa `+0x408`. A rotina `update_Normal`, RVA `0x194AD0`,
possui apenas esses dois chamadores nesta build. O plugin registra o ponteiro do
gerenciador antes de cada atualização e resolve o lado do estado pelo vínculo
acima.

Uma inspeção somente de leitura durante uma fase carregada encontrou exatamente
dois gerenciadores e 18 estados táticos: 17 apontavam para o gerenciador de
inimigos e um para o gerenciador de parceiros. Todos os 18 usavam
`CEnemyTactics + 0xB0`; nenhum apontava para `CEnemyStatus`.

A sonda reproduzível está em `native/research/tactical_ai_probe.cpp`. Ela
descobre o processo automaticamente e usa apenas permissão de consulta e leitura.

### 13.3 `RELOTTERY_SEC`

O campo `EnemyTacticsOrderData + 0x84` define o modo da nova loteria. A rotina
`0x18AAA0` converte os modos do dado para o estado tático:

| Valor no dado | Modo local |
| ---: | ---: |
| `1`, `SEC` | `0` |
| `2`, `TASKSHORT` | `1` |
| `3`, `TASKSHORT_SEC` | `2` |
| `4`, `TASKFINISH` | `3` |
| `5`, sentinela `MAX` | `4` |
| outro | `0` |

As condições temporizadas são materializadas por `0x18BC90`. Para os ramos
`SEC` e `TASKSHORT_SEC`, essa rotina:

1. lê o argumento da condição carregada;
2. converte o valor para a base de tempo usada pelo jogo;
3. cria um nó de 72 bytes na lista `CEnemyTacticsState + 0xB0`;
4. grava o modo do nó em `+0x14` (`1` ou `3`);
5. grava a duração em `+0x30` e o marcador ativo em `+0x44`.

A atualização base em `0x18AF90` percorre a lista. Para cada nó ativo, atualiza
o relógio em `+0x20`, compara com a duração em `+0x30` e solicita nova loteria
quando o limite vence.

O controle **Frequência de Nova Decisão** altera somente `nó + 0x30` nos modos
`1` e `3`. A relação é inversa: `200%` usa metade da duração original; `50%` usa
o dobro. `TASKFINISH` não possui temporizador e não é convertido silenciosamente
em outro modo.

### 13.4 Opções

Os valores são percentuais do campo original:

| Equipe | Ataque | Pausa | Busca | Movimento | Alcance | Nova decisão |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| inimigos | 40% | 30% | 45% | 160% | 175% | 250% |
| parceiros | 30% | 20% | 30% | 180% | 200% | 300% |

Ataque, pausa e busca representam duração: um valor menor encerra o intervalo
mais cedo. Movimento e alcance são multiplicadores diretos. Nova decisão
representa frequência e, por isso, usa a relação inversa com a duração.

As faixas são:

- ataque, pausa e busca: `10%` a `300%`;
- movimento, alcance e nova decisão: `25%` a `500%`.

Os padrões foram escolhidos para produzir uma diferença visível no primeiro
teste. Todos os controles em `100%` conservam os valores do jogo.

Cada equipe também possui um toggle **Ativar Ajustes**. Desmarcá-lo restaura os
snapshots apenas daquela equipe e mantém a IA original do jogo ativa. O toggle
principal do mod continua controlando os dois perfis em conjunto.

O toggle **IA Ativa** controla a chamada de
`CEnemyTacticsManagement::update_Normal` da equipe. Desmarcado, o hook não chama
a rotina original para o gerenciador correspondente. Como esta build possui
somente os dois chamadores confirmados da seção 13.2, inimigos e parceiros podem
ser interrompidos separadamente. O toggle do perfil não participa desse bloqueio.

### 13.5 Segurança e reversão

- o fingerprint PE é validado pelo loader;
- os estados, os dois gerenciadores, a fábrica, o destrutor e todos os seus
  chamadores conhecidos são verificados;
- cada objeto é validado como memória gravável e cada duração tem limite de
  sanidade;
- estados de execução exigem uma unidade registrada por um dos quatro criadores;
- estados táticos exigem o vínculo confirmado
  `estado -> tática -> gerenciador`;
- uma recriação só é aceita para uma unidade que já possua identidade;
- estados, campos de movimento e temporizadores táticos mantêm snapshots
  separados, com até 512 entradas em cada grupo;
- mudar um slider recalcula os snapshots a partir do original;
- desligar **IA Ativa** bloqueia a atualização do gerenciador somente enquanto
  o mod estiver ativo;
- desativar suspende os hooks, aguarda chamadas em andamento e restaura os
  estados ainda válidos;
- uma divergência encerra a operação sem procurar outro offset e sem aplicar uma
  alteração parcial ao estado rejeitado.

O plugin não altera `.dat`, scripts `.lub`, banco local ou save.

### 13.6 Telemetria atual

O log registra a primeira amostra de cada estado por equipe e, ao desativar,
informa separadamente quantas entradas de espera de ataque, pausa e busca foram
ajustadas em inimigos e parceiros. Movimento, alcance e frequência de nova
decisão também registram uma amostra por equipe. O primeiro bloqueio da
atualização completa de cada equipe também é registrado.

### 13.7 Fora do perfil atual

Permanecem fora desta versão: pesos de tasks, troca do modo `TASKFINISH`,
posicionamento coletivo, desvio, coordenação, seleção de habilidade e regras
específicas por boss.

## 14. Estrutura do mod

```text
mods/tactical_ai/
├── config.json
├── enabled.txt
├── mod.json
├── README.md
├── tactical_ai.cpp
└── tactical_ai.dll
```

Responsabilidades da DLL:

1. validar o fingerprint da build;
2. validar as rotinas, VTables e objetos de estado;
3. interceptar as três entradas de duração, os três carregamentos de parâmetros
   e a atualização tática documentados;
4. capturar um snapshot antes da primeira alteração de cada campo;
5. recalcular os valores sempre a partir do snapshot;
6. restaurar o snapshot ao desativar;
7. rejeitar a ativação se qualquer validação estrutural falhar.

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
- criação de `CEnemyController` nos fluxos de companion player e companion kids;
- cadeia `CEnemyState -> CEnemyController -> CCom_ExploreUnit`;
- quatro origens de criação e dois caminhos de recriação do controller;
- `CEnemyController + 0x18 -> CEnemyStatus`;
- `CEnemyTacticsState + 0x28 -> CEnemyTactics`;
- `CEnemyTactics + 0xB0 -> CEnemyTacticsManagement`;
- gerenciador de inimigos em `+0x400` e de parceiros em `+0x408`;
- origem e cópia local de intervalo de ataque, movimento e alcance de busca;
- lista de temporizadores táticos em `CEnemyTacticsState + 0xB0`;
- modo do nó temporizado em `+0x14`, duração em `+0x30` e ativo em `+0x44`;
- separação dos seis controles, dos toggles e da telemetria por equipe;
- atualização sobre unidades da exploração em tempo real.

### Inferência apoiada por evidência

- scripts `attack_Enemy*.lub` concentram a coreografia da ação concreta;
- tasks longas combinadas com `TASKFINISH` aumentam a demora para reagir;
- os modos 4 e 5 especializam o ramo de maior distância conforme os nomes das
  tasks de companion.

### Pendente

- significado dos argumentos não temporizados de `RELOTTERY`;
- nomes definitivos dos campos internos de condição da task table;
- semântica dos três campos restantes das ações em `enemyTaskActionList`;
- ponteiro proprietário da máquina de estado tática;
- nome definitivo do marcador `definition[+0x110][+0x40]` usado no spawn;
- papel completo de boids, cerco e colisão na coordenação coletiva;
- limite de companions simultâneos e efeitos sobre HUD, câmera e alocação.
