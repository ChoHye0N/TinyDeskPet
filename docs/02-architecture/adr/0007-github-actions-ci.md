# ADR-0007: GitHub Actions 기반 CI/CD

| 항목 | 내용 |
|---|---|
| 상태 | 승인 |
| 날짜 | 2026-10-01 |
| 관련 요구사항 | FR-32, NFR-PORT-01, NFR-MAINT-02, NFR-MAINT-03, NFR-SEC-02 |

## 맥락

개인 프로젝트지만 실무와 같은 개발 흐름(PR → 자동 검증 → 머지 → 태그 → 자동 배포)을 익히고, 포트폴리오에서 자동화 역량을 보여 주고 싶습니다.

## 고려한 대안

| 대안 | 장점 | 단점 |
|---|---|---|
| **A. GitHub Actions** | 저장소와 통합, 공개 저장소 무료, Windows 러너 제공, 자료가 가장 많음 | GitHub 종속 |
| B. Azure Pipelines | Windows/MSVC 친화적 | 별도 서비스 가입, 설정 분산 |
| C. Jenkins (자체 호스팅) | 완전한 통제 | 서버 운영 부담, 개인 프로젝트에 과함 |

## 결정

**A. GitHub Actions**를 사용하고, 워크플로를 목적별로 나눕니다.

| 워크플로 | 트리거 | 하는 일 |
|---|---|---|
| `ci.yml` | 모든 PR, `main` 푸시 | 포맷 검사, Linux 빌드+테스트(ASan/UBSan), Windows Debug/Release 빌드+테스트, 실행 파일 아티팩트 업로드 |
| `release.yml` | `v*.*.*` 태그 푸시 | 태그와 CMake 버전 일치 확인 → Release 빌드 → zip 패키지 → GitHub Release 생성 |
| `codeql.yml` | `main` 푸시, PR, 매주 | 정적 보안 분석 (CodeQL C++) |
| `dependabot.yml` | 매주 | 워크플로에서 쓰는 Action 버전 업데이트 PR |

플랫폼 독립 모듈은 **Linux 러너에서도** 빌드·테스트합니다. Windows 헤더를 실수로 include 하면 Linux 잡이 실패하므로 의존성 규칙(R2)이 자동으로 지켜집니다.

## 결과

- 좋아지는 점: `main`은 항상 빌드 가능한 상태로 유지됩니다. 릴리스는 태그 하나로 재현 가능하게 만들어집니다.
- 감수하는 점: Windows 러너는 Linux보다 느립니다 (CI 한 번에 수 분).
- 후속 작업: 빌드 캐시(ccache/sccache), 코드 커버리지, 렌더링 스크린샷 비교 테스트는 [CI/CD 구현 문서](../../05-devops/ci-cd-implementation.md)의 개선 과제로 남깁니다.
