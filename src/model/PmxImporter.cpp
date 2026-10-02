// PMX 2.0 / 2.1 (MikuMikuDance 모델) 로더. 명세: docs/03-detailed-design/model.md §4.4, ADR-0009
// 필드 순서는 PmxEditor 동봉 사양서(PMX仕様.txt)를 따릅니다. 모프·강체·조인트는 읽지 않습니다.

#include "model/Importers.h"
#include "model/TextureSource.h"

#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace deskpet::model::detail {
namespace {

// MMD 1단위 ≈ 8cm (미쿠 키 약 20단위 ≈ 1.58m에서 나온 관례값)
constexpr float kMetersPerUnit = 0.08f;
constexpr float kEdgeMetersPerSize = 0.004f;

// 범위를 검사하는 리더. 끝을 넘으면 failed가 되고 이후 값은 0 — 호출자가 구간마다 failed()를 확인
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - pos_; }

    template <class T>
    T read() {
        T value{};
        if (remaining() < sizeof(T)) {
            fail();
            return value;
        }
        std::memcpy(&value, bytes_.data() + pos_, sizeof(T));  // 정렬되지 않은 위치도 안전
        pos_ += sizeof(T);
        return value;
    }

    void skip(std::size_t size) {
        if (remaining() < size) {
            fail();
            return;
        }
        pos_ += size;
    }

    core::Vec3 vec3() {
        const auto x = read<float>();
        const auto y = read<float>();
        const auto z = read<float>();
        return {x, y, z};
    }

    // 개수 필드: 음수이거나 남은 바이트로 불가능한 값이면 실패 (손상 파일의 거대 할당 방지)
    std::size_t count(std::size_t minBytesPerItem) {
        const auto value = read<std::int32_t>();
        if (value < 0 || static_cast<std::size_t>(value) * minBytesPerItem > remaining()) {
            fail();
            return 0;
        }
        return static_cast<std::size_t>(value);
    }

    // 정점 인덱스는 부호 없는 1/2바이트, 4바이트는 int32
    std::uint32_t vertexIndex(std::uint8_t size) {
        switch (size) {
            case 1: return read<std::uint8_t>();
            case 2: return read<std::uint16_t>();
            default: return static_cast<std::uint32_t>(read<std::int32_t>());
        }
    }

    // 그 외 인덱스는 부호 있음. -1 = 없음
    int index(std::uint8_t size) {
        switch (size) {
            case 1: return read<std::int8_t>();
            case 2: return read<std::int16_t>();
            default: return read<std::int32_t>();
        }
    }

    // 텍스트: int32 바이트 길이 + 본문. encoding 0 = UTF-16LE, 1 = UTF-8. 결과는 UTF-8
    std::string text(std::uint8_t encoding) {
        const std::size_t length = count(1);
        if (failed()) {
            return {};
        }
        const std::span<const std::uint8_t> raw = bytes_.subspan(pos_, length);
        pos_ += length;
        if (encoding == 1) {
            return {raw.begin(), raw.end()};
        }
        return utf16ToUtf8(raw);
    }

private:
    void fail() {
        failed_ = true;
        pos_ = bytes_.size();
    }

    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0U | (cp >> 6U));
            out += static_cast<char>(0x80U | (cp & 0x3FU));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0U | (cp >> 12U));
            out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (cp & 0x3FU));
        } else {
            out += static_cast<char>(0xF0U | (cp >> 18U));
            out += static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU));
            out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (cp & 0x3FU));
        }
    }

    static std::string utf16ToUtf8(std::span<const std::uint8_t> raw) {
        std::string out;
        for (std::size_t i = 0; i + 1 < raw.size(); i += 2) {
            std::uint32_t cp = raw[i] | (static_cast<std::uint32_t>(raw[i + 1]) << 8U);
            // 서로게이트 쌍 (U+10000 이상): 상위(D800~DBFF) + 하위(DC00~DFFF)
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < raw.size()) {
                const std::uint32_t low =
                    raw[i + 2] | (static_cast<std::uint32_t>(raw[i + 3]) << 8U);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10U) + (low - 0xDC00);
                    i += 2;
                }
            }
            appendUtf8(out, cp);
        }
        return out;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
    bool failed_ = false;
};

