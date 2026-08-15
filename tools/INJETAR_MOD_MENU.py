#!/usr/bin/env python3
"""Injeta o item Mods e seu renderer DirectX 12 no processo do jogo.

O injetor aceita somente a versao conhecida do executavel, do atlas e do DLL.
Qualquer divergencia encerra a operacao com erro explicito.
"""

from __future__ import annotations

import ctypes
from ctypes import wintypes as wt
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import sys
import time


GAME_EXE_NAME = "Disgaea_Mayhem.exe"
GAME_EXE_SHA256 = "13988368F66ADE40205C1D0D18157B6AE2D7736D67AC0C8734FE1DD4E62D5B41"
PATCHED_FAD_SHA256 = "81429742F0410E20813B8C300F6A6B633E9FE2598A72DEF5EB6CC16FB540EAA5"
NATIVE_DLL_SHA256 = "7D573CA7D44C88482D78FCB3D615437580D8ED0F367B2874A5AC77DFF5C947AD"

PROCESS_CREATE_THREAD = 0x0002
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
PAGE_READWRITE = 0x04
PAGE_EXECUTE_READ = 0x20
PAGE_EXECUTE_READWRITE = 0x40
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
STILL_ACTIVE = 259
TH32CS_SNAPTHREAD = 0x00000004
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010
THREAD_SUSPEND_RESUME = 0x0002
THREAD_QUERY_INFORMATION = 0x0040
WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 258
INFINITE = 0xFFFFFFFF
DONT_RESOLVE_DLL_REFERENCES = 0x00000001
GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT = 0x00000002
GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS = 0x00000004
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
ERROR_NO_MORE_FILES = 18

EVENT_OFFSET = 0
CALLBACK_CONTEXT_OFFSET = 8
NATIVE_STATUS_OFFSET = 16
NATIVE_ERROR_CODE_OFFSET = 20
NATIVE_ERROR_MESSAGE_OFFSET = 32
NATIVE_ERROR_MESSAGE_SIZE = 256
STATUS_WAITING = 0
STATUS_HOOKS_INSTALLED = 1
STATUS_RENDER_READY = 2
STATUS_FAILED = -1
STATE_SIZE = 0x1000
CODE_SIZE = 0x1000


class InjectionError(RuntimeError):
    """Falha explicita de validacao ou injecao."""


class Patch:
    def __init__(self, name: str, rva: int, expected: bytes, replacement: bytes):
        if len(expected) != len(replacement):
            raise ValueError(f"patch '{name}' possui tamanhos diferentes")
        self.name = name
        self.rva = rva
        self.expected = expected
        self.replacement = replacement


class THREADENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", wt.DWORD),
        ("cntUsage", wt.DWORD),
        ("th32ThreadID", wt.DWORD),
        ("th32OwnerProcessID", wt.DWORD),
        ("tpBasePri", ctypes.c_long),
        ("tpDeltaPri", ctypes.c_long),
        ("dwFlags", wt.DWORD),
    ]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wt.DWORD),
        ("th32ModuleID", wt.DWORD),
        ("th32ProcessID", wt.DWORD),
        ("GlblcntUsage", wt.DWORD),
        ("ProccntUsage", wt.DWORD),
        ("modBaseAddr", ctypes.POINTER(ctypes.c_ubyte)),
        ("modBaseSize", wt.DWORD),
        ("hModule", wt.HMODULE),
        ("szModule", wt.WCHAR * 256),
        ("szExePath", wt.WCHAR * 260),
    ]


ROOT = Path(__file__).resolve().parent
EXE_PATH = ROOT / GAME_EXE_NAME
FAD_PATH = ROOT / "data" / "fairy" / "AnmDat_1_00_EN.fad"
REGISTRY_PATH = ROOT / "mods" / "registry.json"
NATIVE_DLL_PATH = ROOT / "mods" / "native" / "DisgaeaMayhemModMenu.dll"

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

