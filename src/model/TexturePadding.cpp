#include "model/TexturePadding.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace deskpet::model {
namespace {

int wrap(int value, int size) {
    const int r = value % size;
    return r < 0 ? r + size : r;
}

std::size_t texel(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x);
}

float edge(core::Vec2 a, core::Vec2 b, core::Vec2 p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

// 텍셀 중심이 삼각형 안(경계 포함)에 있으면 덮인 것으로 표시
void rasterize(const UvTriangle& uv, int width, int height, std::vector<std::uint8_t>& covered) {
    // 반복 UV: 삼각형을 [0, 1) 근처로 옮겨 픽셀 좌표로 (텍셀 i의 중심 = (i + 0.5) / 크기)
    const float shiftU = std::floor(std::min({uv[0].x, uv[1].x, uv[2].x}));
    const float shiftV = std::floor(std::min({uv[0].y, uv[1].y, uv[2].y}));
    std::array<core::Vec2, 3> p{};
    for (std::size_t i = 0; i < 3; ++i) {
        p[i] = {(uv[i].x - shiftU) * static_cast<float>(width),
                (uv[i].y - shiftV) * static_cast<float>(height)};
    }
    float area = edge(p[0], p[1], p[2]);
    if (std::abs(area) < 1e-8f) {
        return;  // 넓이 0 (선·점)
    }
    if (area < 0.0f) {
        std::swap(p[1], p[2]);  // 감기 방향과 무관하게 같은 부호 판정
        area = -area;
    }

    // 텍스처를 여러 번 감싸는 비정상 삼각형이 루프를 키우지 않도록 2배 크기로 제한
    const int x0 = std::max(static_cast<int>(std::floor(std::min({p[0].x, p[1].x, p[2].x}))), 0);
    const int y0 = std::max(static_cast<int>(std::floor(std::min({p[0].y, p[1].y, p[2].y}))), 0);
    const int x1 =
        std::min(static_cast<int>(std::ceil(std::max({p[0].x, p[1].x, p[2].x}))), width * 2);
    const int y1 =
        std::min(static_cast<int>(std::ceil(std::max({p[0].y, p[1].y, p[2].y}))), height * 2);
    constexpr float kEpsilon = -1e-4f;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const core::Vec2 c{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            if (edge(p[0], p[1], c) >= kEpsilon && edge(p[1], p[2], c) >= kEpsilon &&
                edge(p[2], p[0], c) >= kEpsilon) {
                covered[texel(wrap(x, width), wrap(y, height), width)] = 1;
            }
        }
    }
}

// 섬 바깥으로 한 겹씩 번지는(dilation) 과정. 덮인 이웃(8방향)이 있는 빈 텍셀을 이웃 평균색으로
// 채웁니다. 한 겹을 다 계산한 뒤에 표시해야 같은 겹 안에서 연쇄적으로 번지지 않음 (거리 = 겹 수).
// 반복(REPEAT) 샘플링은 반대편 가장자리와도 섞이므로 이웃도 감싸서 봅니다.
class Dilation {
public:
    Dilation(std::span<std::uint8_t> rgba, int width, int height, std::vector<std::uint8_t> covered)
        : rgba_(rgba),
          width_(width),
          height_(height),
          covered_(std::move(covered)),
          queued_(covered_.size(), 0) {}

    void run(int maxDistance) {
        std::vector<std::size_t> candidates = firstLayer();
        for (int step = 0; step < maxDistance && !candidates.empty(); ++step) {
            candidates = fillLayer(candidates);
        }
    }

private:
    [[nodiscard]] std::size_t neighbor(std::size_t index, int d) const {
        // d = 0..8 (4 = 자기 자신): 3×3 이웃
        const auto w = static_cast<std::size_t>(width_);
        const int x = static_cast<int>(index % w) + d % 3 - 1;
        const int y = static_cast<int>(index / w) + d / 3 - 1;
        return texel(wrap(x, width_), wrap(y, height_), width_);
    }

