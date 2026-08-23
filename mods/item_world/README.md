# Item World - Multiplicador de EXP de Nível

Plugin residente ABI v1 que altera somente a progressão nativa de nível dos
itens. O jogo acumula pontos por inimigo em `CItemWorldData + 0x68` e converte
esses pontos em `CItemStatus.lv_` na aplicação das recompensas.

## Ciclo de vida

- `Mod_Initialize` valida o fingerprint PE e o prólogo do RVA `0x1D77E0` antes
  de instalar o MinHook.
- `Mod_Enable` ativa o multiplicador configurado.
- `Mod_Disable` volta imediatamente ao cálculo original.
- `Mod_Shutdown` remove o hook e libera o MinHook.

O hook multiplica temporariamente os pontos entregues à função nativa. Depois da
aplicação, o valor transitório é restaurado. Os contadores de chefes em
`+0x70/+0x74/+0x78`, Item Points e o recálculo de atributos permanecem sob
controle do jogo.

Não há polling, escrita em `+0x74`, subjugação automática de Inocentes nem opção
de Mystery Rooms. Esses comportamentos antigos não possuíam implementação
validada e foram removidos.

Compatibilidade confirmada para o executável com SHA-256:

```text
13988368F66ADE40205C1D0D18157B6AE2D7736D67AC0C8734FE1DD4E62D5B41
```

Consulte `docs/SUBSISTEMA_ITEM_WORLD.md` para o fluxo desmontado.
