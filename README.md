# Disgaea Mayhem Mod Loader

Mod Loader nativo em C++ para **Disgaea Mayhem**, com menu dentro do jogo e um conjunto de mods que podem ser ativados, desativados e configurados durante a partida.

## Instalação

1. Feche o jogo.
2. Extraia o pacote em uma pasta normal.
3. Execute `INSTALAR_MOD.exe`.
4. Inicie o jogo normalmente depois que a instalação e a validação terminarem sem erros.

Se o instalador encontrar mais de uma instalação do jogo, você pode informar manualmente a pasta que contém `Disgaea_Mayhem.exe`.

Em uma atualização do pacote, suas configurações e o estado dos mods são preservados. Em uma instalação nova, apenas o Mod Menu começa ativado.

## Como usar

Abra o Mod Menu durante o jogo usando:

- `F1`, `Insert` ou `Home` no teclado;
- `L3 + R3` ou `Back` no controle.

Pelo menu você pode ativar e desativar os mods e alterar suas opções em tempo real.

Se algum mod não puder ser carregado, por exemplo após uma atualização incompatível do jogo, o motivo aparece no estado do mod e também é registrado em `mods/mod_loader.log`.

## Mods incluídos

### Chara World

Permite travar a energia do Chara World em um valor configurável e possui uma opção experimental para multiplicar os atributos recebidos nos tiles.

A trava de energia é a parte mais previsível do mod. O multiplicador de atributos ainda está em investigação: os resultados podem variar dependendo do tile e da situação, portanto não espere que todo ganho seja multiplicado de forma uniforme.

### Item World

Permite aumentar o progresso de nível do item em até `20x`, aumentar os Item Points em até `200x` e experimentar uma raridade mínima configurável para equipamentos.

Os multiplicadores de progresso e Item Points são as partes mais estáveis. **A modificação de raridade funciona apenas parcialmente e ainda está sendo investigada.** O jogo possui vários caminhos diferentes para gerar equipamentos e nem todos se comportam da mesma maneira. Há uma opção específica para o Item World e uma opção global, mas a cobertura não deve ser considerada completa.

### Cheat Shop

Mantém EXP, Mana, HL, Weapon Mastery e Item Drops em `5000%` enquanto estiver ativado. Ao desativar, os valores anteriores são restaurados.

Item Drops pode aumentar bastante a quantidade de recompensas geradas pelo jogo.

### Dark Assembly

Faz as propostas da Dark Assembly serem aprovadas enquanto o mod estiver ativado. A alteração acontece durante a execução do jogo e não modifica o banco de dados do jogo.

### DLC Unlocker

Trabalha junto com o SmokeAPI incluído no pacote para permitir resgatar repetidamente cinco consumíveis: HL Bag, Mana Bag e os Boost Tickets de 100%, 400% e 900%.

### Tactical AI

Permite configurar separadamente o comportamento de inimigos e parceiros. Inclui controles para ativação da IA, intervalo de ataque, pausas, busca por alvos, velocidade de movimento, alcance e frequência de novas decisões.

### Safe Backup

Cria backups dos saves e monitora novas gravações de `save.002`. A quantidade de backups mantidos por slot pode ser configurada pelo Mod Menu.

## Sobre os arquivos do jogo

Algumas documentações técnicas deste repositório mencionam arquivos `.dat` do próprio Disgaea Mayhem. Eles são citados porque ajudaram a entender onde determinados dados e sistemas existem no jogo durante a pesquisa.

**Os mods atuais não dependem de editar esses `.dat` diretamente.** As alterações de gameplay implementadas pelos plugins são feitas em memória durante a execução do jogo, salvo quando a documentação de um recurso disser explicitamente o contrário.

## Para desenvolvedores

O pacote usa um Mod Loader nativo com plugins ABI v2. `dxgi.dll` inicializa o loader, enquanto `mods/mod_menu/mod_menu.dll` fornece a interface DirectX 12/ImGui.

Para compilar com o jogo fechado:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native/mod_menu_overlay/build.ps1
```

O script compila o loader, o Mod Menu, os plugins e o instalador, executa as validações e gera `Disgaea_Mayhem_Mod_Loader_Nexus.zip`.

Detalhes internos da arquitetura e informações de engenharia reversa ficam em `docs/`. Eles são documentação de desenvolvimento e não são necessários para instalar ou usar os mods.