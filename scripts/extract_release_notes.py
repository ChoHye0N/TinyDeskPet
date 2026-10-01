#!/usr/bin/env python3
"""CHANGELOG.md에서 특정 버전의 변경 내용을 뽑아 릴리스 노트로 만듭니다.

CHANGELOG.md는 Keep a Changelog 형식을 따릅니다:
    ## [0.1.0] - 2026-10-01
    ### Added
    - ...

사용법:
    python scripts/extract_release_notes.py --version 0.1.0 --output release-notes.md

해당 버전의 섹션이 없으면 실패합니다. (릴리스 전에 CHANGELOG를 쓰도록 강제)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def extract(changelog: str, version: str) -> str | None:
    heading = re.compile(rf"^## \[{re.escape(version)}\][^\n]*$", re.MULTILINE)
    match = heading.search(changelog)
    if not match:
        return None
    next_heading = re.compile(r"^## \[", re.MULTILINE).search(changelog, match.end())
    end = next_heading.start() if next_heading else len(changelog)
    body = changelog[match.end():end]
    # 문서 하단의 링크 정의([0.1.0]: https://...)는 제외
    body = re.sub(r"^\[[^\]]+\]:\s*\S+\s*$", "", body, flags=re.MULTILINE)
    return body.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True, help="예: 0.1.0 (앞의 v는 있어도 됨)")
    parser.add_argument("--changelog", default=str(ROOT / "CHANGELOG.md"))
    parser.add_argument("--output", help="결과 파일. 생략하면 표준 출력")
    args = parser.parse_args()

    version = args.version.removeprefix("v")
    notes = extract(Path(args.changelog).read_text(encoding="utf-8"), version)
    if not notes:
        print(
            f"CHANGELOG에 '## [{version}]' 섹션이 없거나 비어 있습니다. 릴리스 전에 작성하세요.",
            file=sys.stderr,
        )
        return 1

    if args.output:
        Path(args.output).write_text(notes + "\n", encoding="utf-8")
        print(f"릴리스 노트 작성: {args.output}")
    else:
        print(notes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
