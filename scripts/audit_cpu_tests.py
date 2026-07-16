#!/usr/bin/env python3
"""Build and report the miniRV ISA gap in selected AM cpu-tests."""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
from collections import Counter, defaultdict
from pathlib import Path

SUPPORTED = (
    "add", "addi", "lui", "lw", "lbu", "sw", "sb", "jalr", "ebreak",
    "auipc", "beq", "bne", "sltiu",
)
REQUESTED_TESTS = (
    "add", "bit", "fact", "fib", "if-else", "load-store", "max", "min3",
    "shift", "string", "sum", "switch",
)
INSTRUCTION = re.compile(
    r"^\s*[0-9a-fA-F]+:\s+(?:[0-9a-fA-F]{8}|(?:[0-9a-fA-F]{2}\s+){2,4})"
    r"\s+([a-zA-Z0-9_.]+)(?:\s|$)"
)
CATEGORIES = {
    "add": "arithmetic", "addi": "arithmetic", "sub": "arithmetic",
    "and": "logical", "andi": "logical", "or": "logical", "ori": "logical",
    "xor": "logical", "xori": "logical",
    "sll": "shift", "slli": "shift", "srl": "shift", "srli": "shift",
    "sra": "shift", "srai": "shift",
    "slt": "comparison", "slti": "comparison", "sltu": "comparison",
    "sltiu": "comparison",
    "beq": "branch", "bne": "branch", "blt": "branch", "bge": "branch",
    "bltu": "branch", "bgeu": "branch",
    "jal": "jump", "jalr": "jump",
    "lb": "load/store", "lbu": "load/store", "lh": "load/store",
    "lhu": "load/store", "lw": "load/store", "sb": "load/store",
    "sh": "load/store", "sw": "load/store",
    "mul": "multiply/divide", "mulh": "multiply/divide",
    "mulhsu": "multiply/divide", "mulhu": "multiply/divide",
    "div": "multiply/divide", "divu": "multiply/divide",
    "rem": "multiply/divide", "remu": "multiply/divide",
}


def stage(status: str = "not_run", error: str = "") -> dict[str, str]:
    return {"status": status, "error": error}


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False)


def failure_stage(output: str) -> str:
    markers = [(output.rfind("+ CC "), "compile"),
               (output.rfind("+ AS "), "assemble"),
               (output.rfind("+ LD "), "link")]
    marker, name = max(markers)
    return name if marker >= 0 else "compile"


def audit_disassembly(path: Path) -> Counter[str]:
    counts: Counter[str] = Counter()
    for line in path.read_text(encoding="utf-8").splitlines():
        match = INSTRUCTION.match(line)
        if match:
            counts[match.group(1)] += 1
    if not counts:
        raise ValueError("no instructions found in final linked disassembly")
    return counts


def audit_test(name: str, tests_dir: Path, am_home: Path, objdump: str) -> dict:
    arch = "minirv-npc"
    build = tests_dir / "build"
    source = tests_dir / "tests" / f"{name}.c"
    makefile = tests_dir / f"Makefile.audit-{name}"
    elf = build / f"{name}-{arch}.elf"
    text = build / f"{name}-{arch}.txt"
    result = {
        "test": name, "source": str(source), "present": source.is_file(),
        "elf": str(elf), "disassembly": str(text),
        "stages": {key: stage() for key in
                   ("compile", "assemble", "link", "isa_audit", "execution")},
        "instructions": {}, "supported": {}, "unsupported": {},
    }
    if not source.is_file():
        result["build_status"] = "not_present"
        return result

    # Remove only this test's generated source object and final image artifacts.
    for path in (build / arch / "tests" / f"{name}.o",
                 build / arch / "tests" / f"{name}.d", elf, text,
                 build / f"{name}-{arch}.bin"):
        path.unlink(missing_ok=True)
    makefile.write_text(
        f"NAME = {name}\nSRCS = tests/{name}.c\ninclude ${{AM_HOME}}/Makefile\n",
        encoding="utf-8",
    )
    try:
        built = run(["make", "-s", "-f", makefile.name, f"ARCH={arch}",
                     f"AM_HOME={am_home}", "image-dep"], tests_dir)
    finally:
        makefile.unlink(missing_ok=True)

    if built.returncode != 0 or not elf.is_file():
        failed = failure_stage(built.stdout)
        order = ("compile", "assemble", "link")
        for key in order[:order.index(failed)]:
            result["stages"][key] = stage("pass")
        result["stages"][failed] = stage("fail", built.stdout.rstrip())
        result["build_status"] = "fail"
        return result
    for key in ("compile", "assemble", "link"):
        result["stages"][key] = stage("pass")

    dumped = run([objdump, "-d", "-M", "no-aliases", str(elf)], tests_dir)
    if dumped.returncode != 0:
        result["stages"]["isa_audit"] = stage("fail", dumped.stdout.rstrip())
        result["build_status"] = "isa_audit_fail"
        return result
    text.write_text(dumped.stdout, encoding="utf-8")
    try:
        counts = audit_disassembly(text)
    except (OSError, ValueError) as error:
        result["stages"]["isa_audit"] = stage("fail", str(error))
        result["build_status"] = "isa_audit_fail"
        return result

    result["instructions"] = dict(sorted(counts.items()))
    result["supported"] = {key: value for key, value in sorted(counts.items())
                           if key in SUPPORTED}
    result["unsupported"] = {key: value for key, value in sorted(counts.items())
                             if key not in SUPPORTED}
    result["stages"]["isa_audit"] = stage(
        "unsupported" if result["unsupported"] else "pass")
    result["stages"]["execution"] = stage(
        "skipped_unsupported" if result["unsupported"] else "not_run_audit_only")
    result["build_status"] = "linked"
    return result


