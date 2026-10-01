#pragma once

// 텍스처 바이트 → model::Texture. 형식마다 다른 이미지 저장 방식(내장/외부 파일)을 통일합니다.

#include "model/Model.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace deskpet::model {

// PNG/JPEG/BMP/GIF/DDS/TIFF(매직 바이트로 판별)는 encoded로 두고 렌더러(WIC)가 디코딩합니다.
// 그 외는 TGA로 보고 RGBA로 디코딩하며, 실패하면 원본을 encoded로 둡니다.
[[nodiscard]] Texture makeTexture(std::vector<std::uint8_t> bytes, std::string name);

// baseDirectory 기준 상대 경로(UTF-8, '\' 구분자 허용)의 파일을 읽습니다.
// 기준 폴더가 비었거나 파일이 없으면 빈 Texture (name만 채움)
[[nodiscard]] Texture loadTextureFile(const std::filesystem::path& baseDirectory,
                                      std::string_view relativePathUtf8);

}  // namespace deskpet::model
