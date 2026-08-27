# Instalação em outro computador

Este guia explica como instalar o pacote pronto do Disgaea Mayhem Mod Loader em outro computador. Você **não precisa compilar o projeto** para usar os mods.

## Requisitos

- Disgaea Mayhem x64 instalado pela Steam;
- Windows compatível com o jogo;
- o jogo fechado durante a instalação ou atualização.

## Instalação do pacote pronto

1. Extraia `Disgaea_Mayhem_Mod_Loader_Nexus.zip` em uma pasta normal.
2. Feche o Disgaea Mayhem caso esteja aberto.
3. Execute `INSTALAR_MOD.exe`.
4. Aguarde a cópia e a validação dos arquivos.
5. Inicie o jogo normalmente.

O instalador tenta localizar automaticamente a instalação do jogo.

Se mais de uma instalação for encontrada, informe manualmente a pasta que contém `Disgaea_Mayhem.exe`:

```text
INSTALAR_MOD.exe "C:\caminho\para\pasta-do-jogo"
```

## Atualizações

Você pode instalar uma versão nova do pacote sobre uma instalação existente.

O instalador preserva:

- `config.json`, que contém as opções escolhidas;
- `enabled.txt`, que contém o estado ligado/desligado dos mods.

Em uma instalação nova, apenas o Mod Menu começa ativado.

Se ocorrer uma falha durante a cópia ou validação, o instalador tenta restaurar os arquivos anteriores em vez de deixar uma instalação parcialmente atualizada.

## Abrindo o Mod Menu

Durante o jogo, use:

- `F1`, `Insert` ou `Home` no teclado;
- `L3 + R3` ou `Back` no controle.

Os mods podem ser ativados, desativados e configurados pelo menu.

## Arquivos instalados

A instalação possui, de forma simplificada:

```text
<pasta-do-jogo>/
|-- Disgaea_Mayhem.exe
|-- dxgi.dll
|-- SmokeAPI.config.json
|-- mods/
|   |-- mod_menu/
|   |-- chara_world/
|   |-- item_world/
|   |-- cheat_shop/
|   |-- dark_assembly/
|   |-- dlc_unlocker/
|   |-- tactical_ai/
|   `-- safe_backup/
`-- tools/
    `-- mod_loader_validate.exe
```

`dxgi.dll` é o Mod Loader. Cada pasta dentro de `mods/` contém um plugin e seus arquivos de configuração. `mod_menu` é o plugin responsável pela interface dentro do jogo.

## SmokeAPI e DLC Unlocker

O DLC Unlocker utiliza o SmokeAPI incluído no pacote para disponibilizar cinco consumíveis na interface do jogo:

- HL Bag;
- Mana Bag;
- Boost Ticket 100%;
- Boost Ticket 400%;
- Boost Ticket 900%.

O instalador mantém a biblioteca Steam original como `steam_api64_o.dll` para que o SmokeAPI possa encaminhar as chamadas necessárias.

Se a configuração do SmokeAPI for removida ou alterada, esses cinco consumíveis podem deixar de aparecer.

## Recursos experimentais

Dois recursos ainda estão sendo investigados e podem apresentar comportamento inconsistente:

- **Item World — raridade mínima:** funciona em parte dos caminhos de geração de equipamentos, mas ainda não cobre de forma confiável todos eles;
- **Chara World — multiplicador de atributos:** alguns ganhos em tiles podem ser multiplicados corretamente enquanto outros podem seguir caminhos diferentes do jogo.

Essas limitações não significam necessariamente que a instalação falhou. Consulte o README do mod correspondente para detalhes.

## Sobre arquivos `.dat`

A documentação técnica do projeto cita alguns arquivos `.dat` pertencentes ao próprio Disgaea Mayhem porque eles foram úteis durante a investigação de sistemas como Item World, Cheat Shop, Dark Assembly e outros.

**Esses arquivos não precisam ser editados pelo usuário e não são o mecanismo usado pelos plugins atuais para aplicar os mods.** Os efeitos de gameplay implementados atualmente são aplicados em memória durante a execução do jogo, salvo quando um recurso disser explicitamente o contrário.

## Problemas

Se um mod aparecer como incompatível ou falhar ao carregar, consulte:

`mods/mod_loader.log`

Uma atualização do Disgaea Mayhem pode alterar as rotinas internas utilizadas por um plugin. Quando uma incompatibilidade conhecida é detectada, o plugin pode recusar o carregamento em vez de modificar uma região incorreta da memória.

## Para quem quer compilar o projeto

Compilar não é necessário para instalar o pacote pronto.

Para desenvolvimento, o projeto utiliza TDM-GCC-64. Com o jogo fechado, execute na raiz do projeto:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/mod_menu_overlay/build.ps1
```

O script compila e valida o loader, o Mod Menu, os plugins e o instalador antes de gerar `Disgaea_Mayhem_Mod_Loader_Nexus.zip`.

Para detalhes internos, consulte `docs/MOD_LOADER_ARQUITETURA.md` e `docs/GUIA_INTEGRACAO_MODS.md`.