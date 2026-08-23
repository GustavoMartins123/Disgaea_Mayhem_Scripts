# Mod Menu de Disgaea Mayhem

O Mod Menu e a interface do Mod Loader, nao o loader. Ele e carregado como system mod obrigatorio de `mods/mod_menu/mod_menu.dll` e recebe a API ABI v1 criada pelo `dxgi.dll`.

## Uso

1. Compile com `native/mod_menu_overlay/build.ps1`.
2. Abra o jogo normalmente.
3. Abra o overlay com `F1`, `Insert` ou `Home`. No controle, use `L3 + R3` ou `Back`.
4. Feche com `Esc` ou `B`.

Enquanto o menu está aberto, teclado, mouse e controle ficam bloqueados para o
jogo. Os comandos voltam ao jogo depois que a tecla ou o botão usado para fechar
o menu é solto. A janela e os painéis se ajustam ao espaço disponível na tela.

Durante o arraste de um slider, o efeito acompanha o valor em memória. A
configuração é gravada quando o controle é solto ou quando o menu é fechado.

O quinto item visual do Main Menu depende de um patch separado no atlas NMPLTEX/YKCMP. O instalador C++ atual apenas valida o pacote e retorna erro explicito porque a escrita do atlas ainda nao esta implementada. Os atalhos sao a entrada canonica do overlay.

O Mod Menu consulta o catalogo, solicita enable/disable, envia opcoes tipadas e pede a execucao de actions. Ele nao descobre pastas, interpreta manifestos, carrega DLLs, grava `config.json`/`enabled.txt` ou escolhe executaveis.

Consulte `docs/MOD_LOADER_ARQUITETURA.md` para o fluxo e a ABI.
