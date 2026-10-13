// Application의 설정 창 부분 (ADR-0016, docs/03-detailed-design/app.md §4.9).
// 설정 창(ui 모듈)은 앱 상태를 보여 주기만 하고, 바꾸는 것은 여기서 SettingsAction을 처리해서 함

#include "app/Application.h"
#include "deskpet/Version.h"
#include "model/Motion.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace deskpet::app {
namespace {

// 동작별 설정 (anim::Motion 순서)
struct MotionSlot {
    const char* name;     // 설정 창에 보이는 이름
    const char* clipKey;  // [animation] 키
    std::string core::AnimationConfig::*clip;
    float core::AnimationConfig::*loopStart;
};
constexpr std::array<MotionSlot, anim::kMotionCount> kSlots = {{
    {"대기", "idle_clip", &core::AnimationConfig::idleClip, &core::AnimationConfig::idleLoopStart},
    {"걷기", "walk_clip", &core::AnimationConfig::walkClip, &core::AnimationConfig::walkLoopStart},
    {"끌려 다닐 때", "dragged_clip", &core::AnimationConfig::draggedClip,
     &core::AnimationConfig::draggedLoopStart},
    {"공중", "airborne_clip", &core::AnimationConfig::airborneClip,
     &core::AnimationConfig::airborneLoopStart},
}};
static_assert(kSlots.size() == ui::kMotionSettingCount);

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();  // 설정 파일에는 '/'로 (Windows에서도 읽힘)
    return {text.begin(), text.end()};
}

const char* boolText(bool value) {
    return value ? "true" : "false";
}

}  // namespace

void Application::setClipLoader(ClipLoader loader) {
    clipLoader_ = std::move(loader);
}

void Application::setSettingsWriter(SettingsWriter writer) {
    settingsWriter_ = std::move(writer);
}

void Application::openSettings() {
    // 캐릭터가 그려지는 영역을 가리지 않게 그 옆에 띄움 (오버레이 내부 px)
    const core::RectI box = characterRect();
    const core::RectI visible = visibleRect();  // 상자 내부 px
    const int left = box.left - overlay_.left;
    const int top = box.top - overlay_.top;
    settingsUi_.setScale(dpiScale_);
    refreshClipChoices();
    settingsUi_.open(
        {left + visible.left, top + visible.top, left + visible.right, top + visible.bottom});
}

core::RectI Application::settingsRect() const {
    const core::RectI local = settingsUi_.windowRect();
    return {local.left + overlay_.left, local.top + overlay_.top, local.right + overlay_.left,
            local.bottom + overlay_.top};
}

// 클립 선택 목록 = 기본 동작 + 모델 파일 안 애니메이션 ("모델 파일#이름"). 모델이 바뀔 때만 다시
// 읽음
void Application::refreshClipChoices() {
    if (!clipChoices_.empty() && clipChoicesPath_ == modelPath_) {
        return;
    }
    clipChoicesPath_ = modelPath_;
    clipChoices_ = {{"기본 동작", ""}};
    if (modelPath_.empty()) {
        return;
    }
    const std::string file = utf8(modelPath_) + '#';
    for (const std::string& name : model::listMotionClips(modelPath_)) {
        std::string value = file;
        value += name;
        clipChoices_.push_back({name, std::move(value)});
    }
}

ImDrawData* Application::updateSettings(float dt) {
    if (!settingsUi_.isOpen()) {
        return nullptr;
    }
    // 클릭 통과 중에는 마우스 메시지가 오지 않으므로 위치는 매 프레임 직접 물어봄
    const core::PointI cursor = window_->cursorPosition();
    settingsUi_.setMousePosition({static_cast<float>(cursor.x - overlay_.left),
                                  static_cast<float>(cursor.y - overlay_.top)});
    refreshClipChoices();  // 설정 창을 연 채로 다른 모델을 열었을 수 있음
    std::vector<ui::SettingsAction> actions;
    ImDrawData* data =
        settingsUi_.frame(dt, {overlay_.width(), overlay_.height()}, settingsState(), actions);
    for (const ui::SettingsAction& action : actions) {
        applySettingsAction(action);
    }
    return data;
}

ui::SettingsState Application::settingsState() const {
    ui::SettingsState state;
    state.version = DESKPET_VERSION_STRING;
    state.modelLoading = pendingModel_.valid();
    if (model_) {
        const model::ModelMeta& meta = model_->meta;
        state.modelName = meta.name.empty() ? utf8(modelPath_.stem()) : meta.name;
        if (state.modelName.empty()) {
            state.modelName = "모델";
        }
        state.modelAuthor = meta.author;
        state.modelLicense = meta.license;
    }
    state.modelFile = utf8(modelPath_.filename());

    for (std::size_t m = 0; m < kSlots.size(); ++m) {
        ui::MotionSetting& motion = state.motions[m];
        motion.name = kSlots[m].name;
        motion.current = config_.animation.*kSlots[m].clip;
        motion.choices = clipChoices_;
        // 설정 파일에서 다른 모션 파일을 지정했으면 그것도 목록에 (파일 이름으로)
        if (std::ranges::none_of(motion.choices, [&](const ui::ClipChoice& choice) {
                return choice.value == motion.current;
            })) {
            const std::filesystem::path path(
                std::u8string(motion.current.begin(), motion.current.end()));
            motion.choices.push_back({utf8(path.filename()), motion.current});
        }
    }

    state.scalePercent = scalePercent_;
    state.minScalePercent = kMinScalePercent;
    state.maxScalePercent = kMaxScalePercent;
    state.scaleStepPercent = kScaleStepPercent;
    state.lookAt = config_.animation.lookAt;
    state.walkOnWindows = config_.character.walkOnWindows;
    state.idleMotion = config_.animation.idleMotion;
    return state;
}

void Application::applySettingsAction(const ui::SettingsAction& action) {
    using Kind = ui::SettingsAction::Kind;
    switch (action.kind) {
        case Kind::OpenModel: openModelFile(); break;
        case Kind::SetClip: {
            if (action.motion >= kSlots.size()) {
                break;
            }
            const MotionSlot& slot = kSlots[action.motion];
            config_.animation.*slot.clip = action.text;
            std::shared_ptr<const model::MotionClip> clip;
            if (!action.text.empty() && clipLoader_) {
                clip = clipLoader_(static_cast<anim::Motion>(action.motion), action.text,
                                   config_.animation.*slot.loopStart);
            }
            clips_[action.motion] = std::move(clip);
            refreshAnimationAsync();
            writeSettings({{"animation", slot.clipKey, action.text}});
            break;
        }
        case Kind::SetScale:
            setScalePercent(action.number);
            writeSettings({{"state", "scale", std::to_string(scalePercent_)}});
            break;
        case Kind::SetLookAt:
            config_.animation.lookAt = action.flag;
            writeSettings({{"animation", "look_at", boolText(action.flag)}});
            break;
        case Kind::SetWalkOnWindows:
            config_.character.walkOnWindows = action.flag;
            refreshGround();
            writeSettings({{"character", "walk_on_windows", boolText(action.flag)}});
            break;
        case Kind::SetIdleMotion:
            config_.animation.idleMotion = action.flag;
            writeSettings({{"animation", "idle_motion", boolText(action.flag)}});
            break;
    }
}

void Application::writeSettings(const std::vector<core::IniValue>& values) const {
    if (settingsWriter_) {
        settingsWriter_(values);
    }
}

}  // namespace deskpet::app
