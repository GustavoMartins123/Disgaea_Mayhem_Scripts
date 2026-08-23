# Guia de Integração de Mods

Como escrever um mod novo para o Disgaea Mayhem Mod Loader: o que o loader faz por
você, o que ele não faz, e o que dá para reaproveitar.

Para o desenho interno do loader, ver [MOD_LOADER_ARQUITETURA.md](MOD_LOADER_ARQUITETURA.md).

---

## 1. Divisão de responsabilidades

### O que o loader faz por você

Você **não** precisa escrever nada disso:

| Serviço | Detalhe |
|---|---|
| Descoberta | Varre `mods/*/mod.json`. Pasta sem manifesto é ignorada, sem erro. |
| Validação de manifesto | Schema, tipos, limites, id duplicado, caminho com separador, arquivo declarado ausente. Manifesto inválido é rejeitado com motivo no log; os outros mods seguem carregando. |
| Configuração tipada | Lê `config.json`, exige correspondência **exata** com as `options` do manifesto, valida tipo, intervalo e finitude **antes** de carregar a sua DLL. |
| Ciclo de vida | Carrega a DLL uma vez, chama `Mod_Initialize` → `Mod_SetOption` (uma vez por option) → `Mod_Enable`. |
| Persistência | Grava `config.json` e `enabled.txt` por arquivo temporário + rename atômico, com rollback se a escrita falhar. |
| Ordem de carga | `system` primeiro, depois `load_order`, desempate por `id`. |
| Identidade da build do jogo | Resolve o módulo do jogo e valida o fingerprint PE **uma vez**, entregando o resultado pronto no `DmModHostContext`. |
| Log | `mods/mod_loader.log`, com timestamp, thread e componente. |
| Actions | Executa o `executable` declarado, com timeout de 30 s e checagem de exit code. |

### O que o loader NÃO faz

Isso é responsabilidade do seu mod:

- **Hooks.** Instalar, remover e drenar chamadas em voo. Hoje cada plugin linka a própria cópia do MinHook (a Fase 8 do `TAREFAS.md` centraliza isso).
- **Leitura e escrita da memória do jogo.** Resolver estruturas, validar ponteiros, conferir vtables.
- **UI.** O Mod Menu desenha a partir do manifesto. Você declara `options`; ele renderiza.

### O que o loader proíbe

| Regra | Motivo |
|---|---|
| `DllMain` passivo — só `DisableThreadLibraryCalls` | Hook, thread ou chamada ao host sob o loader lock trava o processo |
| Não se auto-ativar | O estado é do loader; auto-ativação cria duas autoridades |
| Não chamar `LoadLibrary` para outro mod | Só o loader carrega plugins |
| Não escrever `enabled.txt` nem `config.json` | O loader é o dono desses arquivos |
| Não procurar DLL por padrão de nome | O `plugin` do manifesto é o único caminho aceito |

---

## 2. Estrutura de um mod

```text
mods/<id>/
├── mod.json        manifesto (obrigatório)
├── config.json     valores das options (obrigatório para toggle e system)
├── enabled.txt     exatamente "0" ou "1" (obrigatório para toggle e system)
├── <id>.dll        o plugin
└── README.md
```

`mod.json` descreve o que é **imutável**: id, tipo, limites das options.
`config.json` guarda o que **muda**: os valores correntes. Os dois nunca se misturam.

### Manifesto mínimo

```json
{
  "schema_version": 1,
  "id": "meu_mod",
  "name": "Meu Mod",
  "category": "Categoria",
  "version": "1.0.0",
  "author": "Autor",
  "description": "O que o mod faz.",
  "type": "toggle",
  "load_order": 150,
  "plugin": "meu_mod.dll",
  "options": [
    { "id": "intensidade", "name": "Intensidade", "type": "slider_int", "min": 1, "max": 100 }
  ]
}
```

```json
{
  "schema_version": 1,
  "mod_id": "meu_mod",
  "options": { "intensidade": 50 }
}
```

