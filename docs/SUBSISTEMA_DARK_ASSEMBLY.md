# Subsistema: Dark Assembly

## Resultado da votação

`CTask_Vote_Voting` guarda os totais da votação e o resultado final.

| Campo | Uso |
| --- | --- |
| `+0x214` | votos favoráveis |
| `+0x218` | votos contrários |
| `+0x250` | resultado: aprovado ou rejeitado |

A decisão é feita entre as RVAs `0x004D3F03` e `0x004D3F27`. Depois dela, o
jogo escolhe a tela de aprovação ou rejeição.

## Funcionamento do mod

O plugin acompanha a rotina da votação na RVA `0x004D19E0`. Enquanto estiver
ativado, ele marca a votação atual para seguir pelo resultado aprovado. A marca
é restaurada ao final da chamada e não é gravada em arquivo.

Ao desativar o mod, as votações seguintes usam os votos normais do jogo. O banco
`data/database/wish.dat` permanece original.

## Classes confirmadas

| Classe | VTable RVA | TypeDescriptor RVA |
| --- | ---: | ---: |
| `CTask_Vote_Voting` | `0xA59960` | `0xCE7EB8` |
| `CState_Vote@CTask_Vote_Voting` | `0xA59850` | `0xCE7D58` |