struct Header {
    std::uint8_t encoding = 0;
    std::uint8_t additionalUv = 0;
    std::uint8_t vertexIndexSize = 0;
    std::uint8_t textureIndexSize = 0;
    std::uint8_t materialIndexSize = 0;
    std::uint8_t boneIndexSize = 0;
    std::uint8_t morphIndexSize = 0;
    std::uint8_t rigidIndexSize = 0;
};

// MMD 표준 표정 모프 이름 → 공통 표정
Expression expressionFromMmdName(std::string_view name) {
    if (name == "まばたき") {
        return Expression::Blink;
    }
    if (name == "笑い") {
        return Expression::Happy;  // 눈웃음
    }
    if (name == "びっくり") {
        return Expression::Surprised;
    }
    return Expression::Count;
}

// MMD(왼손, 정면 -Z, 8cm 단위) → 공통 규약(오른손, 정면 +Z, 미터). z 반전 한 번으로 둘 다 해결
core::Vec3 toModelSpace(core::Vec3 p) {
    return {p.x * kMetersPerUnit, p.y * kMetersPerUnit, -p.z * kMetersPerUnit};
}

bool validIndexSize(std::uint8_t size) {
    return size == 1 || size == 2 || size == 4;
}

class PmxParser {
public:
    PmxParser(std::span<const std::uint8_t> bytes, const ImportContext& context)
        : reader_(bytes), context_(context) {}

    std::optional<Model> run(std::string& error) {
        model_.format = ModelFormat::Pmx;
        const bool ok = readHeader(error) && readVertices(error) && readFaces(error) &&
                        readTextures(error) && readMaterials(error) && readBones(error) &&
                        readMorphs(error);
        if (!ok) {
            return std::nullopt;
        }
        if (model_.vertices.empty() || model_.primitives.empty()) {
            error = "그릴 수 있는 메시가 없습니다";
            return std::nullopt;
        }
        model_.bounds = computeBounds(model_.vertices);
        return std::move(model_);
    }

private:
    bool corrupted(std::string& error, std::string_view section) {
        error = std::format("PMX 파일이 손상되었습니다 ({})", section);
        return false;
    }

    bool readHeader(std::string& error) {
        reader_.skip(4);  // "PMX " (판별은 ModelLoader가 이미 함)
        const auto version = reader_.read<float>();
        const auto globalCount = reader_.read<std::uint8_t>();
        if (reader_.failed() || globalCount < 8) {
            return corrupted(error, "헤더");
        }
        if (std::isnan(version) || version < 2.0f || version >= 3.0f) {
            error = std::format("지원하지 않는 PMX 버전입니다 ({})", version);
            return false;
        }
        header_.encoding = reader_.read<std::uint8_t>();
        header_.additionalUv = reader_.read<std::uint8_t>();
        header_.vertexIndexSize = reader_.read<std::uint8_t>();
        header_.textureIndexSize = reader_.read<std::uint8_t>();
        header_.materialIndexSize = reader_.read<std::uint8_t>();
        header_.boneIndexSize = reader_.read<std::uint8_t>();
        header_.morphIndexSize = reader_.read<std::uint8_t>();
        header_.rigidIndexSize = reader_.read<std::uint8_t>();
        reader_.skip(globalCount - 8U);  // 향후 확장분
        if (reader_.failed() || header_.encoding > 1 || header_.additionalUv > 4 ||
            !validIndexSize(header_.vertexIndexSize) || !validIndexSize(header_.textureIndexSize) ||
            !validIndexSize(header_.materialIndexSize) || !validIndexSize(header_.boneIndexSize) ||
            !validIndexSize(header_.morphIndexSize) || !validIndexSize(header_.rigidIndexSize)) {
            return corrupted(error, "헤더 설정값");
        }
        for (int i = 0; i < 4; ++i) {
            (void)reader_.text(header_.encoding);  // 모델 이름·설명 (일본어/영어)
        }
        return !reader_.failed() || corrupted(error, "모델 정보");
    }

