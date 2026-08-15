#!/usr/bin/env python3
"""Inspect and extract NMPLTEX1 textures stored in a Fairy .fad archive.

The tool is intentionally strict: an unsupported archive, texture format, or
malformed YKCMP_V1 stream is reported as an error instead of being guessed.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


FAD_TEXTURE_DATA_OFFSET_FIELD = 0x28
FAD_STRING_TABLE_OFFSET_FIELD = 0x34
TEXTURE_RECORD_HEADER_SIZE = 0x40
NMPLTEX_HEADER_SIZE = 0x80
NMPLTEX_MAGIC = b"NMPLTEX1"
YKCMP_MAGIC = b"YKCMP_V1"
TEXTURE_CONTAINER_FORMAT = 0x66
LINEAR_RGBA8_LAYOUT = 0x02000002
BLOCK_COMPRESSED_LAYOUT = 0x00800006
DXGI_FORMAT_BC7_UNORM = 98
DDS_HEADER_SIZE = 148


class FormatError(RuntimeError):
    """Raised when an input file does not match the supported format."""


def read_u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise FormatError(f"u32 fora do arquivo em 0x{offset:X}")
    return struct.unpack_from("<I", data, offset)[0]


def read_u64(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 8 > len(data):
        raise FormatError(f"u64 fora do arquivo em 0x{offset:X}")
    return struct.unpack_from("<Q", data, offset)[0]


def read_c_string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        raise FormatError(f"string fora do arquivo em 0x{offset:X}")
    end = data.find(b"\0", offset)
    if end < 0:
        raise FormatError(f"string sem terminador em 0x{offset:X}")
    return data[offset:end].decode("cp932")


@dataclass(frozen=True)
class TextureRecord:
    record_offset: int
    record_size: int
    name: str
    width: int
    height: int
    format_id: int
    layout_id: int
    compression_id: int
    ykcmp_offset: int
    ykcmp_size: int
    raw_size: int


def parse_texture_records(data: bytes) -> list[TextureRecord]:
    if len(data) < 0x40:
        raise FormatError("arquivo FAD pequeno demais")

    data_offset = read_u64(data, FAD_TEXTURE_DATA_OFFSET_FIELD)
    strings_offset = read_u64(data, FAD_STRING_TABLE_OFFSET_FIELD)
    if not (0 < strings_offset < data_offset < len(data)):
        raise FormatError(
            "offsets das tabelas FAD invalidos: "
            f"strings=0x{strings_offset:X}, texturas=0x{data_offset:X}"
        )

    records: list[TextureRecord] = []
    offset = data_offset
    while offset < len(data):
        if offset + TEXTURE_RECORD_HEADER_SIZE + NMPLTEX_HEADER_SIZE > len(data):
            raise FormatError(f"registro truncado em 0x{offset:X}")

        record_size = read_u64(data, offset + 0x08)
        if record_size == 0 or record_size % 0x10 != 0:
            raise FormatError(
                f"tamanho de registro invalido em 0x{offset:X}: 0x{record_size:X}"
            )
        if offset + record_size > len(data):
            raise FormatError(f"registro ultrapassa o arquivo em 0x{offset:X}")

        name_relative_offset = read_u32(data, offset + 0x10)
        name_offset = strings_offset + name_relative_offset
        if not (strings_offset <= name_offset < data_offset):
            raise FormatError(f"nome de textura fora da tabela em 0x{offset:X}")
        name = read_c_string(data, name_offset)

        nltx_offset = offset + TEXTURE_RECORD_HEADER_SIZE
        if data[nltx_offset : nltx_offset + 8] != NMPLTEX_MAGIC:
            raise FormatError(f"NMPLTEX1 ausente em 0x{nltx_offset:X}")
        format_id = read_u32(data, nltx_offset + 0x10)
        layout_id = read_u32(data, nltx_offset + 0x14)
        width = read_u32(data, nltx_offset + 0x18)
        height = read_u32(data, nltx_offset + 0x1C)
        ykcmp_relative_offset = read_u32(data, nltx_offset + 0x34)
        if ykcmp_relative_offset != NMPLTEX_HEADER_SIZE:
            raise FormatError(
                f"offset YKCMP nao suportado em {name}: 0x{ykcmp_relative_offset:X}"
            )

        ykcmp_offset = nltx_offset + ykcmp_relative_offset
        if data[ykcmp_offset : ykcmp_offset + 8] != YKCMP_MAGIC:
            raise FormatError(f"YKCMP_V1 ausente em {name}")
        compression_id = read_u32(data, ykcmp_offset + 0x08)
        ykcmp_size = read_u32(data, ykcmp_offset + 0x0C)
        raw_size = read_u32(data, ykcmp_offset + 0x10)
        if ykcmp_size < 0x14 or ykcmp_offset + ykcmp_size > offset + record_size:
            raise FormatError(f"stream YKCMP invalido em {name}")

        records.append(
            TextureRecord(
                record_offset=offset,
                record_size=record_size,
                name=name,
                width=width,
                height=height,
                format_id=format_id,
                layout_id=layout_id,
                compression_id=compression_id,
                ykcmp_offset=ykcmp_offset,
                ykcmp_size=ykcmp_size,
                raw_size=raw_size,
            )
        )
        offset += record_size

    if offset != len(data):
        raise FormatError(f"dados extras apos o ultimo registro em 0x{offset:X}")
    if not records:
        raise FormatError("nenhuma textura NMPLTEX1 encontrada")
    return records


def ykcmp_decompress(stream: bytes) -> bytes:
    if len(stream) < 0x14 or stream[:8] != YKCMP_MAGIC:
        raise FormatError("stream nao e YKCMP_V1")
    compressed_size = read_u32(stream, 0x0C)
    raw_size = read_u32(stream, 0x10)
    if compressed_size != len(stream):
        raise FormatError(
            f"tamanho YKCMP divergente: cabecalho={compressed_size}, real={len(stream)}"
        )

    source = 0x14
    output = bytearray()
    while source < len(stream) and len(output) < raw_size:
        command = stream[source]
        source += 1
        if command < 0x80:
            literal_size = command
            if literal_size == 0:
                continue
            end = source + literal_size
            if end > len(stream):
                raise FormatError("literal YKCMP truncado")
            output.extend(stream[source:end])
            source = end
            continue

        if command < 0xC0:
            copy_size = (command >> 4) - 0x8 + 1
            distance = (command & 0x0F) + 1
        elif command < 0xE0:
            if source >= len(stream):
                raise FormatError("referencia YKCMP de 2 bytes truncada")
            copy_size = command - 0xC0 + 2
            distance = stream[source] + 1
            source += 1
        else:
            if source + 1 >= len(stream):
                raise FormatError("referencia YKCMP de 3 bytes truncada")
            second = stream[source]
            third = stream[source + 1]
            source += 2
            copy_size = (command << 4) + (second >> 4) - 0xE00 + 3
            distance = ((second & 0x0F) << 8) + third + 1

        if distance > len(output):
            raise FormatError(
                f"referencia YKCMP invalida: distancia={distance}, saida={len(output)}"
            )
        for _ in range(copy_size):
            output.append(output[-distance])
            if len(output) == raw_size:
                break

    if len(output) != raw_size:
        raise FormatError(
            f"saida YKCMP incompleta: esperado={raw_size}, obtido={len(output)}"
        )
    return bytes(output)


def lz4_decompress_block(payload: bytes, raw_size: int) -> bytes:
    source = 0
    output = bytearray()

    def read_extended_length(base: int) -> int:
        nonlocal source
        length = base
        if base != 0x0F:
            return length
        while True:
            if source >= len(payload):
                raise FormatError("comprimento LZ4 truncado")
            value = payload[source]
            source += 1
            length += value
            if value != 0xFF:
                return length

    while source < len(payload) and len(output) < raw_size:
        token = payload[source]
        source += 1

        literal_size = read_extended_length(token >> 4)
        literal_end = source + literal_size
        if literal_end > len(payload):
            raise FormatError("literal LZ4 truncado")
        if len(output) + literal_size > raw_size:
            raise FormatError("literal LZ4 ultrapassa o tamanho de saida")
        output.extend(payload[source:literal_end])
        source = literal_end

        if source == len(payload):
            break
        if source + 2 > len(payload):
            raise FormatError("distancia LZ4 truncada")
        distance = struct.unpack_from("<H", payload, source)[0]
        source += 2
        if distance == 0 or distance > len(output):
            raise FormatError(
                f"referencia LZ4 invalida: distancia={distance}, saida={len(output)}"
            )

        match_size = read_extended_length(token & 0x0F) + 4
        if len(output) + match_size > raw_size:
            raise FormatError("referencia LZ4 ultrapassa o tamanho de saida")
        for _ in range(match_size):
            output.append(output[-distance])

    if source != len(payload):
        raise FormatError(
            f"dados LZ4 nao consumidos: consumidos={source}, total={len(payload)}"
        )
    if len(output) != raw_size:
        raise FormatError(
            f"saida LZ4 incompleta: esperado={raw_size}, obtido={len(output)}"
        )
    return bytes(output)


def nmpl_decompress(stream: bytes) -> bytes:
    if len(stream) < 0x14 or stream[:8] != YKCMP_MAGIC:
        raise FormatError("stream nao e um container YKCMP_V1")
    archive_size = read_u32(stream, 0x0C)
    raw_size = read_u32(stream, 0x10)
    if archive_size != len(stream):
        raise FormatError(
            f"tamanho do container divergente: cabecalho={archive_size}, "
            f"real={len(stream)}"
        )

    compression_id = read_u32(stream, 0x08)
    if compression_id == 4:
        return ykcmp_decompress(stream)
    if compression_id in (8, 9):
        return lz4_decompress_block(stream[0x14:], raw_size)
    raise FormatError(f"compressao NMPL nao suportada: {compression_id}")


def rgba_to_bmp(rgba: bytes, width: int, height: int) -> bytes:
    expected_size = width * height * 4
    if len(rgba) != expected_size:
        raise FormatError(
            f"RGBA8 deveria ter {expected_size} bytes, mas possui {len(rgba)}"
        )

    bgra = bytearray(expected_size)
    bgra[0::4] = rgba[2::4]
    bgra[1::4] = rgba[1::4]
    bgra[2::4] = rgba[0::4]
    bgra[3::4] = rgba[3::4]

    pixel_offset = 14 + 40
    file_size = pixel_offset + len(bgra)
    file_header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset)
    # A negative height marks top-down rows, matching the texture byte order.
    info_header = struct.pack(
        "<IiiHHIIiiII",
        40,
        width,
        -height,
        1,
        32,
        0,
        len(bgra),
        2835,
        2835,
        0,
        0,
    )
    return file_header + info_header + bytes(bgra)


def bc7_to_dds(blocks: bytes, width: int, height: int) -> bytes:
    expected_size = ((width + 3) // 4) * ((height + 3) // 4) * 16
    if len(blocks) != expected_size:
        raise FormatError(
            f"BC7 deveria ter {expected_size} bytes, mas possui {len(blocks)}"
        )

    dds_header = struct.pack(
        "<IIIIIII11I",
        124,  # dwSize
        0x00081007,  # CAPS | HEIGHT | WIDTH | PIXELFORMAT | LINEARSIZE
        height,
        width,
        len(blocks),
        0,
        1,
        *([0] * 11),
    )
    pixel_format = struct.pack(
        "<II4sIIIII",
        32,
        0x4,  # DDPF_FOURCC
        b"DX10",
        0,
        0,
        0,
        0,
        0,
    )
    caps = struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)
    dx10_header = struct.pack(
        "<IIIII",
        DXGI_FORMAT_BC7_UNORM,
        3,  # D3D10_RESOURCE_DIMENSION_TEXTURE2D
        0,
        1,
        0,
    )
    return b"DDS " + dds_header + pixel_format + caps + dx10_header + blocks


def dds_to_bc7(dds: bytes) -> tuple[int, int, bytes]:
    if len(dds) < DDS_HEADER_SIZE or dds[:4] != b"DDS ":
        raise FormatError("arquivo nao e DDS")
    if read_u32(dds, 4) != 124 or read_u32(dds, 0x4C) != 32:
        raise FormatError("cabecalho DDS invalido")
    if dds[0x54:0x58] != b"DX10":
        raise FormatError("DDS precisa usar o cabecalho DX10")

    height = read_u32(dds, 0x0C)
    width = read_u32(dds, 0x10)
    dxgi_format = read_u32(dds, 0x80)
    resource_dimension = read_u32(dds, 0x84)
    array_size = read_u32(dds, 0x8C)
    if dxgi_format != DXGI_FORMAT_BC7_UNORM:
        raise FormatError(
            f"DDS precisa ser BC7_UNORM; DXGI encontrado={dxgi_format}"
        )
    if resource_dimension != 3 or array_size != 1:
        raise FormatError("DDS precisa ser uma unica textura 2D")

    blocks = dds[DDS_HEADER_SIZE:]
    expected_size = ((width + 3) // 4) * ((height + 3) // 4) * 16
    if len(blocks) != expected_size:
        raise FormatError(
            f"DDS deveria ter {expected_size} bytes BC7, mas possui {len(blocks)}"
        )
    return width, height, blocks


def lz4_compress_hc(raw: bytes, library_path: Path) -> bytes:
    if not library_path.is_file():
        raise FormatError(f"biblioteca LZ4 canonica ausente: {library_path}")
    try:
        library = ctypes.WinDLL(str(library_path.resolve()))
    except OSError as error:
        raise FormatError(f"nao foi possivel carregar {library_path}: {error}") from error

    compress_bound = library.LZ4_compressBound
    compress_bound.argtypes = [ctypes.c_int]
    compress_bound.restype = ctypes.c_int
    compress_hc = library.LZ4_compress_HC
    compress_hc.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
    ]
    compress_hc.restype = ctypes.c_int

    if len(raw) > 0x7FFFFFFF:
        raise FormatError("entrada grande demais para a API LZ4")
    capacity = compress_bound(len(raw))
    if capacity <= 0:
        raise FormatError("LZ4_compressBound falhou")
    source = ctypes.create_string_buffer(raw)
    destination = ctypes.create_string_buffer(capacity)
    compressed_size = compress_hc(
        source,
        destination,
        len(raw),
        capacity,
        12,
    )
    if compressed_size <= 0:
        raise FormatError("LZ4_compress_HC falhou")
    return bytes(destination[:compressed_size])


def splice_bc7_region(
    texture: bytes,
    texture_width: int,
    texture_height: int,
    region: bytes,
    region_width: int,
    region_height: int,
    x: int,
    y: int,
) -> bytes:
    values = (texture_width, texture_height, region_width, region_height, x, y)
    if any(value < 0 or value % 4 != 0 for value in values):
        raise FormatError("dimensoes e coordenadas BC7 precisam ser multiplas de 4")
    if x + region_width > texture_width or y + region_height > texture_height:
        raise FormatError("regiao BC7 ultrapassa a textura de destino")

    texture_pitch = (texture_width // 4) * 16
    region_pitch = (region_width // 4) * 16
    expected_texture_size = texture_pitch * (texture_height // 4)
    expected_region_size = region_pitch * (region_height // 4)
    if len(texture) != expected_texture_size:
        raise FormatError("tamanho BC7 da textura de destino invalido")
    if len(region) != expected_region_size:
        raise FormatError("tamanho BC7 da regiao invalido")

    result = bytearray(texture)
    first_block_x = x // 4
    first_block_y = y // 4
    for row in range(region_height // 4):
        destination = (first_block_y + row) * texture_pitch + first_block_x * 16
        source = row * region_pitch
        result[destination : destination + region_pitch] = region[
            source : source + region_pitch
        ]
    return bytes(result)


def write_transactionally(path: Path, data: bytes) -> None:
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as temporary_file:
            temporary_file.write(data)
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(temporary_path, path)
    except Exception:
        if temporary_path.exists():
            temporary_path.unlink()
        raise


def find_unique(records: list[TextureRecord], name: str) -> TextureRecord:
    matches = [record for record in records if record.name == name]
    if len(matches) != 1:
        raise FormatError(
            f"textura '{name}' deveria ocorrer uma vez; ocorrencias={len(matches)}"
        )
    return matches[0]


def command_list(archive: Path, pattern: str | None) -> None:
    data = archive.read_bytes()
    records = parse_texture_records(data)
    selected = [record for record in records if not pattern or pattern in record.name]
    if not selected:
        raise FormatError(f"nenhuma textura corresponde a '{pattern}'")
    for record in selected:
        print(
            f"{record.name}\t{record.width}x{record.height}\t"
            f"formato=0x{record.format_id:X}\tregistro=0x{record.record_offset:X}"
        )


def command_extract_bmp(archive: Path, texture_name: str, output: Path) -> None:
    data = archive.read_bytes()
    record = find_unique(parse_texture_records(data), texture_name)
    if record.format_id != TEXTURE_CONTAINER_FORMAT:
        raise FormatError(
            f"formato de textura nao suportado em {texture_name}: 0x{record.format_id:X}"
        )
    if record.layout_id != LINEAR_RGBA8_LAYOUT:
        raise FormatError(
            f"layout RGBA8 linear nao suportado em {texture_name}: "
            f"0x{record.layout_id:X}"
        )
    if record.raw_size != record.width * record.height * 4:
        raise FormatError(
            f"{texture_name} nao contem RGBA8 linear: "
            f"raw={record.raw_size}, esperado={record.width * record.height * 4}"
        )
    stream = data[record.ykcmp_offset : record.ykcmp_offset + record.ykcmp_size]
    raw = nmpl_decompress(stream)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(rgba_to_bmp(raw, record.width, record.height))
    print(f"Extraida: {texture_name} -> {output}")


def command_extract_dds(archive: Path, texture_name: str, output: Path) -> None:
    data = archive.read_bytes()
    record = find_unique(parse_texture_records(data), texture_name)
    if record.format_id != TEXTURE_CONTAINER_FORMAT:
        raise FormatError(
            f"formato de textura nao suportado em {texture_name}: 0x{record.format_id:X}"
        )
    if record.layout_id != BLOCK_COMPRESSED_LAYOUT:
        raise FormatError(
            f"layout BC7 nao suportado em {texture_name}: 0x{record.layout_id:X}"
        )
    stream = data[record.ykcmp_offset : record.ykcmp_offset + record.ykcmp_size]
    blocks = nmpl_decompress(stream)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(bc7_to_dds(blocks, record.width, record.height))
    print(f"Extraida: {texture_name} -> {output}")


def command_patch_bc7_region(
    archive: Path,
    texture_name: str,
    region_dds: Path,
    x: int,
    y: int,
    lz4_dll: Path,
) -> None:
    data = bytearray(archive.read_bytes())
    record = find_unique(parse_texture_records(data), texture_name)
    if record.format_id != TEXTURE_CONTAINER_FORMAT:
        raise FormatError(
            f"formato de textura nao suportado em {texture_name}: 0x{record.format_id:X}"
        )
    if record.layout_id != BLOCK_COMPRESSED_LAYOUT:
        raise FormatError(
            f"layout BC7 nao suportado em {texture_name}: 0x{record.layout_id:X}"
        )
    if record.compression_id not in (8, 9):
        raise FormatError(
            f"compressao LZ4 esperada em {texture_name}; encontrada={record.compression_id}"
        )

    stream = bytes(data[record.ykcmp_offset : record.ykcmp_offset + record.ykcmp_size])
    original_blocks = nmpl_decompress(stream)
    region_width, region_height, region_blocks = dds_to_bc7(region_dds.read_bytes())
    patched_blocks = splice_bc7_region(
        original_blocks,
        record.width,
        record.height,
        region_blocks,
        region_width,
        region_height,
        x,
        y,
    )
    if patched_blocks == original_blocks:
        print(f"Ja instalada: {texture_name}")
        return

    payload = lz4_compress_hc(patched_blocks, lz4_dll)
    patched_stream = (
        YKCMP_MAGIC
        + struct.pack(
            "<III",
            record.compression_id,
            0x14 + len(payload),
            len(patched_blocks),
        )
        + payload
    )
    stream_capacity = record.record_offset + record.record_size - record.ykcmp_offset
    if len(patched_stream) > stream_capacity:
        raise FormatError(
            "textura recomposta nao cabe no registro original: "
            f"necessario={len(patched_stream)}, capacidade={stream_capacity}"
        )

    nltx_offset = record.record_offset + TEXTURE_RECORD_HEADER_SIZE
    struct.pack_into("<I", data, record.record_offset + 0x20, 0x80 + len(patched_stream))
    struct.pack_into("<I", data, nltx_offset + 0x30, len(patched_stream))
    data[record.ykcmp_offset : record.ykcmp_offset + len(patched_stream)] = patched_stream
    padding_start = record.ykcmp_offset + len(patched_stream)
    padding_end = record.record_offset + record.record_size
    data[padding_start:padding_end] = b"\0" * (padding_end - padding_start)

    verified_records = parse_texture_records(data)
    verified_record = find_unique(verified_records, texture_name)
    verified_stream = bytes(
        data[
            verified_record.ykcmp_offset :
            verified_record.ykcmp_offset + verified_record.ykcmp_size
        ]
    )
    if nmpl_decompress(verified_stream) != patched_blocks:
        raise FormatError("verificacao da textura recomposta falhou")

    write_transactionally(archive, bytes(data))
    print(
        f"Atualizada: {texture_name} em ({x}, {y}), "
        f"stream={len(patched_stream)} bytes"
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="lista texturas do FAD")
    list_parser.add_argument("archive", type=Path)
    list_parser.add_argument("--pattern")

    extract_parser = subparsers.add_parser(
        "extract-bmp", help="extrai uma textura RGBA8 para BMP"
    )
    extract_parser.add_argument("archive", type=Path)
    extract_parser.add_argument("texture_name")
    extract_parser.add_argument("output", type=Path)

    dds_parser = subparsers.add_parser(
        "extract-dds", help="extrai uma textura BC7 para DDS"
    )
    dds_parser.add_argument("archive", type=Path)
    dds_parser.add_argument("texture_name")
    dds_parser.add_argument("output", type=Path)

    patch_parser = subparsers.add_parser(
        "patch-bc7-region", help="substitui uma regiao BC7 sem alterar o tamanho do FAD"
    )
    patch_parser.add_argument("archive", type=Path)
    patch_parser.add_argument("texture_name")
    patch_parser.add_argument("region_dds", type=Path)
    patch_parser.add_argument("x", type=int)
    patch_parser.add_argument("y", type=int)
    patch_parser.add_argument("--lz4-dll", type=Path, required=True)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.command == "list":
            command_list(args.archive, args.pattern)
        elif args.command == "extract-bmp":
            command_extract_bmp(args.archive, args.texture_name, args.output)
        elif args.command == "extract-dds":
            command_extract_dds(args.archive, args.texture_name, args.output)
        elif args.command == "patch-bc7-region":
            command_patch_bc7_region(
                args.archive,
                args.texture_name,
                args.region_dds,
                args.x,
                args.y,
                args.lz4_dll,
            )
        else:
            raise AssertionError(f"comando inesperado: {args.command}")
    except (FormatError, OSError, UnicodeError) as error:
        print(f"ERRO: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
