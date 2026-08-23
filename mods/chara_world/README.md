# Chara World - Energia Infinita

Plugin nativo ABI v1 que mantem a energia do Chara World no valor configurado.

## Ciclo de vida

- `Mod_Initialize`: valida a ABI e instala o hook do construtor de `CCharacterWorldInformation`.
- `Mod_SetOption`: recebe `locked_energy` e `freeze_energy` tipados.
- `Mod_Enable`: inicia uma unica worker e habilita o efeito.
- `Mod_Disable`: interrompe o efeito e invalida a instancia em cache.
- `Mod_Shutdown`: encerra a worker e remove o hook.
- `DllMain`: passivo.

O plugin e carregado automaticamente pelo `dxgi.dll` quando `enabled.txt` contem `1`. Nao ha injecao manual nem segundo gerenciador de estado.

Consulte `docs/SUBSISTEMA_CHARA_WORLD.md` e `docs/MOD_LOADER_ARQUITETURA.md`.
