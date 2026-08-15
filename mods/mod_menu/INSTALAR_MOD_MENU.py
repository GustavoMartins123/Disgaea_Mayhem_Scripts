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

GAME_EXE_NAME = "Disgaea_Mayhem.exe"
GAME_EXE_SHA256 = "13988368F66ADE40205C1D0D18157B6AE2D7736D67AC0C8734FE1DD4E62D5B41"
ORIGINAL_FAD_SHA256 = "3236B852AF9DED7610EA6F5FA1E018993ED730CD032631E20AA11D9EF3650F8C"
PATCHED_FAD_SHA256 = "81429742F0410E20813B8C300F6A6B633E9FE2598A72DEF5EB6CC16FB540EAA5"
SLOT_DDS_SHA256 = "677B8F78A365C0E67C23DFF29E52925FF8A148949A6A130B77317133B00B3001"
LZ4_DLL_SHA256 = "E0B615D8F9CD414B718AB00E70E5709F0483992383D9078A81ACF46ABBFD5FDA"

def find_game_root() -> Path:
    current = Path(__file__).resolve().parent
    for p in [current, current.parent, current.parent.parent]:
        if (p / GAME_EXE_NAME).is_file():
            return p
    return current

ROOT = find_game_root()
sys.path.insert(0, str(ROOT))
from tools.fad_texture_tool import FormatError, command_patch_bc7_region

EXE_PATH = ROOT / GAME_EXE_NAME
FAD_PATH = ROOT / "data" / "fairy" / "AnmDat_1_00_EN.fad"
BACKUP_PATH = ROOT / "data" / "fairy" / "AnmDat_1_00_EN.fad.mod-menu-original"
SLOT_DDS_PATH = ROOT / "mods" / "mod_menu" / "main_menu" / "mods_slot.dds"
if not SLOT_DDS_PATH.is_file():
    SLOT_DDS_PATH = ROOT / "mods" / "main_menu" / "mods_slot.dds"
LZ4_DLL_PATH = ROOT / "lz4.dll"

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
                code = ctypes.get_last_error()
                if code != 18:
                    raise InstallError(f"Process32NextW falhou (Win32 {code})")
                break
    finally:
        kernel32.CloseHandle(snapshot)
    return False


def install() -> None:
    if game_is_running():
        raise InstallError(
            "o jogo esta aberto; feche o executavel antes de instalar o atlas"
        )
    require_hash(EXE_PATH, GAME_EXE_SHA256, "executavel")
    require_hash(SLOT_DDS_PATH, SLOT_DDS_SHA256, "asset do quinto item")
    require_hash(LZ4_DLL_PATH, LZ4_DLL_SHA256, "biblioteca LZ4 do jogo")
    if not FAD_PATH.is_file():
        raise InstallError(f"atlas do Main Menu ausente: {FAD_PATH}")

    current_hash = sha256(FAD_PATH)
    if current_hash == PATCHED_FAD_SHA256:
        print("[OK] O atlas do Main Menu ja contem o rotulo Mods.")
        return
    if current_hash != ORIGINAL_FAD_SHA256:
        raise InstallError(
            "atlas do Main Menu possui SHA-256 desconhecido; "
            f"encontrado={current_hash}"
        )

    if not BACKUP_PATH.is_file():
        shutil.copyfile(FAD_PATH, BACKUP_PATH)

    temp_fd, temp_path_str = tempfile.mkstemp(
        prefix="AnmDat_1_00_EN.", suffix=".tmp", dir=str(FAD_PATH.parent)
    )
    os.close(temp_fd)
    temp_path = Path(temp_path_str)
    try:
        try:
            command_patch_bc7_region(
                archive_path=FAD_PATH,
                output_path=temp_path,
                source_identifier="UI_05100",
                patch_dds_path=SLOT_DDS_PATH,
                x=0,
                y=320,
                lz4_dll_path=LZ4_DLL_PATH,
            )
        except FormatError as error:
            raise InstallError(f"falha ao aplicar textura no FAD: {error}") from error

        require_hash(temp_path, PATCHED_FAD_SHA256, "atlas gerado")
        os.replace(temp_path, FAD_PATH)
        require_hash(FAD_PATH, PATCHED_FAD_SHA256, "atlas instalado")
    finally:
        if temp_path.is_file():
            temp_path.unlink()

    print("[OK] Rotulo Mods instalado com sucesso no atlas do Main Menu.")


def main() -> int:
    try:
        install()
    except (InstallError, OSError) as error:
        print(f"ERRO: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