kernel32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
kernel32.OpenProcess.restype = wt.HANDLE
kernel32.CloseHandle.argtypes = [wt.HANDLE]
kernel32.CloseHandle.restype = wt.BOOL
kernel32.ReadProcessMemory.argtypes = [
    wt.HANDLE,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
kernel32.ReadProcessMemory.restype = wt.BOOL
kernel32.WriteProcessMemory.argtypes = kernel32.ReadProcessMemory.argtypes
kernel32.WriteProcessMemory.restype = wt.BOOL
kernel32.VirtualAllocEx.argtypes = [
    wt.HANDLE,
    ctypes.c_void_p,
    ctypes.c_size_t,
    wt.DWORD,
    wt.DWORD,
]
kernel32.VirtualAllocEx.restype = ctypes.c_void_p
kernel32.VirtualFreeEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wt.DWORD]
kernel32.VirtualFreeEx.restype = wt.BOOL
kernel32.VirtualProtectEx.argtypes = [
    wt.HANDLE,
    ctypes.c_void_p,
    ctypes.c_size_t,
    wt.DWORD,
    ctypes.POINTER(wt.DWORD),
]
kernel32.VirtualProtectEx.restype = wt.BOOL
kernel32.FlushInstructionCache.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t]
kernel32.FlushInstructionCache.restype = wt.BOOL
kernel32.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
kernel32.GetExitCodeProcess.restype = wt.BOOL
kernel32.GetExitCodeThread.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
kernel32.GetExitCodeThread.restype = wt.BOOL
kernel32.QueryFullProcessImageNameW.argtypes = [
    wt.HANDLE,
    wt.DWORD,
    wt.LPWSTR,
    ctypes.POINTER(wt.DWORD),
]
kernel32.QueryFullProcessImageNameW.restype = wt.BOOL
kernel32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
kernel32.CreateToolhelp32Snapshot.restype = wt.HANDLE
kernel32.Thread32First.argtypes = [wt.HANDLE, ctypes.POINTER(THREADENTRY32)]
kernel32.Thread32First.restype = wt.BOOL
kernel32.Thread32Next.argtypes = [wt.HANDLE, ctypes.POINTER(THREADENTRY32)]
kernel32.Thread32Next.restype = wt.BOOL
kernel32.Module32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
kernel32.Module32FirstW.restype = wt.BOOL
kernel32.Module32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
kernel32.Module32NextW.restype = wt.BOOL
kernel32.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
kernel32.OpenThread.restype = wt.HANDLE
kernel32.SuspendThread.argtypes = [wt.HANDLE]
kernel32.SuspendThread.restype = wt.DWORD
kernel32.ResumeThread.argtypes = [wt.HANDLE]
kernel32.ResumeThread.restype = wt.DWORD
kernel32.CreateRemoteThread.argtypes = [
    wt.HANDLE,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_void_p,
    wt.DWORD,
    ctypes.POINTER(wt.DWORD),
]
kernel32.CreateRemoteThread.restype = wt.HANDLE
kernel32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
kernel32.WaitForSingleObject.restype = wt.DWORD
kernel32.GetModuleHandleW.argtypes = [wt.LPCWSTR]
kernel32.GetModuleHandleW.restype = wt.HMODULE
kernel32.GetModuleHandleExW.argtypes = [
    wt.DWORD,
    ctypes.c_void_p,
    ctypes.POINTER(wt.HMODULE),
]
kernel32.GetModuleHandleExW.restype = wt.BOOL
kernel32.GetModuleFileNameW.argtypes = [wt.HMODULE, wt.LPWSTR, wt.DWORD]
kernel32.GetModuleFileNameW.restype = wt.DWORD
kernel32.GetProcAddress.argtypes = [wt.HMODULE, ctypes.c_char_p]
kernel32.GetProcAddress.restype = ctypes.c_void_p
kernel32.LoadLibraryExW.argtypes = [wt.LPCWSTR, wt.HANDLE, wt.DWORD]
kernel32.LoadLibraryExW.restype = wt.HMODULE
kernel32.FreeLibrary.argtypes = [wt.HMODULE]
kernel32.FreeLibrary.restype = wt.BOOL
psapi.EnumProcesses.argtypes = [ctypes.POINTER(wt.DWORD), wt.DWORD, ctypes.POINTER(wt.DWORD)]
psapi.EnumProcesses.restype = wt.BOOL
psapi.EnumProcessModules.argtypes = [
    wt.HANDLE,
    ctypes.POINTER(ctypes.c_void_p),
    wt.DWORD,
    ctypes.POINTER(wt.DWORD),
]
psapi.EnumProcessModules.restype = wt.BOOL


