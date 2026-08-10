# 🎮 Disgaea Mayhem - Unlocker de DLCs, Microtransações e Boost Tickets 900% Ilimitados

Repositório completo de automação, documentação de engenharia reversa e ferramentas para desbloqueio offline de conteúdos pagos, pacotes de itens e **Boost Tickets 900% ilimitados** no jogo **Disgaea Mayhem (PC / Steam)**.

---

## 📌 1. Visão Geral do Mod

No *Disgaea Mayhem*, a desenvolvedora Nippon Ichi Software dividiu o conteúdo extra em duas categorias:
1. **DLCs Tradicionais da Loja Steam (AppIDs estáticos):**
   * *Fun Weapons Pack* (AppID: `4687210`)
   * *Bonus Story: Catastrophe Flan Arc* (AppID: `4687220`)
2. **Microtransações de Inventário Dinâmico da Steam (`ISteamInventory`):**
   * *Boost Ticket 900%* (ItemDef: `5`, 30 unidades, multiplicador 9x)
   * *Boost Ticket 400%* (ItemDef: `4`, 30 unidades, multiplicador 4x)
   * *Boost Ticket 100%* (ItemDef: `3`, 30 unidades, multiplicador 1x)
   * *HL Bag* (ItemDef: `1`, 1.000.000 HL)
   * *Mana Bag* (ItemDef: `2`, 100.000 Mana)

### O Problema Original:
* Unlockers comuns (como CreamAPI padrão) liberam apenas os AppIDs estáticos, deixando os Boost Tickets e Sacos de Dinheiro bloqueados ou gerando o erro *"Failed to retrieve information. Please check your network connection."* ao tentar resgatar no NPC **Carlbunch** (*DLC Special Content Shop*), pois o jogo tentava queimar o item com dinheiro real via `ISteamInventory::ConsumeItem()`.

### A Solução Aplicada:
1. **SmokeAPI Wrapper (`steam_api64.dll`):** Substitui a camada da Steamworks para emular `ISteamApps` e `ISteamInventory`.
2. **Conversão de Banco de Dados (`DLC_information.dat`):** Converte todos os itens consumíveis de *Tipo 2 (Microtransação Online)* para *Tipo 1 (DLC Offline)* mantendo rigorosamente os **1.879 bytes originais** (evitando crashes de desalinhamento de memória do motor da NIS).
3. **Multiplicadores Globais de 900% (`DLC_BoostTicket.dat`):** Atualiza todos os tiers de tickets para multiplicador 9x (+900% EXP e Mana).
4. **Resgates Infinitos (`DLC_delivery.dat`):** Configura a flag de repetição (`Repeat = 1`), permitindo que você resgate o pacote quantas vezes quiser no jogo.

---

## 📂 2. Estrutura de Pastas e Localização dos Arquivos

### No Repositório (`C:\Disgaea_Mayhem_Scripts`):
```text
C:\Disgaea_Mayhem_Scripts\
├── INSTALAR_MOD.bat           # Executável de 1 clique para instalar/reaplicar o mod completo
├── INSTALAR_MOD.py            # Script Python universal de detecção e instalação
├── README.md                  # Documentação completa e guia de uso
├── .gitignore                 # Arquivo de exclusão para versionamento Git
│
├── database_mods/             # Tabelas modificadas prontas para o motor da NIS
│   ├── DLC_information.dat    # Converte consumíveis para Tipo 1 (Resgate offline sem erro)
│   ├── DLC_BoostTicket.dat    # Configura multiplicadores de todos os tickets para 900%
│   └── DLC_delivery.dat       # Configura resgates repetíveis/ilimitados no NPC
│
├── SmokeAPI/                  # Emulador Steamworks com suporte a inventário
│   ├── smoke_api64.dll        # DLL principal do SmokeAPI (x64)
│   ├── smoke_api32.dll        # DLL SmokeAPI (x86)
│   ├── SmokeAPI.config.json   # Configuração JSON com injeção dos itens [1, 2, 3, 4, 5]
│   └── README.txt             # Notas originais do desenvolvedor
│
└── tools/                     # Scripts auxiliares e ferramentas de memória
    ├── APLICAR_MOD_DLC.bat    # Script de reaplicação rápida na pasta do jogo
    ├── APLICAR_MOD_DLC.py     # Script Python de patch
    ├── INJETAR_TICKETS.bat    # Injetor direto em memória RAM da sessão ativa
    ├── inject_tickets.py      # Código em Python ctypes para injeção em tempo real
    └── INSTRUCOES_RESGATES.txt# Instruções rápidas em formato TXT
```

