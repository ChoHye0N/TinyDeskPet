# DeskPet 문서 센터

DeskPet 프로젝트의 모든 설계 문서와 운영 문서를 모아 둔 곳입니다.
문서는 실제 회사의 개발 흐름처럼 **요구사항 → 아키텍처 → 상세 설계 → 구현 → 운영** 순서로 구성되어 있습니다.

## 읽는 순서

```mermaid
flowchart LR
    A["01 요구사항<br/>무엇을 만드는가"] --> B["02 아키텍처<br/>어떤 구조로 만드는가"]
    B --> C["03 상세 설계<br/>각 모듈은 어떻게 동작하는가"]
    C --> D["04 코딩 규칙<br/>어떻게 작성하는가"]
    D --> E["05 DevOps<br/>어떻게 빌드·검증·배포하는가"]
    E --> F["06 로드맵<br/>다음에 무엇을 구현하는가"]
```

## 문서 목록

| 분류 | 문서 | 설명 |
|---|---|---|
| 요구사항 | [SRS.md](01-requirements/SRS.md) | 소프트웨어 요구사항 명세서. 기능/비기능 요구사항, 범위, 추적표 |
| 아키텍처 | [SAD.md](02-architecture/SAD.md) | 소프트웨어 아키텍처 설계서. 레이어, 컴포넌트, 런타임 흐름, 배포 구조 |
| 아키텍처 | [adr/](02-architecture/adr/README.md) | 아키텍처 의사결정 기록(ADR). "왜 이렇게 했는가"의 근거 |
| 상세 설계 | [03-detailed-design/](03-detailed-design/README.md) | 모듈별(core, platform, renderer, character, app) 인터페이스와 동작 명세 |
| 코딩 규칙 | [coding-style.md](04-conventions/coding-style.md) | 명명 규칙, 소유권, 오류 처리, 포맷팅 규칙 |
| DevOps | [05-devops/](05-devops/README.md) | CI/CD 구현 설명, 사용 가이드, 브랜치·릴리스 전략, 로컬 개발 환경 |
| 로드맵 | [ROADMAP.md](06-roadmap/ROADMAP.md) | 마일스톤별 구현 과제와 완료 기준 |

## 문서 작성 규칙

- 모든 문서는 Markdown으로 작성하고, 다이어그램은 [Mermaid](https://mermaid.js.org/)를 사용합니다. GitHub에서 바로 렌더링됩니다.
- 요구사항은 `FR-xx`(기능), `NFR-xx`(비기능) ID로 식별하고, 설계와 코드와 테스트에서 이 ID로 추적합니다.
- 코드의 미구현 지점은 `TODO(M번호): 설명` 형식으로 표시하며, [ROADMAP.md](06-roadmap/ROADMAP.md)의 과제와 연결됩니다.
- 설계를 바꾸는 결정은 먼저 ADR을 작성(또는 기존 ADR을 대체)한 뒤 코드를 바꿉니다.
- 각 문서 상단의 **변경 이력** 표를 갱신합니다.
