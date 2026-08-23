# Disgaea Mayhem - Planejamento de mods

Documentação de planejamento para desenvolvimento de mods customizados no *Disgaea Mayhem*.

---

## 🛠️ Mod Base / Core Framework: In-Game Mod Menu (Em Desenvolvimento)
* **Objetivo:** Adicionar uma opção nativa **`Mods`** dentro do **Main Menu** (logo abaixo de `System`), funcionando como um Mod Loader / Gerenciador de Mods in-game (estilo UE4SS / ImGui / ScriptLoader), permitindo ativar, configurar e alternar sub-mods em tempo real sem sair do jogo.

---

## Item World

### 1. Multiplicador de nível do item

Implementado. O jogo usa pontos ganhos por derrotas, e não uma quantidade fixa
de níveis por andar. O mod multiplica esses pontos antes da aplicação do nível.

### 2. Multiplicador de Item Points

Implementado separadamente do nível do item.

### 3. Chance de drop

Pendente. O cálculo específico do Item World ainda precisa ser separado da taxa
geral de drops do jogo.

### 4. Salas misteriosas

Pendente. Os dados de sala e de ondas foram encontrados, mas a escolha da sala
ainda não foi confirmada.

### 5. Innocents

Pendente. A classe e os bancos de dados foram encontrados. Ainda falta confirmar
o ponto que registra derrota, ganho e subjugação.

---

## ⚖️ Mods de Balanceamento e Personagens

### 6. Evilities e Habilidades Sem Restrições
* **Descrição:** Remover restrições de classe e permitir equipar qualquer Evility ou habilidade única de chefes/personagens lendários em qualquer unidade do seu time.
* **Impacto:** Criação de builds extremamente criativas e customizadas.

### 7. Alcance e Formato de Magias / Ataques em Área (AoE)
* **Descrição:** Expandir o alcance e o formato das áreas de efeito de magias e skills especiais de armas para cobrir mapas inteiros.
* **Impacto:** Limpeza de telas inteiras em batalhas de farming.

### 8. Cheat Shop Ilimitado & Overhaul de Dificuldade
* **Descrição:** Expandir o limite do NPC de Trapaças (*Cheat Shop*) para permitir valores de até **+5.000%** de EXP, Mana, HL e Drop Rate, além de opções avançadas de estrelas para balanceamento de inimigos.
* **Impacto:** Controle total da curva de evolução pelo menu nativo do jogo.

---

## ⚡ Mods de Qualidade de Vida (QoL)

### 9. Dark Assembly com 100% de Aprovação
* **Descrição:** Todas as propostas e desejos na Assembleia Sombria passam com aprovação automática de 100% dos senadores, sem necessidade de suborno com itens ou lutas.
* **Impacto:** Agilidade total para aprovar leis e desbloquear novos recursos.

### 10. Velocidade de Movimento 2x / 3x na Base
* **Descrição:** Aumentar a velocidade base de corrida do líder dentro do castelo/vila para travessia instantânea entre instalações.
* **Impacto:** Redução do tempo de deslocamento na base.

### 11. Desbloqueio Total de Paletas de Cores & Skins
* **Descrição:** Liberar todas as variações de cores e aparências alternativas de heróis e classes recrutáveis desde o início do jogo.
* **Impacto:** Customização visual completa dos seus personagens.
