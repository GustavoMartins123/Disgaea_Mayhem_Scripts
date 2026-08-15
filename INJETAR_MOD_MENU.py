#!/usr/bin/env python3
"""Injeta o quinto item "Mods" no Main Menu de Disgaea Mayhem.

Este injetor e deliberadamente especifico para a versao conhecida do jogo.
Assinaturas, executavel, atlas e registro divergentes encerram a execucao com
erro, sem procurar offsets alternativos.
"""

from __future__ import annotations

import ctypes
from ctypes import wintypes as wt
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import signal
import struct
import sys
import time
import tkinter as tk
from tkinter import messagebox


GAME_EXE_NAME = "Disgaea_Mayhem.exe"
GAME_EXE_SHA256 = "13988368F66ADE40205C1D0D18157B6AE2D7736D67AC0C8734FE1DD4E62D5B41"
PATCHED_FAD_SHA256 = "81429742F0410E20813B8C300F6A6B633E9FE2598A72DEF5EB6CC16FB540EAA5"

PROCESS_QUERY_INFORMATION = 0x0400
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
GWLP_HWNDPARENT = -8
TH32CS_SNAPTHREAD = 0x00000004
THREAD_SUSPEND_RESUME = 0x0002
THREAD_QUERY_INFORMATION = 0x0040
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
ERROR_NO_MORE_FILES = 18

EVENT_OFFSET = 0
PASS_GIVE_UP_OFFSET = 1
CALLBACK_CONTEXT_OFFSET = 8
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


ROOT = Path(__file__).resolve().parent
EXE_PATH = ROOT / GAME_EXE_NAME
FAD_PATH = ROOT / "data" / "fairy" / "AnmDat_1_00_EN.fad"
REGISTRY_PATH = ROOT / "mods" / "registry.json"

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
user32 = ctypes.WinDLL("user32", use_last_error=True)

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
kernel32.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
kernel32.OpenThread.restype = wt.HANDLE
kernel32.SuspendThread.argtypes = [wt.HANDLE]
kernel32.SuspendThread.restype = wt.DWORD
kernel32.ResumeThread.argtypes = [wt.HANDLE]
kernel32.ResumeThread.restype = wt.DWORD
psapi.EnumProcesses.argtypes = [ctypes.POINTER(wt.DWORD), wt.DWORD, ctypes.POINTER(wt.DWORD)]
psapi.EnumProcesses.restype = wt.BOOL
psapi.EnumProcessModules.argtypes = [
    wt.HANDLE,
    ctypes.POINTER(ctypes.c_void_p),
    wt.DWORD,
    ctypes.POINTER(wt.DWORD),
]
psapi.EnumProcessModules.restype = wt.BOOL
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
user32.GetWindowThreadProcessId.restype = wt.DWORD
user32.IsWindowVisible.argtypes = [wt.HWND]
user32.IsWindowVisible.restype = wt.BOOL
user32.GetWindow.argtypes = [wt.HWND, wt.UINT]
user32.GetWindow.restype = wt.HWND
user32.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
user32.GetClientRect.restype = wt.BOOL
user32.ClientToScreen.argtypes = [wt.HWND, ctypes.POINTER(wt.POINT)]
user32.ClientToScreen.restype = wt.BOOL
user32.SetForegroundWindow.argtypes = [wt.HWND]
user32.SetForegroundWindow.restype = wt.BOOL


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

    access = (
        PROCESS_QUERY_INFORMATION
        | PROCESS_VM_OPERATION
        | PROCESS_VM_READ
        | PROCESS_VM_WRITE
    )
    matches: list[tuple[int, int]] = []
    for pid in process_ids[: needed.value // ctypes.sizeof(wt.DWORD)]:
        if not pid:
            continue
        handle = kernel32.OpenProcess(access, False, pid)
        if not handle:
            continue
        try:
            path = process_path(handle)
        except InjectionError:
            kernel32.CloseHandle(handle)
            continue
        if path.name.casefold() == GAME_EXE_NAME.casefold():
            if path != EXE_PATH.resolve():
                kernel32.CloseHandle(handle)
                raise InjectionError(
                    f"processo com mesmo nome pertence a outra instalacao: {path}"
                )
            matches.append((pid, handle))
        else:
            kernel32.CloseHandle(handle)

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


@contextmanager
def suspended_process_threads(pid: int):
    """Suspende todas as threads enumeradas para escrever codigo sem tearing."""
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


def read_u64(handle: int, address: int) -> int:
    return struct.unpack("<Q", read_memory(handle, address, 8))[0]


def load_registry() -> dict:
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
            "o registro contem modulos, mas este loader ainda nao possui ABI para executa-los"
        )
    return value


