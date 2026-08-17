#!/usr/bin/env python3
"""Flatten the author's ziwei_core defaults into finite TOML answer tables."""

import argparse
import itertools
import json
from pathlib import Path


STEMS = ["jia", "yi", "bing", "ding", "wu", "ji", "geng", "xin", "ren", "gui"]
BRANCHES = ["zi", "chou", "yin", "mao", "chen", "si", "wu", "wei", "shen", "you", "xu", "hai"]
BUREAUS = ["water2", "wood3", "metal4", "earth5", "fire6"]


def source_for(anchor, boundary):
    prefix = "solar" if boundary == "solar" else "lunar"
    fixed = {
        "ziwei": "anchor.ziwei",
        "tianfu": "anchor.tianfu",
        "ming": "anchor.life",
        "body": "anchor.body",
        "shen": "anchor.body",
        "wuxingjv": "anchor.bureau",
    }
    if anchor in fixed:
        return fixed[anchor]
    mapped = {
        "year_stem": f"{prefix}.year_stem",
        "year_branch": f"{prefix}.year_branch",
        "month_stem": f"{prefix}.month_stem",
        "month_branch": f"{prefix}.month_branch",
        "month": f"{prefix}.month_index",
        "day": f"{prefix}.day_index",
        "day_number": f"{prefix}.day_index",
        "hour": f"{prefix}.hour_branch",
        "zheng_kong": f"{prefix}.zheng_kong",
        "fu_kong": f"{prefix}.fu_kong",
    }
    if anchor not in mapped:
        raise ValueError(f"unsupported oracle anchor: {anchor!r}")
    return mapped[anchor]


def domain_keys(source):
    if source == "birth.gender":
        return ["male", "female"]
    if source.endswith("_stem"):
        return STEMS
    if source == "anchor.bureau":
        return BUREAUS
    if source.endswith("day_index"):
        return [str(i) for i in range(30)]
    if source.endswith("month_index"):
        return [str(i) for i in range(12)]
    return BRANCHES


def table_values(rule, source):
    table = rule["table"]
    return [int(table[key]) for key in domain_keys(source)]


def add_unique(values, value):
    if value not in values:
        values.append(value)


def step_sources(rule):
    if rule["type"] == "constant":
        return []
    return [source_for(rule["anchor"], rule.get("boundary", "lunar"))]


def placement_sources(rule):
    sources = []
    if rule["type"] == "pipeline":
        for step in rule["steps"]:
            for source in step_sources(step):
                add_unique(sources, source)
    else:
        add_unique(sources, source_for(
            rule["anchor"], rule.get("boundary", "lunar")))
        if rule["type"] == "lookup_offset":
            add_unique(sources, source_for(
                rule["shift_anchor"], rule.get("boundary", "lunar")))
    if rule.get("direction") == "gender_shun_ni":
        add_unique(sources, source_for(
            "year_stem", rule.get("boundary", "lunar")))
        add_unique(sources, "birth.gender")
    if not 1 <= len(sources) <= 3:
        raise ValueError(f"placement expected 1..3 bounded inputs, got {sources!r}")
    return sources


def direction_value(rule, values):
    direction = rule.get("direction", 1)
    if direction in (1, -1):
        return int(direction)
    if direction == "gender_shun_ni":
        stem_source = source_for(
            "year_stem", rule.get("boundary", "lunar"))
        return 1 if values[stem_source] % 2 == values["birth.gender"] else -1
    raise ValueError(f"unsupported direction: {direction!r}")


def eval_step(rule, values):
    kind = rule["type"]
    if kind == "constant":
        return int(rule.get("value", 0))
    source = source_for(rule["anchor"], rule.get("boundary", "lunar"))
    value = values[source]
    direction = direction_value(rule, values)
    offset = int(rule.get("offset", 0))
    if kind == "anchor_offset":
        time_anchor = rule["anchor"] in ("month", "day", "day_number", "hour", "year")
        return offset + value * direction if time_anchor else value + offset * direction
    if kind == "lookup":
        return table_values(rule, source)[value] + offset * direction
    if kind == "lookup_offset":
        shift_source = source_for(
            rule["shift_anchor"], rule.get("boundary", "lunar"))
        return table_values(rule, source)[value] + direction * values[shift_source] + offset
    raise ValueError(f"unsupported rule kind: {kind!r}")


def eval_rule(rule, values):
    if rule["type"] == "pipeline":
        return sum(eval_step(step, values) for step in rule["steps"]) % 12
    return eval_step(rule, values) % 12


