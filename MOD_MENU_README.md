# Mod Menu de Disgaea Mayhem

O Main Menu e construido em C++, nao pelos scripts Lua em `data/script`. Esta
implementacao cria o quinto item nativo abaixo de **System**, troca o rotulo
reservado de **Give Up** por **Mods** no atlas e conecta o indice 4 a um Mod
Manager renderizado no backbuffer DirectX 12 do proprio jogo.

## Uso

1. Feche o jogo e execute `INSTALAR_MOD_MENU.bat` uma vez.
2. Abra o jogo pela Steam, carregue o save e mantenha o Main Menu fechado.
3. Execute `INJETAR_MOD_MENU.bat` e aguarde a confirmacao do renderer e dos
   quatro patches.
4. Abra o Main Menu e selecione **Mods**.
5. Mantenha o monitor aberto ate encerrar o jogo.

Enquanto o Mod Manager estiver aberto, a entrada do Main Menu fica bloqueada
sem ocultar seus elementos. **B** ou **Esc** fecha o gerenciador. A entrada do
jogo so e reativada depois que o botao for solto, impedindo que o mesmo comando
tambem feche o Main Menu.

Nao execute o injetor durante a tela inicial: nesta instalacao a Steam encerra
o primeiro processo e cria o processo definitivo do jogo. O injetor encerra
explicitamente se o processo conectado terminar.

O injetor registra o estado nativo de animacao 4 que o jogo normalmente omite
fora dos contextos de **Give Up**. Ele aceita somente o executavel, o atlas e o
DLL conhecidos por SHA-256. Tambem valida as assinaturas nativas antes de
escrever na memoria e opera em modo fechado diante de qualquer divergencia.
As threads enumeradas do jogo ficam suspensas somente durante a escrita ou o
rollback transacional dos patches.

## Estado dos modulos

Nenhum modulo possui ABI nativa registrada. Por isso `mods/registry.json` esta
vazio e o menu informa esse estado, sem toggles ficticios. Um modulo so deve
entrar no registro depois que sua rotina real de ativacao, desativacao e
validacao estiver implementada.

## Arquivos

- `INJETAR_MOD_MENU.py`: valida, injeta o DLL e monitora o estado nativo.
- `INSTALAR_MOD_MENU.py`: valida o pacote e instala o rotulo no FAD de forma
  transacional.
- `mods/native/DisgaeaMayhemModMenu.dll`: renderer DirectX 12 in-process.
- `native/mod_menu_overlay`: fonte, build reproduzivel e dependencias oficiais
  fixadas do Dear ImGui 1.92.6 e MinHook 1.3.4.
- `tools/fad_texture_tool.py`: parser/recompressor estrito NMPLTEX/YKCMP/LZ4.
- `mods/main_menu/mods_slot.dds`: rotulo selecionado e nao selecionado em BC7.

O instalador cria `AnmDat_1_00_EN.fad.mod-menu-original` somente como backup de
rollback local. Esse arquivo e um artefato gerado e nao deve ser versionado.
