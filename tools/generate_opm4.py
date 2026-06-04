#!/usr/bin/env python3

from __future__ import annotations

import argparse
import gzip
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any


OPM4_VERSION = 1
OPM4_HEADER_SIZE = 128
PAYLOAD_ENCODING_SEGMENT_STREAM_V1 = 1
COEFFICIENT_ENCODING_MIXED_SIGNED_INT = 1
SEGMENT_RECORD_ENCODING_ANGLES_V1 = 1
OPM3_HEADER_MIN_SIZE = 48
OPM4_HEADER = struct.Struct("<4sBBHQQiiiiddIBBBBB7sd48s")


@dataclass(frozen=True)
class OPM3Header:
    target_id: int
    jd_tdb_start: float
    jd_tdb_end: float
    segment_count: int
    degree_xy: int
    degree_z: int
    quant_unit_km: float
    header_size: int


@dataclass(frozen=True)
class OPR3Reference:
    coeff_count: int
    quant_unit_km: float
    ref_x_int: tuple[int, ...]
    ref_y_int: tuple[int, ...]


@dataclass(frozen=True)
class OPM4Header:
    version: int
    flags: int
    header_size: int
    payload_offset: int
    payload_size: int
    target_id: int
    center_id: int
    method_id: int
    frame_id: int
    jd_tdb_start: float
    jd_tdb_end: float
    segment_count: int
    degree_xy: int
    degree_z: int
    payload_encoding: int
    coefficient_encoding: int
    segment_record_encoding: int
    quant_unit_km: float


def read_payload(path: Path) -> bytes:
    raw = path.read_bytes()
    if raw[:2] == b"\x1f\x8b":
        return gzip.decompress(raw)
    return raw


def parse_opm3_header(payload: bytes) -> OPM3Header:
    if len(payload) < OPM3_HEADER_MIN_SIZE:
        raise ValueError("OPM3 input is too small")

    magic, version, flags, header_size = struct.unpack_from("<4sBBH", payload, 0)
    if magic != b"OPM3":
        raise ValueError("input is not an OPM3 source")
    if version != 1:
        raise ValueError(f"unsupported OPM3 source version {version}")
    if flags != 0:
        raise ValueError(f"unsupported OPM3 source flags {flags}")
    if header_size < OPM3_HEADER_MIN_SIZE or header_size > len(payload):
        raise ValueError(f"invalid OPM3 source header size {header_size}")

    target_id, jd_start, jd_end, segment_count, degree_xy, degree_z = struct.unpack_from(
        "<IddIBB", payload, 8
    )
    quant_unit_km = struct.unpack_from("<d", payload, 40)[0]
    if not jd_end > jd_start:
        raise ValueError("invalid OPM3 source coverage interval")
    if segment_count <= 0:
        raise ValueError("invalid OPM3 source segment count")
    if quant_unit_km <= 0.0:
        raise ValueError("invalid OPM3 source quantization unit")

    return OPM3Header(
        target_id=target_id,
        jd_tdb_start=jd_start,
        jd_tdb_end=jd_end,
        segment_count=segment_count,
        degree_xy=degree_xy,
        degree_z=degree_z,
        quant_unit_km=quant_unit_km,
        header_size=header_size,
    )


def parse_opr3_reference(payload: bytes) -> OPR3Reference:
    if len(payload) < 24:
        raise ValueError("OPR3 reference is too small")

    magic, version, flags, header_size = struct.unpack_from("<4sBBH", payload, 0)
    if magic != b"OPR3":
        raise ValueError("reference input is not an OPR3 source")
    if version != 1:
        raise ValueError(f"unsupported OPR3 reference version {version}")
    if flags != 0:
        raise ValueError(f"unsupported OPR3 reference flags {flags}")
    if header_size < 24 or header_size > len(payload):
        raise ValueError(f"invalid OPR3 reference header size {header_size}")

    body_id, coeff_count = struct.unpack_from("<II", payload, 8)
    quant_unit_km = struct.unpack_from("<d", payload, 16)[0]
    if body_id != 1:
        raise ValueError(f"unsupported OPR3 reference body_id {body_id}")
    if coeff_count <= 0 or coeff_count > 255:
        raise ValueError("invalid OPR3 reference coefficient count")
    if quant_unit_km <= 0.0:
        raise ValueError("invalid OPR3 reference quantization unit")

    expected_size = header_size + coeff_count * 16
    if len(payload) < expected_size:
        raise ValueError("truncated OPR3 reference payload")
    ref_x = []
    ref_y = []
    offset = header_size
    for _ in range(coeff_count):
        value = struct.unpack_from("<d", payload, offset)[0]
        offset += 8
        ref_x.append(int(value / quant_unit_km + 0.5))
    for _ in range(coeff_count):
        value = struct.unpack_from("<d", payload, offset)[0]
        offset += 8
        ref_y.append(int(value / quant_unit_km + 0.5))

    return OPR3Reference(
        coeff_count=coeff_count,
        quant_unit_km=quant_unit_km,
        ref_x_int=tuple(ref_x),
        ref_y_int=tuple(ref_y),
    )


