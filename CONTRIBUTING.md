# 기여 가이드

혼자 하는 프로젝트라도 실무와 같은 흐름으로 작업합니다. 자세한 배경은 [브랜치·릴리스 전략](docs/05-devops/branching-and-release.md)을 보세요.

## 작업 흐름 요약

```text
1. main에서 브랜치 생성      git switch -c feat/throw-velocity
2. 작업 + 테스트 + 커밋      (Conventional Commits)
3. 푸시 후 PR 생성           gh pr create --fill
4. CI 통과 확인 → 셀프 리뷰 → Squash and merge
5. 브랜치 삭제
```

## 처음 한 번만

```bash
pip install -r requirements-dev.txt     # clang-format, clang-tidy (CI와 같은 버전)
git config core.hooksPath .githooks     # 커밋 전 포맷 자동 검사
```

## 브랜치 이름

| 접두사 | 용도 | 예 |
|---|---|---|
| `feat/` | 기능 추가 | `feat/tray-icon` |
| `fix/` | 버그 수정 | `fix/drag-capture-lost` |
| `refactor/` | 동작 변화 없는 구조 개선 | `refactor/state-pattern` |
| `docs/` | 문서 | `docs/adr-0008-vrm-parser` |
| `ci/`, `build/` | 파이프라인, 빌드 설정 | `ci/add-ccache` |

## 커밋 메시지 (Conventional Commits)

```text
<type>(<scope>): <요약, 50자 이내>

<본문: 무엇을, 왜 (선택)>

Refs: FR-16, TODO(M1)
```

- type: `feat`, `fix`, `refactor`, `test`, `docs`, `build`, `ci`, `perf`, `chore`
- scope: `core`, `character`, `platform`, `renderer`, `app`, `ci`, `docs`
- 예: `feat(character): 놓는 순간의 드래그 속도로 던지기 구현`

## PR 전에 확인

```bash
python scripts/format.py --check                 # 포맷
cmake --workflow --preset windows-release        # 빌드 + 테스트 (Windows)
```

PR 템플릿의 체크리스트를 채우고, 설계가 바뀌면 문서(상세 설계, ADR)도 같은 PR에서 갱신합니다.