def win_error(action: str) -> InjectionError:
    code = ctypes.get_last_error()
    return InjectionError(f"{action} falhou (Win32 {code}: {ctypes.FormatError(code).strip()})")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def require_file_hash(path: Path, expected: str, label: str) -> None:
    if not path.is_file():
        raise InjectionError(f"{label} ausente: {path}")
    actual = sha256(path)
    if actual != expected:
        raise InjectionError(
            f"{label} incompativel. SHA-256 esperado={expected}; encontrado={actual}"
        )


def process_path(handle: int) -> Path:
    capacity = wt.DWORD(32768)
    buffer = ctypes.create_unicode_buffer(capacity.value)
    if not kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(capacity)):
        raise win_error("QueryFullProcessImageNameW")
    return Path(buffer.value).resolve()


def find_game_process() -> tuple[int, int]:
    capacity = 4096
    process_ids = (wt.DWORD * capacity)()
    needed = wt.DWORD()
    if not psapi.EnumProcesses(process_ids, ctypes.sizeof(process_ids), ctypes.byref(needed)):
        raise win_error("EnumProcesses")
    if needed.value == ctypes.sizeof(process_ids):
        raise InjectionError("lista de processos excedeu o limite fixo")

    injection_access = (
        PROCESS_CREATE_THREAD
        | PROCESS_QUERY_INFORMATION
        | PROCESS_VM_OPERATION
        | PROCESS_VM_READ
        | PROCESS_VM_WRITE
    )
    matches: list[tuple[int, int]] = []
    for pid in process_ids[: needed.value // ctypes.sizeof(wt.DWORD)]:
        if not pid:
            continue
        query_handle = kernel32.OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION, False, pid
        )
        if not query_handle:
            continue
        try:
            path = process_path(query_handle)
        except InjectionError:
            kernel32.CloseHandle(query_handle)
            continue
        kernel32.CloseHandle(query_handle)
        if path.name.casefold() == GAME_EXE_NAME.casefold():
            if path != EXE_PATH.resolve():
                raise InjectionError(
                    f"processo com mesmo nome pertence a outra instalacao: {path}"
                )
            handle = kernel32.OpenProcess(injection_access, False, pid)
            if not handle:
                raise win_error(
                    f"OpenProcess(PID {pid}, acesso de injecao); "
                    "execute o injetor com o mesmo nivel de administrador do jogo"
                )
            matches.append((pid, handle))

    if not matches:
        raise InjectionError("o jogo nao esta aberto nesta instalacao")
    if len(matches) != 1:
        for _, handle in matches:
            kernel32.CloseHandle(handle)
        raise InjectionError(f"esperado um processo do jogo; encontrados={len(matches)}")
    return matches[0]


def module_base(handle: int) -> int:
    modules = (ctypes.c_void_p * 1024)()
    needed = wt.DWORD()
    if not psapi.EnumProcessModules(handle, modules, ctypes.sizeof(modules), ctypes.byref(needed)):
        raise win_error("EnumProcessModules")
    if needed.value == 0 or needed.value > ctypes.sizeof(modules):
        raise InjectionError("lista de modulos do jogo invalida")
    base = modules[0]
    if not base:
        raise InjectionError("endereco-base do executavel e nulo")
    return int(base)


def enumerate_modules(pid: int) -> list[tuple[int, Path, str]]:
    snapshot = kernel32.CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid
    )
    if snapshot == INVALID_HANDLE_VALUE:
        raise win_error("CreateToolhelp32Snapshot(modulos)")
    modules: list[tuple[int, Path, str]] = []
    try:
        entry = MODULEENTRY32W()
        entry.dwSize = ctypes.sizeof(entry)
        if not kernel32.Module32FirstW(snapshot, ctypes.byref(entry)):
            raise win_error("Module32FirstW")
        while True:
            base = ctypes.cast(entry.modBaseAddr, ctypes.c_void_p).value
            if not base:
                raise InjectionError(f"modulo com base nula: {entry.szModule}")
            modules.append((int(base), Path(entry.szExePath), entry.szModule))
            if not kernel32.Module32NextW(snapshot, ctypes.byref(entry)):
                code = ctypes.get_last_error()
                if code != ERROR_NO_MORE_FILES:
                    raise win_error("Module32NextW")
                break
    finally:
        kernel32.CloseHandle(snapshot)
    return modules


