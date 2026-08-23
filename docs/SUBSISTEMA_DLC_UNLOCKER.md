# DLCs e consumíveis Steam

## Tipos de conteúdo

A tabela `DLC_information.dat` separa dois fluxos:

- tipo `1`: DLC permanente, marcado como `Received` no save após o resgate;
- tipo `2`: consumível do inventário Steam, consumido a cada resgate.

HL Bag, Mana Bag e os três Boost Tickets são itens do tipo `2`. Convertê-los para o tipo `1` faz o resgate funcionar uma vez, mas também grava o estado permanente que impede novas retiradas.

## SmokeAPI

O SmokeAPI injeta as definições `1` a `5` na lista do inventário. Essas entradas simuladas podem ser exibidas pelo jogo, mas a operação Steam usada para consumi-las não é concluída porque o identificador da instância não existe no inventário real.

O mod mantém as tabelas originais e confirma em memória somente o consumo destas definições:

| Conteúdo | Definição Steam | Recompensa |
| --- | --- | --- |
| HL Bag | `1` | `HL1M` |
| Mana Bag | `2` | `Mana1M` |
| Boost Ticket 100% | `3` | `Boost100` |
| Boost Ticket 400% | `4` | `Boost400` |
| Boost Ticket 900% | `5` | `Boost900` |

Outros itens continuam no fluxo normal.

O plugin é um toggle residente. Enquanto está ativado, aceita somente uma
unidade das definições `1` a `5`. Ao ser desativado, deixa de confirmar essas
operações e o jogo volta a consultar o inventário Steam. O plugin não cria as
cinco entradas; essa responsabilidade permanece no SmokeAPI.

## Pontos confirmados

| Elemento | RVA / campo | Uso |
| --- | --- | --- |
| Serviço global | `0xD3AD88` | Instância ativa do inventário |
| VTable do serviço | `0xA85BE8` | Validação da instância |
| Consumo de item | `0x82F6C0` | Pedido enviado ao inventário Steam |
| Estado da operação | `serviço + 0x08` | `1` indica conclusão |
| Tipo da operação | `serviço + 0x10` | `2` indica consumo |
| Item da operação | `serviço + 0x14` | Definição numérica do item |

O plugin valida a versão do executável, a rotina, o serviço e sua VTable. Se alguma validação falhar, o consumo simulado é recusado e o motivo é registrado pelo loader.

As recompensas concedidas pelo jogo continuam sendo salvas normalmente. O mod não apaga HL, Mana ou tickets recebidos ao ser desativado.
