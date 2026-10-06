#pragma once

// Application 테스트용 가짜 구현.
// 상태(State) 구조체를 테스트가 소유하고, 가짜 객체는 그 참조만 가집니다.
// → Application이 가짜 객체를 소유(unique_ptr)한 뒤에도 테스트에서 결과를 확인할 수 있습니다.

#include "core/Clock.h"
#include "platform/IWindow.h"
#include "renderer/IRenderer.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

namespace deskpet::testing {

struct FakeWindowState {
    // 설정
    bool createResult = true;
    core::RectI workArea{0, 0, 1920, 1040};
    std::deque<std::vector<core::Event>> frames;  // pollEvents 호출마다 앞에서 하나씩 꺼냄
    int pollsBeforeQuit = 1000;  // 이 횟수만큼 poll하면 QuitRequested 발생
    int menuChoice = 0;          // showContextMenu가 돌려줄 id
    float dpiScale = 1.0f;
    core::RectI desktopBounds{0, 0, 1920, 1080};
    bool trayResult = true;
    std::function<void(int pollIndex, FakeWindowState&)> onPoll;  // poll 직전 훅
    core::PointI cursor{-10000, -10000};  // 마우스 커서 (화면 좌표). 기본은 화면 밖

    // 관찰 결과
    bool created = false;
    bool shown = false;
    bool visible = false;
    core::SizeI size;
    std::string trayTooltip;  // 비어 있으면 트레이 아이콘 없음
    int waitCount = 0;
    int pollCount = 0;
    core::PointI position;
    std::vector<core::PointI> positionHistory;
    bool clickThrough = false;  // true면 클릭이 아래 창으로 통과
    int clickThroughChanges = 0;
    int menuShownCount = 0;
    std::vector<platform::MenuItem> lastMenu;
};

class FakeWindow final : public platform::IWindow {
public:
    explicit FakeWindow(FakeWindowState& state) : state_(state) {}

    bool create(const platform::WindowDesc& desc) override {
        state_.size = desc.size;
        state_.position = desc.position;
        state_.created = state_.createResult;
        return state_.createResult;
    }

    void show() override {
        state_.shown = true;
        state_.visible = true;
    }
    void hide() override { state_.visible = false; }

    void waitForEvents(int /*timeoutMs*/) override { ++state_.waitCount; }

    void pollEvents(std::vector<core::Event>& out) override {
        if (state_.onPoll) {
            state_.onPoll(state_.pollCount, state_);
        }
        ++state_.pollCount;

        if (!state_.frames.empty()) {
            const auto& frame = state_.frames.front();
            out.insert(out.end(), frame.begin(), frame.end());
            state_.frames.pop_front();
        }
        if (state_.pollCount >= state_.pollsBeforeQuit) {
            out.emplace_back(core::QuitRequestedEvent{});
        }
    }

    void setPosition(core::PointI topLeft) override {
        state_.position = topLeft;
        state_.positionHistory.push_back(topLeft);
    }

    [[nodiscard]] core::PointI position() const override { return state_.position; }
    void setSize(core::SizeI size) override { state_.size = size; }
    [[nodiscard]] core::SizeI size() const override { return state_.size; }
    [[nodiscard]] core::RectI workArea() const override { return state_.workArea; }
    [[nodiscard]] core::RectI desktopBounds() const override { return state_.desktopBounds; }
    [[nodiscard]] float dpiScale() const override { return state_.dpiScale; }

    bool showTrayIcon(const std::string& tooltip) override {
        if (state_.trayResult) {
            state_.trayTooltip = tooltip;
        }
        return state_.trayResult;
    }

    void setClickThrough(bool enabled) override {
        if (enabled != state_.clickThrough) {
            ++state_.clickThroughChanges;
        }
        state_.clickThrough = enabled;
    }
    [[nodiscard]] core::PointI cursorPosition() const override { return state_.cursor; }

    int showContextMenu(const std::vector<platform::MenuItem>& items,
                        core::PointI /*screen*/) override {
        ++state_.menuShownCount;
        state_.lastMenu = items;
        return state_.menuChoice;
    }

    [[nodiscard]] void* nativeHandle() const override { return &state_; }

private:
    FakeWindowState& state_;
};

struct FakeRendererState {
    bool initializeResult = true;
    std::vector<renderer::FrameResult> results;  // render 호출 순서대로 반환. 다 쓰면 Ok

    bool initialized = false;
    bool shutdownCalled = false;
    std::size_t renderCount = 0;
    renderer::RenderScene previousScene;  // lastScene 바로 앞 프레임
    renderer::RenderScene lastScene;
    core::SizeI initialSize;
    core::SizeI lastResize;
    renderer::RendererOptions options;
    // 창 내부 좌표 → 그 픽셀의 알파. 기본은 전부 투명
    std::function<float(core::PointI)> alphaAt = [](core::PointI) { return 0.0f; };
};

class FakeRenderer final : public renderer::IRenderer {
public:
    explicit FakeRenderer(FakeRendererState& state) : state_(state) {}

    bool initialize(void* /*nativeWindow*/, core::SizeI size,
                    const renderer::RendererOptions& options) override {
        state_.options = options;
        state_.initialSize = size;
        state_.initialized = state_.initializeResult;
        return state_.initializeResult;
    }

    renderer::FrameResult render(const renderer::RenderScene& scene) override {
        state_.previousScene = state_.lastScene;
        state_.lastScene = scene;
        const std::size_t index = state_.renderCount++;
        return index < state_.results.size() ? state_.results[index] : renderer::FrameResult::Ok;
    }

    void resize(core::SizeI size) override { state_.lastResize = size; }
    // 실제 렌더러는 몇 프레임 늦게 결과가 오지만, 가짜는 바로 돌려줌
    std::optional<renderer::AlphaSample> sampleAlpha(core::PointI local) override {
        return renderer::AlphaSample{local, state_.alphaAt(local)};
    }
    void shutdown() override { state_.shutdownCalled = true; }

private:
    FakeRendererState& state_;
};

// 호출될 때마다 1/60초씩 증가하는 가짜 시간
inline core::TimeSource makeFakeTime(double step = 1.0 / 60.0) {
    return [now = 0.0, step]() mutable {
        now += step;
        return now;
    };
}

}  // namespace deskpet::testing