def build_hook(state_address: int, give_up_handler: int) -> bytes:
    # RCX aponta para o objeto std::function do quinto item. O caminho normal
    # sinaliza o monitor; PASS_GIVE_UP redireciona uma confirmacao explicita
    # para o handler original existente no jogo.
    return b"".join(
        (
            b"\x48\xB8" + struct.pack("<Q", state_address),
            b"\x80\x78\x01\x00",       # cmp byte ptr [rax+1], 0
            b"\x75\x0C",               # jne give_up
            b"\x48\x8D\x51\x08",      # lea rdx, [rcx+8]
            b"\x48\x89\x50\x08",      # mov [rax+8], rdx
            b"\xC6\x00\x01",           # mov byte ptr [rax], 1
            b"\xC3",                     # ret
            b"\xC6\x40\x01\x00",      # give_up: mov byte ptr [rax+1], 0
            b"\x48\x83\xC1\x08",      # add rcx, 8
            b"\x48\xB8" + struct.pack("<Q", give_up_handler),
            b"\xFF\xE0",                # jmp rax
        )
    )


def build_hook_jump(hook_address: int) -> bytes:
    return b"\x48\xB8" + struct.pack("<Q", hook_address) + b"\xFF\xE0" + b"\x90" * 4


def game_is_active(handle: int) -> bool:
    exit_code = wt.DWORD()
    if not kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code)):
        raise win_error("GetExitCodeProcess")
    return exit_code.value == STILL_ACTIVE