    bool readVertices(std::string& error) {
        const std::uint8_t bone = header_.boneIndexSize;
        const std::size_t count = reader_.count(32);  // 위치+법선+UV+가중치 종류 최소 크기
        model_.vertices.resize(count);
        for (Vertex& v : model_.vertices) {
            v.position = toModelSpace(reader_.vec3());
            const core::Vec3 n = reader_.vec3();
            v.normal = {n.x, n.y, -n.z};
            v.u = reader_.read<float>();
            v.v = reader_.read<float>();
            reader_.skip(std::size_t{header_.additionalUv} * 16U);

            if (!readWeights(v, bone)) {
                return corrupted(error, "정점 가중치 종류");
            }
            reader_.skip(4);  // 에지 배율
            if (reader_.failed()) {
                return corrupted(error, "정점");
            }
        }
        return true;
    }

    // 정점 가중치 (ADR-0010). SDEF(구면 보간)는 BDEF2로, QDEF(쿼터니언)는 BDEF4로 근사
    bool readWeights(Vertex& v, std::uint8_t bone) {
        std::array<std::pair<int, float>, 4> influences{};
        const auto type = reader_.read<std::uint8_t>();
        switch (type) {
            case 0:  // BDEF1: 본 1개
                influences[0] = {reader_.index(bone), 1.0f};
                break;
            case 1:    // BDEF2: 본 2개 + 첫 본 가중치
            case 3: {  // SDEF: BDEF2와 같고 뒤에 C, R0, R1 (vec3 × 3)
                const int a = reader_.index(bone);
                const int b = reader_.index(bone);
                const auto w = reader_.read<float>();
                influences[0] = {a, w};
                influences[1] = {b, 1.0f - w};
                if (type == 3) {
                    reader_.skip(36);
                }
                break;
            }
            case 2:  // BDEF4: 본 4개 + 가중치 4개 (합이 1이 아닐 수 있음 → 정규화)
            case 4: {  // QDEF (2.1)
                std::array<int, 4> bones{};
                for (int& index : bones) {
                    index = reader_.index(bone);
                }
                for (std::size_t k = 0; k < 4; ++k) {
                    influences[k] = {bones[k], reader_.read<float>()};
                }
                break;
            }
            default: return false;
        }
        setSkinWeights(v, influences);
        return true;
    }

    bool readFaces(std::string& error) {
        const std::size_t count = reader_.count(header_.vertexIndexSize);
        if (reader_.failed() || count % 3 != 0) {
            return corrupted(error, "면");
        }
        model_.indices.resize(count);
        for (std::size_t i = 0; i < count; i += 3) {
            const std::uint32_t a = reader_.vertexIndex(header_.vertexIndexSize);
            const std::uint32_t b = reader_.vertexIndex(header_.vertexIndexSize);
            const std::uint32_t c = reader_.vertexIndex(header_.vertexIndexSize);
            if (a >= model_.vertices.size() || b >= model_.vertices.size() ||
                c >= model_.vertices.size()) {
                return corrupted(error, "면 인덱스가 정점 수를 넘음");
            }
            // z 반전(거울)으로 앞면 감기 방향이 바뀌므로 두 정점을 맞바꿔 CCW로 되돌림
            model_.indices[i] = a;
            model_.indices[i + 1] = c;
            model_.indices[i + 2] = b;
        }
        return !reader_.failed() || corrupted(error, "면");
    }

    bool readTextures(std::string& error) {
        const std::size_t count = reader_.count(4);
        for (std::size_t i = 0; i < count && !reader_.failed(); ++i) {
            // PMX 텍스처는 모델 파일 기준 상대 경로의 외부 파일
            model_.textures.push_back(
                loadTextureFile(context_.baseDirectory, reader_.text(header_.encoding)));
        }
        return !reader_.failed() || corrupted(error, "텍스처");
    }