def normalized_path(path: Path) -> str:
    return os.path.normcase(os.path.abspath(path))


def find_remote_module(pid: int, path: Path) -> int | None:
    target = normalized_path(path)
    matches = [base for base, item_path, _ in enumerate_modules(pid)
               if normalized_path(item_path) == target]
    if len(matches) > 1:
        raise InjectionError(f"modulo carregado mais de uma vez: {path}")
    return matches[0] if matches else None


def find_remote_module_by_name(pid: int, name: str) -> int:
    matches = [(base, path) for base, path, module_name in enumerate_modules(pid)
               if module_name.casefold() == name.casefold()]
    if len(matches) != 1:
        raise InjectionError(
            f"esperado um modulo '{name}' no jogo; encontrados={len(matches)}"
        )
    return matches[0][0]


@contextmanager
def suspended_process_threads(pid: int):
    """Suspende as threads enumeradas para escrever codigo sem tearing."""
    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    if snapshot == INVALID_HANDLE_VALUE:
        raise win_error("CreateToolhelp32Snapshot(threads)")

    suspended: list[int] = []
    try:
        entry = THREADENTRY32()
        entry.dwSize = ctypes.sizeof(entry)
        if not kernel32.Thread32First(snapshot, ctypes.byref(entry)):
            raise win_error("Thread32First")
        while True:
            if entry.th32OwnerProcessID == pid:
                thread = kernel32.OpenThread(
                    THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                    False,
                    entry.th32ThreadID,
                )
                if not thread:
                    raise win_error(f"OpenThread({entry.th32ThreadID})")
                previous_count = kernel32.SuspendThread(thread)
                if previous_count == 0xFFFFFFFF:
                    kernel32.CloseHandle(thread)
                    raise win_error(f"SuspendThread({entry.th32ThreadID})")
                suspended.append(thread)
            if not kernel32.Thread32Next(snapshot, ctypes.byref(entry)):
                code = ctypes.get_last_error()
                if code != ERROR_NO_MORE_FILES:
                    raise win_error("Thread32Next")
                break
        if not suspended:
            raise InjectionError("nenhuma thread do processo foi suspensa")
        yield
    finally:
        resume_errors: list[str] = []
        for thread in reversed(suspended):
            if kernel32.ResumeThread(thread) == 0xFFFFFFFF:
                code = ctypes.get_last_error()
                resume_errors.append(f"handle=0x{int(thread):X}, Win32={code}")
            kernel32.CloseHandle(thread)
        kernel32.CloseHandle(snapshot)
        if resume_errors:
            raise InjectionError(
                "falha ao retomar threads: " + "; ".join(resume_errors)
            )


def read_memory(handle: int, address: int, size: int) -> bytes:
    if size <= 0:
        raise InjectionError("leitura de memoria com tamanho invalido")
    buffer = ctypes.create_string_buffer(size)
    read = ctypes.c_size_t()
    if not kernel32.ReadProcessMemory(
        handle, ctypes.c_void_p(address), buffer, size, ctypes.byref(read)
    ):
        raise win_error(f"ReadProcessMemory(0x{address:X})")
    if read.value != size:
        raise InjectionError(
            f"leitura parcial em 0x{address:X}: esperado={size}, obtido={read.value}"
        )
    return buffer.raw


def write_memory(handle: int, address: int, data: bytes) -> None:
    if not data:
        raise InjectionError("escrita de memoria vazia")
    buffer = ctypes.create_string_buffer(data)
    written = ctypes.c_size_t()
    if not kernel32.WriteProcessMemory(
        handle, ctypes.c_void_p(address), buffer, len(data), ctypes.byref(written)
    ):
        raise win_error(f"WriteProcessMemory(0x{address:X})")
    if written.value != len(data):
        raise InjectionError(
            f"escrita parcial em 0x{address:X}: esperado={len(data)}, obtido={written.value}"
        )


