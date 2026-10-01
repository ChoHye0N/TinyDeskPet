#pragma once

// 모델 로더 진입점. 형식을 판별해 형식별 로더로 보냅니다 (ADR-0005, ADR-0009).
// 지원: glTF 2.0 / GLB / VRM 0.x·1.0 (cgltf), PMX 2.0·2.1 (직접 구현), FBX (ufbx)
// 명세: docs/03-detailed-design/model.md

#include "model/Model.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace deskpet::model {

struct LoadResult {
    std::optional<Model> model;  // 실패하면 비어 있음
    std::string error;           // 실패 이유 (사람이 읽는 메시지)
};

struct ImportContext {
    // 외부 텍스처 파일(PMX, FBX)을 찾을 기준 폴더. 비어 있으면 외부 파일을 읽지 않음
    std::filesystem::path baseDirectory;
};

// 파일 앞부분(매직 바이트)으로 형식을 판별합니다. 매직이 없는 ASCII FBX는 확장자(".fbx")로 판별.
// extension은 점 포함, 대소문자 무관
[[nodiscard]] ModelFormat detectModelFormat(std::span<const std::uint8_t> bytes,
                                            std::string_view extension = {});

[[nodiscard]] LoadResult loadModelFromMemory(std::span<const std::uint8_t> bytes,
                                             std::string_view extension = {},
                                             const ImportContext& context = {});

// 파일을 읽어 loadModelFromMemory. 외부 텍스처는 파일이 있는 폴더 기준
[[nodiscard]] LoadResult loadModelFile(const std::filesystem::path& path);

}  // namespace deskpet::model