    bool readMaterials(std::string& error) {
        const std::uint8_t tex = header_.textureIndexSize;
        const std::size_t count = reader_.count(4);
        std::uint32_t firstIndex = 0;
        for (std::size_t i = 0; i < count; ++i) {
            Material material;
            material.name = reader_.text(header_.encoding);
            const std::string english = reader_.text(header_.encoding);
            if (material.name.empty()) {
                material.name = english;
            }
            const core::Vec3 diffuse = reader_.vec3();
            const auto alpha = reader_.read<float>();
            material.baseColor = {diffuse.x, diffuse.y, diffuse.z, alpha};
            reader_.skip(12 + 4 + 12);  // 반사색, 반사 강도, 환경색
            const auto flags = reader_.read<std::uint8_t>();
            material.doubleSided = (flags & 0x01U) != 0;  // "양면 그리기"
            const core::Vec3 edgeRgb = reader_.vec3();
            const auto edgeAlpha = reader_.read<float>();
            const auto edgeSize = reader_.read<float>();
            if ((flags & 0x10U) != 0 && edgeSize > 0.0f) {  // "에지 그리기"
                // MMD 에지 크기는 화면 기준이라 단위가 없음. MToon과 비슷한 굵기(1 → 4mm)로 근사
                material.outlineWidth = edgeSize * kEdgeMetersPerSize;
                material.outlineColor = {edgeRgb.x, edgeRgb.y, edgeRgb.z, edgeAlpha};
            }
            const int texture = reader_.index(tex);
            reader_.index(tex);  // 스피어 텍스처 (M5 이후)
            reader_.skip(1);     // 스피어 모드
            if (reader_.read<std::uint8_t>() == 0) {
                reader_.index(tex);  // 개별 툰 텍스처
            } else {
                reader_.skip(1);  // 공유 툰 번호
            }
            (void)reader_.text(header_.encoding);  // 메모
            const std::size_t indexCount = reader_.count(0);
            if (reader_.failed() || indexCount % 3 != 0 ||
                firstIndex + indexCount > model_.indices.size()) {
                return corrupted(error, "머티리얼");
            }

            if (texture >= 0 && static_cast<std::size_t>(texture) < model_.textures.size()) {
                material.baseColorTexture = texture;
            }
            // MMD는 모든 재질을 알파 블렌딩으로 그리지만, 깊이 정렬 문제를 피하려고
            // 반투명 재질만 Blend로, 텍스처가 있는 재질은 알파 테스트(Mask)로 근사
            if (alpha < 1.0f) {
                material.alphaMode = AlphaMode::Blend;
            } else if (material.baseColorTexture >= 0) {
                material.alphaMode = AlphaMode::Mask;
            }

            if (indexCount > 0) {
                model_.primitives.push_back({firstIndex, static_cast<std::uint32_t>(indexCount),
                                             static_cast<int>(model_.materials.size())});
            }
            model_.materials.push_back(std::move(material));
            firstIndex += static_cast<std::uint32_t>(indexCount);
        }
        return true;
    }

    bool readBones(std::string& error) {
        const std::uint8_t bone = header_.boneIndexSize;
        const std::size_t count = reader_.count(4);
        model_.bones.resize(count);
        for (Bone& b : model_.bones) {
            b.name = reader_.text(header_.encoding);
            (void)reader_.text(header_.encoding);  // 영어 이름
            b.position = toModelSpace(reader_.vec3());
            b.parent = reader_.index(bone);
            reader_.skip(4);  // 변형 계층
            const auto flags = reader_.read<std::uint16_t>();

            skipBoneOptionalFields(flags);
            if (reader_.failed()) {
                return corrupted(error, "본");
            }
            b.human = humanBoneFromMmdName(b.name);
        }
        for (Bone& b : model_.bones) {
            if (b.parent < 0 || static_cast<std::size_t>(b.parent) >= model_.bones.size()) {
                b.parent = -1;
            }
        }
        dropInvalidJoints();
        return true;
    }