def write_code(handle: int, address: int, data: bytes) -> None:
    old_protection = wt.DWORD()
    if not kernel32.VirtualProtectEx(
        handle,
        ctypes.c_void_p(address),
        len(data),
        PAGE_EXECUTE_READWRITE,
        ctypes.byref(old_protection),
    ):
        raise win_error(f"VirtualProtectEx(0x{address:X})")
    try:
        write_memory(handle, address, data)
        if not kernel32.FlushInstructionCache(handle, ctypes.c_void_p(address), len(data)):
            raise win_error(f"FlushInstructionCache(0x{address:X})")
    finally:
        ignored = wt.DWORD()
        if not kernel32.VirtualProtectEx(
            handle,
            ctypes.c_void_p(address),
            len(data),
            old_protection.value,
            ctypes.byref(ignored),
        ):
            raise win_error(f"restauracao de protecao em 0x{address:X}")


def game_is_active(handle: int) -> bool:
    exit_code = wt.DWORD()
    if not kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code)):
        raise win_error("GetExitCodeProcess")
    return exit_code.value == STILL_ACTIVE


def load_registry() -> None:
    if not REGISTRY_PATH.is_file():
        raise InjectionError(f"registro de mods ausente: {REGISTRY_PATH}")
    try:
        value = json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise InjectionError(f"registro de mods invalido: {error}") from error
    if not isinstance(value, dict) or set(value) != {"schema_version", "mods"}:
        raise InjectionError("registro deve conter apenas 'schema_version' e 'mods'")
    if value["schema_version"] != 1 or not isinstance(value["mods"], list):
        raise InjectionError("schema do registro de mods incompativel")
    if value["mods"]:
        raise InjectionError(
            "o registro contem modulos, mas a ABI nativa ainda nao foi implementada"
        )


def build_hook(state_address: int, resume_address: int) -> bytes:
    return b"".join(
        (
            b"\x48\xB8" + struct.pack("<Q", state_address),
            b"\x48\x89\x78\x08",  # mov [rax+8], rdi (Main Menu)
            b"\xC6\x87\x20\x02\x00\x00\x00",  # mov byte [rdi+220h], 0
            b"\xC6\x00\x01",  # mov byte ptr [rax], 1
            b"\x49\xBA" + struct.pack("<Q", resume_address),
            b"\x41\xFF\xE2",  # jmp r10
        )
    )


def build_hook_jump(hook_address: int) -> bytes:
    return b"\x48\xB8" + struct.pack("<Q", hook_address) + b"\xFF\xE0" + b"\x90" * 2


def validate_hook(hook: bytes) -> None:
    if len(hook) != 37:
        raise InjectionError(f"tamanho inesperado do hook: {len(hook)}")
    if hook[10:24] != bytes.fromhex("48897808C6872002000000C60001"):
        raise InjectionError("hook nao contem o bloqueio esperado do Main Menu")
    if hook[-3:] != bytes.fromhex("41FFE2"):
        raise InjectionError("retorno do hook ao dispatcher e invalido")


def remote_thread_call(
    process: int, address: int, parameter: int, label: str, timeout_ms: int = 15000
) -> int:
    thread_id = wt.DWORD()
    thread = kernel32.CreateRemoteThread(
        process,
        None,
        0,
        ctypes.c_void_p(address),
        ctypes.c_void_p(parameter),
        0,
        ctypes.byref(thread_id),
    )
    if not thread:
        raise win_error(f"CreateRemoteThread({label})")
    try:
        wait_result = kernel32.WaitForSingleObject(thread, timeout_ms)
        if wait_result == WAIT_TIMEOUT:
            raise InjectionError(f"tempo esgotado na chamada remota '{label}'")
        if wait_result != WAIT_OBJECT_0:
            raise win_error(f"WaitForSingleObject({label})")
        exit_code = wt.DWORD()
        if not kernel32.GetExitCodeThread(thread, ctypes.byref(exit_code)):
            raise win_error(f"GetExitCodeThread({label})")
        return exit_code.value
    finally:
        kernel32.CloseHandle(thread)


