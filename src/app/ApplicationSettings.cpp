// Application의 설정 창 부분 (ADR-0016, docs/03-detailed-design/app.md §4.9).
// 설정 창(ui 모듈)은 앱 상태를 보여 주기만 하고, 바꾸는 것은 여기서 SettingsAction을 처리해서 함

#include "app/Application.h"
#include "deskpet/Version.h"
#include "model/Motion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace deskpet::app {
namespace {

// 동작별 설정 (anim::Motion 순서)
struct MotionSlot {
    const char* name;          // 설정 창에 보이는 이름
    const char* clipKey;       // [animation] 키
    const char* loopStartKey;  // [animation] 키
    std::string core::AnimationConfig::*clip;
    float core::AnimationConfig::*loopStart;
};
constexpr std::array<MotionSlot, anim::kMotionCount> kSlots = {{
    {"대기", "idle_clip", "idle_loop_start", &core::AnimationConfig::idleClip,
     &core::AnimationConfig::idleLoopStart},
    {"걷기", "walk_clip", "walk_loop_start", &core::AnimationConfig::walkClip,
     &core::AnimationConfig::walkLoopStart},
    {"끌려 다닐 때", "dragged_clip", "dragged_loop_start", &core::AnimationConfig::draggedClip,
     &core::AnimationConfig::draggedLoopStart},
    {"공중", "airborne_clip", "airborne_loop_start", &core::AnimationConfig::airborneClip,
     &core::AnimationConfig::airborneLoopStart},
    // 더블클릭에 한 번만 재생 → 반복 시작 없음
    {"상호작용", "interact_clip", nullptr, &core::AnimationConfig::interactClip, nullptr},
}};
static_assert(kSlots.size() == ui::kMotionSettingCount);

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();  // 설정 파일에는 '/'로 (Windows에서도 읽힘)
    return {text.begin(), text.end()};
}

const char* boolText(bool value) {
    return value ? "true" : "false";
}

const char* outlineText(core::OutlineMode mode) {
    switch (mode) {
        case core::OutlineMode::All: return "all";
        case core::OutlineMode::Off: return "off";
        case core::OutlineMode::Model:
        default: return "model";
    }
}

// 설정 파일에 쓰는 실수: 소수 둘째 자리까지, 끝의 0은 뺌 ("1.50" → "1.5", "2.00" → "2")
std::string decimalText(float value) {
    std::string text = std::to_string(std::round(value * 100.0f) / 100.0f);
    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

std::string intText(float value) {
    return std::to_string(std::lround(value));
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
        if (static_cast<anim::Motion>(m) == anim::Motion::Interact) {
            motion.choices.front().label = "점프 (기본 동작)";  // 클립이 없으면 더블클릭에 점프
        }
        // 설정 파일에서 다른 모션 파일을 지정했으면 그것도 목록에 (파일 이름으로)
        if (std::ranges::none_of(motion.choices, [&](const ui::ClipChoice& choice) {
                return choice.value == motion.current;
            })) {
            const std::filesystem::path path(
                std::u8string(motion.current.begin(), motion.current.end()));
            motion.choices.push_back({utf8(path.filename()), motion.current});
        }
        if (clips_[m] && kSlots[m].loopStart != nullptr) {  // 반복하는 동작만 슬라이더
            motion.duration = clips_[m]->duration();
            motion.loopStart = config_.animation.*kSlots[m].loopStart;
        }
    }

    state.scalePercent = scalePercent_;
    state.minScalePercent = kMinScalePercent;
    state.maxScalePercent = kMaxScalePercent;
    state.scaleStepPercent = kScaleStepPercent;
    state.walk = config_.character.walk;
    state.walkOnWindows = config_.character.walkOnWindows;
    state.lookAt = config_.animation.lookAt;
    state.idleMotion = config_.animation.idleMotion;
    state.alwaysOnTop = config_.window.alwaysOnTop;

    state.vsync = config_.renderer.vsync;
    state.msaa = config_.renderer.msaa;
    state.outline = config_.renderer.outline;

    const core::CharacterConfig& c = config_.character;
    state.movement = {static_cast<int>(std::lround(c.gravity)),
                      static_cast<int>(std::lround(c.walkSpeed)),
                      static_cast<int>(std::lround(c.dragThreshold))};
    return state;
}

