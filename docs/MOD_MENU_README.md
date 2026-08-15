# Mod Menu de Disgaea Mayhem

O Main Menu e construido em C++, nao pelos scripts Lua em `data/script`. Esta
implementacao cria o quinto item nativo abaixo de **System**, troca o rotulo
reservado de **Give Up** por **Mods** no atlas e conecta o indice 4 a um Mod
Manager renderizado no backbuffer DirectX 12 do proprio jogo.

## Auto-Inicializacao & Uso

1. Feche o jogo e execute `INSTALAR_MOD_MENU.bat` uma unica vez para aplicar a textura do rotulo no atlas.
2. Abra o jogo pela Steam normalmente e carregue o seu save.
   * O proxy nativo `dxgi.dll` inicializa o Mod Menu e aplica os patches em memoria automaticamente ao iniciar o executavel.
3. Abra o Main Menu no jogo e selecione a opcao **Mods**.
4. Pressione **B** (controle) ou **Esc** (teclado) para fechar o overlay e retornar ao jogo.

Enquanto o Mod Manager estiver aberto, a entrada do Main Menu fica bloqueada
sem ocultar seus elementos. A entrada do jogo so e reativada depois que o botao
for solto, impedindo que o mesmo comando tambem feche o Main Menu.

## Estado dos modulos

Nenhum modulo possui ABI nativa registrada no momento. Por isso `mods/registry.json`
esta vazio e o menu informa esse estado, sem toggles ficticios. Novos sub-mods e trapaças
serao adicionados conforme implementados no registro.

## Arquivos

- `dxgi.dll`: proxy DirectX 12 que auto-inicializa o mod menu e aplica os patches em memoria.
- `INSTALAR_MOD_MENU.py`: valida o pacote e instala o rotulo no FAD de forma transacional.
- `mods/native/DisgaeaMayhemModMenu.dll`: DLL nativa DirectX 12 in-process.
- `native/mod_menu_overlay`: fonte C++, build reproduzivel (`build.ps1`) e dependencias oficiais (Dear ImGui 1.92.6 + MinHook 1.3.4).
- `tools/fad_texture_tool.py`: parser/recompressor estrito NMPLTEX/YKCMP/LZ4.
- `mods/main_menu/mods_slot.dds`: rotulo selecionado e nao selecionado em BC7.

O instalador cria `AnmDat_1_00_EN.fad.mod-menu-original` somente como backup de
rollback local. Esse arquivo e um artefato gerado e nao deve ser versionado.
