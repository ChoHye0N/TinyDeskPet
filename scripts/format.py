#!/usr/bin/env python3
"""clang-format 실행 도우미.

사용법:
    python scripts/format.py             # src/, tests/ 의 C++ 파일을 제자리에서 포맷
    python scripts/format.py --check     # 포맷이 맞는지 검사만 (CI와 동일). 어긋나면 종료 코드 1
    python scripts/format.py --check --staged   # git에 스테이징된 파일만 검사 (pre-commit 훅)

clang-format 버전이 다르면 결과가 달라질 수 있으므로 18.x를 요구합니다.
설치: pip install -r requirements-dev.txt
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGET_DIRS = ("src", "tests")
EXTENSIONS = {".h", ".hpp", ".cpp", ".cc"}
REQUIRED_MAJOR = 18


def git_files(args: list[str]) -> list[Path] | None:
    try:
        output = subprocess.run(
            ["git", *args], cwd=ROOT, check=True, capture_output=True, text=True
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return [ROOT / line for line in output.splitlines() if line.strip()]


def collect_files(staged: bool) -> list[Path]:
    if staged:
        files = git_files(["diff", "--cached", "--name-only", "--diff-filter=ACMR"]) or []
    else:
        files = git_files(["ls-files", *TARGET_DIRS])
        if files is None:  # git 저장소가 아니면 디렉터리를 직접 탐색
            files = [p for d in TARGET_DIRS for p in (ROOT / d).rglob("*")]

    result = []
    for path in files:
        relative = path.relative_to(ROOT)
        if relative.parts[0] in TARGET_DIRS and path.suffix in EXTENSIONS and path.exists():
            result.append(path)
    return sorted(result)


def find_clang_format(explicit: str | None) -> str:
    executable = explicit or shutil.which("clang-format")
    if not executable:
        sys.exit("clang-format을 찾을 수 없습니다. 'pip install -r requirements-dev.txt'로 설치하세요.")

    version_text = subprocess.run(
        [executable, "--version"], check=True, capture_output=True, text=True
    ).stdout
    match = re.search(r"version (\d+)\.", version_text)
    if not match or int(match.group(1)) != REQUIRED_MAJOR:
        sys.exit(
            f"clang-format {REQUIRED_MAJOR}.x가 필요합니다 (현재: {version_text.strip()}).\n"
            "'pip install -r requirements-dev.txt'로 CI와 같은 버전을 설치하세요."
        )
    return executable


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="수정하지 않고 검사만 합니다")
    parser.add_argument("--staged", action="store_true", help="git에 스테이징된 파일만 대상")
    parser.add_argument("--clang-format", dest="clang_format", help="clang-format 실행 파일 경로")
    args = parser.parse_args()

    files = collect_files(args.staged)
    if not files:
        print("대상 파일이 없습니다.")
        return 0

    executable = find_clang_format(args.clang_format)
    paths = [str(p) for p in files]

    if args.check:
        completed = subprocess.run([executable, "--dry-run", "--Werror", *paths], cwd=ROOT)
        if completed.returncode != 0:
            print(
                "\n포맷이 맞지 않는 파일이 있습니다. 'python scripts/format.py'로 고친 뒤 다시 커밋하세요.",
                file=sys.stderr,
            )
            return 1
        print(f"포맷 검사 통과: {len(files)}개 파일")
        return 0

    subprocess.run([executable, "-i", *paths], cwd=ROOT, check=True)
    print(f"포맷 완료: {len(files)}개 파일")
    return 0


if __name__ == "__main__":
    sys.exit(main())
