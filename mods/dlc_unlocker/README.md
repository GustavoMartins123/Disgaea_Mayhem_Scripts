# DLC Unlocker

O mod mantém o desbloqueio de DLCs fornecido pelo SmokeAPI e torna reutilizáveis os cinco consumíveis exibidos na loja de conteúdo especial:

- HL Bag
- Mana Bag
- Boost Ticket 100%
- Boost Ticket 400%
- Boost Ticket 900%

O SmokeAPI instalado na pasta do jogo deve manter a injeção automática das definições `1` a `5`. Se essa configuração estiver ausente, os consumíveis não aparecem para resgate.

Esses itens continuam cadastrados como consumíveis Steam. Ao resgatá-los, o mod confirma o consumo apenas na sessão atual, sem alterar as tabelas do jogo e sem criar o estado permanente `Received` usado pelos DLCs comuns.

O HL, Mana e os tickets concedidos pelo resgate são salvos normalmente pelo jogo. Desativar o mod não remove recompensas já recebidas. Com o mod desativado, os cinco itens voltam ao comportamento normal do inventário Steam.

O mod aceita somente a versão do executável para a qual foi validado. Em outra versão, ele não é carregado e registra o motivo em `mods/mod_loader.log`.
