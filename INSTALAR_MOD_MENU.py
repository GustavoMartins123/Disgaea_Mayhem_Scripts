#!/usr/bin/env python3
"""Instala, de forma transacional, o rotulo Mods no atlas do Main Menu."""

from __future__ import annotations

import ctypes
from ctypes import wintypes as wt
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tempfile

from tools.fad_texture_tool import FormatError, command_patch_bc7_region


GAME_EXE_NAME = "Disgaea_Mayhem.exe"
GAME_EXE_SHA256 = "13988368F66ADE40205C1D0D18157B6AE2D7736D67AC0C8734FE1DD4E62D5B41"
ORIGINAL_FAD_SHA256 = "3236B852AF9DED7610EA6F5FA1E018993ED730CD032631E20AA11D9EF3650F8C"
PATCHED_FAD_SHA256 = "81429742F0410E20813B8C300F6A6B633E9FE2598A72DEF5EB6CC16FB540EAA5"
SLOT_DDS_SHA256 = "677B8F78A365C0E67C23DFF29E52925FF8A148949A6A130B77317133B00B3001"
LZ4_DLL_SHA256 = "E0B615D8F9CD414B718AB00E70E5709F0483992383D9078A81ACF46ABBFD5FDA"
NATIVE_DLL_SHA256 = "7D573CA7D44C88482D78FCB3D615437580D8ED0F367B2874A5AC77DFF5C947AD"

ROOT = Path(__file__).resolve().parent
EXE_PATH = ROOT / GAME_EXE_NAME
FAD_PATH = ROOT / "data" / "fairy" / "AnmDat_1_00_EN.fad"
BACKUP_PATH = ROOT / "data" / "fairy" / "AnmDat_1_00_EN.fad.mod-menu-original"
SLOT_DDS_PATH = ROOT / "mods" / "main_menu" / "mods_slot.dds"
LZ4_DLL_PATH = ROOT / "lz4.dll"
NATIVE_DLL_PATH = ROOT / "mods" / "native" / "DisgaeaMayhemModMenu.dll"

TH32CS_SNAPPROCESS = 0x00000002
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


class InstallError(RuntimeError):
    """Falha explicita de instalacao."""


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wt.DWORD),
        ("cntUsage", wt.DWORD),
        ("th32ProcessID", wt.DWORD),
        ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
        ("th32ModuleID", wt.DWORD),
        ("cntThreads", wt.DWORD),
        ("th32ParentProcessID", wt.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wt.DWORD),
        ("szExeFile", wt.WCHAR * 260),
    ]


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
kernel32.CreateToolhelp32Snapshot.restype = wt.HANDLE
kernel32.Process32FirstW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
kernel32.Process32FirstW.restype = wt.BOOL
kernel32.Process32NextW.argtypes = [wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
kernel32.Process32NextW.restype = wt.BOOL
kernel32.CloseHandle.argtypes = [wt.HANDLE]
kernel32.CloseHandle.restype = wt.BOOL


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def require_hash(path: Path, expected: str, label: str) -> None:
    if not path.is_file():
        raise InstallError(f"{label} ausente: {path}")
    actual = sha256(path)
    if actual != expected:
        raise InstallError(
            f"{label} incompativel. SHA-256 esperado={expected}; encontrado={actual}"
        )


def game_is_running() -> bool:
    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snapshot == INVALID_HANDLE_VALUE:
        code = ctypes.get_last_error()
        raise InstallError(f"CreateToolhelp32Snapshot falhou (Win32 {code})")
    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(entry)
        if not kernel32.Process32FirstW(snapshot, ctypes.byref(entry)):
            code = ctypes.get_last_error()
            raise InstallError(f"Process32FirstW falhou (Win32 {code})")
        while True:
            if entry.szExeFile.casefold() == GAME_EXE_NAME.casefold():
                return True
            if not kernel32.Process32NextW(snapshot, ctypes.byref(entry)):
                break
        return False
    finally:
        kernel32.CloseHandle(snapshot)


def create_backup_transactionally() -> None:
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{BACKUP_PATH.name}.", suffix=".tmp", dir=BACKUP_PATH.parent
    )
    temporary_path = Path(temporary_name)
    try:
        with FAD_PATH.open("rb") as source, os.fdopen(descriptor, "wb") as destination:
            shutil.copyfileobj(source, destination, length=1024 * 1024)
            destination.flush()
            os.fsync(destination.fileno())
        require_hash(temporary_path, ORIGINAL_FAD_SHA256, "backup temporario")
        os.replace(temporary_path, BACKUP_PATH)
    except Exception:
        if temporary_path.exists():
            temporary_path.unlink()
        raise


def install() -> None:
    if game_is_running():
        raise InstallError("feche Disgaea_Mayhem.exe antes de alterar o atlas")
    require_hash(EXE_PATH, GAME_EXE_SHA256, "executavel")
    require_hash(SLOT_DDS_PATH, SLOT_DDS_SHA256, "asset do quinto item")
    require_hash(LZ4_DLL_PATH, LZ4_DLL_SHA256, "biblioteca LZ4 do jogo")
    require_hash(NATIVE_DLL_PATH, NATIVE_DLL_SHA256, "renderer DirectX 12")
    if not FAD_PATH.is_file():
        raise InstallError(f"atlas do Main Menu ausente: {FAD_PATH}")

    archive_hash = sha256(FAD_PATH)
    if archive_hash == PATCHED_FAD_SHA256:
        require_hash(BACKUP_PATH, ORIGINAL_FAD_SHA256, "backup transacional")
        print("[OK] Rotulo Mods ja esta instalado e foi validado.")
        return
    if archive_hash != ORIGINAL_FAD_SHA256:
        raise InstallError(
            "atlas incompativel; nenhuma alteracao foi feita. "
            f"SHA-256 encontrado={archive_hash}"
        )

    if BACKUP_PATH.exists():
        require_hash(BACKUP_PATH, ORIGINAL_FAD_SHA256, "backup transacional")
    else:
        create_backup_transactionally()
        require_hash(BACKUP_PATH, ORIGINAL_FAD_SHA256, "backup transacional")

    command_patch_bc7_region(
        FAD_PATH,
        "UI_05100_MainMenu02.tga",
        SLOT_DDS_PATH,
        768,
        128,
        LZ4_DLL_PATH,
    )
    require_hash(FAD_PATH, PATCHED_FAD_SHA256, "atlas instalado")
    print("[OK] Rotulo Mods instalado abaixo de System.")
    print(f"[OK] Backup original validado: {BACKUP_PATH.name}")


def main() -> int:
    try:
        install()
    except (InstallError, FormatError, OSError) as error:
        print(f"ERRO: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
