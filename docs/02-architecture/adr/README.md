# 아키텍처 의사결정 기록 (ADR)

ADR(Architecture Decision Record)은 **중요한 기술 결정과 그 이유**를 짧게 남기는 문서입니다.
코드는 "무엇을 했는지"를 보여 주지만 "왜 다른 방법을 쓰지 않았는지"는 보여 주지 못합니다. 6개월 뒤의 나, 또는 면접관이 그 이유를 알 수 있게 하는 것이 목적입니다.

## 규칙

- 파일 이름: `NNNN-짧은-영문-제목.md` (번호는 순서대로 증가, 재사용 금지)
- 상태: `제안` → `승인` → (필요 시) `폐기` 또는 `대체됨: ADR-NNNN`
- 승인된 ADR은 **내용을 고치지 않습니다.** 결정이 바뀌면 새 ADR을 쓰고 기존 ADR의 상태만 `대체됨`으로 바꿉니다.
- 새 ADR은 [0000-template.md](0000-template.md)를 복사해서 작성합니다.

## 목록

| 번호 | 제목 | 상태 |
|---|---|---|
| [0001](0001-directcomposition-for-transparency.md) | 투명 창 합성에 DirectComposition 사용 | 승인 |
| [0002](0002-platform-renderer-abstraction.md) | 플랫폼·렌더러 인터페이스 분리와 의존성 주입 | 승인 |
| [0003](0003-manual-window-drag.md) | OS 기본 창 드래그 대신 직접 드래그 구현 | 승인 |
| [0004](0004-fixed-timestep-update.md) | 고정 시간 간격 업데이트 | 승인 |
| [0005](0005-vrm-over-live2d.md) | 캐릭터 포맷으로 VRM 채택 | 승인 |
| [0006](0006-cmake-presets-ninja.md) | CMake Presets + Ninja Multi-Config 빌드 | 승인 |
| [0007](0007-github-actions-ci.md) | GitHub Actions 기반 CI/CD | 승인 |
| [0008](0008-vrm-before-interaction.md) | VRM 모델 표시를 M1 상호작용보다 먼저 진행 | 승인 |
| [0009](0009-multiple-model-formats.md) | VRM 외에 PMX·FBX 모델 형식 지원 | 승인 |
| [0010](0010-procedural-animation.md) | 코드로 계산하는 휴머노이드 애니메이션과 GPU 스키닝 | 승인 |
| [0011](0011-fullscreen-overlay.md) | 화면 전체 오버레이 창 + 픽셀 단위 클릭 통과 | 승인 |
| [0012](0012-mtoon-linear-color.md) | MToon 셰이딩과 선형 색공간 (셰이더에서 sRGB 변환) | 승인 |
