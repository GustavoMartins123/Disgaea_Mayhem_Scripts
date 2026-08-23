# Motor Ngf

`Disgaea_Mayhem.exe` usa o motor Ngf, DirectX 12 e a biblioteca `NmplDLL.dll`
para entrada de teclado, mouse e controle.

## Organização do jogo

O jogo divide cada tela e atividade em tarefas. Uma tarefa possui estados para
entrada, atualização e saída. Item World, Chara World e suas interfaces seguem
esse modelo.

Os objetos compartilhados usam contagem de referências. Por isso um mod não deve
guardar um endereço obtido em uma sessão e reutilizá-lo depois da troca de tela.
O endereço deve ser obtido durante uma chamada válida do próprio jogo.

## Entrada de comandos

O jogo consulta `NmplDLL.dll` para ler teclado, mouse e controle. Quando o Mod
Menu está aberto, essas consultas são bloqueadas para impedir que o mesmo comando
também seja executado no jogo.

O controle mantém botões, gatilhos e eixos em uma área de `0x270` bytes. Os
campos usados pelo bloqueio são:

| Posição | Conteúdo |
| --- | --- |
| `+0x00..+0x1F` | estado dos botões |
| `+0x40/+0x44` | gatilhos |
| `+0x258..+0x26B` | eixos analógicos |

Ao fechar o menu, o comando de fechamento só é liberado para o jogo depois que o
botão ou a tecla é solto. Isso evita uma ação involuntária na tela que estiver
atrás do menu.

## Renderização

O Mod Menu é desenhado sobre o DirectX 12 pelo proxy `dxgi.dll`. O proxy também
inicia o Mod Loader. A interface apenas envia pedidos ao loader; ela não procura
mods nem carrega plugins por conta própria.