def remote_system_function(pid: int, function_name: bytes) -> int:
    kernel_module = kernel32.GetModuleHandleW("kernel32.dll")
    if not kernel_module:
        raise win_error("GetModuleHandleW(kernel32.dll)")
    function = kernel32.GetProcAddress(kernel_module, function_name)
    if not function:
        raise win_error(f"GetProcAddress({function_name.decode('ascii')})")

    containing_module = wt.HMODULE()
    flags = (
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT
    )
    if not kernel32.GetModuleHandleExW(
        flags, ctypes.c_void_p(function), ctypes.byref(containing_module)
    ):
        raise win_error("GetModuleHandleExW(endereco de funcao)")
    path_buffer = ctypes.create_unicode_buffer(32768)
    length = kernel32.GetModuleFileNameW(
        containing_module, path_buffer, len(path_buffer)
    )
    if length == 0 or length == len(path_buffer):
        raise win_error("GetModuleFileNameW(modulo da funcao)")

    local_base = int(containing_module.value)
    remote_base = find_remote_module_by_name(pid, Path(path_buffer.value).name)
    return remote_base + int(function) - local_base


def exported_function_rva(dll_path: Path, export_name: bytes) -> int:
    module = kernel32.LoadLibraryExW(
        str(dll_path), None, DONT_RESOLVE_DLL_REFERENCES
    )
    if not module:
        raise win_error(f"LoadLibraryExW({dll_path.name})")
    try:
        function = kernel32.GetProcAddress(module, export_name)
        if not function:
            raise win_error(f"GetProcAddress({export_name.decode('ascii')})")
        rva = int(function) - int(module)
        if rva <= 0:
            raise InjectionError(
                f"RVA invalido para exportacao {export_name.decode('ascii')}"
            )
        return rva
    finally:
        if not kernel32.FreeLibrary(module):
            raise win_error(f"FreeLibrary local({dll_path.name})")


def load_native_dll(process: int, pid: int) -> int:
    if find_remote_module(pid, NATIVE_DLL_PATH) is not None:
        raise InjectionError(f"DLL nativo ja esta carregado: {NATIVE_DLL_PATH}")
    encoded_path = (str(NATIVE_DLL_PATH.resolve()) + "\0").encode("utf-16-le")
    remote_path = int(
        kernel32.VirtualAllocEx(
            process,
            None,
            len(encoded_path),
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE,
        )
        or 0
    )
    if not remote_path:
        raise win_error("VirtualAllocEx(caminho do DLL)")
    try:
        write_memory(process, remote_path, encoded_path)
        load_library = remote_system_function(pid, b"LoadLibraryW")
        remote_thread_call(process, load_library, remote_path, "LoadLibraryW")
    finally:
        if not kernel32.VirtualFreeEx(
            process, ctypes.c_void_p(remote_path), 0, MEM_RELEASE
        ):
            raise win_error("VirtualFreeEx(caminho do DLL)")

    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        module = find_remote_module(pid, NATIVE_DLL_PATH)
        if module is not None:
            return module
        time.sleep(0.05)
    raise InjectionError("LoadLibraryW nao carregou o DLL nativo no processo do jogo")


def native_status(process: int, state_address: int) -> tuple[int, int, str]:
    raw = read_memory(
        process,
        state_address + NATIVE_STATUS_OFFSET,
        NATIVE_ERROR_MESSAGE_OFFSET + NATIVE_ERROR_MESSAGE_SIZE - NATIVE_STATUS_OFFSET,
    )
    status, error_code = struct.unpack_from("<ii", raw, 0)
    message_start = NATIVE_ERROR_MESSAGE_OFFSET - NATIVE_STATUS_OFFSET
    message_raw = raw[message_start:message_start + NATIVE_ERROR_MESSAGE_SIZE]
    message = message_raw.split(b"\0", 1)[0].decode("ascii", errors="strict")
    return status, error_code, message


def wait_for_renderer(process: int, state_address: int) -> None:
    deadline = time.monotonic() + 15.0
    while time.monotonic() < deadline:
        if not game_is_active(process):
            raise InjectionError("o jogo encerrou durante a inicializacao DirectX 12")
        status, error_code, message = native_status(process, state_address)
        if status == STATUS_RENDER_READY:
            return
        if status == STATUS_FAILED:
            raise InjectionError(
                f"renderer nativo falhou (codigo {error_code}): {message}"
            )
        if status not in (STATUS_WAITING, STATUS_HOOKS_INSTALLED):
            raise InjectionError(f"estado nativo desconhecido: {status}")
        time.sleep(0.05)
    raise InjectionError(
        "renderer DirectX 12 nao ficou pronto em 15 segundos; "
        "nenhum patch do Main Menu foi aplicado"
    )


