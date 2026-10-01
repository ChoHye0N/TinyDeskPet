# prototypes

정식 코드에 들어가기 전, **기술적으로 가능한지 확인하기 위해 만든 실험 코드(스파이크)** 를 보관합니다. 빌드 시스템에 포함되지 않습니다.

| 파일 | 목적 | 결과 | 관련 문서 |
|---|---|---|---|
| `desktop_pet_prototype.cpp` | Direct2D + DirectComposition으로 투명 창에 캐릭터를 그리고 드래그·점프가 되는지 확인 | 동작 확인. 단, `HTCAPTION` 드래그 중 애니메이션이 멈추는 문제 발견 | [ADR-0001](../docs/02-architecture/adr/0001-directcomposition-for-transparency.md), [ADR-0003](../docs/02-architecture/adr/0003-manual-window-drag.md) |

## 단독 빌드 (참고용)

Visual Studio "x64 Native Tools Command Prompt"에서:

```bat
cl /EHsc /std:c++17 /O2 /utf-8 desktop_pet_prototype.cpp /link /SUBSYSTEM:WINDOWS
```

> 실무에서도 스파이크 코드는 버리거나 이렇게 따로 보관하고, 정식 코드는 설계를 거쳐 새로 작성하는 경우가 많습니다. 실험 코드를 그대로 키우면 구조가 무너지기 쉽기 때문입니다.
