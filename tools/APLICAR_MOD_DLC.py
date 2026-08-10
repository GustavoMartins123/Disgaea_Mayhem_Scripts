import os
import shutil
import json
import struct

print("="*60)
print("  DISGAEA MAYHEM - MOD DE RESGATE DE DLCS ILIMITADAS")
print("="*60)

game_dir = os.path.dirname(os.path.abspath(__file__))
db_dir = os.path.join(game_dir, "data", "database")
backup_dir = os.path.join(game_dir, "data", "database_backup")

# 1. Configurar SmokeAPI.config.json
smoke_config = {
  "$schema": "https://raw.githubusercontent.com/acidicoala/SmokeAPI/refs/tags/v4.0.0/res/SmokeAPI.schema.json",
  "$version": 4,
  "logging": False,
  "log_steam_http": False,
  "default_app_status": "unlocked",
  "override_app_status": {},
  "override_dlc_status": {},
  "auto_inject_inventory": True,
  "extra_inventory_items": [1, 2, 3, 4, 5],
  "extra_dlcs": {}
}

config_path = os.path.join(game_dir, "SmokeAPI.config.json")
with open(config_path, "w", encoding="utf-8") as f:
    json.dump(smoke_config, f, indent=2)
print("[OK] SmokeAPI.config.json configurado com sucesso.")

# 2. Patch em DLC_information.dat (converter consumiveis para Tipo 1 offline)
info_path = os.path.join(db_dir, "DLC_information.dat")
with open(info_path, "rb") as f:
    data = bytearray(f.read())

items = [
    (b"DLC_INFORMATION_HL", 3001, b"HL1M\x00"),
    (b"DLC_INFORMATION_MANA", 3002, b"Mana1M\x00"),
    (b"DLC_INFORMATION_BOOST_TICKET_100", 3003, b"Boost100\x00"),
    (b"DLC_INFORMATION_BOOST_TICKET_400", 3004, b"Boost400\x00"),
    (b"DLC_INFORMATION_BOOST_TICKET_900", 3005, b"Boost900\x00"),
]

for sym, item_id, item_str in items:
    p = data.find(sym)
    if p != -1:
        id_bytes = struct.pack("<I", item_id)
        p_id = data.find(id_bytes, p + len(sym))
        p_type = p_id - 4
        struct.pack_into("<I", data, p_type, 1) # Tipo 1
        p_str = data.find(item_str, p)
        while p_str != -1 and p_str < p + 160:
            l = len(item_str)
            replacement = b"4687210\x00".ljust(l, b"\x00")[:l]
            data[p_str:p_str+l] = replacement
            p_str = data.find(item_str, p_str + 1)

with open(info_path, "wb") as f:
    f.write(data)
print("[OK] DLC_information.dat configurado para resgate offline sem erros de rede.")

# 3. Patch em DLC_BoostTicket.dat (todos os tickets com +900%)
boost_path = os.path.join(db_dir, "DLC_BoostTicket.dat")
with open(boost_path, "rb") as f:
    b_data = bytearray(f.read())

p100 = b_data.find(b"BOOST_TICKET_100")
p400 = b_data.find(b"BOOST_TICKET_400")
if p100 != -1:
    b_data[p100 + 17] = 0x09
if p400 != -1:
    b_data[p400 + 17] = 0x09

with open(boost_path, "wb") as f:
    f.write(b_data)
print("[OK] DLC_BoostTicket.dat configurado (multiplicador 9x / 900% em todos os tickets).")

print("\n" + "="*60)
print("  TUDO PRONTO! Agora voce pode resgatar quando quiser no NPC Carlbunch.")
print("="*60)
