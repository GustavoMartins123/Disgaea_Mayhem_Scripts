import os
import sys
import shutil
import json
import struct

print("="*70)
print("  DISGAEA MAYHEM - INSTALADOR AUTOMATICO DE DLCS E BOOST ILIMITADO")
print("="*70)

# 1. Detectar pasta do jogo
default_paths = [
    r"E:\Steam\steamapps\common\Disgaea Mayhem",
    r"C:\Program Files (x86)\Steam\steamapps\common\Disgaea Mayhem",
    r"D:\Steam\steamapps\common\Disgaea Mayhem",
    r"D:\SteamLibrary\steamapps\common\Disgaea Mayhem",
    r"E:\SteamLibrary\steamapps\common\Disgaea Mayhem",
]

game_dir = None
for p in default_paths:
    if os.path.exists(os.path.join(p, "Disgaea_Mayhem.exe")):
        game_dir = p
        break

if not game_dir:
    print("\n[?] Nao foi possivel detectar a pasta do jogo automaticamente.")
    user_input = input("Digite o caminho completo da pasta do jogo (onde fica o Disgaea_Mayhem.exe): ").strip()
    if os.path.exists(os.path.join(user_input, "Disgaea_Mayhem.exe")):
        game_dir = user_input
    else:
        print("[ERRO] Executavel Disgaea_Mayhem.exe nao encontrado no caminho informado.")
        input("\nPressione ENTER para sair...")
        sys.exit(1)

print(f"[OK] Pasta do jogo localizada: {game_dir}")
script_root = os.path.dirname(os.path.abspath(__file__))

# 2. Configurar SmokeAPI DLLs
smoke_src_dll = os.path.join(script_root, "SmokeAPI", "smoke_api64.dll")
target_dll = os.path.join(game_dir, "steam_api64.dll")
orig_backup_dll = os.path.join(game_dir, "steam_api64_o.dll")

if not os.path.exists(orig_backup_dll) and os.path.exists(target_dll):
    shutil.copy(target_dll, orig_backup_dll)
    print("[OK] Backup da DLL original da Steam salvo como steam_api64_o.dll")

if os.path.exists(smoke_src_dll):
    try:
        shutil.copy(smoke_src_dll, target_dll)
        print("[OK] SmokeAPI DLL instalada como steam_api64.dll")
    except PermissionError:
        print("[AVISO] O jogo esta aberto! Feche o Disgaea Mayhem e rode o instalador novamente.")
        input("\nPressione ENTER para sair...")
        sys.exit(1)

# 3. Configurar SmokeAPI.config.json
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

with open(os.path.join(game_dir, "SmokeAPI.config.json"), "w", encoding="utf-8") as f:
    json.dump(smoke_config, f, indent=2)
print("[OK] SmokeAPI.config.json configurado na raiz do jogo.")

# 4. Patch nos arquivos de banco de dados
db_dir = os.path.join(game_dir, "data", "database")
db_backup = os.path.join(game_dir, "data", "database_backup")
os.makedirs(db_backup, exist_ok=True)

# Copiar arquivos pre-modificados se existirem na pasta database_mods
mods_src_dir = os.path.join(script_root, "database_mods")
for f in ["DLC_information.dat", "DLC_BoostTicket.dat", "DLC_delivery.dat"]:
    src_mod = os.path.join(mods_src_dir, f)
    dst_target = os.path.join(db_dir, f)
    dst_bak = os.path.join(db_backup, f)
    if os.path.exists(dst_target) and not os.path.exists(dst_bak):
        shutil.copy(dst_target, dst_bak)
    if os.path.exists(src_mod):
        shutil.copy(src_mod, dst_target)
        print(f"[OK] Banco de dados atualizado: {f}")

print("\n" + "="*70)
print("  INSTALACAO CONCLUIDA COM SUCESSO!")
print("  - Abra o jogo e fale com Carlbunch (DLC Special Content Shop).")
print("  - Resgate seus Boost Tickets 900%, HL e Mana ilimitados!")
print("="*70)
