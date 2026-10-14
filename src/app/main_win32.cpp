// Windows 진입점. 구체 클래스(Win32Window, D3D11Renderer)를 아는 유일한 파일입니다 (SAD 규칙 R4).
// 흐름: docs/03-detailed-design/app.md §4.6

#include "app/Application.h"
#include "core/Config.h"
#include "core/Log.h"
#include "core/LogFile.h"
#include "deskpet/Version.h"
#include "model/ModelLoader.h"
#include "model/Motion.h"
#include "platform/win32/Win32Strings.h"
#include "platform/win32/Win32Window.h"
#include "renderer/d3d11/D3D11Renderer.h"
#include "renderer/d3d11/WicTexture.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <objbase.h>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace deskpet;
namespace logging = core::logging;
namespace fs = std::filesystem;

// 텍스처 여백 채우기 방식 (설정, 설정 창에서 바뀜). 모델 로더가 작업 스레드에서 읽음
std::atomic<core::TexturePadding> gTexturePadding{core::TexturePadding::Auto};

fs::path executableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return fs::current_path();
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return fs::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);  // 긴 경로
    }
}

// 로그 파일 한도. 대기 중에는 거의 쓰지 않으므로 넉넉함 (이전 실행 로그는 .1로 보존)
constexpr std::uintmax_t kMaxLogBytes = 4U * 1024U * 1024U;

// Visual Studio 출력 창 + (설정 시) 로그 파일로 보내는 싱크
void setupLogging(const core::LogConfig& config, const fs::path& logFile) {
    logging::setLevel(config.level);

    auto file = std::make_shared<core::LogFile>();
    if (config.toFile) {
        (void)file->open(logFile, kMaxLogBytes);
    }

    logging::setSink([file](logging::Level /*level*/, std::string_view line) {
        std::wstring wide = platform::win32::widen(line);
        wide += L'\n';
        OutputDebugStringW(wide.c_str());
        file->write(line);
    });
}

// 이름 있는 뮤텍스로 중복 실행을 막습니다 (FR-19). 핸들은 프로세스가 끝날 때 OS가 닫으므로
// 살아 있는 동안 계속 가지고 있기만 하면 됩니다.
// "Local\\" 접두사: 같은 로그인 세션 안에서만 유일 (다른 사용자 세션의 실행은 막지 않음)
class SingleInstance {
public:
    SingleInstance() : handle_(CreateMutexW(nullptr, FALSE, L"Local\\TinyDeskPet.SingleInstance")) {
        alreadyRunning_ = handle_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
    }
    ~SingleInstance() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    SingleInstance(SingleInstance&&) = delete;
    SingleInstance& operator=(SingleInstance&&) = delete;

    [[nodiscard]] bool alreadyRunning() const { return alreadyRunning_; }

private:
    HANDLE handle_;
    bool alreadyRunning_ = false;
};

// 다음 실행 때 같은 자리·같은 크기로 나타나도록 발 위치와 크기를 deskpet.ini [state]에 기록
// modelPath: 메뉴로 다른 모델을 열었으면 그 경로 (UTF-8, 비면 저장 안 함)
void saveState(const fs::path& configFile, core::Vec2 feet, int scalePercent,
               const std::string& modelPath) {
    std::vector<core::IniValue> values = {
        {"state", "last_x", std::to_string(std::lround(feet.x))},
        {"state", "last_y", std::to_string(std::lround(feet.y))},
        {"state", "scale", std::to_string(scalePercent)},
    };
    if (!modelPath.empty()) {
        values.push_back({"model", "path", modelPath});
    }
    if (!core::saveConfigValues(configFile, values)) {
        logging::warn("마지막 위치·크기를 저장하지 못했습니다 (쓰기 권한 확인)");
    }
}

