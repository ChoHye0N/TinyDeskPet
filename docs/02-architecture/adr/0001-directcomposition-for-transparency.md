# ADR-0001: 투명 창 합성에 DirectComposition 사용

| 항목 | 내용 |
|---|---|
| 상태 | 승인 |
| 날짜 | 2026-10-01 |
| 관련 요구사항 | FR-01, NFR-PERF-01, NFR-PERF-03 |

## 맥락

캐릭터 픽셀만 보이고 나머지는 완전히 투명한 창이 필요합니다. 펫은 하루 종일 떠 있는 프로그램이므로 매 프레임의 비용이 작아야 합니다.

## 고려한 대안

| 대안 | 방식 | 장점 | 단점 |
|---|---|---|---|
| A. 레이어드 윈도우 + `UpdateLayeredWindow` | CPU 메모리의 비트맵을 OS에 전달 | 구현이 쉽고 오래된 OS도 지원 | GPU로 그려도 매 프레임 CPU 메모리로 복사해야 함. 창이 크면 CPU 부하가 큼 |
| B. WPF `AllowsTransparency` | 내부적으로 A와 같은 레이어드 윈도우 | 생산성 | A와 같은 성능 문제, .NET 런타임 부담 |
| C. DWM 프레임 확장 (`DwmExtendFrameIntoClientArea`) + 리다이렉션 비트맵 | 창 전체를 유리 영역으로 확장 | 비교적 간단 | 알파 처리가 OS 버전마다 다르고, 리다이렉션 비트맵 복사 비용이 남음 |
| **D. DirectComposition + 컴포지션 스왑체인** | 스왑체인을 DComp 비주얼로 DWM에 직접 전달 | 복사 없이 GPU에서 합성, Present 모델과 VSync를 그대로 사용 | 초기 설정 코드가 많음, Windows 8 이상 |

## 결정

**D. DirectComposition**을 사용합니다. 창은 `WS_EX_NOREDIRECTIONBITMAP`으로 만들고, `CreateSwapChainForComposition` + `DXGI_ALPHA_MODE_PREMULTIPLIED` 스왑체인을 DComp 비주얼에 연결합니다.

`prototypes/desktop_pet_prototype.cpp`에서 이 방식이 동작함을 먼저 확인했습니다 (스파이크).

## 결과

- 좋아지는 점: 프레임당 CPU 복사가 없어 NFR-PERF-03을 만족합니다. 3D 렌더링(D3D11)을 같은 스왑체인에 그대로 그릴 수 있습니다.
- 감수하는 점: 모든 셰이더 출력은 premultiplied alpha여야 합니다. GPU 드라이버 리셋 시 창이 사라질 수 있어 디바이스 손실 복구가 필수입니다 (FR-33).
- 후속 작업: 디바이스 손실 복구를 M0에 포함합니다. 클릭 통과는 DComp와 별개로 `SetWindowRgn`으로 처리합니다.