    // 첫 겹 후보: 덮인 텍셀과 닿은 빈 텍셀 (전체를 한 번만 훑음)
    std::vector<std::size_t> firstLayer() {
        std::vector<std::size_t> candidates;
        for (std::size_t index = 0; index < covered_.size(); ++index) {
            if (covered_[index] == 0 && hasCoveredNeighbor(index)) {
                queued_[index] = 1;
                candidates.push_back(index);
            }
        }
        return candidates;
    }

    [[nodiscard]] bool hasCoveredNeighbor(std::size_t index) const {
        for (int d = 0; d < 9; ++d) {
            if (d != 4 && covered_[neighbor(index, d)] != 0) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::array<std::uint8_t, 4> averageOfCovered(std::size_t index) const {
        std::array<std::uint32_t, 4> sum{};
        std::uint32_t count = 0;
        for (int d = 0; d < 9; ++d) {
            const std::size_t n = neighbor(index, d);
            if (d != 4 && covered_[n] != 0) {
                for (std::size_t c = 0; c < 4; ++c) {
                    sum[c] += rgba_[n * 4 + c];
                }
                ++count;
            }
        }
        std::array<std::uint8_t, 4> color{};
        for (std::size_t c = 0; c < 4 && count > 0; ++c) {
            color[c] = static_cast<std::uint8_t>((sum[c] + count / 2) / count);  // 반올림 평균
        }
        return color;
    }

    // 이번 겹을 칠하고, 그 이웃 중 빈 텍셀을 다음 겹 후보로 반환
    std::vector<std::size_t> fillLayer(const std::vector<std::size_t>& candidates) {
        std::vector<std::array<std::uint8_t, 4>> colors;
        colors.reserve(candidates.size());
        for (const std::size_t index : candidates) {
            colors.push_back(averageOfCovered(index));
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            std::ranges::copy(colors[i],
                              rgba_.begin() + static_cast<std::ptrdiff_t>(candidates[i] * 4));
            covered_[candidates[i]] = 1;
        }
        std::vector<std::size_t> next;
        for (const std::size_t index : candidates) {
            for (int d = 0; d < 9; ++d) {
                const std::size_t n = neighbor(index, d);
                if (covered_[n] == 0 && queued_[n] == 0) {
                    queued_[n] = 1;
                    next.push_back(n);
                }
            }
        }
        return next;
    }

    std::span<std::uint8_t> rgba_;
    int width_;
    int height_;
    std::vector<std::uint8_t> covered_;
    std::vector<std::uint8_t> queued_;
};

}  // namespace

void padUvIslands(std::span<std::uint8_t> rgba, int width, int height,
                  std::span<const UvTriangle> triangles, int maxDistance) {
    const std::size_t pixels = texel(0, height, width);  // width × height
    if (triangles.empty() || width <= 0 || height <= 0 || rgba.size() < pixels * 4) {
        return;
    }
    // 1) 섬에 덮인 텍셀 표시 → 2) 바깥으로 번짐
    std::vector<std::uint8_t> covered(pixels, 0);
    for (const UvTriangle& triangle : triangles) {
        rasterize(triangle, width, height, covered);
    }
    Dilation(rgba, width, height, std::move(covered)).run(maxDistance);
}

std::vector<UvTriangle> collectUvTriangles(const Model& model, int textureIndex) {
    std::vector<UvTriangle> triangles;
    for (const Primitive& primitive : model.primitives) {
        if (primitive.material < 0 ||
            static_cast<std::size_t>(primitive.material) >= model.materials.size() ||
            model.materials[static_cast<std::size_t>(primitive.material)].baseColorTexture !=
                textureIndex) {
            continue;
        }
        const std::size_t end = std::min<std::size_t>(primitive.firstIndex + primitive.indexCount,
                                                      model.indices.size());
        for (std::size_t i = primitive.firstIndex; i + 2 < end; i += 3) {
            UvTriangle uv{};
            bool valid = true;
            for (std::size_t k = 0; k < 3; ++k) {
                const std::uint32_t v = model.indices[i + k];
                if (v >= model.vertices.size()) {
                    valid = false;
                    break;
                }
                uv[k] = {model.vertices[v].u, model.vertices[v].v};
            }
            if (valid) {
                triangles.push_back(uv);
            }
        }
    }
    return triangles;
}

}  // namespace deskpet::model
