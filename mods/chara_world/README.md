# 🎲 Mod: Chara World - Energia Infinita (Disgaea Mayhem)

Este mod congela a **Energia (ENERGY)** dentro do tabuleiro do **Mundo dos Personagens (Chara World / Sugoroku)** em 100/100 ou no valor desejado.

---

## ⚡ Recursos

* **Energia Travada:** Impede o esgotamento de energia ao rolar dados, andar pelos blocos e travar batalhas.
* **Exploração Completa:** Permite visitar 100% dos blocos do mapa, derrotar todos os clones e passar por todas as fontes de bônus de atributos.
* **Hook Residente (`chara_world.dll`):** Roda de forma contínua e em tempo real em segundo plano quando o mod estiver **ATIVADO (ON)** no Mod Menu.
* **Ajuste Dinâmico:** Altere o valor da energia travada (ex: 100, 200, 999) direto pelo slider do Mod Menu.

---

## 📁 Estrutura de Arquivos

```text
mods/chara_world/
├── enabled.txt              # Flag de ativacao (1 = ON, 0 = OFF)
├── mod.json                 # Metadados e opcoes do Mod Menu
├── chara_world.dll          # DLL de hook residente em memoria (C++)
├── chara_world.cpp          # Codigo-fonte C++ nativo
├── APLICAR_MOD_CHARA_WORLD.exe # Injetor standalone C++
├── APLICAR_MOD_CHARA_WORLD.bat # Lancador de 1 clique
└── README.md                # Este documento
```
