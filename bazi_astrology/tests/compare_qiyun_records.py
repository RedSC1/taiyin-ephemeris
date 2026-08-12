#!/usr/bin/env python3
"""Compare 10,000 QiYun records from pre-migration and native executables."""

import argparse
import itertools
import math
import struct
import subprocess
import sys


RECORD_FIELD_COUNT = 22
SECONDS_PER_DAY = 86400.0
DECIMAL_FIELD_INDICES = (0, 1, 2, 3)
HEX_INTEGER_FIELD_INDICES = (6, 7, 8, 9, 10, 12, 14, 16, 17, 18, 19, 20)
FLOATING_FIELD_INDICES = (4, 5, 11, 13, 15, 21)
PROCESS_WAIT_SECONDS = 5


class ComparisonError(Exception):
    """A record stream cannot be compared safely."""


def parse_integer(token, base, label, line_number, field_index):
    try:
        return int(token, base)
    except ValueError as error:
        raise ComparisonError(
            "{} record {} field {} is not a base-{} integer: {!r}".format(
                label, line_number, field_index, base, token
            )
        ) from error


def double_from_bits(token, label, line_number, field_index):
    bits = parse_integer(token, 16, label, line_number, field_index)
    if bits < 0 or bits > 0xFFFFFFFFFFFFFFFF:
        raise ComparisonError(
            "{} record {} field {} is outside uint64 range: {!r}".format(
                label, line_number, field_index, token
            )
        )
    value = struct.unpack(">d", bits.to_bytes(8, "big"))[0]
    if not math.isfinite(value):
        raise ComparisonError(
            "{} record {} field {} is not finite".format(
                label, line_number, field_index
            )
        )
    return value


def parse_record(line, label, line_number):
    fields = line.split()
    if len(fields) != RECORD_FIELD_COUNT:
        raise ValueError(
            "{} record {} has {} fields, expected {}".format(
                label, line_number, len(fields), RECORD_FIELD_COUNT
            )
        )
    decimal_values = tuple(
        parse_integer(fields[index], 10, label, line_number, index)
        for index in DECIMAL_FIELD_INDICES
    )
    hexadecimal_values = tuple(
        parse_integer(fields[index], 16, label, line_number, index)
        for index in HEX_INTEGER_FIELD_INDICES
    )
    floating_values = tuple(
        double_from_bits(fields[index], label, line_number, index)
        for index in FLOATING_FIELD_INDICES
    )
    return (decimal_values, hexadecimal_values, floating_values)


def integer_fields_match(reference, candidate):
    return reference[0] == candidate[0] and reference[1] == candidate[1]


def floating_deltas_seconds(reference, candidate):
    interval_seconds = abs(
        reference[2][0] - candidate[2][0]
    ) * SECONDS_PER_DAY
    start_age_seconds = abs(
        reference[2][1] - candidate[2][1]
    ) * 3.0 * SECONDS_PER_DAY
    offset_seconds = abs(
        reference[2][2] - candidate[2][2]
    )
    reference_jie_seconds = abs(
        reference[2][3] - candidate[2][3]
    ) * SECONDS_PER_DAY
    start_jd_seconds = abs(
        reference[2][4] - candidate[2][4]
    ) * SECONDS_PER_DAY
    civil_seconds = abs(
        reference[2][5] - candidate[2][5]
    )
    return (
        interval_seconds,
        start_age_seconds,
        offset_seconds,
        reference_jie_seconds,
        start_jd_seconds,
        civil_seconds,
    )


def command(executable, data_root):
    return [executable, data_root]


def reap_process(process, terminate):
    if process is None:
        return None
    if terminate and process.poll() is None:
        process.terminate()
    try:
        return process.wait(timeout=PROCESS_WAIT_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        try:
            return process.wait(timeout=PROCESS_WAIT_SECONDS)
        except subprocess.TimeoutExpired:
            return "did not exit after kill"


def compare(args):
    exact_bitwise_differences = 0
    semantic_mismatches = []
    maxima = [0.0] * 6
    record_count = 0
    reference_process = None
    candidate_process = None
    completed_normally = False

    try:
        reference_process = subprocess.Popen(
            command(args.reference, args.data_root),
            stdout=subprocess.PIPE,
            text=True,
        )
        candidate_process = subprocess.Popen(
            command(args.candidate, args.data_root),
            stdout=subprocess.PIPE,
            text=True,
        )
        paired_lines = itertools.zip_longest(
            reference_process.stdout, candidate_process.stdout
        )
        for line_number, pair in enumerate(paired_lines, 1):
            reference_line, candidate_line = pair
            if reference_line is None or candidate_line is None:
                semantic_mismatches.append(
                    "record count differs at line {}".format(line_number)
                )
                break
            record_count += 1
            reference = parse_record(reference_line, "reference", line_number)
            candidate = parse_record(candidate_line, "candidate", line_number)
            if reference_line != candidate_line:
                exact_bitwise_differences += 1
            if not integer_fields_match(reference, candidate):
                semantic_mismatches.append(
                    "record {} has a discrete-field mismatch".format(line_number - 1)
                )
            deltas = floating_deltas_seconds(reference, candidate)
            maxima = [max(current, delta) for current, delta in zip(maxima, deltas)]
            if any(delta > args.tolerance_seconds for delta in deltas):
                semantic_mismatches.append(
                    "record {} exceeds the {:.9g}-second tolerance: {}".format(
                        line_number - 1,
                        args.tolerance_seconds,
                        ", ".join("{:.12g}".format(delta) for delta in deltas),
                    )
                )
            if len(semantic_mismatches) >= args.max_reported_mismatches:
                break
        else:
            completed_normally = True
    except (ComparisonError, OSError) as error:
        semantic_mismatches.append("comparison failed: {}".format(error))
    finally:
        terminate_children = not completed_normally or bool(semantic_mismatches)
        reference_status = reap_process(reference_process, terminate_children)
        candidate_status = reap_process(candidate_process, terminate_children)

    if reference_status != 0 or candidate_status != 0:
        semantic_mismatches.append(
            "record executable failed: reference={}, candidate={}".format(
                reference_status, candidate_status
            )
        )
    if record_count != args.expected_count:
        semantic_mismatches.append(
            "compared {} records, expected {}".format(
                record_count, args.expected_count
            )
        )

    labels = (
        "jie_interval",
        "start_age",
        "offset_seconds",
        "reference_jie",
        "start_jd",
        "start_civil_seconds",
    )
    print(
        "records={} exact_bitwise_differences={} semantic_mismatches={}".format(
            record_count, exact_bitwise_differences, len(semantic_mismatches)
        )
    )
    print(
        "maximum_deltas_seconds "
        + " ".join(
            "{}={:.12g}".format(label, value)
            for label, value in zip(labels, maxima)
        )
    )
    if semantic_mismatches:
        for mismatch in semantic_mismatches:
            print("mismatch: " + mismatch, file=sys.stderr)
        return 1
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--data-root", required=True)
    parser.add_argument("--expected-count", type=int, default=10000)
    parser.add_argument("--tolerance-seconds", type=float, default=1.0e-6)
    parser.add_argument("--max-reported-mismatches", type=int, default=20)
    args = parser.parse_args()
    if not math.isfinite(args.tolerance_seconds) or args.tolerance_seconds < 0.0:
        parser.error("--tolerance-seconds must be a finite non-negative value")
    if args.expected_count < 0:
        parser.error("--expected-count must be non-negative")
    if args.max_reported_mismatches < 1:
        parser.error("--max-reported-mismatches must be positive")
    return compare(args)


if __name__ == "__main__":
    sys.exit(main())
