#!/usr/bin/env python3
"""Unit tests for bin/md.extract's `field` selector and multi-part contract.

Run as: test_md_extract.py <path-to-md.extract>
"""

from __future__ import annotations

import subprocess
import sys

TOOL = sys.argv[1]


def run(args, stdin=""):
    return subprocess.run(
        [TOOL, *args],
        input=stdin,
        text=True,
        capture_output=True,
    )


def check(condition, message):
    if not condition:
        print(f"test_md_extract: {message}", file=sys.stderr)
        sys.exit(1)


DOC = """\
---
filename: fix-parser.prompt
other: ignored
---

```sh
prompt .note hi
```
"""


def test_field_value():
    result = run(["--select", "field:filename"], DOC)
    check(result.returncode == 0, "field:filename should succeed")
    check(result.stdout == "fix-parser.prompt\n", f"field value: {result.stdout!r}")


def test_field_case_insensitive_and_body():
    result = run(["--select", "field:filenAME"], DOC)
    check(result.stdout == "fix-parser.prompt\n", "field label is case-insensitive")
    body = "intro\n\n- **filename**: fix-parser.prompt\n\nend\n"
    result = run(["--select", "field:filename"], body)
    check(result.stdout == "fix-parser.prompt\n", f"body field: {result.stdout!r}")


def test_field_quotes_and_equals():
    for doc in ('filename: "a b"\n', "filename: 'a b'\n", "filename: `a b`\n", "filename = a b\n"):
        result = run(["--select", "field:filename"], doc)
        check(result.stdout == "a b\n", f"field cleanup for {doc!r}: {result.stdout!r}")


def test_field_first_match_and_one():
    doc = "filename: first\nfilename: second\n"
    result = run(["--select", "field:filename"], doc)
    check(result.stdout == "first\n\nsecond\n", f"every labelled line renders: {result.stdout!r}")
    result = run(["--select", "field:filename", "--one"], doc)
    check(result.returncode == 2, "--one rejects more than one match")
    result = run(["--select", "field:filename@2"], doc)
    check(result.stdout == "second\n", "@2 picks the second labelled line")


def test_field_missing_is_soft():
    result = run(["--select", "field:filename"], "no label here\n")
    check(result.returncode == 1, "a missing field is a soft error (exit 1)")
    check("no matching" in result.stderr, "the missing-field note names the kind")


def test_field_needs_label():
    result = run(["--kind", "field"], "filename: x\n")
    check(result.returncode == 2, "--kind field without a label is a usage error")


def test_single_contract_unchanged():
    result = run(["--contract", "command:prompt"])
    check("single `prompt` command line" in result.stdout, "single contract is unchanged")


def test_combined_contract():
    result = run(["--contract", "command:prompt", "--contract", "field:filename"])
    check("containing all of the following" in result.stdout, "combined contract preamble")
    check("single `prompt` command line" in result.stdout, "combined contract keeps the primary")
    check("filename: <value>" in result.stdout, "combined contract asks for the field")


def test_kinds_lists_field():
    result = run(["--kinds"])
    check("field" in result.stdout, "--kinds advertises field")


def main():
    test_field_value()
    test_field_case_insensitive_and_body()
    test_field_quotes_and_equals()
    test_field_first_match_and_one()
    test_field_missing_is_soft()
    test_field_needs_label()
    test_single_contract_unchanged()
    test_combined_contract()
    test_kinds_lists_field()
    print("all md.extract tests passed")


if __name__ == "__main__":
    main()