def inject() -> None:
    require_file_hash(EXE_PATH, GAME_EXE_SHA256, "executavel")
    require_file_hash(FAD_PATH, PATCHED_FAD_SHA256, "atlas do Main Menu")
    require_file_hash(NATIVE_DLL_PATH, NATIVE_DLL_SHA256, "renderer nativo")
    load_registry()

    pid, process = find_game_process()
    state_address = 0
    code_address = 0
    remote_dll = 0
    native_initialized = False
    installed: list[tuple[int, Patch]] = []
    stop_requested = False

    def request_stop(*_args: object) -> None:
        nonlocal stop_requested
        stop_requested = True

    signal.signal(signal.SIGINT, request_stop)
    try:
        base = module_base(process)
        patches = [
            Patch(
                "registro do estado nativo de animacao 4",
                0x006FD501,
                bytes.fromhex("0F8449040000"),
                b"\x90" * 6,
            ),
            Patch(
                "criacao do quinto item",
                0x002B8A21,
                bytes.fromhex("0F84C3020000"),
                b"\x90" * 6,
            ),
            Patch("limite de navegacao", 0x002B907D, b"\x03", b"\x04"),
        ]
        dispatcher_rva = 0x002B9582
        dispatcher_expected = bytes.fromhex(
            "488D8F00020000488D05F0517700"
        )

        for patch in patches:
            actual = read_memory(process, base + patch.rva, len(patch.expected))
            if actual != patch.expected:
                raise InjectionError(
                    f"assinatura divergente em '{patch.name}' "
                    f"(RVA 0x{patch.rva:X}): esperado={patch.expected.hex()}; "
                    f"encontrado={actual.hex()}"
                )
        actual_dispatcher = read_memory(
            process, base + dispatcher_rva, len(dispatcher_expected)
        )
        if actual_dispatcher != dispatcher_expected:
            raise InjectionError(
                "assinatura divergente no dispatcher do quinto item: "
                f"esperado={dispatcher_expected.hex()}; encontrado={actual_dispatcher.hex()}"
            )

        state_address = int(
            kernel32.VirtualAllocEx(
                process,
                None,
                STATE_SIZE,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE,
            )
            or 0
        )
        if not state_address:
            raise win_error("VirtualAllocEx(estado)")
        write_memory(process, state_address, b"\0" * STATE_SIZE)

        remote_dll = load_native_dll(process, pid)
        initialize_rva = exported_function_rva(NATIVE_DLL_PATH, b"InitializeModMenu")
        if remote_thread_call(
            process,
            remote_dll + initialize_rva,
            state_address,
            "InitializeModMenu",
        ) != 1:
            status, error_code, message = native_status(process, state_address)
            raise InjectionError(
                "InitializeModMenu recusou a inicializacao "
                f"(estado={status}, codigo={error_code}, mensagem={message!r})"
            )
        native_initialized = True
        wait_for_renderer(process, state_address)

        code_address = int(
            kernel32.VirtualAllocEx(
                process,
                None,
                CODE_SIZE,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE,
            )
            or 0
        )
        if not code_address:
            raise win_error("VirtualAllocEx(codigo)")
        hook = build_hook(state_address, base + 0x002B95B9)
        validate_hook(hook)
        write_memory(process, code_address, hook)
        old_protection = wt.DWORD()
        if not kernel32.VirtualProtectEx(
            process,
            ctypes.c_void_p(code_address),
            CODE_SIZE,
            PAGE_EXECUTE_READ,
            ctypes.byref(old_protection),
        ):
            raise win_error("VirtualProtectEx(codigo do hook)")
        if not kernel32.FlushInstructionCache(
            process, ctypes.c_void_p(code_address), len(hook)
        ):
            raise win_error("FlushInstructionCache(hook)")

        dispatcher_patch = Patch(
            "interceptacao anterior ao fechamento do Main Menu",
            dispatcher_rva,
            dispatcher_expected,
            build_hook_jump(code_address),
        )
        runtime_patches = [*patches, dispatcher_patch]
        with suspended_process_threads(pid):
            for patch in runtime_patches:
                address = base + patch.rva
                actual = read_memory(process, address, len(patch.expected))
                if actual != patch.expected:
                    raise InjectionError(
                        f"assinatura mudou durante a instalacao de '{patch.name}'"
                    )
                write_code(process, address, patch.replacement)
                installed.append((address, patch))
                if read_memory(process, address, len(patch.replacement)) != patch.replacement:
                    raise InjectionError(f"verificacao do patch '{patch.name}' falhou")

        print("=" * 68)
        print(" DISGAEA MAYHEM - MOD MENU DIRECTX 12")
        print("=" * 68)
        print(f"[OK] Processo validado: PID {pid}, base 0x{base:X}")
        print("[OK] Renderer DirectX 12 carregado dentro do jogo.")
        print("[OK] Quinto item Mods conectado ao Mod Manager.")
        print("[INFO] Feche e abra o Main Menu; depois selecione Mods.")
        print("[INFO] B ou Esc retorna ao Main Menu.")
        print("[INFO] Mantenha este monitor aberto ate encerrar o jogo.")
        print("=" * 68, flush=True)

        while game_is_active(process):
            if stop_requested:
                raise InjectionError("encerramento solicitado pelo usuario")
            status, error_code, message = native_status(process, state_address)
            if status == STATUS_FAILED:
                raise InjectionError(
                    f"renderer nativo falhou (codigo {error_code}): {message}"
                )
            if status not in (STATUS_HOOKS_INSTALLED, STATUS_RENDER_READY):
                raise InjectionError(f"estado nativo inesperado durante execucao: {status}")
            time.sleep(0.05)

        installed.clear()
        state_address = 0
        code_address = 0
        remote_dll = 0
        native_initialized = False
    except Exception as original_error:
        rollback_errors: list[str] = []
        if game_is_active(process):
            patches_restored = False
            try:
                with suspended_process_threads(pid):
                    for address, patch in reversed(installed):
                        actual = read_memory(process, address, len(patch.replacement))
                        if actual != patch.replacement:
                            raise InjectionError(
                                f"patch '{patch.name}' mudou antes do rollback"
                            )
                        write_code(process, address, patch.expected)
                patches_restored = True
                installed.clear()
            except InjectionError as error:
                rollback_errors.append(f"patches: {error}")

            renderer_stopped = not native_initialized
            if patches_restored and remote_dll and native_initialized:
                try:
                    shutdown_rva = exported_function_rva(
                        NATIVE_DLL_PATH, b"ShutdownModMenu"
                    )
                    if remote_thread_call(
                        process,
                        remote_dll + shutdown_rva,
                        0,
                        "ShutdownModMenu",
                    ) != 1:
                        raise InjectionError("ShutdownModMenu recusou o encerramento")
                    native_initialized = False
                    renderer_stopped = True
                except InjectionError as error:
                    rollback_errors.append(f"renderer: {error}")

            # O runtime estatico do MinGW registra finalizadores com vida util
            # de processo. Depois de ShutdownModMenu, o DLL fica inerte e
            # carregado ate o jogo terminar; descarrega-lo antecipadamente
            # invalidaria esses finalizadores.
            if patches_restored and renderer_stopped:
                if code_address and not kernel32.VirtualFreeEx(
                    process, ctypes.c_void_p(code_address), 0, MEM_RELEASE
                ):
                    rollback_errors.append(str(win_error("VirtualFreeEx(codigo)")))
                else:
                    code_address = 0
                if state_address and not kernel32.VirtualFreeEx(
                    process, ctypes.c_void_p(state_address), 0, MEM_RELEASE
                ):
                    rollback_errors.append(str(win_error("VirtualFreeEx(estado)")))
                else:
                    state_address = 0

        if rollback_errors:
            raise InjectionError(
                f"{original_error}; rollback incompleto: " + "; ".join(rollback_errors)
            ) from original_error
        raise
    finally:
        kernel32.CloseHandle(process)


def main() -> int:
    try:
        inject()
    except (InjectionError, OSError, UnicodeError) as error:
        print(f"ERRO: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
