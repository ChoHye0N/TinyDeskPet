#!/usr/bin/env python3
"""clang-tidy 정적 분석 실행 도우미.

플랫폼 독립 모듈(core, model, character, app)의 소스를 compile_commands.json 기준으로 분석합니다.
Windows 전용 코드는 Linux에서 분석할 수 없으므로 제외합니다 (Windows 코드는 MSVC /W4 + CodeQL이 담당).

사용법:
    cmake --preset linux-gcc                       # compile_commands.json 생성
    python scripts/run_clang_tidy.py --build-dir build/linux-gcc
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE_DIRS = ("src/core", "src/model", "src/anim", "src/character", "src/app")
# src/model/ThirdPartyImpl.cpp는 서드파티(cgltf, stb) 구현부라 제외
EXCLUDE_FILES = {"src/app/main_win32.cpp", "src/model/ThirdPartyImpl.cpp"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", required=True, help="compile_commands.json이 있는 빌드 폴더")
    parser.add_argument("--clang-tidy", dest="clang_tidy", help="clang-tidy 실행 파일 경로")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2)
    args = parser.parse_args()

    build_dir = (ROOT / args.build_dir).resolve()
    database = build_dir / "compile_commands.json"
    if not database.exists():
        sys.exit(f"{database} 가 없습니다. 먼저 'cmake --preset linux-gcc'를 실행하세요.")

    executable = args.clang_tidy or shutil.which("clang-tidy")
    if not executable:
        sys.exit("clang-tidy를 찾을 수 없습니다. 'pip install -r requirements-dev.txt'로 설치하세요.")

    entries = json.loads(database.read_text(encoding="utf-8"))
    files = sorted(
        {
            Path(entry["file"]).resolve()
            for entry in entries
            if any(
                Path(entry["file"]).resolve().is_relative_to(ROOT / d) for d in INCLUDE_DIRS
            )
            and Path(entry["file"]).resolve().relative_to(ROOT).as_posix() not in EXCLUDE_FILES
        }
    )
    if not files:
        sys.exit("분석할 파일이 없습니다.")

    def run(path: Path) -> tuple[Path, int, str]:
        completed = subprocess.run(
            [executable, "-p", str(build_dir), "--quiet", str(path)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        return path, completed.returncode, completed.stdout + completed.stderr

    failures = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for path, code, output in pool.map(run, files):
            relative = path.relative_to(ROOT).as_posix()
            if code != 0:
                failures += 1
                print(f"✗ {relative}\n{output}")
            else:
                print(f"✓ {relative}")

    print(f"\n{len(files)}개 파일 분석, 실패 {failures}개")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
