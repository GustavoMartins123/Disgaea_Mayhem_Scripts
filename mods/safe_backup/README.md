# Guardiao de Backup Seguro

Plugin nativo ABI v1 que cria backups com data e hora em
`mods/safe_backup/backups/`.

Ao ser ativado, ele copia todos os arquivos `save.*`. Depois, verifica
`save.002` a cada cinco segundos e cria uma nova copia de todos os saves quando
esse arquivo muda. O plugin nao remove backups antigos nem limita a quantidade
de arquivos; a limpeza da pasta ainda e manual.

## Ciclo de vida

- `Mod_Initialize`: valida a ABI e os caminhos fornecidos pelo loader.
- `Mod_Enable`: inicia uma unica worker de monitoramento.
- `Mod_Disable`: sinaliza a parada e aguarda a worker terminar.
- `Mod_Shutdown`: garante o encerramento do plugin.
- `DllMain`: passivo.

O estado de ativacao fica exclusivamente em `enabled.txt`. O `config.json` e obrigatorio para o plugin residente e possui `options` vazio enquanto nao houver configuracoes expostas pela ABI.

Nao existe aplicador ou modo de execucao manual alternativo no pacote atual.