Tipos de option: `toggle`, `slider_int` (exige `min`/`max`), `slider_float` (exige
`min`/`max`). Máximo de 16 por mod.

Tipos de mod:

| `type` | Exige | Comportamento |
|---|---|---|
| `toggle` | `plugin`, `enabled.txt` | Ligado/desligado pelo menu |
| `system` | `plugin`, `required`, `enabled.txt=1` | Carregado primeiro, não desligável |
| `action` | `executable`, `action_label`, `success_status`, `auto_apply` | Executa um `.exe` e reporta o status |

---

## 3. A ABI

Todo plugin `toggle` ou `system` exporta **exatamente** estas seis funções:

```cpp
uint32_t WINAPI Mod_GetAbiVersion();
BOOL     WINAPI Mod_Initialize(const DmModHostContext* context);
BOOL     WINAPI Mod_Enable();
BOOL     WINAPI Mod_Disable();
BOOL     WINAPI Mod_SetOption(const char* id, const DmModValue* value);
void     WINAPI Mod_Shutdown();
```

`Mod_SetOption` só é obrigatório quando o manifesto declara `options`; sem options,
retorne `FALSE`.

### O que chega em `Mod_Initialize`

```cpp
struct DmModHostContext {
    std::uint32_t struct_size;
    std::uint32_t abi_version;
    const DmModLoaderApi* loader;
    const char* game_directory;
    const char* mod_directory;      // a sua pasta; use para arquivos próprios
    std::uintptr_t game_module_base; // base do módulo do jogo, já resolvida
    std::size_t game_module_size;
    BOOL game_build_verified;        // fingerprint PE conferiu
};
```

`game_module_base` e `game_build_verified` existem para você **não** repetir
`GetModuleHandleW(nullptr)` nem carregar a sua própria cópia do timestamp da build.
Quando o jogo receber patch, muda um lugar só: `mod_loader.cpp`.

### O que você pode chamar de volta

```cpp
struct DmModLoaderApi {
    GetModCount / GetMod / GetModById     // inspecionar o estado dos mods
    SetModEnabled                          // ligar/desligar outro mod
    SetModOption                           // aplicar valor (não grava em disco)
    FlushModConfig                         // grava o pendente; nullptr = todos
    ExecuteModAction
    Log                                    // vai para mods/mod_loader.log
};
```

Na prática, um mod comum só usa `Log`. O resto existe para o Mod Menu.

**`SetModOption` não grava em disco.** Ele aplica no plugin e marca pendente; quem
grava é `FlushModConfig`. Isso existe porque um slider arrastado reporta mudança a cada
frame, e gravar com rename síncrono por frame trava a render thread. Se você chamar
`SetModOption` de código próprio, chame `FlushModConfig` ao terminar a edição.

---

## 4. Reaproveitando código: `dm_mod_common.h`

`#include "../../native/mod_loader/dm_mod_common.h"` (que já inclui `mod_loader_api.h`).

É **header-only** — inlinado no seu DLL, sem indireção, sem passar pelo loader.

### Por que header e não um serviço do loader

| | Header (`dm::`) | Serviço (`DmModLoaderApi`) |
|---|---|---|
| Custo | Inlinado, zero indireção | Chamada indireta cruzando DLL |
| Estado | Um por plugin | Um por processo |
| Serve para | Validação em hot path, matemática, guardas | Estado único: config, ciclo de vida, identidade da build |

`dm::IsAccessibleRange` roda milhares de vezes por segundo dentro de hooks. Como serviço
do loader ela ficaria **mais lenta que código duplicado**. Serviço é bom para o raro e
único, ruim para hot path. Use a regra: **hot path sem estado compartilhado → header;
estado único do processo → loader.**

### O que o header oferece

