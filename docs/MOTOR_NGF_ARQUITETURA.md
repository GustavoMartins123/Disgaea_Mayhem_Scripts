# ⚙️ Arquitetura do Motor Ngf (Nippon Ichi Game Framework)

Este documento detalha a arquitetura central e os subsistemas do motor proprietário **Ngf** utilizado em **Disgaea Mayhem (PC / Steam / x64)**.

---

## 📌 1. Visão Geral da Engine

* **Binário:** `Disgaea_Mayhem.exe` (Windows PE x64).
* **Base Address Padrão:** `0x140000000` (com suporte a ASLR).
* **Renderizador Gráfico:** DirectX 12 nativo (`d3d12.dll` / hook via proxy `dxgi.dll`).
* **Compilador Utilizado pelo Jogo:** Microsoft Visual C++ 2019/2022 (MSVC x64) com RTTI habilitado (`.?AV` CompleteObjectLocator).

---

## 🧬 2. Gerenciamento de Memória & Smart Pointers Intrusivos

A engine implementa um sistema proprietário de contagem atômica de referências na namespace `Core::Nmpl`:

* **Tipo:** `Core::Nmpl::intrusive_ptr<T>`
* **Mecanismo:** Cada objeto herdando de `RefCountForIntrusivePtr` armazena um contador atômico no offset `+0x08`.
* **Sincronização:** As operações de incremento e decremento utilizam instruções atômicas com prefixo `lock`:
  ```x86asm
  lock inc DWORD PTR [rcx+0x08]          ; AddRef
  lock xadd DWORD PTR [rcx+0x08], eax    ; Release / Check zero
  ```

---

## 🔄 3. Padrão Arquitetural: Tasks (`CTask`) & Estados (`CState`)

O fluxo do jogo é desacoplado em tarefas assíncronas modulares:

* **`CTask`:** Representa uma tarefa ou subsistema ativo (ex: `CTask_Explore_ItemWorldClear`, `CTask_CharacterWorldGame_Move`).
* **`CState`:** Máquina de estados finitos que gerencia as fases da tarefa (ex: `CState_Main`, `CState_Item`, `CState_Performance`).
* **`CUIUnion`:** Componentes de interface de usuário modulares anexados às tarefas de renderização.

---

## 🎮 4. Camada de Input `Nmpl::Input`

O executável consulta teclado, mouse e controle por exports C++ de
`NmplDLL.dll`. O Mod Menu resolve esses exports pelo nome decorado e rejeita a
inicialização se algum contrato obrigatório não estiver presente.

### Teclado e mouse

- `CKeyboard::press`, `trigger`, `repeat` e `release` são os pontos de consulta
  usados pelo jogo.
- `CMouseWin::isPress`, `isTrigger`, `isRepeat`, `isRelease`, `axisX` e `axisY`
  formam o caminho de consulta do mouse.
- Enquanto o overlay está aberto, os hooks retornam estado neutro. Quando está
  fechado, despacham para a implementação original.

### Controle

`CPad` ocupa `0x270` bytes na build validada. Os offsets relevantes confirmados
na desmontagem de `NmplDLL.dll` são:

| Offset | Tamanho | Conteúdo |
| :--- | :--- | :--- |
| `+0x00..+0x1F` | `0x20` | Máscaras `now`, `trig`, `rept` e `release` |
| `+0x30` | `uint8_t` | Índice usado por `CPad::rawData` |
| `+0x40/+0x44` | `float` | Gatilhos analógicos |
| `+0x258..+0x26B` | `0x14` | Estado dos eixos analógicos |

`CPad::rawData` seleciona um bloco nativo de `0x138` bytes. O hook de
`CNmplInput::update(float)` chama a rotina original e, durante a captura
exclusiva, limpa botões, repetição, gatilhos e eixos dos quatro `CPad`. O overlay
lê o controle separadamente por XInput, portanto continua navegável sem entregar
os mesmos comandos ao jogo.
