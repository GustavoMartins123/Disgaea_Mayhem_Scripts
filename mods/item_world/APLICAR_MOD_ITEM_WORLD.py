import sys
import os
import json
import ctypes
from ctypes import wintypes
import struct
from pathlib import Path

# Garantir suporte a UTF-8 no terminal
if hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass

print("=" * 65)
print("  DISGAEA MAYHEM - MOD DE ITEM WORLD & INOCENTES SUBJUGADOS")
print("=" * 65)

# 1. Carregar configuracoes do mod.json (se existir)
script_dir = Path(__file__).resolve().parent
mod_json_path = script_dir / "mod.json"

levels_per_floor = 5
auto_subdue = True
mystery_room_rate = 75

if mod_json_path.exists():
    try:
        with open(mod_json_path, "r", encoding="utf-8") as f:
            cfg = json.load(f)
            for opt in cfg.get("options", []):
                opt_id = opt.get("id")
                val = opt.get("value", opt.get("default"))
                if opt_id == "levels_per_floor":
                    levels_per_floor = int(val)
                elif opt_id == "auto_subdue":
                    auto_subdue = bool(val)
                elif opt_id == "mystery_room_rate":
                    mystery_room_rate = int(val)
        print(f"[CONFIG] Carregado de mod.json:")
        print(f"  -> Niveis por andar: +{levels_per_floor} Lv")
        print(f"  -> Subjugar Inocentes: {'Sim (100%)' if auto_subdue else 'Nao'}")
        print(f"  -> Taxa Mystery Rooms: {mystery_room_rate}%")
    except Exception as e:
        print(f"[AVISO] Falha ao ler mod.json: {e}")

# 2. Localizar processo do jogo Disgaea_Mayhem.exe
PROCESS_ALL_ACCESS = 0x1F0FFF
TH32CS_SNAPPROCESS = 0x00000002

class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", ctypes.c_char * 260)
    ]

kernel32 = ctypes.windll.kernel32
kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
kernel32.Process32First.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32)]
kernel32.Process32First.restype = wintypes.BOOL
kernel32.Process32Next.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32)]
kernel32.Process32Next.restype = wintypes.BOOL
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL

kernel32.ReadProcessMemory.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)
]
kernel32.ReadProcessMemory.restype = wintypes.BOOL

kernel32.WriteProcessMemory.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)
]
kernel32.WriteProcessMemory.restype = wintypes.BOOL

hSnap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
pe = PROCESSENTRY32()
pe.dwSize = ctypes.sizeof(PROCESSENTRY32)
pid = None

if kernel32.Process32First(hSnap, ctypes.byref(pe)):
    while True:
        exe_name = pe.szExeFile.decode('utf-8', errors='ignore').lower()
        if "disgaea" in exe_name and "mayhem" in exe_name:
            pid = pe.th32ProcessID
            break
        if not kernel32.Process32Next(hSnap, ctypes.byref(pe)):
            break
kernel32.CloseHandle(hSnap)

if not pid:
    print("\n[INFO] O processo Disgaea_Mayhem.exe nao esta em execucao no momento.")
    print("       As configuracoes foram salvas no mod.json para a proxima sessao.")
    print("\n" + "=" * 65)
    sys.exit(0)

print(f"\n[OK] Jogo detectado em execucao (PID: {pid})")
hProcess = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
if not hProcess:
    print("[ERRO] Nao foi possivel abrir o processo do jogo com permissoes de leitura/escrita.")
    sys.exit(1)

# 3. Varredura e Injecao em Memoria RAM
MEM_COMMIT = 0x1000
PAGE_READWRITE = 0x04
PAGE_EXECUTE_READWRITE = 0x40

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", wintypes.DWORD),
        ("PartitionId", wintypes.WORD),
        ("RegionSize", ctypes.c_size_t),
        ("State", wintypes.DWORD),
        ("Protect", wintypes.DWORD),
        ("Type", wintypes.DWORD),
    ]

# Obter Base do Modulo Principal
class MODULEENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("th32ModuleID", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("GlblcntUsage", wintypes.DWORD),
        ("ProccntUsage", wintypes.DWORD),
        ("modBaseAddr", ctypes.c_void_p),
        ("modBaseSize", wintypes.DWORD),
        ("hModule", wintypes.HMODULE),
        ("szModule", ctypes.c_char * 256),
        ("szExePath", ctypes.c_char * 260)
    ]

TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010
hModSnap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
me = MODULEENTRY32()
me.dwSize = ctypes.sizeof(MODULEENTRY32)
exe_base = 0x140000000

if kernel32.Process32First(hModSnap, ctypes.byref(me)):
    if me.modBaseAddr:
        exe_base = me.modBaseAddr
kernel32.CloseHandle(hModSnap)

vtable_item_status = exe_base + 0xA252C0
vtable_item_world = exe_base + 0xA251F0

print(f"[OK] Base do executavel: 0x{exe_base:016x}")
print(f"[OK] VTable CItemStatus: 0x{vtable_item_status:016x}")
print(f"[OK] VTable CItemWorldData: 0x{vtable_item_world:016x}")

