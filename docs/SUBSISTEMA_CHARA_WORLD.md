# Subsistema: Chara World

Este documento registra os dados confirmados da energia e dos bônus do Chara
World na versão atual de `Disgaea_Mayhem.exe`.

## Energia da sessão

O objeto `CCharacterWorldInformation` guarda os dados da partida. Os campos
relevantes são:

| Posição | Uso |
| --- | --- |
| `+0x170` | início do controle interno de energia |
| `+0x174` | parte de um ponteiro interno; não pode receber valores |
| `+0x178` | energia atual |
| `+0x180` | objeto com os bônus obtidos na sessão |

O limite normal é `100`. O jogo consulta esse limite em `0x1401A0120`.

Durante a resolução de uma ação, a mudança de energia é aplicada em
`0x140461E07`. O objeto da sessão é obtido a partir de `task + 0x200`.

## Pontos usados pelo mod

O plugin acompanha duas rotinas:

| Rotina | Uso no mod |
| --- | --- |
| RVA `0x00453050` | corrige a energia quando os dados da sessão são preparados para uso |
| RVA `0x00461CA0` | corrige a energia antes e depois de uma ação do tabuleiro |

As duas rotinas e a versão do executável precisam corresponder ao esperado. Se
alguma verificação falhar, o plugin não é ativado.

Não há leitura periódica nem armazenamento do endereço da sessão. O endereço é
obtido somente durante uma chamada do próprio jogo.

## Atributos ganhos nos tiles

A rotina na RVA `0x00458930` soma o prêmio do tile ao quadro de bônus. O tipo e
o valor vêm do efeito atual da sessão.

| Tipo | Atributo | Campo em `CCharacterWorldBonus` |
| ---: | --- | --- |
| `0` | HP | `+0x18` |
| `1` | ATK | `+0x28` |
| `2` | DEF | `+0x38` |
| `3` | MOVE | `+0x48` |
| `4` | CRITICAL | `+0x58` |

O plugin multiplica somente o valor positivo recebido nessa rotina. O valor
temporário do efeito é restaurado depois da soma, evitando que o mesmo ganho
seja multiplicado novamente.

## Causa do crash anterior

A versão anterior tratava `+0x174` como energia máxima. Esse local faz parte de
um ponteiro usado pelo jogo, e a escrita deixava o objeto inválido ao entrar no
Chara World. O plugin atual escreve somente em `+0x178`.

## Interface

A tela de energia usa classes separadas para o tabuleiro e para a batalha. O
valor mostrado vem da energia atual da sessão. Não é necessário alterar os
campos da interface quando `+0x178` é atualizado no momento certo.
