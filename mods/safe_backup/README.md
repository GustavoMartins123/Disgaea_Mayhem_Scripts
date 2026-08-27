# Guardião de Backup Seguro

Este mod cria cópias automáticas dos saves para ajudar a recuperar uma partida caso um arquivo seja sobrescrito ou corrompido.

Os backups ficam em:

`mods/safe_backup/backups/`

## Como funciona

Ao ser ativado, o mod cria um backup inicial dos saves existentes. Depois, monitora `save.002` e cria um novo conjunto de backups quando detecta uma nova gravação.

A opção **Backups Mantidos por Slot** (`max_backups`) define quantos backups recentes devem ser conservados para cada slot. O valor pode ser configurado entre `1` e `200` pelo Mod Menu.

Quando o limite é ultrapassado, os backups mais antigos daquele slot são removidos, mantendo os mais recentes.

## Uso

O mod funciona automaticamente enquanto estiver ativado. Não é necessário executar outro programa ou iniciar o backup manualmente.

A configuração escolhida é salva em `config.json`, e o estado ligado/desligado é preservado pelo Mod Loader.

Este mod trabalha diretamente com os arquivos de save para criar as cópias de segurança. Ele não modifica bancos `.dat` ou outros dados de gameplay do jogo.