```cpp
// Aceita o contexto e publica a imagem do jogo em dm::g_game_image.
// require_verified_build = true rejeita build diferente da esperada.
bool dm::AcceptHostContext(const DmModHostContext*, bool require_verified_build);

std::uintptr_t dm::Rva(std::uintptr_t rva);   // base do jogo + rva
bool dm::HasVtable(std::uintptr_t obj, std::uintptr_t vtable_rva);
bool dm::MatchesPrologue(std::uintptr_t addr, const std::uint8_t*, std::size_t);

bool dm::IsReadableRange(const void*, std::size_t);
bool dm::IsWritableRange(const void*, std::size_t);
bool dm::IsExecutableAddress(const void*);

dm::CallGuard scope(g_active_calls);           // conta chamadas em voo no hook
void dm::DrainActiveCalls(const std::atomic<LONG>&);  // espera zerar antes de desinstalar

dm::HostLog Log;  Log.Bind(context->loader, "meu_mod");  Log("mensagem");

std::int64_t dm::ScalePositive(valor, multiplicador, escala, maximo);
```

`IsReadableRange`/`IsWritableRange` mantêm um cache `thread_local` da última região
consultada, o que elimina a maioria das `VirtualQuery` em hooks que revalidam o mesmo
objeto. O cache é `thread_local` e não global de propósito: compartilhado entre threads,
uma leitura rasgada de base/tamanho faria a função aceitar um endereço inválido.

---

## 5. Esqueleto

```cpp
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <atomic>
#include <cstring>

#include "../../native/mod_menu_overlay/vendor/minhook/include/MinHook.h"
#include "../../native/mod_loader/dm_mod_common.h"

namespace {

constexpr std::uintptr_t kAlvoRva = 0x00000000;

std::atomic<bool> g_enabled{false};
std::atomic<LONG> g_active_calls{0};
void* g_target = nullptr;
bool g_minhook_initialized = false;
dm::HostLog Log;

void (*g_original)(void*) = nullptr;

void HookAlvo(void* arg) {
    dm::CallGuard scope(g_active_calls);
    if (!g_enabled.load(std::memory_order_acquire)) {
        g_original(arg);
        return;
    }
    g_original(arg);
}

}  // namespace

extern "C" __declspec(dllexport) std::uint32_t WINAPI Mod_GetAbiVersion() {
    return DM_MOD_LOADER_ABI_VERSION;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Initialize(const DmModHostContext* context) {
    if (!dm::AcceptHostContext(context, true)) return FALSE;
    Log.Bind(context->loader, "meu_mod");

    static const std::uint8_t prologo[] = { 0x48, 0x89, 0x5C, 0x24 };
    if (!dm::MatchesPrologue(dm::Rva(kAlvoRva), prologo, sizeof(prologo))) {
        Log("Build rejeitada: rotina alvo nao corresponde.");
        return FALSE;
    }
    g_target = reinterpret_cast<void*>(dm::Rva(kAlvoRva));

    if (MH_Initialize() != MH_OK) return FALSE;
    g_minhook_initialized = true;
    if (MH_CreateHook(g_target, reinterpret_cast<LPVOID>(&HookAlvo),
                      reinterpret_cast<LPVOID*>(&g_original)) != MH_OK ||
        MH_EnableHook(g_target) != MH_OK) {
        MH_RemoveHook(g_target);
        MH_Uninitialize();
        g_minhook_initialized = false;
        return FALSE;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Enable() {
    if (!g_minhook_initialized) return FALSE;
    g_enabled.store(true, std::memory_order_release);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_Disable() {
    g_enabled.store(false, std::memory_order_release);
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL WINAPI Mod_SetOption(const char*, const DmModValue*) {
    return FALSE;
}

extern "C" __declspec(dllexport) void WINAPI Mod_Shutdown() {
    g_enabled.store(false, std::memory_order_release);
    if (g_minhook_initialized) {
        MH_DisableHook(g_target);
        dm::DrainActiveCalls(g_active_calls);
        MH_RemoveHook(g_target);
        MH_Uninitialize();
        g_minhook_initialized = false;
    }
    Log.Reset();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
```

Ao instalar mais de um hook, use `MH_QueueEnableHook` para cada e **um**
`MH_ApplyQueued` no fim: cada `MH_EnableHook` congela todas as threads do processo.

### `Mod_SetOption` com options

