# 🎲 Subsistema: Mundo dos Personagens (`Chara World / Sugoroku`)

Este documento consolida a engenharia reversa completa do subsistema do **Mundo dos Personagens (Chara World / Tabuleiro Sugoroku)** em `Disgaea_Mayhem.exe`.

---

## 🎲 1. Estruturas Principais e VTables

| Classe | VTable VA | VTable RVA | Descrição |
| :--- | :--- | :--- | :--- |
| `CCharacterWorldInformation` | `0x140A57610` | `0xA57610` | Gerenciador da sessão de jogo e regras de dados/energia do Chara World |
| `CCharacterWorldBonus` | `0x140A57620` | `0xA57620` | Tabela de bônus de atributos acumulados na partida |
| `CCharacterWorldBattleInformation` | `0x140A57630` | `0xA57630` | Gerenciador de combates de clones no Chara World |
| `CUIUnion_CharacterWorld_Energy` | `0x140A71728` | `0xA71728` | Widget de UI que renderiza a barra e texto de Energia |
| `CUIUnion_CharacterWorldBattle_Energy`| `0x140A710F8` | `0xA710F8` | Widget de exibição de energia durante batalhas |
| `CTask_CharacterWorldGame_Move` | `0x140A53D88` | `0xA53D88` | Tarefa de movimentação e passos do personagem |
| `CTask_CharacterWorldGame_TurnStart` | `0x140A52E18` | `0xA52E18` | Tarefa de início de turno e decremento de energia |

---

## 🧠 2. Layout de Memória do `CCharacterWorldInformation`

O objeto `CCharacterWorldInformation` é alocado na Heap (`0x27D...`) ao entrar no Chara World e contém os dados mestres do jogo:

| Offset (Hex) | Tipo | Descrição |
| :--- | :--- | :--- |
| `+0x00` | `void*` | Ponteiro para VTable `0x140A57610` (RVA `0xA57610`) |
| `+0x08` | `uint32_t` | Contador Atômico de Referências (`intrusive_ptr`) |
| `+0x50` | `int32_t` | Quantidade de Dados Disponíveis |
| `+0x174` | `int32_t` | Energia Máxima do Tabuleiro (Padrão: 100) |
| `+0x178` | `int32_t` | **Energia Atual do Jogador (Master Logic Energy / Turnos Restantes)** |
| `+0x1A0` | `int32_t` | Total de Passos Efetuados na Partida |
| `+0x1B8` | `int32_t` | Multiplicador de Recompensas do Tabuleiro |

---

## 🖥️ 3. Layout de Memória do `CUIUnion_CharacterWorld_Energy`

O widget de interface gerencia a animação e interpolação da barra de energia:

| Offset (Hex) | Tipo | Descrição |
| :--- | :--- | :--- |
| `+0x00` | `void*` | Ponteiro para VTable `0x140A71728` (RVA `0xA71728`) |
| `+0x70` | `int32_t` | Energia Atual de Exibição (`0x27D6476F810`) |
| `+0x74` | `int32_t` | Energia Máxima de Exibição (`100`) |
| `+0x78` | `int32_t` | Valor da Barra de Progresso (`0x27D6476F818`) |
| `+0x7C` | `int32_t` | Valor Alvo da Transição de Energia (`0x27D6476F81C`) |
| `+0x80` | `int32_t` | Valor de Texto Formatado da Energia (`0x27D6476F820`) |

---

## 🔍 4. Desmontagem dos Checks de Energia em `Disgaea_Mayhem.exe`

A engine realiza verificações de thresholds de energia nas seguintes instruções:

```x86asm
0x140461E8D: 83 B8 78 01 00 00 0A    cmp DWORD PTR [rax+0x178], 10   ; Alerta de Energia Baixa (<= 10)
0x140461EA7: 83 B8 78 01 00 00 05    cmp DWORD PTR [rax+0x178], 5    ; Alerta Crítico (<= 5)
0x140461EC7: 83 B8 78 01 00 00 02    cmp DWORD PTR [rax+0x178], 2    ; Alerta Final (<= 2)
```

---

## ⚡ 5. Implementação do Mod (`chara_world.dll`)

* **Mecanismo:** Hook residente em segundo plano que localiza as instâncias ativas de `CCharacterWorldInformation` (VTable `0x140A57610`) e `CUIUnion_CharacterWorld_Energy` (VTable `0x140A71728`) via varredura por assinatura de VTable na RAM.
* **Trava:** Mantém a energia do jogador congelada em 100/100 (ou no valor configurado pelo slider do menu) a cada 80ms, permitindo turnos e passos infinitos.

### Construtor validado

- `CCharacterWorldInformation::CCharacterWorldInformation`: RVA `0x004501D0` (VA `0x1404501D0`).
- Prólogo esperado: `48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57`.
- A escrita da VTable `0x140A57610` ocorre em `0x1404501FF`.
- Assinatura observada no ABI x64: `void* (this, constructor_arg, allocator_arg)`, em `RCX`, `RDX` e `R8`.

O plugin valida o prólogo antes de instalar MinHook e confirma a VTable do objeto retornado antes de armazenar a instância. Uma build divergente é rejeitada.
