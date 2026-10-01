#pragma once

// 테스트용 최소 glTF(JSON + base64 data URI 버퍼) 생성기.
// 접근자(accessor)·버퍼 뷰·버퍼는 자동으로 만들고, 나머지(meshes, nodes 등)는 JSON 조각으로
// 받습니다.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace deskpet::test {

class GltfBuilder {
public:
    // float 접근자 (type: "SCALAR", "VEC2", "VEC3", "VEC4", "MAT4"). 반환: 접근자 인덱스
    int addFloats(const std::vector<float>& values, const std::string& type) {
        const int components = componentCount(type);
        std::string minMax;
        if (type == "VEC3") {  // POSITION은 min/max가 필수
            float lo[3] = {1e30f, 1e30f, 1e30f};
            float hi[3] = {-1e30f, -1e30f, -1e30f};
            for (std::size_t i = 0; i < values.size(); ++i) {
                lo[i % 3] = std::min(lo[i % 3], values[i]);
                hi[i % 3] = std::max(hi[i % 3], values[i]);
            }
            minMax = ",\"min\":[" + list(lo, 3) + "],\"max\":[" + list(hi, 3) + "]";
        }
        return addAccessor(values.data(), values.size() * sizeof(float), 5126,
                           static_cast<int>(values.size()) / components, type, minMax);
    }

    int addUint16(const std::vector<std::uint16_t>& values, const std::string& type) {
        return addAccessor(values.data(), values.size() * sizeof(std::uint16_t), 5123,
                           static_cast<int>(values.size()) / componentCount(type), type, "");
    }

    int addUint8(const std::vector<std::uint8_t>& values, const std::string& type) {
        return addAccessor(values.data(), values.size(), 5121,
                           static_cast<int>(values.size()) / componentCount(type), type, "");
    }

    // 이미지용 원시 바이트 버퍼 뷰. 반환: 버퍼 뷰 인덱스
    int addBufferView(const std::vector<std::uint8_t>& bytes) {
        return appendView(bytes.data(), bytes.size());
    }

    // extra: 최상위 JSON 멤버들 (예: "\"meshes\":[...],\"nodes\":[...]")
    [[nodiscard]] std::string build(const std::string& extra) const {
        std::string json = "{\"asset\":{\"version\":\"2.0\"},";
        json += "\"buffers\":[{\"byteLength\":" + std::to_string(data_.size()) +
                ",\"uri\":\"data:application/octet-stream;base64," + base64(data_) + "\"}],";
        json += "\"bufferViews\":[" + join(views_) + "],";
        json += "\"accessors\":[" + join(accessors_) + "]";
        if (!extra.empty()) {
            json += "," + extra;
        }
        json += "}";
        return json;
    }

private:
    static int componentCount(const std::string& type) {
        if (type == "SCALAR")
            return 1;
        if (type == "VEC2")
            return 2;
        if (type == "VEC3")
            return 3;
        if (type == "VEC4")
            return 4;
        return 16;  // MAT4
    }

    static std::string list(const float* values, int count) {
        std::string s;
        for (int i = 0; i < count; ++i) {
            s += (i > 0 ? "," : "") + std::to_string(values[i]);
        }
        return s;
    }

    static std::string join(const std::vector<std::string>& items) {
        std::string s;
        for (std::size_t i = 0; i < items.size(); ++i) {
            s += (i > 0 ? "," : "") + items[i];
        }
        return s;
    }

    static std::string base64(const std::vector<std::uint8_t>& bytes) {
        static constexpr char kTable[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < bytes.size(); i += 3) {
            const std::size_t left = bytes.size() - i;
            std::uint32_t chunk = static_cast<std::uint32_t>(bytes[i]) << 16U;
            if (left > 1)
                chunk |= static_cast<std::uint32_t>(bytes[i + 1]) << 8U;
            if (left > 2)
                chunk |= bytes[i + 2];
            out += kTable[(chunk >> 18U) & 63U];
            out += kTable[(chunk >> 12U) & 63U];
            out += left > 1 ? kTable[(chunk >> 6U) & 63U] : '=';
            out += left > 2 ? kTable[chunk & 63U] : '=';
        }
        return out;
    }

    int appendView(const void* bytes, std::size_t size) {
        while (data_.size() % 4 != 0) {  // 접근자 정렬 요구사항
            data_.push_back(0);
        }
        const std::size_t offset = data_.size();
        data_.resize(offset + size);
        std::memcpy(data_.data() + offset, bytes, size);
        views_.push_back("{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) +
                         ",\"byteLength\":" + std::to_string(size) + "}");
        return static_cast<int>(views_.size()) - 1;
    }

    int addAccessor(const void* bytes, std::size_t size, int componentType, int count,
                    const std::string& type, const std::string& minMax) {
        const int view = appendView(bytes, size);
        accessors_.push_back("{\"bufferView\":" + std::to_string(view) + ",\"componentType\":" +
                             std::to_string(componentType) + ",\"count\":" + std::to_string(count) +
                             ",\"type\":\"" + type + "\"" + minMax + "}");
        return static_cast<int>(accessors_.size()) - 1;
    }

    std::vector<std::uint8_t> data_;
    std::vector<std::string> views_;
    std::vector<std::string> accessors_;
};

}  // namespace deskpet::test
