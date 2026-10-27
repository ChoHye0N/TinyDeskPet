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

const char* paddingText(core::TexturePadding padding) {
    switch (padding) {
        case core::TexturePadding::On: return "on";
        case core::TexturePadding::Off: return "off";
        case core::TexturePadding::Auto:
        default: return "auto";
    }
}

const char* outlineText(core::OutlineMode mode) {
    switch (mode) {
        case core::OutlineMode::All: return "all";
        case core::OutlineMode::Off: return "off";
        case core::OutlineMode::Silhouette: return "silhouette";
        case core::OutlineMode::Auto: return "auto";
        case core::OutlineMode::Model:
        default: return "model";
    }
}

const char* accentText(core::AccentColor accent) {
    switch (accent) {
        case core::AccentColor::Purple: return "purple";
        case core::AccentColor::Blue: return "blue";
        case core::AccentColor::Green: return "green";
        case core::AccentColor::Orange: return "orange";
        case core::AccentColor::Pink:
        default: return "pink";
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

void Application::setTexturePaddingHandler(TexturePaddingHandler handler) {
    texturePaddingHandler_ = std::move(handler);
}

void Application::setTextureProcessor(TextureProcessor processor) {
    textureProcessor_ = std::move(processor);
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
    state.accent = config_.ui.accent;

    state.vsync = config_.renderer.vsync;
    state.msaa = config_.renderer.msaa;
    state.outline = config_.renderer.outline;
    state.texturePadding = config_.renderer.texturePadding;
    state.outlineScale = config_.renderer.outlineScale;

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
    // 렌더러에는 메시 외곽선 방식만 (자동은 모델에 따라 정하고, 실루엣은 장면의 두께로 넘김)
    const core::OutlineMode effective = effectiveOutline();
    options.outline =
        effective == core::OutlineMode::Silhouette ? core::OutlineMode::Off : effective;
    options.outlineScale = static_cast<float>(config_.renderer.outlineScale) / 100.0f;
    return options;
}

// 자동이면 모델에 외곽선 정보(재질별 굵기, VRM MToon·PMX 에지)가 있는지로 모델 지정·외곽만을 고름.
// 정보가 없는 모델(게임에서 내보낸 glTF 등)에 모든 재질을 쓰면 눈·입에도 선이 생기므로 실루엣
core::OutlineMode Application::effectiveOutline() const {
    const core::OutlineMode mode = config_.renderer.outline;
    if (mode != core::OutlineMode::Auto) {
        return mode;
    }
    const bool hasOutlineInfo =
        model_ && std::ranges::any_of(model_->materials, [](const model::Material& material) {
            return material.outlineWidth > 0.0f;
        });
    return hasOutlineInfo ? core::OutlineMode::Model : core::OutlineMode::Silhouette;
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
        case Kind::SetLoopStart: setLoopStart(action.motion, action.value); break;
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
        case Kind::SetAccent:
            if (action.number < 0 || action.number > static_cast<int>(core::AccentColor::Orange)) {
                break;
            }
            config_.ui.accent = static_cast<core::AccentColor>(action.number);
            writeSettings({{"ui", "accent", accentText(config_.ui.accent)}});
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
            if (action.number < 0 || action.number > static_cast<int>(core::OutlineMode::Auto)) {
                break;
            }
            config_.renderer.outline = static_cast<core::OutlineMode>(action.number);
            renderer_->setOptions(rendererOptions());
            writeSettings({{"renderer", "outline", outlineText(config_.renderer.outline)}});
            break;
        }
        case Kind::SetTexturePadding: setTexturePadding(action.number); break;
        case Kind::SetOutlineScale:
            config_.renderer.outlineScale = std::clamp(action.number, 0, 300);
            renderer_->setOptions(rendererOptions());
            writeSettings(
                {{"renderer", "outline_scale", std::to_string(config_.renderer.outlineScale)}});
            break;
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

// 텍스처 여백 채우기 방식 (설정 창). 텍스처를 준비할 때 쓰이므로 로더에 알리고 지금 모델을 다시
// 읽음
void Application::setTexturePadding(int number) {
    if (number < 0 || number > static_cast<int>(core::TexturePadding::Off)) {
        return;
    }
    const auto padding = static_cast<core::TexturePadding>(number);
    if (padding == config_.renderer.texturePadding) {
        return;
    }
    config_.renderer.texturePadding = padding;
    if (texturePaddingHandler_) {
        texturePaddingHandler_(padding);
    }
    // 바로 보이도록 지금 모델에 새 방식을 적용. 텍스처만 다시 준비할 수 있으면 그것만 (파일 파싱·
    // 클립·자세 경계 상자를 건너뜀), 아니면 파일을 다시 읽음
    if (model_ && textureProcessor_) {
        retextureAsync(padding);
    } else if (!modelPath_.empty()) {
        loadModelAsync(modelPath_);
    }
    writeSettings({{"renderer", "texture_padding", paddingText(padding)}});
}

// 동작별 반복 시작 시각 (설정 창). 반복하지 않는 동작(상호작용)은 무시
void Application::setLoopStart(std::size_t motion, float value) {
    if (motion >= kSlots.size() || kSlots[motion].loopStart == nullptr) {
        return;
    }
    const MotionSlot& slot = kSlots[motion];
    const float seconds = std::max(value, 0.0f);
    config_.animation.*slot.loopStart = seconds;
    // 클립은 공유되는 const 데이터라 복사해서 반복 시작만 바꿈. 자세 경계 상자는 클립 전체 길이로
    // 재므로 그대로
    if (const auto& current = clips_[motion]) {
        auto changed = std::make_shared<model::MotionClip>(*current);
        changed->loopStart = seconds;
        clips_[motion] = changed;
        if (animator_) {
            animator_->setClip(static_cast<anim::Motion>(motion), changed);
        }
    }
    writeSettings({{"animation", slot.loopStartKey, decimalText(seconds)}});
}

void Application::writeSettings(const std::vector<core::IniValue>& values) const {
    if (settingsWriter_) {
        settingsWriter_(values);
    }
}

}  // namespace deskpet::app
