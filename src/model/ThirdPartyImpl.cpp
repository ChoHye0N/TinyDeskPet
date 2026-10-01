// 단일 헤더 서드파티 라이브러리의 구현부를 이 번역 단위 하나에서만 생성합니다.
// (ufbx는 ufbx.c를 같은 타깃에서 C로 컴파일)

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

// TGA만 필요하므로 나머지 디코더는 빼서 크기와 공격 표면을 줄임
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_TGA
#define STBI_NO_STDIO  // 파일 입출력은 우리 코드가 담당 (메모리에서만 디코딩)
#include <stb_image.h>
