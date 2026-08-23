# Arquitetura do Mod Loader

## Diagnostico do desenho anterior

O arquivo `native/mod_menu_overlay/mod_menu_overlay.cpp` reunia seis responsabilidades em uma unica DLL: proxy de `dxgi.dll`, bootstrap, descoberta de manifestos, ciclo de vida das DLLs, actions externas e a interface DirectX 12/ImGui.

O build copiava o mesmo binario para `dxgi.dll` e para `mods/native/DisgaeaMayhemModMenu.dll`. Ao mesmo tempo, `item_world.dll` e `chara_world.dll` liam `enabled.txt`, criavam threads e ativavam o proprio mod dentro de `DllMain`. Depois, o Mod Menu carregava a DLL novamente e chamava `Mod_Enable`. Esse fluxo tinha duas autoridades de estado e permitia inicializacao duplicada sob o loader lock do Windows.

Outros problemas confirmados:

- atualizar a lista repetia a inicializacao dos mods ativos;
- a DLL podia ser escolhida por busca de `*.dll`, sem `plugin` canonico;
- a ABI aceitava `start_mod`/`uninstall_mod` silenciosamente;
- actions eram escolhidas por busca de nomes e extensoes;
- o caminho Lua chamava um `lua.exe` externo ausente;
- pastas sem `mod.json` viravam mods implicitos;
- uma falha de ativacao podia deixar `enabled.txt` marcado como ativo.

## Componentes atuais

```text
Disgaea_Mayhem.exe
  -> dxgi.dll
       proxy DXGI + Mod Loader
       -> valida mods/*/mod.json
       -> carrega primeiro type=system
            -> mods/mod_menu/mod_menu.dll
                 hooks DX12 + ImGui; somente UI
       -> carrega uma vez os toggles habilitados
       -> chama actions com executable explicito
```

`dxgi.dll` e infraestrutura residente. Ele nao e um mod e nao contem UI. O Mod Menu e um plugin `type: "system"`, obrigatorio e carregado em ordem zero. Ele recebe uma tabela de funcoes do loader e nunca chama `LoadLibrary`, procura DLLs, grava `enabled.txt` ou executa processos diretamente.

## Bootstrap canonico

1. O jogo chama uma exportacao do proxy DXGI.
2. O proxy resolve `%SystemRoot%/System32/dxgi.dll` e inicia uma unica worker por `InitOnceExecuteOnce`.
3. A worker resolve a pasta do jogo por `GetModuleFileNameW(nullptr, ...)`.
4. Cada subpasta com `mod.json` e validada. Pastas sem manifesto sao ignoradas.
5. IDs duplicados, caminhos com separadores, arquivo ausente, schema, configuracao ou tipo desconhecido sao rejeitados.
6. O system mod obrigatorio e carregado primeiro. Se falhar, o bootstrap termina fail-closed.
7. Toggles com `enabled.txt=1` sao inicializados pela ABI v1 e permanecem residentes.
8. Actions usam somente o campo `executable`.

Plugins nao sao descarregados durante a sessao. `Mod_Disable` remove o efeito funcional, para workers e desabilita hooks, mas a DLL continua residente. Isso evita callbacks, TLS e threads apontando para codigo descarregado.

## ABI nativa v1

Todo plugin `toggle` ou `system` exporta exatamente:

```cpp
uint32_t WINAPI Mod_GetAbiVersion();
BOOL WINAPI Mod_Initialize(const DmModHostContext* context);
BOOL WINAPI Mod_Enable();
BOOL WINAPI Mod_Disable();
BOOL WINAPI Mod_SetOption(const char* id, const DmModValue* value);
void WINAPI Mod_Shutdown();
```

`Mod_SetOption` e obrigatorio quando o manifesto declara opcoes. `DllMain` deve ser passivo: hooks, threads e chamadas ao host pertencem a `Mod_Initialize`/`Mod_Enable`.

A ABI esta em `native/mod_loader/mod_loader_api.h`. Estruturas incluem `struct_size` e `abi_version`; divergencias sao rejeitadas explicitamente.

## Manifesto canonico

Cada mod possui somente `mods/<diretorio>/mod.json`, com `schema_version: 1`.

- `toggle`: exige `plugin` e `enabled.txt` contendo exatamente `0` ou `1`.
- `system`: exige `plugin`, `required` e `enabled.txt=1`.
- `action`: exige `executable` e `action_label`.

Nao existe registro agregado, descoberta de qualquer DLL, nome inferido de executavel ou ABI alternativa.

## Configuracao persistente

`mod.json` descreve opcoes imutaveis: `id`, nome, tipo e limites. Os valores nao ficam no manifesto. Todo plugin `toggle` ou `system` exige `mods/<id>/config.json` com esta estrutura:

```json
{
  "schema_version": 1,
  "mod_id": "exemplo",
  "options": {
    "opcao_inteira": 100,
    "opcao_toggle": true
  }
}
```

O loader exige correspondencia exata com o manifesto: nenhuma option pode faltar, sobrar ou aparecer duplicada. Tipos, intervalos e numeros finitos sao validados antes de carregar a DLL. Em runtime, a configuracao validada fica em RAM. Alteracoes sao persistidas por `config.json.tmp` e `MoveFileExW`; se a gravacao falhar, o valor anterior e reaplicado ao plugin. Se o rollback tambem falhar, o mod e desativado e marcado como falho.

## Estado atual dos mods

- `mod_menu`: system mod ABI v1; nao gerencia DLLs.
- `chara_world`: corrige a energia ao preparar a sessão e ao resolver ações do tabuleiro; escreve somente no valor atual em `+0x178`.
- `safe_backup`: uma unica worker, evento de parada e backups dentro da pasta do mod.
- `item_world`: multiplica separadamente os pontos de nível e os Item Points. Os demais resultados continuam sob controle do jogo.
- `cheat_shop`: action nativa estrita que valida o schema e expande atomicamente os cinco limites de porcentagem para 5000.
- `dark_assembly` e `dlc_unlocker`: actions nativas com executavel declarado.

Os arquivos `data/script/*.lub` sao artefatos Lua compilados da engine. Nao existem fontes `.lua` de mods neste repositorio e o loader nao anuncia um runtime Lua inexistente.

## Build e validacao

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/mod_menu_overlay/build.ps1
```

O build implanta separadamente loader, Mod Menu e plugins ABI v1. Ao final, `mod_loader_validate.exe` valida schema, arquivos e exports sem executar actions. Depois, um smoke test cria uma raiz isolada apenas com o proxy e o system mod, chama `CreateDXGIFactory1` e confirma no log que os hooks e o bootstrap foram concluidos.