```cpp
extern "C" __declspec(dllexport) BOOL WINAPI Mod_SetOption(const char* key, const DmModValue* value) {
    if (key == nullptr || value == nullptr || value->struct_size != sizeof(DmModValue)) return FALSE;
    if (std::strcmp(key, "intensidade") == 0) {
        if (value->type != DmOptionType::SliderInt ||
            value->int_value < 1 || value->int_value > 100) return FALSE;
        g_intensidade.store(value->int_value, std::memory_order_release);
        return TRUE;
    }
    return FALSE;  // chave desconhecida
}
```

O loader já validou tipo e intervalo. Revalide mesmo assim: `FALSE` aqui marca o mod
como falho, o que é melhor que aplicar um valor incoerente.

---

## 6. Build

Adicione ao `native/mod_menu_overlay/build.ps1`, junto dos outros plugins:

```powershell
$meuObject = Compile-CppObject (Join-Path $gameRoot 'mods\meu_mod\meu_mod.cpp') $pluginObjectRoot
$meuOutput = Join-Path $buildRoot 'meu_mod.dll'
& $gxx -shared -static-libgcc -static-libstdc++ -o $meuOutput $meuObject @pluginMinHookObjects -lkernel32
if ($LASTEXITCODE -ne 0) { throw 'Falha ao vincular meu_mod.dll' }
```

E a entrada correspondente em `$deployments`. Rode com o jogo fechado:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/mod_menu_overlay/build.ps1
```

Compila com `-Wall -Wextra -Werror`; warning quebra o build.

---

## 7. Erros que o validador rejeita

| Mensagem | Causa |
|---|---|
| `mod.json ausente ou ilegivel` | Sem manifesto, ou maior que 1 MB |
| `manifesto exige schema_version=1, id, name e type` | Campo obrigatório faltando |
| `id invalido` | Fora de `[A-Za-z0-9_-]` |
| `manifesto exige category, version, author e description explicitos` | Nenhum aceita valor vazio |
| `toggle exige enabled.txt valido (0 ou 1)` | Conteúdo diferente de `0`/`1` (com `\n` ou `\r\n` opcional) |
| `mod residente exige plugin explicito e sem subdiretorios` | `plugin` com `/`, `\` ou `:`, ou sem `.dll` |
| `plugin declarado nao existe` | Caminho não bate com o arquivo |
| `config.json exige schema_version=1, mod_id exato e options` | `mod_id` diferente do `id` do manifesto |
| `option desconhecida em config.json` | Chave que não existe no manifesto |
| `option ausente em config.json` | Option do manifesto sem valor |
| `option inteira fora do intervalo` | Valor fora de `min`/`max` |
| `Plugin nao implementa a ABI nativa obrigatoria` | Falta um dos 6 exports (ou `Mod_SetOption` com options declaradas) |
| `Versao de ABI incompativel` | `Mod_GetAbiVersion` diferente de `DM_MOD_LOADER_ABI_VERSION` |
| `id duplicado rejeitado` | Dois manifestos com o mesmo `id` |

Um manifesto rejeitado não derruba os outros: o loader registra o motivo e segue. A
exceção é o system mod obrigatório — se ele falhar, o bootstrap termina fail-closed.

---

## 8. Checklist

- [ ] `mod.json` com todos os campos e `load_order` livre
- [ ] `config.json` com exatamente as options do manifesto
- [ ] `enabled.txt` com `0` ou `1`
- [ ] Os 6 exports da ABI
- [ ] `DllMain` só com `DisableThreadLibraryCalls`
- [ ] `dm::AcceptHostContext` no início de `Mod_Initialize`
- [ ] Prólogo conferido antes de hookar
- [ ] `dm::CallGuard` em todo detour
- [ ] `dm::DrainActiveCalls` antes de `MH_RemoveHook`
- [ ] `MH_QueueEnableHook` + `MH_ApplyQueued` se houver mais de um hook
- [ ] Entrada no `build.ps1` (compilação e deployment)
- [ ] `build.ps1` passa inteiro
