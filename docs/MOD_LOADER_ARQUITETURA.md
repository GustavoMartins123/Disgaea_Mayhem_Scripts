# Arquitetura do Mod Loader

Este documento descreve a arquitetura atual do Mod Loader e serve como referência para quem quiser entender ou desenvolver plugins para o projeto.

Para instalar e usar os mods, não é necessário conhecer estes detalhes. Consulte o `README.md` da raiz para uma visão geral do pacote.

## Visão geral

```text
Disgaea_Mayhem.exe
  -> dxgi.dll
       proxy DXGI + Mod Loader
       -> descobre mods/*/mod.json
       -> carrega primeiro os system mods
            -> mods/mod_menu/mod_menu.dll
                 DirectX 12 + ImGui
       -> inicializa os plugins habilitados
       -> fornece configuração, logging e serviços de hook
```

`dxgi.dll` é a infraestrutura principal do loader. Ele não contém a interface do Mod Menu nem a lógica específica dos mods de gameplay.

O Mod Menu é um plugin obrigatório do tipo `system`. Os demais recursos ficam em plugins separados dentro de `mods/<id>/`.

## Estrutura de um plugin

Um plugin residente normalmente possui:

```text
mods/<id>/
|-- mod.json
|-- config.json
|-- enabled.txt
|-- <plugin>.dll
`-- README.md
```

- `mod.json`: descreve o mod, sua DLL e as opções disponíveis;
- `config.json`: guarda os valores escolhidos para as opções;
- `enabled.txt`: guarda o estado ligado/desligado;
- DLL: implementação nativa do plugin;
- `README.md`: documentação do mod para o usuário.

O loader descobre os mods pelos manifestos `mod.json`. Não existe um registro global de mods e o nome da DLL não é inferido: o arquivo usado por cada plugin é declarado explicitamente no manifesto.

## Inicialização

Quando o jogo carrega `dxgi.dll`, o proxy encaminha as funções DXGI necessárias para a biblioteca original do Windows e inicializa o Mod Loader.

O loader então:

1. encontra a pasta do jogo;
2. procura as subpastas de `mods/` que possuem `mod.json`;
3. valida os manifestos e as configurações;
4. carrega o Mod Menu obrigatório;
5. inicializa os demais plugins residentes;
6. ativa aqueles que estão marcados como habilitados.

Uma falha em um mod comum não deve impedir os outros de serem carregados. Uma falha no Mod Menu obrigatório impede a inicialização normal do conjunto.

As DLLs permanecem carregadas durante a sessão. Desativar um mod remove seu efeito funcional e desabilita seus hooks ou workers, mas não descarrega a DLL no meio da execução do jogo.

## ABI nativa v2

Plugins residentes usam a ABI definida em `native/mod_loader/mod_loader_api.h`.

Cada plugin exporta:

```cpp
uint32_t WINAPI Mod_GetAbiVersion();
BOOL WINAPI Mod_Initialize(const DmModHostContext* context);
BOOL WINAPI Mod_Enable();
BOOL WINAPI Mod_Disable();
BOOL WINAPI Mod_SetOption(const char* id, const DmModValue* value);
void WINAPI Mod_Shutdown();
```

`Mod_Initialize` recebe o contexto do loader e prepara o plugin. `Mod_Enable` e `Mod_Disable` controlam o efeito durante a sessão. `Mod_SetOption` recebe alterações feitas pelo Mod Menu e `Mod_Shutdown` encerra o plugin de forma limpa.

Inicialização pesada não deve ser feita em `DllMain`.

## Manifestos

Os manifestos usam `schema_version: 1` e descrevem informações como:

- identificador e nome do mod;
- categoria, versão, autor e descrição;
- tipo do mod;
- ordem de carregamento;
- DLL ou executável correspondente;
- opções exibidas no Mod Menu.

Os tipos atualmente suportados são:

- `toggle`: plugin que pode ser ligado e desligado;
- `system`: plugin obrigatório de infraestrutura ou interface;
- `action`: ação executável disparada pelo loader.

Manifestos inválidos, arquivos ausentes e configurações incompatíveis são rejeitados antes da ativação do mod.

## Configuração

`mod.json` define quais opções existem e seus limites. `config.json` guarda somente os valores escolhidos pelo usuário.

Exemplo:

```json
{
  "schema_version": 1,
  "mod_id": "exemplo",
  "options": {
    "multiplicador": 5.0,
    "ativar_recurso": true
  }
}
```

O loader valida tipos e intervalos antes de entregar os valores ao plugin.

Durante o uso de sliders, o novo valor pode ser aplicado imediatamente em memória. A configuração é persistida quando a edição termina ou quando o menu é fechado. Se a gravação falhar, o loader tenta restaurar o último valor persistido.

## Hooks

O Mod Loader mantém uma única instância do MinHook para o processo e fornece os serviços de hook aos plugins pela ABI.

Isso evita que cada mod inicialize uma cópia independente do MinHook e permite ao loader controlar conflitos entre plugins que tentem usar o mesmo endereço.

Funções auxiliares compartilhadas pelos plugins ficam em `native/mod_loader/dm_mod_common.h`.

## Plugins atuais

- `mod_menu`: interface DirectX 12/ImGui e entrada de teclado, mouse e controle;
- `chara_world`: trava de energia e multiplicador experimental dos atributos recebidos nos tiles;
- `item_world`: multiplicadores de progresso de nível e Item Points, além da raridade mínima experimental;
- `cheat_shop`: mantém os cinco valores principais da Cheat Shop em 5000% enquanto ativo;
- `dark_assembly`: garante aprovação das propostas durante a votação;
- `dlc_unlocker`: trabalha com as entradas injetadas pelo SmokeAPI para tornar reutilizáveis cinco consumíveis;
- `tactical_ai`: ajustes separados da IA de inimigos e parceiros;
- `safe_backup`: cria e rotaciona backups dos saves conforme `max_backups`.

As limitações funcionais de cada mod ficam no respectivo `mods/<id>/README.md`. Em particular, a raridade do Item World e o multiplicador de atributos do Chara World ainda são recursos experimentais.

## Dados internos do jogo

Os documentos `docs/SUBSISTEMA_*.md` registram informações encontradas durante a investigação do executável, incluindo estruturas, offsets, RVAs e nomes de arquivos `.dat` usados pelo próprio jogo.

Essas referências ajudam a explicar como os sistemas do Disgaea Mayhem foram identificados. **A presença de um `.dat` na documentação não significa que o mod edite esse arquivo.** Os plugins de gameplay atuais aplicam seus efeitos em memória, salvo quando a documentação de um recurso disser explicitamente o contrário.

## Build e validação

Com o jogo fechado:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/mod_menu_overlay/build.ps1
```

O processo de build compila o loader, o Mod Menu, os plugins, o validador e o instalador. Antes de gerar o pacote final, são feitas validações dos manifestos, arquivos e exports esperados.

O pacote final é gerado como:

`Disgaea_Mayhem_Mod_Loader_Nexus.zip`

Para um guia específico de criação de novos plugins, consulte `docs/GUIA_INTEGRACAO_MODS.md`.