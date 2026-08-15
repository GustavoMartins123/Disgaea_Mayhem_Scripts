# Mod Menu de Disgaea Mayhem

O Main Menu e construído em C++, não pelos scripts Lua em `data/script`. Esta
implementação cria o quinto item nativo abaixo de **System**, troca o rótulo
reservado de **Give Up** por **Mods** no atlas e intercepta somente o callback
do índice 4.

## Uso

1. Feche o jogo e execute `INSTALAR_MOD_MENU.bat` uma vez.
2. Abra o jogo pela Steam, carregue o save e mantenha o Main Menu fechado.
3. Execute `INJETAR_MOD_MENU.bat` e aguarde a confirmação dos quatro patches.
4. Abra o Main Menu.
5. Mantenha o monitor aberto até encerrar o jogo.

Não execute o injetor durante a tela inicial: nesta instalação a Steam encerra
o primeiro processo e cria o processo definitivo do jogo. O injetor não tenta
reatar a outra instância; ele encerra explicitamente se o processo conectado
terminar.

O injetor registra explicitamente o estado nativo de animação 4 que o jogo
normalmente omite fora dos contextos de **Give Up**. Ele aceita somente o
executável e o atlas conhecidos por SHA-256, valida as assinaturas nativas antes
de escrever na memória e encerra com
erro explícito diante de qualquer divergência. Todas as threads enumeradas do
jogo ficam suspensas apenas durante a escrita e voltam a executar depois da
verificação, evitando instruções parcialmente alteradas.

O índice 4 originalmente implementa **Give Up** em contextos de batalha. Nesses
contextos, a função continua acessível dentro de Mods pelo botão **Give Up
original**. Depois de acioná-lo, pressione **Confirm** novamente no jogo em até
dez segundos; essa segunda confirmação volta ao handler original.

## Estado dos módulos

O shell do menu está funcional, mas nenhum módulo possui ABI nativa registrada.
Por isso `mods/registry.json` está vazio e o menu informa esse estado, sem
apresentar toggles fictícios. Um módulo só deve entrar no registro depois que
sua rotina real de ativação, desativação e validação estiver implementada.

## Arquivos

- `INJETAR_MOD_MENU.py`: valida, injeta e monitora o callback nativo.
- `INSTALAR_MOD_MENU.py`: instala o rótulo no FAD de forma transacional.
- `tools/fad_texture_tool.py`: parser/recompressor estrito NMPLTEX/YKCMP/LZ4.
- `mods/main_menu/mods_slot.dds`: rótulo selecionado e não selecionado em BC7.

O instalador cria `AnmDat_1_00_EN.fad.mod-menu-original` apenas como backup de
rollback local. Esse arquivo é um artefato gerado e não deve ser versionado.