### Localização no Jogo Instalado (`E:\Steam\steamapps\common\Disgaea Mayhem\`):
```text
E:\Steam\steamapps\common\Disgaea Mayhem\
├── Disgaea_Mayhem.exe         # Executável principal do jogo
├── steam_api64.dll            # SmokeAPI (Substitui o wrapper original)
├── steam_api64_o.dll          # DLL original da Steam da Valve (Backup de segurança)
├── SmokeAPI.config.json       # Arquivo de configuração do SmokeAPI
├── APLICAR_MOD_DLC.bat        # Ferramenta de reaplicação
│
└── data\
    └── database\              # Bancos de dados binários (.dat)
        ├── DLC_information.dat# Tabela de verificação de propriedade das DLCs
        ├── DLC_BoostTicket.dat# Tabela de multiplicadores de Boost Tickets
        └── DLC_delivery.dat   # Tabela de regras de entrega e resgate no NPC
```

---

## 🚀 3. Como Reaplicar o Mod Caso o Jogo Seja Reinstalado

Se você formatar o computador, reinstalar o jogo ou a Steam atualizar os arquivos:

1. **Abra o Repositório / Pasta:** `C:\Disgaea_Mayhem_Scripts\`
2. **Dê um duplo clique em:** `INSTALAR_MOD.bat`
3. O script irá:
   * Localizar automaticamente a pasta de instalação da Steam.
   * Criar o backup `steam_api64_o.dll`.
   * Instalar o `steam_api64.dll` (SmokeAPI) e o `SmokeAPI.config.json`.
   * Aplicar as tabelas modificadas em `data/database/`.
4. Pronto! O mod estará ativo novamente em menos de 2 segundos.

---

## 🎮 4. Como Usar e Fazer Novos Resgates no Jogo

1. Abra o jogo normalmente pela Steam.
2. Na base principal, fale com o NPC **Carlbunch** (*DLC Special Content Shop* - o coelhinho com o balão "DLC").
3. Entre na opção **Apply DLC** (Resgatar DLC).
4. Clique no item desejado:
   * **Boost Ticket 900%:** Adiciona 30 unidades de tickets 900% à sua conta.
   * **Boost Ticket 400%:** Adiciona 30 unidades de tickets 400% (com efeito turbinado para 900%).
   * **Boost Ticket 100%:** Adiciona 30 unidades de tickets 100% (com efeito turbinado para 900%).
   * **HL Bag:** Adiciona 1.000.000 de HL.
   * **Mana Bag:** Adiciona 100.000 de Mana.
   * **Fun Weapons Pack:** Armas únicas e especiais.
   * **Catastrophe Flan Arc:** Episódio e história especial.
5. **Para Ativar os Tickets nas Batalhas:**
   * Abra o menu do jogo -> **Settings (Configurações)**.
   * Defina **`Use Boost Tickets`** *(Usar tickets de aumento)* para **`[On]`**.
   * Ao vencer as fases, o jogo multiplicará automaticamente todo o ganho de EXP e Mana por **+900%**!
6. **Resgates Ilimitados:** Sempre que seus tickets acabarem, basta voltar ao Carlbunch e resgatar mais pacotes quantas vezes quiser!
