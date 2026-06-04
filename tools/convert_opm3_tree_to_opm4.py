#!/usr/bin/env python3

from __future__ import annotations

import argparse
import fnmatch
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import generate_opm4


LEGACY_SOURCE_TARGET_TO_NAIF_ID = {
    101: 2000001,  # Ceres
    102: 2000002,  # Pallas
    103: 2000003,  # Juno
    104: 2000004,  # Vesta
    105: 2000433,  # Eros
    106: 20002060,  # Chiron
    107: 20005145,  # Pholus
    108: 20007066,  # Nessus
    109: 20001181,  # Lilith 1181
}


@dataclass(frozen=True)
class Metadata:
    target_id: int | None
    center_id: int
    method_id: int
    frame_id: int


@dataclass(frozen=True)
class Rule:
    pattern: str
    metadata: Metadata


@dataclass(frozen=True)
class MetadataConfig:
    defaults: Metadata | None
    rules: list[Rule]
    files: dict[str, Metadata]


def parse_int(value: Any, key: str) -> int | None:
    if value is None:
        return None
    if not isinstance(value, int):
        raise ValueError(f"{key} must be an integer")
    return value


def metadata_from_mapping(data: dict[str, Any], *, require_all: bool) -> Metadata:
    target_id = parse_int(data.get("target_id"), "target_id")
    center_id = parse_int(data.get("center_id"), "center_id")
    method_id = parse_int(data.get("method_id"), "method_id")
    frame_id = parse_int(data.get("frame_id"), "frame_id")
    if require_all and (center_id is None or method_id is None or frame_id is None):
        raise ValueError("metadata requires center_id, method_id, and frame_id")
    return Metadata(
        target_id=target_id,
        center_id=0 if center_id is None else center_id,
        method_id=0 if method_id is None else method_id,
        frame_id=0 if frame_id is None else frame_id,
    )


def load_metadata_config(path: Path | None) -> MetadataConfig:
    if path is None:
        return MetadataConfig(defaults=None, rules=[], files={})

    root = json.loads(path.read_text())
    if not isinstance(root, dict):
        raise ValueError("metadata config root must be an object")

    defaults = None
    if "defaults" in root:
        if not isinstance(root["defaults"], dict):
            raise ValueError("metadata defaults must be an object")
        defaults = metadata_from_mapping(root["defaults"], require_all=True)

    rules: list[Rule] = []
    for item in root.get("rules", []):
        if not isinstance(item, dict):
            raise ValueError("metadata rules entries must be objects")
        pattern = item.get("pattern")
        if not isinstance(pattern, str) or not pattern:
            raise ValueError("metadata rule requires string pattern")
        rules.append(Rule(pattern=pattern, metadata=metadata_from_mapping(item, require_all=True)))

    files: dict[str, Metadata] = {}
    for item in root.get("files", []):
        if not isinstance(item, dict):
            raise ValueError("metadata files entries must be objects")
        relpath = item.get("path")
        if not isinstance(relpath, str) or not relpath:
            raise ValueError("metadata file entry requires string path")
        files[relpath] = metadata_from_mapping(item, require_all=True)

    return MetadataConfig(defaults=defaults, rules=rules, files=files)


def parse_override(value: str) -> Rule:
    if ":" not in value:
        raise ValueError("override must look like 'pattern:key=value,...'")
    pattern, payload = value.split(":", 1)
    if not pattern:
        raise ValueError("override pattern cannot be empty")
    data: dict[str, Any] = {}
    for part in payload.split(","):
        if not part:
            continue
        if "=" not in part:
            raise ValueError(f"invalid override item {part!r}")
        key, raw = part.split("=", 1)
        if key not in {"target_id", "center_id", "method_id", "frame_id"}:
            raise ValueError(f"unsupported override key {key!r}")
        data[key] = int(raw)
    return Rule(pattern=pattern, metadata=metadata_from_mapping(data, require_all=False))


def merge_metadata(base: Metadata | None, overlay: Metadata) -> Metadata:
    return Metadata(
        target_id=overlay.target_id if overlay.target_id is not None else (base.target_id if base else None),
        center_id=overlay.center_id if overlay.center_id != 0 else (base.center_id if base else 0),
        method_id=overlay.method_id if overlay.method_id != 0 else (base.method_id if base else 0),
        frame_id=overlay.frame_id if overlay.frame_id != 0 else (base.frame_id if base else 0),
    )


def relpath_string(path: Path) -> str:
    return path.as_posix()


def naif_target_id_from_legacy_source(source_target_id: int) -> int:
    return LEGACY_SOURCE_TARGET_TO_NAIF_ID.get(source_target_id, source_target_id)


