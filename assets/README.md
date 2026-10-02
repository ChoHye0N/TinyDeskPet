# assets

캐릭터 모델과 텍스처를 두는 폴더입니다.

## 규칙

- **모델 파일(`.vrm`, `.glb`)은 저장소에 커밋하지 않습니다.** `models/` 폴더는 `.gitignore`에 등록되어 있습니다 ([ADR-0005](../docs/02-architecture/adr/0005-vrm-over-live2d.md)).
  VRM 모델에는 제작자가 정한 이용 조건(재배포 금지 등)이 있어, 공개 저장소에 올리면 라이선스를 위반할 수 있기 때문입니다.

빌드하면 `assets/models/`가 실행 파일 옆 `models/`로 복사되고, `deskpet.ini`의 `[model] path`가 가리키는 파일을 읽습니다.

## 다른 형식 (PMX, FBX)

ADR-0009부터 `.pmx`(MMD)와 `.fbx`도 읽습니다. 경로만 바꾸면 됩니다.

```ini
[model]
path = models/miku/miku.pmx     ; 텍스처는 .pmx와 같은 폴더 기준 상대 경로로 찾음
```

- **PMX는 폴더째** 넣으세요. 텍스처가 모델 파일 밖에 따로 있어서, `.pmx`만 옮기면 흰색으로 나옵니다. 지원하는 텍스처 형식은 PNG/JPG/BMP/TGA/DDS이고, 스피어(.spa/.sph)와 툰 텍스처는 아직 쓰지 않습니다.
- **FBX**는 텍스처를 내장한 파일(Mixamo의 "Embed textures" 등)이 가장 편합니다. 외부 텍스처면 FBX와 같은 폴더에 두세요.
- **MMD 모델의 이용 규약**(읽어보기/Readme)을 꼭 확인하세요. "MMD 외 소프트웨어 사용 금지", "개조 금지"인 모델이 많습니다.

## 직접 만들기

[VRoid Studio](https://vroid.com/studio)로 만든 캐릭터는 내보낼 때 이용 조건을 직접 정할 수 있어, 포트폴리오 시연용으로 가장 안전합니다.
