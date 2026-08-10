import sys
import ctypes
from ctypes import wintypes
import struct

PROCESS_ALL_ACCESS = 0x1F0FFF
kernel32 = ctypes.windll.kernel32
kernel32.ReadProcessMemory.argtypes = [
    wintypes.HANDLE,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t)
]
kernel32.ReadProcessMemory.restype = wintypes.BOOL

kernel32.WriteProcessMemory.argtypes = [
    wintypes.HANDLE,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t)
]
kernel32.WriteProcessMemory.restype = wintypes.BOOL

# Find PID
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

hSnap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
pe = PROCESSENTRY32()
pe.dwSize = ctypes.sizeof(PROCESSENTRY32)
pid = None
if kernel32.Process32First(hSnap, ctypes.byref(pe)):
    while True:
        if "disgaea" in pe.szExeFile.decode('utf-8', errors='ignore').lower():
            pid = pe.th32ProcessID
            break
        if not kernel32.Process32Next(hSnap, ctypes.byref(pe)):
            break
kernel32.CloseHandle(hSnap)

if not pid:
    print("[ERRO] O processo Disgaea_Mayhem.exe nao foi encontrado! Abra o jogo primeiro.")
    input("\nPressione ENTER para sair...")
    sys.exit(1)

print(f"[OK] Jogo encontrado (PID: {pid})")
hProcess = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)

# Memory scanning structures
MEM_COMMIT = 0x1000
PAGE_READWRITE = 0x04
PAGE_WRITECOPY = 0x08
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

mbi = MEMORY_BASIC_INFORMATION()
address = 0
writable = [PAGE_READWRITE, PAGE_WRITECOPY, PAGE_EXECUTE_READWRITE]

id_3003 = struct.pack("<I", 3003)
id_3004 = struct.pack("<I", 3004)
id_3005 = struct.pack("<I", 3005)

injected = 0
while kernel32.VirtualQueryEx(hProcess, ctypes.c_void_p(address), ctypes.byref(mbi), ctypes.sizeof(mbi)):
    if mbi.State == MEM_COMMIT and (mbi.Protect in writable):
        buf = ctypes.create_string_buffer(mbi.RegionSize)
        br = ctypes.c_size_t()
        if kernel32.ReadProcessMemory(hProcess, ctypes.c_void_p(address), buf, mbi.RegionSize, ctypes.byref(br)):
            raw = bytes(buf[:br.value])
            pos = 0
            while True:
                idx = raw.find(id_3003, pos)
                if idx == -1:
                    break
                next_chunk = raw[idx:idx+128]
                if id_3004 in next_chunk and id_3005 in next_chunk:
                    match_addr = address + idx
                    print(f"-> Localizada tabela de Boost Tickets em 0x{match_addr:016x}")
                    injected += 1
                pos = idx + 4
    address += mbi.RegionSize

kernel32.CloseHandle(hProcess)
print(f"\n[SUCESSO] Operacao concluida! {injected} tabelas mapeadas.")