class ModMenu:
    def __init__(self, process_handle: int, state_address: int, base: int, pid: int):
        self.process_handle = process_handle
        self.state_address = state_address
        self.base = base
        self.pid = pid
        self.overlay: tk.Toplevel | None = None
        self.root = tk.Tk()
        self.root.withdraw()
        self.root.title("Disgaea Mayhem Mod Menu Monitor")
        self.stopping = False
        self.pass_deadline: float | None = None
        signal.signal(signal.SIGINT, self.request_stop)

    def request_stop(self, *_args: object) -> None:
        self.stopping = True

    def callback_context(self) -> int:
        return read_u64(
            self.process_handle, self.state_address + CALLBACK_CONTEXT_OFFSET
        )

    def give_up_available(self) -> bool:
        callback_context = self.callback_context()
        if callback_context == 0:
            raise InjectionError("contexto do quinto item nao foi capturado")
        main_task = read_u64(self.process_handle, callback_context)
        state = read_u64(self.process_handle, main_task + 0x218)
        context = read_u64(self.process_handle, state + 0x200)
        return read_memory(self.process_handle, context + 0x36, 1) != b"\0"

    def close_overlay(self) -> None:
        if self.overlay is not None:
            try:
                self.overlay.grab_release()
            except tk.TclError:
                pass
            self.overlay.destroy()
            self.overlay = None

    def invoke_give_up(self) -> None:
        write_memory(
            self.process_handle,
            self.state_address + PASS_GIVE_UP_OFFSET,
            b"\x01",
        )
        self.pass_deadline = time.monotonic() + 10.0
        self.close_overlay()
        window = find_game_window(self.pid)
        if not user32.SetForegroundWindow(window):
            write_memory(
                self.process_handle,
                self.state_address + PASS_GIVE_UP_OFFSET,
                b"\x00",
            )
            self.pass_deadline = None
            raise win_error("SetForegroundWindow")

    def open_overlay(self) -> None:
        if self.overlay is not None:
            self.overlay.lift()
            return
        window = find_game_window(self.pid)
        left, top, right, bottom = client_bounds_on_screen(window)
        width = min(760, max(520, right - left - 120))
        height = min(500, max(360, bottom - top - 100))
        x = left + ((right - left) - width) // 2
        y = top + ((bottom - top) - height) // 2

        overlay = tk.Toplevel(self.root)
        self.overlay = overlay
        overlay.overrideredirect(True)
        overlay.configure(background="#15121d")
        overlay.geometry(f"{width}x{height}+{x}+{y}")
        overlay.attributes("-topmost", True)
        overlay.update_idletasks()
        set_owner(overlay.winfo_id(), window)
        overlay.bind("<Escape>", lambda _event: self.close_overlay())

        border = tk.Frame(overlay, background="#ffae27", padx=3, pady=3)
        border.pack(fill="both", expand=True)
        body = tk.Frame(border, background="#15121d")
        body.pack(fill="both", expand=True)
        header = tk.Frame(body, background="#8d1e2c", height=78)
        header.pack(fill="x")
        header.pack_propagate(False)
        tk.Label(
            header,
            text="MOD MANAGER",
            foreground="#fff1a8",
            background="#8d1e2c",
            font=("Segoe UI", 27, "bold"),
        ).pack(side="left", padx=26)
        tk.Button(
            header,
            text="  X  ",
            command=self.close_overlay,
            foreground="white",
            background="#5c111d",
            activebackground="#b62b3d",
            activeforeground="white",
            relief="flat",
            font=("Segoe UI", 12, "bold"),
        ).pack(side="right", padx=18, pady=18)

        content = tk.Frame(body, background="#15121d")
        content.pack(fill="both", expand=True, padx=32, pady=30)
        tk.Label(
            content,
            text="Nenhum mod nativo registrado.",
            foreground="#f4f0f8",
            background="#15121d",
            font=("Segoe UI", 18, "bold"),
        ).pack(anchor="w")
        tk.Label(
            content,
            text=(
                "O quinto item esta conectado ao loader. Modulos futuros precisam "
                "implementar a ABI nativa antes de entrarem neste registro."
            ),
            foreground="#aaa4b7",
            background="#15121d",
            font=("Segoe UI", 11),
            justify="left",
            wraplength=width - 80,
        ).pack(anchor="w", pady=(10, 24))

        actions = tk.Frame(content, background="#15121d")
        actions.pack(side="bottom", fill="x")
        try:
            can_give_up = self.give_up_available()
        except InjectionError as error:
            self.close_overlay()
            raise InjectionError(f"nao foi possivel validar Give Up: {error}") from error
        if can_give_up:
            tk.Button(
                actions,
                text="Give Up original — confirme novamente no jogo",
                command=self.invoke_give_up,
                foreground="white",
                background="#8d1e2c",
                activebackground="#b62b3d",
                activeforeground="white",
                relief="flat",
                font=("Segoe UI", 12, "bold"),
                padx=18,
                pady=10,
            ).pack(side="left")
        tk.Button(
            actions,
            text="Voltar  [Esc]",
            command=self.close_overlay,
            foreground="#1d1824",
            background="#ffbf36",
            activebackground="#ffd66b",
            relief="flat",
            font=("Segoe UI", 12, "bold"),
            padx=20,
            pady=10,
        ).pack(side="right")
        overlay.grab_set()
        overlay.focus_force()

    def poll(self) -> None:
        if self.stopping:
            if game_is_active(self.process_handle):
                messagebox.showerror(
                    "Mod Menu",
                    "O monitor so pode encerrar depois que o jogo for fechado.",
                    parent=self.overlay,
                )
                self.stopping = False
            else:
                self.root.quit()
                return
        if not game_is_active(self.process_handle):
            self.root.quit()
            return

        state = read_memory(self.process_handle, self.state_address, 2)
        if state[EVENT_OFFSET]:
            write_memory(
                self.process_handle, self.state_address + EVENT_OFFSET, b"\x00"
            )
            try:
                self.open_overlay()
            except InjectionError as error:
                messagebox.showerror("Mod Menu", str(error), parent=self.root)

        if self.pass_deadline is not None and not state[PASS_GIVE_UP_OFFSET]:
            self.pass_deadline = None
        elif self.pass_deadline is not None and time.monotonic() >= self.pass_deadline:
            write_memory(
                self.process_handle,
                self.state_address + PASS_GIVE_UP_OFFSET,
                b"\x00",
            )
            self.pass_deadline = None
            messagebox.showerror(
                "Mod Menu",
                "O jogo nao confirmou a chamada de Give Up no prazo esperado.",
                parent=self.root,
            )
        self.root.after(40, self.poll)

    def run(self) -> None:
        self.root.after(40, self.poll)
        self.root.mainloop()
        self.close_overlay()
        self.root.destroy()


WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]
user32.EnumWindows.restype = wt.BOOL


def find_game_window(pid: int) -> int:
    matches: list[int] = []

    @WNDENUMPROC
    def callback(window: int, _parameter: int) -> bool:
        window_pid = wt.DWORD()
        user32.GetWindowThreadProcessId(window, ctypes.byref(window_pid))
        if window_pid.value == pid and user32.IsWindowVisible(window) and not user32.GetWindow(window, 4):
            matches.append(window)
        return True

    if not user32.EnumWindows(callback, 0):
        raise win_error("EnumWindows")
    if len(matches) != 1:
        raise InjectionError(
            f"esperada uma janela principal visivel do jogo; encontradas={len(matches)}"
        )
    return matches[0]