def flattened_placement(rule):
    sources = placement_sources(rule)
    domains = [range(len(domain_keys(source))) for source in sources]
    positions = []
    for coordinates in itertools.product(*domains):
        values = dict(zip(sources, coordinates))
        positions.append(eval_rule(rule, values))
    return sources, [len(domain) for domain in domains], positions


def array(values):
    return "[" + ", ".join(str(value) for value in values) + "]"


def string_array(values):
    return "[" + ", ".join(f'"{value}"' for value in values) + "]"


def category_for(value):
    if value == "bad":
        return "malefic"
    if value in ("boshi12", "jiangqian12", "suijian12", "changsheng12"):
        return "cycle"
    return value


def write_text(path, lines):
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def generate(oracle_root, output_dir):
    config = oracle_root / "assets" / "config" / "default"
    stars = json.loads((config / "stars.json").read_text(encoding="utf-8"))
    flow_stars = json.loads((config / "flow_stars.json").read_text(encoding="utf-8"))
    brightness = json.loads((config / "brightness.json").read_text(encoding="utf-8"))
    sihua = json.loads((config / "sihua.json").read_text(encoding="utf-8"))
    masters = json.loads((config / "masters.json").read_text(encoding="utf-8"))
    output_dir.mkdir(parents=True, exist_ok=True)

    provenance = [
        "# Generated from the author's MIT-licensed ziwei_core 0.13.0 defaults.",
        "# All placement algorithms are flattened offline into final answer tables.",
        "format_version = 1",
        "",
    ]

    star_lines = list(provenance)
    for section, values in (("natal_stars", stars), ("flow_stars", flow_stars)):
        for star in values:
            star_lines.extend([
                f"[[{section}]]",
                f'key = "{star["key"]}"',
                f'category = "{category_for(star.get("type", "other"))}"',
                "",
            ])
    write_text(output_dir / "stars.toml", star_lines)

    placement_lines = list(provenance)
    for star in stars + flow_stars:
        inputs, shape, positions = flattened_placement(star["rule"])
        placement_lines.extend([
            "[[placements]]",
            f'star = "{star["key"]}"',
            'option = "option1"',
            f"inputs = {string_array(inputs)}",
            f"shape = {array(shape)}",
            f"positions = {array(positions)}",
            "",
        ])
    write_text(output_dir / "placement.toml", placement_lines)

    brightness_lines = list(provenance)
    for star in stars:
        values = brightness[star["key"]]
        brightness_lines.extend([
            "[[brightness]]",
            f'star = "{star["key"]}"',
            'option = "option1"',
            f"values = {array(values)}",
            "",
        ])
    for star in flow_stars:
        brightness_lines.extend([
            "[[brightness]]",
            f'star = "{star["key"]}"',
            'option = "option1"',
            f"values = {array(star['brightness'])}",
            "",
        ])
    write_text(output_dir / "brightness.toml", brightness_lines)

    sihua_lines = list(provenance)
    for stem in STEMS:
        row = sihua[stem]
        sihua_lines.extend([
            "[[sihua]]",
            f'stem = "{stem}"',
            'option = "option1"',
            f'lu = "{row["lu"]}"',
            f'quan = "{row["quan"]}"',
            f'ke = "{row["ke"]}"',
            f'ji = "{row["ji"]}"',
            "",
        ])
    sihua_lines.pop()
    write_text(output_dir / "sihua.toml", sihua_lines)

    life = [masters["ming_zhu"]["table"][str(i)] for i in range(12)]
    body = [masters["shen_zhu"]["table"][str(i)] for i in range(12)]
    master_lines = list(provenance)
    master_lines.extend([
        "[[masters]]",
        'option = "option1"',
        f"life = {string_array(life)}",
        f"body = {string_array(body)}",
    ])
    write_text(output_dir / "masters.toml", master_lines)

    profile_lines = [
        "# Default Ziwei rule profile. Missing selections resolve to option1.",
        "format_version = 1",
        'stars = "stars.toml"',
        'placement_rules = "placement.toml"',
        'brightness_rules = "brightness.toml"',
        'sihua_rules = "sihua.toml"',
        'master_rules = "masters.toml"',
    ]
    write_text(output_dir / "default.toml", profile_lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("oracle_root", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    generate(args.oracle_root, args.output_dir)


if __name__ == "__main__":
    main()
