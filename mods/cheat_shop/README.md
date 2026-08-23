# Cheat Shop - Limite de 5000%

Action nativa ABI v1 que modifica o banco usado pelo Cheat Shop antes da leitura
normal do jogo. O executável localiza a instalação relativamente à própria pasta
e nunca contém caminhos absolutos.

## Campos alterados

- EXP
- Mana
- HL
- Weapon Mastery
- Item Drops

Cada registro mantém os valores nativos de base `100`, passo `90` e demais
campos. Somente o máximo `500` é substituído por `5000`.

O patcher exige o tamanho e a sequência estrutural confirmados da versão atual.
Ele valida IDs únicos, ordem, campos adjacentes e IDs numéricos antes de criar um
arquivo temporário. A substituição do banco é atômica. Uma estrutura divergente
retorna erro e não altera o arquivo.

Execuções repetidas são idempotentes: um máximo já igual a `5000` é aceito como
o estado canônico.

## Códigos de saída

- `1`: caminho do próprio executável indisponível;
- `2`: raiz do jogo inválida;
- `3`: banco ausente ou ilegível;
- `4`: schema/fingerprint estrutural divergente;
- `5`: falha na gravação ou substituição atômica;
- `6`: verificação final divergente.

Quando executada pelo loader durante o bootstrap, a alteração vale para a carga
daquela inicialização. Se o botão for usado depois que o Cheat Shop já estiver
aberto, reinicie o jogo para que o banco seja carregado novamente.