def recommend(results: list[dict]) -> tuple[list[str], str]:
    gaps = {item["test"]: set(item["unsupported"]) for item in results
            if item["build_status"] == "linked" and item["unsupported"]}
    if not gaps:
        return [], "No unsupported instructions were found in linked images."
    # Smallest dependency set that unlocks at least one currently blocked test;
    # frequency and test coverage break ties deterministically.
    aggregate = Counter({mnemonic: sum(item["unsupported"].get(mnemonic, 0)
                                       for item in results)
                         for mnemonic in set().union(*gaps.values())})
    minimum = min(len(required) for required in gaps.values())
    candidates = [required for required in gaps.values() if len(required) == minimum]
    chosen = max(candidates, key=lambda required: (
        sum(set(required) >= other for other in gaps.values()),
        sum(aggregate[item] for item in required), sorted(required)))
    unlocked = sorted(name for name, required in gaps.items() if required <= chosen)
    reason = (f"This is the smallest observed unsupported-instruction set for any "
              f"linked test ({minimum} instruction(s)); it would unlock "
              f"{', '.join(unlocked)}. Frequency and test coverage break equal-size ties.")
    return sorted(chosen), reason


def write_csv(path: Path, results: list[dict]) -> None:
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=(
            "test", "present", "build_status", "compile", "assemble", "link",
            "isa_audit", "execution", "mnemonic", "category", "support", "count"))
        writer.writeheader()
        for item in results:
            rows = item["instructions"].items() or [("", "")]
            for mnemonic, count in rows:
                writer.writerow({
                    "test": item["test"], "present": item["present"],
                    "build_status": item["build_status"],
                    **{key: item["stages"][key]["status"] for key in item["stages"]},
                    "mnemonic": mnemonic,
                    "category": CATEGORIES.get(mnemonic, "other") if mnemonic else "",
                    "support": ("supported" if mnemonic in SUPPORTED else "unsupported")
                               if mnemonic else "",
                    "count": count,
                })


def table_counts(counts: dict[str, int]) -> str:
    return ", ".join(f"`{key}` {value}" for key, value in counts.items()) or "—"


