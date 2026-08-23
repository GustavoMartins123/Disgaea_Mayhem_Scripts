# Item World

Plugin nativo planejado para multiplicar niveis e subjugar Inocentes no Item World.

## Estado real

O codigo atual conhece os layouts de `CItemWorldData` e `CItemStatus`, mas ainda nao possui um hook validado que capture a instancia ativa de `CItemWorldData`. A variavel `g_cached_item_world` nunca era preenchida; por isso a versao anterior podia aparecer como ativa sem alterar o jogo.

Na ABI v1, `Mod_Initialize` retorna falha explicitamente e o loader mostra o erro. Nao use injecao manual para contornar essa validacao.

Os valores planejados permanecem em `config.json`, separados das definicoes e limites de `mod.json`. Isso nao habilita o mod enquanto o hook obrigatorio estiver ausente.

Para concluir o mod e necessario:

1. identificar e validar o construtor/factory ou um metodo estavel que receba `CItemWorldData*`;
2. instalar o hook em `Mod_Initialize` fora de `DllMain`;
3. limpar a referencia ao destruir/sair da sessao;
4. implementar ou remover a opcao `mystery_room_rate`;
5. somente entao permitir `Mod_Enable`.

Consulte `docs/SUBSISTEMA_ITEM_WORLD.md` e `docs/MOD_LOADER_ARQUITETURA.md`.