def decode_mixed_signed_ints(payload: bytes, offset: int, count: int) -> tuple[list[int], int]:
    width_byte_count = (count + 3) // 4
    if offset + width_byte_count > len(payload):
        raise ValueError("truncated coefficient width table")
    width_bytes = payload[offset : offset + width_byte_count]
    offset += width_byte_count
    values = []
    for i in range(count):
        width_code = (width_bytes[i >> 2] >> ((i & 3) * 2)) & 0x03
        size = width_code + 1
        if offset + size > len(payload):
            raise ValueError("truncated coefficient payload")
        values.append(int.from_bytes(payload[offset : offset + size], "little", signed=True))
        offset += size
    return values, offset


def coefficient_width_code(value: int) -> int:
    if -128 <= value <= 127:
        return 0
    if -32768 <= value <= 32767:
        return 1
    if -8388608 <= value <= 8388607:
        return 2
    if -2147483648 <= value <= 2147483647:
        return 3
    raise ValueError(f"coefficient out of int32 range: {value}")


def encode_mixed_signed_ints(values: list[int]) -> bytes:
    width_bytes = bytearray((len(values) + 3) // 4)
    encoded = bytearray()
    for i, value in enumerate(values):
        width_code = coefficient_width_code(value)
        width_bytes[i >> 2] |= width_code << ((i & 3) * 2)
        encoded.extend(value.to_bytes(width_code + 1, "little", signed=True))
    return bytes(width_bytes + encoded)


def bake_opr3_reference_into_segment_stream(source_payload: bytes, opm3: OPM3Header, reference: OPR3Reference) -> bytes:
    if abs(reference.quant_unit_km - opm3.quant_unit_km) > 1e-15:
        raise ValueError(
            f"reference quantization mismatch: reference={reference.quant_unit_km} source={opm3.quant_unit_km}"
        )
    n_xy = opm3.degree_xy + 1
    n_z = opm3.degree_z + 1
    if reference.coeff_count < n_xy:
        raise ValueError("reference coefficient count is smaller than source degree_xy")

    offset = opm3.header_size
    out = bytearray()
    for _ in range(opm3.segment_count):
        if offset + 40 > len(source_payload):
            raise ValueError("truncated segment header")
        out.extend(source_payload[offset : offset + 40])
        offset += 40
        coeff_x, offset = decode_mixed_signed_ints(source_payload, offset, n_xy)
        coeff_y, offset = decode_mixed_signed_ints(source_payload, offset, n_xy)
        coeff_z, offset = decode_mixed_signed_ints(source_payload, offset, n_z)
        coeff_x = [coeff_x[i] + reference.ref_x_int[i] for i in range(n_xy)]
        coeff_y = [coeff_y[i] + reference.ref_y_int[i] for i in range(n_xy)]
        out.extend(encode_mixed_signed_ints(coeff_x))
        out.extend(encode_mixed_signed_ints(coeff_y))
        out.extend(encode_mixed_signed_ints(coeff_z))
    if offset != len(source_payload):
        raise ValueError("unexpected trailing source payload bytes")
    return bytes(out)


def pack_opm4_header(
    opm3: OPM3Header,
    target_id: int,
    center_id: int,
    method_id: int,
    frame_id: int,
    payload_size: int,
) -> bytes:
    return OPM4_HEADER.pack(
        b"OPM4",
        OPM4_VERSION,
        0,
        OPM4_HEADER_SIZE,
        OPM4_HEADER_SIZE,
        payload_size,
        target_id,
        center_id,
        method_id,
        frame_id,
        opm3.jd_tdb_start,
        opm3.jd_tdb_end,
        opm3.segment_count,
        opm3.degree_xy,
        opm3.degree_z,
        PAYLOAD_ENCODING_SEGMENT_STREAM_V1,
        COEFFICIENT_ENCODING_MIXED_SIGNED_INT,
        SEGMENT_RECORD_ENCODING_ANGLES_V1,
        b"\x00" * 7,
        opm3.quant_unit_km,
        b"\x00" * 48,
    )


def convert_payload_to_opm4(
    source_payload: bytes,
    target_id: int,
    center_id: int,
    method_id: int,
    frame_id: int,
    allow_target_override: bool = False,
    reference_payload: bytes | None = None,
) -> bytes:
    opm3 = parse_opm3_header(source_payload)
    if opm3.target_id != target_id and not allow_target_override:
        raise ValueError(f"target_id mismatch: requested={target_id} opm3_source={opm3.target_id}")

    if opm3.target_id == 1:
        if reference_payload is None:
            raise ValueError("Mercury OPM4 conversion requires a legacy OPR3 reference to bake coefficients")
        reference = parse_opr3_reference(reference_payload)
        opm4_payload = bake_opr3_reference_into_segment_stream(source_payload, opm3, reference)
    else:
        opm4_payload = source_payload[opm3.header_size :]
    if opm4_payload[:4] == b"OPM3":
        raise ValueError("OPM4 payload would contain nested OPM3 magic")
    header = pack_opm4_header(
        opm3,
        target_id=target_id,
        center_id=center_id,
        method_id=method_id,
        frame_id=frame_id,
        payload_size=len(opm4_payload),
    )
    return header + opm4_payload


def convert_one(
    input_path: Path,
    output_path: Path,
    target_id: int,
    center_id: int,
    method_id: int,
    frame_id: int,
    reference_path: Path | None = None,
    allow_target_override: bool = False,
) -> None:
    source_payload = read_payload(input_path)
    opm3 = parse_opm3_header(source_payload)
    reference_payload = read_payload(reference_path) if reference_path else None
    converted = convert_payload_to_opm4(
        source_payload,
        target_id=target_id,
        center_id=center_id,
        method_id=method_id,
        frame_id=frame_id,
        allow_target_override=allow_target_override,
        reference_payload=reference_payload,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(converted)
    print(
        f"{input_path} -> {output_path} "
        f"target={target_id} center={center_id} method={method_id} frame={frame_id} "
        f"coverage=[{opm3.jd_tdb_start},{opm3.jd_tdb_end}) segments={opm3.segment_count}"
    )


def parse_opm4_header(payload: bytes) -> OPM4Header:
    if len(payload) < OPM4_HEADER_SIZE:
        raise ValueError("OPM4 file is too small")
    unpacked = OPM4_HEADER.unpack_from(payload, 0)
    magic = unpacked[0]
    if magic != b"OPM4":
        raise ValueError("not an OPM4 file")
    return OPM4Header(
        version=unpacked[1],
        flags=unpacked[2],
        header_size=unpacked[3],
        payload_offset=unpacked[4],
        payload_size=unpacked[5],
        target_id=unpacked[6],
        center_id=unpacked[7],
        method_id=unpacked[8],
        frame_id=unpacked[9],
        jd_tdb_start=unpacked[10],
        jd_tdb_end=unpacked[11],
        segment_count=unpacked[12],
        degree_xy=unpacked[13],
        degree_z=unpacked[14],
        payload_encoding=unpacked[15],
        coefficient_encoding=unpacked[16],
        segment_record_encoding=unpacked[17],
        quant_unit_km=unpacked[19],
    )


def inspect(path: Path) -> None:
    payload = path.read_bytes()
    header = parse_opm4_header(payload)
    payload_start = header.payload_offset
    payload_end = payload_start + header.payload_size
    if payload_start < header.header_size or payload_end > len(payload):
        raise ValueError("OPM4 payload range is invalid")
    nested_magic_absent = payload[payload_start : payload_start + 4] != b"OPM3"

    print("magic=OPM4")
    print(f"version={header.version}")
    print(f"flags={header.flags}")
    print(f"header_size={header.header_size}")
    print(f"payload_offset={header.payload_offset}")
    print(f"payload_size={header.payload_size}")
    print(f"target_id={header.target_id}")
    print(f"center_id={header.center_id}")
    print(f"method_id={header.method_id}")
    print(f"frame_id={header.frame_id}")
    print(f"coverage=[{header.jd_tdb_start}, {header.jd_tdb_end})")
    print(f"segment_count={header.segment_count}")
    print(f"degree_xy={header.degree_xy}")
    print(f"degree_z={header.degree_z}")
    print(f"payload_encoding={header.payload_encoding}")
    print(f"coefficient_encoding={header.coefficient_encoding}")
    print(f"segment_record_encoding={header.segment_record_encoding}")
    print(f"quant_unit_km={header.quant_unit_km}")
    print(f"payload_magic_absent={str(nested_magic_absent).lower()}")


def resolve_manifest_path(manifest_path: Path, value: str) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    return (manifest_path.parent / path).resolve()


def required_int(entry: dict[str, Any], key: str) -> int:
    value = entry.get(key)
    if not isinstance(value, int):
        raise ValueError(f"manifest entry requires integer {key!r}")
    return value


def required_path(manifest_path: Path, entry: dict[str, Any], key: str) -> Path:
    value = entry.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"manifest entry requires string {key!r}")
    return resolve_manifest_path(manifest_path, value)


def run_batch_manifest(path: Path) -> int:
    data = json.loads(path.read_text())
    files = data.get("files") if isinstance(data, dict) else None
    if not isinstance(files, list):
        raise ValueError("batch manifest root must contain a files array")

    converted = 0
    for entry in files:
        if not isinstance(entry, dict):
            raise ValueError("batch manifest files entries must be objects")
        reference_path = resolve_manifest_path(path, entry["reference"]) if isinstance(entry.get("reference"), str) else None
        convert_one(
            required_path(path, entry, "input"),
            required_path(path, entry, "output"),
            required_int(entry, "target_id"),
            required_int(entry, "center_id"),
            required_int(entry, "method_id"),
            required_int(entry, "frame_id"),
            reference_path=reference_path,
            allow_target_override=bool(entry.get("allow_target_override", False)),
        )
        converted += 1
    return converted


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate self-contained OPM4 files from legacy OPM3 source files."
    )
    parser.add_argument("--input", type=Path, help="Legacy OPM3 source file (.bin or .bin.gz)")
    parser.add_argument("--output", type=Path, help="Output .opm4 file")
    parser.add_argument("--target-id", type=int, help="Target body ID to embed and verify")
    parser.add_argument("--center-id", type=int, help="Center body ID to embed")
    parser.add_argument("--method-id", type=int, help="Method ID to embed")
    parser.add_argument("--frame-id", type=int, help="Frame ID to embed")
    parser.add_argument("--allow-target-override", action="store_true", help="Allow explicit target_id to correct a legacy source ID")
    parser.add_argument("--reference", type=Path, help="Legacy OPR3 reference to bake into Mercury OPM4 coefficients")
    parser.add_argument("--batch-manifest", type=Path, help="Optional batch conversion manifest; generator convenience only")
    parser.add_argument("--inspect", dest="inspect_path", type=Path, help="Inspect an OPM4 file header")
    args = parser.parse_args()

    if args.inspect_path:
        inspect(args.inspect_path.resolve())
        return 0

    if args.batch_manifest:
        converted = run_batch_manifest(args.batch_manifest.resolve())
        print(f"converted {converted} OPM3 source file(s)")
        return 0

    required = [args.input, args.output, args.target_id, args.center_id, args.method_id, args.frame_id]
    if any(value is None for value in required):
        parser.error("single-file conversion requires --input, --output, --target-id, --center-id, --method-id, and --frame-id")

    convert_one(
        args.input.resolve(),
        args.output.resolve(),
        args.target_id,
        args.center_id,
        args.method_id,
        args.frame_id,
        reference_path=args.reference.resolve() if args.reference else None,
        allow_target_override=args.allow_target_override,
    )
    print("converted 1 OPM3 source file")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