renderer::RendererOptions Application::rendererOptions() const {
    renderer::RendererOptions options;
    options.vsync = config_.renderer.vsync;
    options.debugLayer = config_.renderer.debugLayer;
    options.msaaSamples = config_.renderer.msaa;
    options.outline = config_.renderer.outline;
    return options;
}

void Application::applyMovement() {
    character_.setParams(character::Params::fromConfig(config_.character));
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
                clip = clipLoader_(
                    static_cast<anim::Motion>(action.motion), action.text,
                    slot.loopStart != nullptr ? config_.animation.*slot.loopStart : 0.0f);
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
        case Kind::SetLoopStart: {
            if (action.motion >= kSlots.size() || kSlots[action.motion].loopStart == nullptr) {
                break;
            }
            const MotionSlot& slot = kSlots[action.motion];
            const float seconds = std::max(action.value, 0.0f);
            config_.animation.*slot.loopStart = seconds;
            // 클립은 공유되는 const 데이터라 복사해서 반복 시작만 바꿈. 자세 경계 상자는 클립 전체
            // 길이로 재므로 그대로
            if (const auto& current = clips_[action.motion]) {
                auto changed = std::make_shared<model::MotionClip>(*current);
                changed->loopStart = seconds;
                clips_[action.motion] = changed;
                if (animator_) {
                    animator_->setClip(static_cast<anim::Motion>(action.motion), changed);
                }
            }
            writeSettings({{"animation", slot.loopStartKey, decimalText(seconds)}});
            break;
        }
        case Kind::SetWalk:
            config_.character.walk = action.flag;
            applyMovement();
            writeSettings({{"character", "walk", boolText(action.flag)}});
            break;
        case Kind::SetAlwaysOnTop:
            config_.window.alwaysOnTop = action.flag;
            window_->setAlwaysOnTop(action.flag);
            writeSettings({{"window", "always_on_top", boolText(action.flag)}});
            break;
        case Kind::SetVsync:
            config_.renderer.vsync = action.flag;
            renderer_->setOptions(rendererOptions());
            writeSettings({{"renderer", "vsync", boolText(action.flag)}});
            break;
        case Kind::SetMsaa:
            if (action.number != 1 && action.number != 2 && action.number != 4 &&
                action.number != 8) {
                break;
            }
            config_.renderer.msaa = action.number;
            renderer_->setOptions(rendererOptions());
            writeSettings({{"renderer", "msaa", std::to_string(action.number)}});
            break;
        case Kind::SetOutline: {
            if (action.number < 0 || action.number > static_cast<int>(core::OutlineMode::Off)) {
                break;
            }
            config_.renderer.outline = static_cast<core::OutlineMode>(action.number);
            renderer_->setOptions(rendererOptions());
            writeSettings({{"renderer", "outline", outlineText(config_.renderer.outline)}});
            break;
        }
        case Kind::SetGravity:
            config_.character.gravity = static_cast<float>(std::max(action.number, 1));
            applyMovement();
            writeSettings({{"character", "gravity", intText(config_.character.gravity)}});
            break;
        case Kind::SetWalkSpeed:
            config_.character.walkSpeed = static_cast<float>(std::max(action.number, 1));
            applyMovement();
            writeSettings({{"character", "walk_speed", intText(config_.character.walkSpeed)}});
            break;
        case Kind::SetDragThreshold:
            config_.character.dragThreshold = static_cast<float>(std::max(action.number, 0));
            applyMovement();
            writeSettings(
                {{"character", "drag_threshold", intText(config_.character.dragThreshold)}});
            break;
        case Kind::ResetMovement: {
            const core::CharacterConfig defaults;
            core::CharacterConfig& c = config_.character;
            c.gravity = defaults.gravity;
            c.walkSpeed = defaults.walkSpeed;
            c.dragThreshold = defaults.dragThreshold;
            applyMovement();
            writeSettings({{"character", "gravity", intText(c.gravity)},
                           {"character", "walk_speed", intText(c.walkSpeed)},
                           {"character", "drag_threshold", intText(c.dragThreshold)}});
            break;
        }
    }
}

void Application::writeSettings(const std::vector<core::IniValue>& values) const {
    if (settingsWriter_) {
        settingsWriter_(values);
    }
}

}  // namespace deskpet::app