struct ComScope {
    ComScope() : ok(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ~ComScope() {
        if (ok) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ComScope(ComScope&&) = delete;
    ComScope& operator=(ComScope&&) = delete;

    bool ok;
};

// 설정의 경로(UTF-8) → 파일 경로. u8 경로로 만들어야 한글 폴더 이름이 깨지지 않음.
// 상대 경로는 실행 파일 폴더 기준
fs::path resolvePath(const std::string& configured, const fs::path& baseDir) {
    fs::path path(std::u8string(configured.begin(), configured.end()));
    return path.is_relative() ? baseDir / path : path;
}

// 작업 스레드용 COM (텍스처 디코딩 WIC). std::async는 스레드를 재사용할 수 있어 이미 다른 방식으로
// 초기화되어 있을 수 있음 (RPC_E_CHANGED_MODE) → 그래도 쓸 수 있고, 해제는 우리가 성공했을 때만
struct WorkerCom {
    WorkerCom() : result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~WorkerCom() {
        if (SUCCEEDED(result)) {
            CoUninitialize();
        }
    }
    WorkerCom(const WorkerCom&) = delete;
    WorkerCom& operator=(const WorkerCom&) = delete;
    WorkerCom(WorkerCom&&) = delete;
    WorkerCom& operator=(WorkerCom&&) = delete;

    [[nodiscard]] bool usable() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }

    HRESULT result;
};

std::string toUtf8(const fs::path& path) {
    const std::u8string u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

// 실패하면 nullptr (앱은 지금 모델 또는 슬라임으로 계속, ADR-0008). Application이 작업 스레드에서
// 부름: 파일 읽기와 파싱만 하고 COM·창은 건드리지 않음 (로그는 스레드 안전)
std::shared_ptr<const model::Model> loadModel(const fs::path& path) {
    const auto started = std::chrono::steady_clock::now();
    const std::string name = toUtf8(path.filename());
    model::LoadResult result = model::loadModelFile(path);
    if (!result.model) {
        logging::warn("모델을 불러오지 못했습니다 ({}): {}", name, result.error);
        return nullptr;
    }
    // 텍스처 디코딩·축소도 여기서 (렌더 스레드에서 하면 큰 모델은 0.7초가량 화면이 멈춤)
    int preparedTextures = 0;
    {
        const WorkerCom com;
        if (com.usable()) {
            preparedTextures =
                renderer::d3d11::prepareTextures(*result.model, gTexturePadding.load());
        }
    }
    const model::Model& m = *result.model;
    const char* format = "glTF";
    if (m.format == model::ModelFormat::Pmx) {
        format = "PMX";
    } else if (m.format == model::ModelFormat::Fbx) {
        format = "FBX";
    } else if (m.version == model::VrmVersion::V1) {
        format = "VRM 1.0";
    } else if (m.version == model::VrmVersion::V0) {
        format = "VRM 0.x";
    }
    const auto humanoid = std::ranges::count_if(
        m.bones, [](const model::Bone& b) { return b.human != model::HumanBone::None; });
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
    logging::info(
        "모델 로드: {} ({}, 정점 {}, 머티리얼 {}, 텍스처 {}(준비 {}), 본 {}(휴머노이드 {}), "
        "흔들림 {}(충돌체 {}), 높이 {:.2f}m, {:.2f}초)",
        name, format, m.vertices.size(), m.materials.size(), m.textures.size(), preparedTextures,
        m.bones.size(), humanoid, m.springGroups.size(), m.springColliders.size(),
        m.bounds.size().y, elapsed.count());
    if (!m.droppedProps.empty()) {
        std::string names;
        for (const std::string& prop : m.droppedProps) {
            names += (names.empty() ? "" : ", ") + prop;
        }
        logging::info("소품 메시 {}개 제외 (몸 밖의 뼈에만 붙음): {}", m.droppedProps.size(),
                      names);
    }
    if (!m.meta.name.empty()) {
        logging::info("모델 정보: {} / 작가 {} / 라이선스 {}", m.meta.name, m.meta.author,
                      m.meta.license);
    }
    return std::make_shared<const model::Model>(std::move(*result.model));
}

// 동작 하나의 모션 파일 (ADR-0013). 실패하면 nullptr (그 동작은 코드로 만든 동작으로 계속)
std::shared_ptr<const model::MotionClip> loadMotionClip(anim::Motion motion,
                                                        const std::string& configured,
                                                        float loopStart, const fs::path& baseDir) {
    // 크기를 적지 않고 개수를 검사: 동작이 늘었는데 이름을 빠뜨리면 nullptr이 되어 로그에서 예외
    constexpr auto kNames =
        std::to_array<const char*>({"idle", "walk", "dragged", "airborne", "interact"});
    static_assert(kNames.size() == anim::kMotionCount);
    constexpr std::array<const char*, 5> kFormats = {"?", "VRMA", "VMD", "FBX", "glTF"};
    const char* name = kNames[static_cast<std::size_t>(motion)];
    model::MotionLoadResult result = model::loadMotionFile(resolvePath(configured, baseDir));
    if (!result.clip) {
        logging::warn("모션을 불러오지 못해 기본 동작을 씁니다 ({} = {}): {}", name, configured,
                      result.error);
        return nullptr;
    }
    model::MotionClip& clip = *result.clip;
    clip.loopStart = loopStart;
    logging::info(
        "모션 로드: {} = {} ({}, {:.2f}초, 본 {}개, 그 밖의 본 {}개, 숨김 본 {}개, "
        "모프 가중치 {}개, 엉덩이 이동 {}, 반복 {:.2f}초부터)",
        name, configured, kFormats[static_cast<std::size_t>(clip.format)], clip.duration(),
        clip.trackCount(), clip.boneTracks.size(), clip.hiddenBones.size(),
        clip.weightTracks.size(), clip.hipsTranslation.empty() ? "없음" : "있음", clip.loopStart);
    return std::make_shared<const model::MotionClip>(std::move(clip));
}

// 동작별 모션 파일. 비어 있는 동작은 코드로 만든 동작
app::Application::MotionClips loadMotionClips(const core::AnimationConfig& config,
                                              const fs::path& baseDir) {
    struct Entry {
        anim::Motion motion;
        const std::string* path;
        float loopStart;
    };
    const std::array<Entry, anim::kMotionCount> entries = {{
        {anim::Motion::Idle, &config.idleClip, config.idleLoopStart},
        {anim::Motion::Walk, &config.walkClip, config.walkLoopStart},
        {anim::Motion::Dragged, &config.draggedClip, config.draggedLoopStart},
        {anim::Motion::Airborne, &config.airborneClip, config.airborneLoopStart},
        {anim::Motion::Interact, &config.interactClip, 0.0f},  // 한 번만 재생
    }};
    app::Application::MotionClips clips{};
    for (const auto& [motion, configured, loopStart] : entries) {
        if (!configured->empty()) {
            clips[static_cast<std::size_t>(motion)] =
                loadMotionClip(motion, *configured, loopStart, baseDir);
        }
    }
    return clips;
}

// Windows 앱 모드가 어둡게인지 (설정 > 개인 설정 > 색). 값이 없으면 밝게
bool appsUseDarkTheme() {
    DWORD light = 1;
    DWORD size = sizeof(light);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return status == ERROR_SUCCESS && light == 0;
}

// 설정 창 한글 글꼴: 시스템의 맑은 고딕 (배포 파일에 글꼴을 넣지 않음)
fs::path systemUiFont() {
    std::array<wchar_t, MAX_PATH> windows{};
    const UINT length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (length == 0 || length >= windows.size()) {
        return {};
    }
    return fs::path(windows.data()) / L"Fonts" / L"malgun.ttf";
}

void showError(const std::string& message) {
    const std::wstring text = platform::win32::widen(message);
    MessageBoxW(nullptr, text.c_str(), L"TinyDeskPet", MB_ICONERROR | MB_OK);
}

int runApplication(HINSTANCE instance) {
    const fs::path baseDir = executableDirectory();
    const fs::path logFile = baseDir / "deskpet.log";

    // 로그 파일을 열기 전에 검사해야 두 번째 실행이 첫 실행의 로그를 .1로 밀어내지 않음
    const SingleInstance instanceGuard;
    if (instanceGuard.alreadyRunning()) {
        return 0;  // 이미 떠 있는 펫이 보이므로 조용히 종료
    }

    const fs::path configFile = baseDir / "deskpet.ini";
    core::ConfigLoadResult loaded = core::loadConfigFile(configFile);
    setupLogging(loaded.config.log, logFile);

    logging::info("TinyDeskPet {} ({}) 시작", DESKPET_VERSION_STRING, DESKPET_GIT_HASH);
    for (const std::string& warning : loaded.warnings) {
        logging::warn("설정: {}", warning);
    }

    // WIC(텍스처 디코딩)는 COM 객체라, 쓰는 스레드에서 COM을 먼저 초기화해야 합니다.
    // application보다 먼저 선언해 렌더러가 해제된 뒤에 CoUninitialize되게 합니다.
    const ComScope com;

    app::Application application(loaded.config,
                                 std::make_unique<platform::win32::Win32Window>(instance),
                                 std::make_unique<renderer::d3d11::D3D11Renderer>());
    application.setMotionClips(loadMotionClips(loaded.config.animation, baseDir));
    // 설정 창 (ADR-0016): 고른 클립을 읽고, 바꾼 값은 바로 deskpet.ini에 씀
    application.setClipLoader(
        [&baseDir](anim::Motion motion, const std::string& configured, float loopStart) {
            return loadMotionClip(motion, configured, loopStart, baseDir);
        });
    application.setSettingsWriter([&configFile](const std::vector<core::IniValue>& values) {
        if (!core::saveConfigValues(configFile, values)) {
            logging::warn("설정을 저장하지 못했습니다 (쓰기 권한 확인)");
        }
    });
    application.setUiFont(systemUiFont());
    application.setUiDarkTheme(appsUseDarkTheme());
    gTexturePadding = loaded.config.renderer.texturePadding;
    application.setTexturePaddingHandler(
        [](core::TexturePadding padding) { gTexturePadding = padding; });
    // 큰 모델은 읽는 데 1초 넘게 걸리므로 작업 스레드에서 (그동안 창·메뉴는 바로 동작)
    application.setModelLoader(&loadModel);
    const fs::path configuredModel = loaded.config.model.path.empty()
                                         ? fs::path{}
                                         : resolvePath(loaded.config.model.path, baseDir);
    if (!configuredModel.empty()) {
        application.loadModelAsync(configuredModel);
    }

    const int exitCode = application.run();
    if (exitCode == 0) {
        // 공중에서 종료했어도 높이는 바닥으로 저장 (화면 위쪽 밖 좌표는 복원 시 버려지므로)
        const auto& character = application.character();
        // 메뉴로 다른 모델을 열었으면 다음 실행에도 그 모델로
        const fs::path& opened = application.modelPath();
        const bool changed = !opened.empty() && opened != configuredModel;
        saveState(configFile, {character.position().x, character.ground()},
                  application.scalePercent(), changed ? toUtf8(opened) : std::string{});
    }
    if (exitCode != 0) {
        const std::u8string logPath = logFile.u8string();
        showError(
            std::format("TinyDeskPet을 실행하지 못했습니다 (코드 {}).\n자세한 내용은 로그를 "
                        "확인하세요:\n{}",
                        exitCode, std::string(logPath.begin(), logPath.end())));
    }
    return exitCode;
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE /*previous*/,
                    _In_ PWSTR /*commandLine*/, _In_ int /*showCommand*/) {
    // 우리 코드는 예외를 던지지 않지만, 표준 라이브러리 예외(bad_alloc 등)는 여기서 최종
    // 처리합니다.
    try {
        return runApplication(instance);
    } catch (const std::exception& e) {
        logging::error("처리되지 않은 예외: {}", e.what());
        showError(std::format("예기치 않은 오류: {}", e.what()));
    } catch (...) {
        logging::error("처리되지 않은 알 수 없는 예외");
        showError("예기치 않은 오류가 발생했습니다.");
    }
    return static_cast<int>(app::ExitCode::UnhandledException);
}
