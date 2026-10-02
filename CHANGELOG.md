# Changelog

이 프로젝트의 모든 주요 변경 사항을 기록합니다.
형식은 [Keep a Changelog](https://keepachangelog.com/ko/1.1.0/)를, 버전은 [Semantic Versioning](https://semver.org/lang/ko/)을 따릅니다.

릴리스 워크플로는 아래에서 해당 버전의 섹션을 읽어 GitHub Release 본문으로 사용합니다.
작업 중인 변경은 `## [Unreleased]`에 쌓아 두고, 릴리스할 때 버전 번호로 바꿉니다.

## [Unreleased]

### Added

- VRM 모델 표시 (ADR-0008): cgltf 기반 VRM 0.x/1.0 로더(`model` 모듈), D3D11 `MeshPass`(깊이, premultiplied alpha, 2단 툰), HLSL 빌드 시 컴파일, WIC 텍스처 + 밉맵, 모델에 맞춘 카메라. 기본 모델 Seed-san (`[model] path`)
- 던지기(FR-16): 놓기 직전 드래그 속도로 날아가고, 공기 저항·바닥 마찰로 멈춤
- 화면 경계(FR-17): 가상 데스크톱 좌우 벽에서 반사
- 걷기 상태: 무작위로 좌우로 걷고, 모델은 걷는 방향으로 몸을 돌림
- 시스템 트레이 아이콘(FR-18): 캐릭터와 같은 메뉴 + 숨기기/보이기
- 단일 인스턴스(FR-19)
- 고 DPI 배율 반영과 모니터 간 DPI 변경 처리 (DEBT-01)
- 마지막 위치 저장·복원 (`deskpet.ini [state]`, 주석 유지 쓰기)
- 로그 회전: 직전 실행 로그를 `deskpet.log.1`로 보존, 4MB 한도
- 다중 모델 형식 (ADR-0009): PMX 2.0/2.1(MMD), FBX(바이너리·ASCII, ufbx) 로더. 외부 텍스처 파일과 TGA 지원
- 휴머노이드 본 이름 통일: VRM humanBones, MMD 표준 본, Mixamo 이름 → 공통 `HumanBone`
- 애니메이션 (ADR-0010): 팔 내림, 대기 숨쉬기, 걷기(팔다리 교차), 매달림, 공중, 착지 무릎 굽힘. GPU 스키닝(구조화 버퍼)과 표정(깜빡임·기쁨·놀람). VRM·PMX·FBX 공통
- `[animation] idle_motion` 설정 (끄면 대기 중 GPU 0%)
- Release 빌드도 PDB 생성 (크래시 주소 분석용)

### Changed

- 화면 변화가 없으면 Present를 생략하고 입력을 기다림 (DEBT-02, 대기 중 GPU 0%)
- 기본 창 크기 200×200 → 320×340
- `deskpet.ini`와 모델 폴더를 매 빌드마다 실행 파일 옆으로 복사

### Fixed

- 텍스처를 512로 축소할 때 WIC 스케일러가 BGRA로 내보내 R/B 채널이 뒤바뀌던 문제 (피부가 파랗게 보임)
- 모델이 있을 때도 숨긴 슬라임의 숨쉬기 값이 장면을 바꿔 Present 생략이 동작하지 않던 문제

## [0.1.0] - 2026-10-01

### Added

- 설계 문서: 요구사항 명세서(SRS), 아키텍처 설계서(SAD), ADR 7건, 모듈별 상세 설계, 로드맵
- DirectComposition 기반 투명·항상 위 창과 Direct3D 11 컴포지션 스왑체인
- GPU 디바이스 손실 감지와 자동 복구
- 플레이스홀더 캐릭터: 숨쉬기, 눈 깜빡임, 착지 반동
- 상호작용: 드래그, 낙하, 더블클릭 점프, 우클릭 메뉴(점프/위치 초기화/종료), 캐릭터 밖 클릭 통과
- `deskpet.ini` 설정 파일과 레벨별 로그(`deskpet.log`)
- 실행 파일 버전 정보 리소스 (버전 + Git 커밋)
- 단위 테스트 58개 (core, character, app)
- CI/CD: 포맷 검사, clang-tidy, Linux(ASan/UBSan)·Windows 빌드와 테스트, CodeQL, 태그 기반 자동 릴리스, Dependabot

[Unreleased]: https://github.com/ChoHye0N/TinyDeskPet/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/ChoHye0N/TinyDeskPet/releases/tag/v0.1.0