def write_markdown(path: Path, payload: dict) -> None:
    results = payload["tests"]
    aggregate = payload["aggregate_unsupported"]
    required_by = payload["unsupported_required_by"]
    lines = [
        f"# {payload['report_title']}", "",
        "This is a report-only audit of final linked ELF disassembly. It does not change "
        "the strict `ARCH=minirv-npc` image check or execute images with ISA gaps.", "",
        "## Current supported instruction whitelist", "",
        ", ".join(f"`{item.upper()}`" for item in SUPPORTED), "",
        "## Tests newly unlocked by E6B-2A", "",
        ", ".join(f"`{item}`" for item in payload["newly_unlocked_tests"]) or "None",
        "",
        "## Tests attempted", "",
        "| Test | Present | Build | Compile | Assemble | Link | ISA audit | Execution |",
        "|---|---:|---|---|---|---|---|---|",
    ]
    for item in results:
        stages = item["stages"]
        lines.append("| " + " | ".join((item["test"], str(item["present"]),
            item["build_status"], stages["compile"]["status"],
            stages["assemble"]["status"], stages["link"]["status"],
            stages["isa_audit"]["status"], stages["execution"]["status"])) + " |")
    lines += ["", "## Per-test instruction counts", "",
              "| Test | Supported | Unsupported |", "|---|---|---|"]
    for item in results:
        lines.append(f"| {item['test']} | {table_counts(item['supported'])} | "
                     f"{table_counts(item['unsupported'])} |")
    lines += ["", "## Aggregate unsupported-instruction frequency", "",
              "| Mnemonic | Count | Tests requiring it | Category |",
              "|---|---:|---|---|"]
    for mnemonic, count in aggregate.items():
        lines.append(f"| `{mnemonic}` | {count} | {', '.join(required_by[mnemonic])} | "
                     f"{CATEGORIES.get(mnemonic, 'other')} |")
    lines += ["", "## Instruction dependency categories", "",
              "| Category | Unsupported instructions |", "|---|---|"]
    categorized: dict[str, list[str]] = defaultdict(list)
    for mnemonic in aggregate:
        categorized[CATEGORIES.get(mnemonic, "other")].append(mnemonic)
    for category in ("arithmetic", "logical", "shift", "comparison", "branch",
                     "jump", "load/store", "multiply/divide", "other"):
        values = ", ".join(f"`{item}`" for item in categorized[category]) or "—"
        lines.append(f"| {category} | {values} |")
    batch = payload["recommended_batch"]
    lines += ["", "## Proposed smallest first implementation batch", "",
              ", ".join(f"`{item.upper()}`" for item in batch) or "None", "",
              payload["recommendation_reason"], "",
              "The proposal is derived only from the generated final-linked disassembly "
              "recorded in this audit; it is not implemented by this report.", ""]
    failures = [(item["test"], name, data["error"])
                for item in results for name, data in item["stages"].items()
                if data["status"] == "fail"]
    if failures:
        lines += ["## Exact build/audit errors", ""]
        for test, name, error in failures:
            lines += [f"### {test}: {name}", "", "```text", error, "```", ""]
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--am-home", type=Path,
                        default=Path("/home/chzione/projects/abstract-machine"))
    parser.add_argument("--am-kernels", type=Path,
                        default=Path("/home/chzione/projects/am-kernels"))
    parser.add_argument("--objdump", default="riscv64-unknown-elf-objdump")
    parser.add_argument(
        "--report-name", default="e6b_post_expansion_isa_gap_audit",
        help="output basename under docs/ and docs/results/ (no path separators)",
    )
    parser.add_argument(
        "--report-title", default="E6B post-expansion miniRV ISA gap audit",
        help="Markdown report title",
    )
    args = parser.parse_args()
    if (not args.report_name or Path(args.report_name).name != args.report_name or
            args.report_name in (".", "..")):
        parser.error("--report-name must be a nonempty basename without path separators")
    tests_dir = args.am_kernels.resolve() / "tests" / "cpu-tests"
    results = [audit_test(name, tests_dir, args.am_home.resolve(), args.objdump)
               for name in REQUESTED_TESTS]
    aggregate = Counter()
    required_by: dict[str, list[str]] = defaultdict(list)
    for item in results:
        aggregate.update(item["unsupported"])
        for mnemonic in item["unsupported"]:
            required_by[mnemonic].append(item["test"])
    batch, reason = recommend(results)
    payload = {
        "schema_version": 1, "mode": "report-only",
        "report_name": args.report_name, "report_title": args.report_title,
        "supported_whitelist": list(SUPPORTED),
        "requested_tests": list(REQUESTED_TESTS), "tests": results,
        "newly_unlocked_tests": sorted(
            item["test"] for item in results
            if item["build_status"] == "linked" and not item["unsupported"]),
        "aggregate_unsupported": dict(sorted(aggregate.items(),
                                               key=lambda item: (-item[1], item[0]))),
        "unsupported_required_by": {key: value for key, value in sorted(required_by.items())},
        "recommended_batch": batch, "recommendation_reason": reason,
    }
    results_dir = root / "docs" / "results"
    results_dir.mkdir(parents=True, exist_ok=True)
    json_path = results_dir / f"{args.report_name}.json"
    csv_path = results_dir / f"{args.report_name}.csv"
    markdown_path = root / "docs" / f"{args.report_name}.md"
    json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    write_csv(csv_path, results)
    write_markdown(markdown_path, payload)
    print(f"audited {sum(item['present'] for item in results)} present test(s)")
    print(f"linked {sum(item['build_status'] == 'linked' for item in results)} test(s)")
    print("unsupported: " + table_counts(payload["aggregate_unsupported"]))
    return 0 if all(item["build_status"] in ("linked", "not_present")
                    for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
