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
