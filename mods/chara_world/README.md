# Chara World - Energia Infinita

Plugin nativo ABI v1 que mantem a energia do Chara World no valor configurado.

## Ciclo de vida

- `Mod_Initialize`: valida ABI, fingerprint PE e prólogo, então instala o hook
  síncrono da resolução de turno no RVA `0x00461CA0`.
- `Mod_SetOption`: recebe `locked_energy` e `freeze_energy` tipados.
- `Mod_Enable`: habilita o efeito já instalado.
- `Mod_Disable`: interrompe as escritas imediatamente.
- `Mod_Shutdown`: desabilita e remove o hook depois de aguardar chamadas ativas.
- `DllMain`: passivo.

O plugin resolve `CCharacterWorldInformation` somente a partir do objeto da
rotina nativa que está em execução. Ele valida as VTables e escreve apenas na
energia atual em `+0x178`, antes e depois da resolução do turno. Não existe
worker, polling ou ponteiro de instância em cache.

O limite configurável máximo é `100`, porque esse é o limite superior retornado
pela própria classe nativa. `+0x174` pertence ao ponteiro da VTable do subobjeto
de energia e nunca pode receber valores do mod.

O plugin e carregado automaticamente pelo `dxgi.dll` quando `enabled.txt` contem `1`. Nao ha injecao manual nem segundo gerenciador de estado.

Os tipos e limites das opcoes ficam em `mod.json`; `locked_energy` e `freeze_energy` sao lidos e persistidos exclusivamente em `config.json`.

Consulte `docs/SUBSISTEMA_CHARA_WORLD.md` e `docs/MOD_LOADER_ARQUITETURA.md`.
