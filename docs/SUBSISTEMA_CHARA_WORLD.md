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
| `+0x170` | subobjeto | Valor limitado de energia; começa pela VTable `0x140A1A7B0` |
| `+0x174` | — | Metade superior do ponteiro da VTable em `+0x170`; **não é um campo gravável** |
| `+0x178` | `int32_t` | **Energia atual do jogador (turnos restantes)** |
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

O limite superior não fica em `+0x174`. O terceiro slot da VTable do subobjeto
de energia aponta para `0x1401A0120`, cuja implementação retorna `100`. A rotina
de resolução de turno começa em RVA `0x00461CA0`; nela:

```x86asm
0x140461DF3: mov rdi, [rsi+0x200]  ; CCharacterWorldInformation
0x140461DFA: add rdi, 0x170        ; subobjeto de energia
0x140461E01: mov eax, [rsi+0x298]  ; variação do turno
0x140461E07: add [rdi+0x08], eax   ; altera CCharacterWorldInformation+0x178
```

Depois da soma, a própria engine limita o valor pelos métodos virtuais do
subobjeto.

---

## ⚡ 5. Implementação do Mod (`chara_world.dll`)

* **Mecanismo:** MinHook síncrono na resolução de turno, RVA `0x00461CA0`.
* **Trava:** valida `task+0x200`, a VTable de `CCharacterWorldInformation` e a
  VTable do subobjeto em `+0x170`; então escreve exclusivamente em `+0x178`
  antes e depois da rotina nativa.
* **Ciclo de vida:** não existe worker, polling, varredura de heap nem ponteiro de
  instância em cache.
* **Compatibilidade:** fingerprint PE e prólogo exato são obrigatórios. Uma build
  divergente é rejeitada.

### Rotina validada

- Resolução de turno: RVA `0x00461CA0` (VA preferencial `0x140461CA0`).
- Prólogo esperado: `48 8B C4 48 89 58 10 48 89 70 18 55 57 41 54 41`.
- Assinatura observada no ABI x64: `bool (task)`, com `task` em `RCX`.

### Causa do crash da implementação anterior

A implementação anterior escrevia `100` em `+0x174` supondo que o campo fosse
"energia máxima". Isso substituía os quatro bytes superiores da VTable
`0x0000000140A1A7B0` por `0x00000064`, produzindo o ponteiro inválido
`0x0000006440A1A7B0`. O próximo chamado virtual do subobjeto causava o crash ao
entrar no Chara World. O worker ainda mantinha um ponteiro sem propriedade para
uma instância que podia ser destruída durante a troca de mapa. Ambos os caminhos
foram removidos.