mbi = MEMORY_BASIC_INFORMATION()
address = 0
writable = [PAGE_READWRITE, PAGE_EXECUTE_READWRITE]

items_boosted = 0
innocents_subdued = 0
sessions_hooked = 0

vptr_bytes_item = struct.pack("<Q", vtable_item_status)
vptr_bytes_iw = struct.pack("<Q", vtable_item_world)

while kernel32.VirtualQueryEx(hProcess, ctypes.c_void_p(address), ctypes.byref(mbi), ctypes.sizeof(mbi)):
    if mbi.State == MEM_COMMIT and (mbi.Protect in writable):
        buf = ctypes.create_string_buffer(mbi.RegionSize)
        br = ctypes.c_size_t()
        if kernel32.ReadProcessMemory(hProcess, ctypes.c_void_p(address), buf, mbi.RegionSize, ctypes.byref(br)):
            raw = bytes(buf[:br.value])
            
            # 1. Modificar CItemWorldData (Sessao do Item World)
            pos = 0
            while True:
                idx = raw.find(vptr_bytes_iw, pos)
                if idx == -1: break
                iw_addr = address + idx
                # Offset +0x74: Level increment
                p_level_inc = iw_addr + 0x74
                cur_inc_buf = (ctypes.c_int32)()
                kernel32.ReadProcessMemory(hProcess, ctypes.c_void_p(p_level_inc), ctypes.byref(cur_inc_buf), 4, None)
                if 0 <= cur_inc_buf.value <= 100:
                    new_val = ctypes.c_int32(levels_per_floor)
                    kernel32.WriteProcessMemory(hProcess, ctypes.c_void_p(p_level_inc), ctypes.byref(new_val), 4, None)
                    sessions_hooked += 1
                pos = idx + 8
            
            # 2. Modificar CItemStatus (Itens e Inocentes)
            pos = 0
            while True:
                idx = raw.find(vptr_bytes_item, pos)
                if idx == -1: break
                item_addr = address + idx
                
                # Ler RefCount
                ref_count = struct.unpack_from("<I", raw, idx + 8)[0]
                if 0 < ref_count < 100000 and idx + 0x380 <= len(raw):
                    # Offset +0x328: Nivel do Item World
                    p_iw_lv = item_addr + 0x328
                    cur_lv_buf = (ctypes.c_uint16)()
                    kernel32.ReadProcessMemory(hProcess, ctypes.c_void_p(p_iw_lv), ctypes.byref(cur_lv_buf), 2, None)
                    
                    if cur_lv_buf.value < 9999:
                        new_lv = min(9999, cur_lv_buf.value + levels_per_floor)
                        new_lv_buf = ctypes.c_uint16(new_lv)
                        kernel32.WriteProcessMemory(hProcess, ctypes.c_void_p(p_iw_lv), ctypes.byref(new_lv_buf), 2, None)
                        items_boosted += 1
                    
                    # Subjugar Inocentes se habilitado
                    if auto_subdue:
                        inno_start = struct.unpack_from("<Q", raw, idx + 0x358)[0]
                        inno_end = struct.unpack_from("<Q", raw, idx + 0x360)[0]
                        
                        if inno_start and inno_end >= inno_start and (inno_end - inno_start) <= 64 * 8:
                            for p in range(inno_start, inno_end, 8):
                                inno_ptr_buf = (ctypes.c_uint64)()
                                if kernel32.ReadProcessMemory(hProcess, ctypes.c_void_p(p), ctypes.byref(inno_ptr_buf), 8, None):
                                    inno_obj = inno_ptr_buf.value
                                    if inno_obj:
                                        # Offset +0x14: Subdued flag
                                        # Offset +0x18: Power value
                                        subdue_buf = (ctypes.c_uint32)()
                                        power_buf = (ctypes.c_int32)()
                                        kernel32.ReadProcessMemory(hProcess, ctypes.c_void_p(inno_obj + 0x14), ctypes.byref(subdue_buf), 4, None)
                                        kernel32.ReadProcessMemory(hProcess, ctypes.c_void_p(inno_obj + 0x18), ctypes.byref(power_buf), 4, None)
                                        
                                        if subdue_buf.value == 0:
                                            subdue_one = ctypes.c_uint32(1)
                                            kernel32.WriteProcessMemory(hProcess, ctypes.c_void_p(inno_obj + 0x14), ctypes.byref(subdue_one), 4, None)
                                            if power_buf.value > 0:
                                                double_power = ctypes.c_int32(power_buf.value * 2)
                                                kernel32.WriteProcessMemory(hProcess, ctypes.c_void_p(inno_obj + 0x18), ctypes.byref(double_power), 4, None)
                                            innocents_subdued += 1
                pos = idx + 8
                
    address += mbi.RegionSize

kernel32.CloseHandle(hProcess)

print(f"\n[SUCESSO] Operacao concluida com sucesso!")
print(f"  -> Sessoes de Item World atualizadas: {sessions_hooked}")
print(f"  -> Itens acelerados (+{levels_per_floor} Lv): {items_boosted}")
print(f"  -> Inocentes 100% subjugados e duplicados: {innocents_subdued}")
print("=" * 65)