    // 본 플래그에 따라 뒤따르는 가변 길이 필드를 사양서 순서대로 건너뜀
    void skipBoneOptionalFields(std::uint16_t flags) {
        const std::uint8_t bone = header_.boneIndexSize;
        if ((flags & 0x0001U) != 0) {
            reader_.index(bone);  // 꼬리 = 본
        } else {
            reader_.skip(12);  // 꼬리 = 위치 오프셋
        }
        if ((flags & (0x0100U | 0x0200U)) != 0) {
            reader_.skip(bone + 4U);  // 회전·이동 부여: 부모 본 + 부여율
        }
        if ((flags & 0x0400U) != 0) {
            reader_.skip(12);  // 고정 축
        }
        if ((flags & 0x0800U) != 0) {
            reader_.skip(24);  // 로컬 축 X, Z
        }
        if ((flags & 0x2000U) != 0) {
            reader_.skip(4);  // 외부 부모 키
        }
        if ((flags & 0x0020U) != 0) {
            reader_.skip(bone + 4U + 4U);  // IK 타깃, 반복 횟수, 각도 제한
            const std::size_t links = reader_.count(bone + 1U);
            for (std::size_t l = 0; l < links && !reader_.failed(); ++l) {
                reader_.skip(bone);
                if (reader_.read<std::uint8_t>() != 0) {
                    reader_.skip(24);  // 각도 제한 하한·상한
                }
            }
        }
    }

    // 없는 본을 가리키는 영향만 빼고 다시 정규화 (손상 파일 대비)
    void dropInvalidJoints() {
        for (Vertex& v : model_.vertices) {
            std::array<std::pair<int, float>, kMaxInfluences> influences{};
            for (std::size_t k = 0; k < kMaxInfluences; ++k) {
                const bool valid = v.joints[k] < model_.bones.size();
                influences[k] = {valid ? v.joints[k] : -1, v.weights[k]};
            }
            setSkinWeights(v, influences);
        }
    }

    // 모프: 표정에 해당하는 정점 모프만 모으고 나머지 종류는 크기만큼 건너뜀
    bool readMorphs(std::string& error) {
        const std::size_t count = reader_.count(4);
        for (std::size_t i = 0; i < count && !reader_.failed(); ++i) {
            const Expression expression = expressionFromMmdName(reader_.text(header_.encoding));
            (void)reader_.text(header_.encoding);  // 영어 이름
            reader_.skip(1);                       // 조작 패널 (눈썹·눈·입·기타)
            const auto type = reader_.read<std::uint8_t>();
            const std::size_t offsets = reader_.count(1);
            for (std::size_t o = 0; o < offsets && !reader_.failed(); ++o) {
                if (!readMorphOffset(type, expression)) {
                    return corrupted(error, "모프 종류");
                }
            }
        }
        return !reader_.failed() || corrupted(error, "모프");
    }

    bool readMorphOffset(std::uint8_t type, Expression expression) {
        switch (type) {
            case 1: {  // 정점: 정점 인덱스 + 위치 오프셋
                const std::uint32_t vertex = reader_.vertexIndex(header_.vertexIndexSize);
                const core::Vec3 offset = toModelSpace(reader_.vec3());
                if (expression != Expression::Count && vertex < model_.vertices.size()) {
                    model_.expression(expression).add(vertex, offset);
                }
                return true;
            }
            case 0:  // 그룹: 모프 인덱스 + 비율
            case 9:  // 플립 (2.1)
                reader_.skip(header_.morphIndexSize + 4U);
                return true;
            case 2:  // 본: 본 인덱스 + 이동 + 회전(쿼터니언)
                reader_.skip(header_.boneIndexSize + 12U + 16U);
                return true;
            case 3:  // UV, 추가 UV 1~4: 정점 인덱스 + vec4
            case 4:
            case 5:
            case 6:
            case 7: reader_.skip(header_.vertexIndexSize + 16U); return true;
            case 8:  // 재질: 재질 인덱스 + 연산 + 확산·반사·강도·환경·에지색·에지
                     // 크기·텍스처·스피어·툰 계수
                reader_.skip(header_.materialIndexSize + 1U + 16U + 12U + 4U + 12U + 16U + 4U +
                             16U + 16U + 16U);
                return true;
            case 10:  // 임펄스 (2.1): 강체 인덱스 + 로컬 플래그 + 속도 + 토크
                reader_.skip(header_.rigidIndexSize + 1U + 12U + 12U);
                return true;
            default: return false;
        }
    }

    Reader reader_;
    const ImportContext& context_;
    Header header_;
    Model model_;
};

}  // namespace

LoadResult importPmx(std::span<const std::uint8_t> bytes, const ImportContext& context) {
    LoadResult result;
    result.model = PmxParser(bytes, context).run(result.error);
    return result;
}

}  // namespace deskpet::model::detail
