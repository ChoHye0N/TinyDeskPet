#!/usr/bin/env python3
"""버전 확인 도우미. 버전의 단일 원천은 저장소 루트의 VERSION 파일입니다.

사용법:
    python scripts/check_version.py                 # VERSION 파일의 버전 출력 (SemVer 형식 검사)
    python scripts/check_version.py --tag v0.1.0    # 태그와 VERSION이 일치하는지 검사 (릴리스 워크플로)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SEMVER = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")


def read_version() -> str:
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not SEMVER.match(version):
        sys.exit(f"VERSION 파일의 값 '{version}'이(가) MAJOR.MINOR.PATCH 형식이 아닙니다.")
    return version


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tag", help="비교할 Git 태그 (예: v0.1.0 또는 refs/tags/v0.1.0)")
    args = parser.parse_args()

    version = read_version()
    if args.tag is None:
        print(version)
        return 0

    tag = args.tag.removeprefix("refs/tags/")
    if not tag.startswith("v") or not SEMVER.match(tag[1:]):
        print(f"태그 '{tag}'가 'vMAJOR.MINOR.PATCH' 형식이 아닙니다.", file=sys.stderr)
        return 1
    if tag[1:] != version:
        print(
            f"태그({tag})와 VERSION 파일({version})이 다릅니다.\n"
            "VERSION 파일을 올리고 커밋한 뒤 같은 버전으로 태그를 만드세요.",
            file=sys.stderr,
        )
        return 1

    print(f"버전 일치: {tag} == {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