def client_bounds_on_screen(window: int) -> tuple[int, int, int, int]:
    rect = wt.RECT()
    if not user32.GetClientRect(window, ctypes.byref(rect)):
        raise win_error("GetClientRect")
    origin = wt.POINT(0, 0)
    if not user32.ClientToScreen(window, ctypes.byref(origin)):
        raise win_error("ClientToScreen")
    return origin.x, origin.y, origin.x + rect.right, origin.y + rect.bottom


def set_owner(overlay: int, game_window: int) -> None:
    setter = user32.SetWindowLongPtrW
    setter.argtypes = [wt.HWND, ctypes.c_int, ctypes.c_void_p]
    setter.restype = ctypes.c_void_p
    ctypes.set_last_error(0)
    previous = setter(overlay, GWLP_HWNDPARENT, ctypes.c_void_p(game_window))
    if not previous and ctypes.get_last_error():
        raise win_error("SetWindowLongPtrW")


def inject() -> None:
    require_file_hash(EXE_PATH, GAME_EXE_SHA256, "executavel")
    require_file_hash(FAD_PATH, PATCHED_FAD_SHA256, "atlas do Main Menu")
    load_registry()

    pid, process = find_game_process()
    state_address = 0
    code_address = 0
    installed: list[tuple[int, bytes]] = []
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
        callback_rva = 0x002EFB70
        callback_expected = bytes.fromhex(
            "4883C108E9C7A9FCFFCCCCCCCCCCCCCC"
        )
        if len(callback_expected) != 16:
            raise AssertionError("assinatura interna do callback invalida")

        with suspended_process_threads(pid):
            for patch in patches:
                actual = read_memory(process, base + patch.rva, len(patch.expected))
                if actual != patch.expected:
                    raise InjectionError(
                        f"assinatura divergente em '{patch.name}' "
                        f"(RVA 0x{patch.rva:X}): esperado={patch.expected.hex()}; "
                        f"encontrado={actual.hex()}"
                    )
            actual_callback = read_memory(process, base + callback_rva, 16)
            if actual_callback != callback_expected:
                raise InjectionError(
                    "assinatura divergente no callback do quinto item: "
                    f"esperado={callback_expected.hex()}; encontrado={actual_callback.hex()}"
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

            write_memory(process, state_address, b"\0" * 16)
            hook = build_hook(state_address, base + 0x002BA540)
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

            callback_patch = Patch(
                "callback do quinto item",
                callback_rva,
                callback_expected,
                build_hook_jump(code_address),
            )
            runtime_patches = [callback_patch, *patches]
            for patch in runtime_patches:
                address = base + patch.rva
                write_code(process, address, patch.replacement)
                installed.append((address, patch.expected))
                if read_memory(process, address, len(patch.replacement)) != patch.replacement:
                    raise InjectionError(f"verificacao do patch '{patch.name}' falhou")

        print("=" * 68)
        print(" DISGAEA MAYHEM - MOD MENU NATIVO")
        print("=" * 68)
        print(f"[OK] Processo validado: PID {pid}, base 0x{base:X}")
        print("[OK] Quinto item criado abaixo de System e callback conectado.")
        print("[INFO] Abra o Main Menu somente depois desta confirmacao.")
        print("[INFO] Mantenha este monitor aberto ate encerrar o jogo.")
        print("=" * 68)
        ModMenu(process, state_address, base, pid).run()
        installed.clear()  # o processo terminou; seus enderecos deixaram de existir
        state_address = 0
        code_address = 0
    except Exception:
        if game_is_active(process):
            rollback_complete = False
            try:
                with suspended_process_threads(pid):
                    for address, original in reversed(installed):
                        write_code(process, address, original)
                rollback_complete = True
            except InjectionError as rollback_error:
                print(f"ERRO DE ROLLBACK: {rollback_error}", file=sys.stderr)
            if rollback_complete:
                if code_address:
                    kernel32.VirtualFreeEx(
                        process, ctypes.c_void_p(code_address), 0, MEM_RELEASE
                    )
                if state_address:
                    kernel32.VirtualFreeEx(
                        process, ctypes.c_void_p(state_address), 0, MEM_RELEASE
                    )
        raise
    finally:
        kernel32.CloseHandle(process)


def main() -> int:
    try:
        inject()
    except (InjectionError, OSError, tk.TclError) as error:
        print(f"ERRO: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
