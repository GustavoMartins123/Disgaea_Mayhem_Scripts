# Guardiao de Backup Seguro

Plugin nativo ABI v1 que cria backups rotativos dos saves em `mods/safe_backup/backups/`.

## Ciclo de vida

- `Mod_Initialize`: valida a ABI e os caminhos fornecidos pelo loader.
- `Mod_Enable`: inicia uma unica worker de monitoramento.
- `Mod_Disable`: sinaliza a parada e aguarda a worker terminar.
- `Mod_Shutdown`: garante o encerramento do plugin.
- `DllMain`: passivo.

O estado de ativacao fica exclusivamente em `enabled.txt`. O `config.json` e obrigatorio para o plugin residente e possui `options` vazio enquanto nao houver configuracoes expostas pela ABI.

Nao existe aplicador ou modo de execucao manual alternativo.