def resolve_metadata(
    relative_path: Path,
    source_target_id: int,
    args: argparse.Namespace,
    config: MetadataConfig,
    overrides: list[Rule],
) -> Metadata:
    metadata: Metadata | None = None

    cli_defaults = Metadata(
        target_id=source_target_id if args.target_id_from_source else None,
        center_id=args.default_center_id or 0,
        method_id=args.default_method_id or 0,
        frame_id=args.default_frame_id or 0,
    )
    metadata = merge_metadata(metadata, cli_defaults)
    if config.defaults:
        metadata = merge_metadata(metadata, config.defaults)

    key = relpath_string(relative_path)
    for rule in config.rules:
        if fnmatch.fnmatch(key, rule.pattern):
            metadata = merge_metadata(metadata, rule.metadata)
    if key in config.files:
        metadata = merge_metadata(metadata, config.files[key])
    for rule in overrides:
        if fnmatch.fnmatch(key, rule.pattern):
            metadata = merge_metadata(metadata, rule.metadata)

    if metadata is None or metadata.center_id == 0 or metadata.method_id == 0 or metadata.frame_id == 0:
        raise ValueError(f"metadata unresolved for {key}: need center_id, method_id, and frame_id")
    if metadata.target_id is None:
        raise ValueError(f"target_id unresolved for {key}; pass --target-id-from-source or explicit metadata")
    if metadata.target_id != source_target_id and not args.allow_target_override:
        raise ValueError(
            f"target_id mismatch for {key}: metadata={metadata.target_id} naif_source={source_target_id}; "
            "pass --allow-target-override for explicit legacy corrections"
        )
    return metadata


def output_path_for(input_root: Path, output_root: Path, source: Path, extension: str) -> Path:
    relative = source.relative_to(input_root)
    name = relative.name
    if name.endswith(".bin.gz"):
        name = name[:-7]
    elif name.endswith(".bin"):
        name = name[:-4]
    return output_root / relative.with_name(name + extension)


def iter_sources(input_root: Path, glob_pattern: str, include_bin: bool) -> list[Path]:
    sources = sorted(input_root.glob(glob_pattern))
    if include_bin:
        seen = set(sources)
        for path in input_root.glob("**/*.bin"):
            if path not in seen:
                sources.append(path)
        sources.sort()
    return [
        path for path in sources
        if path.is_file()
        and not path.name.endswith("_ref.bin.gz")
        and not path.name.endswith("_ref.bin")
    ]


def convert_tree(args: argparse.Namespace) -> int:
    input_root = args.input_root.resolve()
    output_root = args.output_root.resolve()
    config = load_metadata_config(args.metadata_json.resolve() if args.metadata_json else None)
    overrides = [parse_override(value) for value in args.override]
    mercury_reference_path = input_root / "mer" / "mercury_ref.bin.gz"
    mercury_reference_payload = generate_opm4.read_payload(mercury_reference_path) if mercury_reference_path.exists() else None
    sources = iter_sources(input_root, args.glob, args.include_bin)
    if not sources:
        raise ValueError(f"no input files matched under {input_root}")

    converted = 0
    failed = 0
    for source in sources:
        try:
            payload = generate_opm4.read_payload(source)
            source_header = generate_opm4.parse_opm3_header(payload)
            naif_source_target_id = naif_target_id_from_legacy_source(source_header.target_id)
            relative = source.relative_to(input_root)
            metadata = resolve_metadata(relative, naif_source_target_id, args, config, overrides)
            output = output_path_for(input_root, output_root, source, args.extension)
            line = (
                f"{source} -> {output} target={metadata.target_id} center={metadata.center_id} "
                f"method={metadata.method_id} frame={metadata.frame_id} "
                f"coverage=[{source_header.jd_tdb_start},{source_header.jd_tdb_end}) "
                f"segments={source_header.segment_count}"
            )
            if args.dry_run:
                print("dry-run " + line)
            else:
                if output.exists() and not args.overwrite:
                    raise ValueError(f"output exists; pass --overwrite: {output}")
                output.parent.mkdir(parents=True, exist_ok=True)
                opm4_payload = generate_opm4.convert_payload_to_opm4(
                    payload,
                    target_id=metadata.target_id,
                    center_id=metadata.center_id,
                    method_id=metadata.method_id,
                    frame_id=metadata.frame_id,
                    allow_target_override=args.allow_target_override or metadata.target_id != source_header.target_id,
                    reference_payload=mercury_reference_payload if source_header.target_id == 1 else None,
                )
                output.write_bytes(opm4_payload)
                if args.inspect_output:
                    generate_opm4.parse_opm4_header(output.read_bytes())
                print(line)
            converted += 1
        except Exception as exc:  # noqa: BLE001 - command-line converter reports per-file failures.
            failed += 1
            print(f"error {source}: {exc}", file=sys.stderr)
            if not args.continue_on_error:
                raise

    print(f"converted={converted} failed={failed} dry_run={str(args.dry_run).lower()}")
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert a legacy OPM3 data tree into mirrored OPM4 files.")
    parser.add_argument("--input-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--default-center-id", type=int)
    parser.add_argument("--default-method-id", type=int)
    parser.add_argument("--default-frame-id", type=int)
    parser.add_argument("--target-id-from-source", action="store_true", help="Use the source target ID, normalized to NASA/NAIF IDs for known legacy asteroid IDs")
    parser.add_argument("--allow-target-override", action="store_true", help="Allow explicit metadata to correct a legacy source target_id")
    parser.add_argument("--metadata-json", type=Path, help="Optional conversion config only; not a runtime sidecar")
    parser.add_argument("--override", action="append", default=[], help="Explicit metadata override: pattern:key=value,...")
    parser.add_argument("--glob", default="**/*.bin.gz")
    parser.add_argument("--include-bin", action="store_true")
    parser.add_argument("--extension", default=".opm4")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--continue-on-error", action="store_true")
    parser.add_argument("--inspect-output", action="store_true")
    args = parser.parse_args()
    return convert_tree(args)


if __name__ == "__main__":
    raise SystemExit(main())
