// Windows 진입점. 구체 클래스(Win32Window, D3D11Renderer)를 아는 유일한 파일입니다 (SAD 규칙 R4).
// 흐름: docs/03-detailed-design/app.md §4.6

#include "app/Application.h"
#include "core/Config.h"
#include "core/Log.h"
#include "core/LogFile.h"
#include "deskpet/Version.h"
#include "model/ModelLoader.h"
#include "platform/win32/Win32Strings.h"
#include "platform/win32/Win32Window.h"
#include "renderer/d3d11/D3D11Renderer.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <objbase.h>
#include <string>
#include <vector>

namespace {

using namespace deskpet;
namespace logging = core::logging;
namespace fs = std::filesystem;

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
    SingleInstance() : handle_(CreateMutexW(nullptr, FALSE, L"Local\\DeskPet.SingleInstance")) {
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

// 다음 실행 때 같은 자리에 나타나도록 발 위치를 deskpet.ini [state]에 기록
void saveLastPosition(const fs::path& configFile, core::Vec2 feet) {
    const std::vector<core::IniValue> values = {
        {"state", "last_x", std::to_string(std::lround(feet.x))},
        {"state", "last_y", std::to_string(std::lround(feet.y))},
    };
    if (!core::saveConfigValues(configFile, values)) {
        logging::warn("마지막 위치를 저장하지 못했습니다 (쓰기 권한 확인)");
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

// 실패해도 종료하지 않고 슬라임으로 계속합니다 (ADR-0008).
std::shared_ptr<const model::Model> loadModel(const core::ModelConfig& config,
                                              const fs::path& baseDir) {
    if (config.path.empty()) {
        return nullptr;
    }
    // 설정 파일은 UTF-8 → u8 경로로 만들어야 한글 폴더 이름이 깨지지 않음
    fs::path path(std::u8string(config.path.begin(), config.path.end()));
    if (path.is_relative()) {
        path = baseDir / path;
    }

    model::LoadResult result = model::loadModelFile(path);
    if (!result.model) {
        logging::warn("모델을 불러오지 못해 슬라임으로 표시합니다 ({}): {}", config.path,
                      result.error);
        return nullptr;
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
    logging::info(
        "모델 로드: {} ({}, 정점 {}, 머티리얼 {}, 텍스처 {}, 본 {}(휴머노이드 {}), 높이 {:.2f}m)",
        config.path, format, m.vertices.size(), m.materials.size(), m.textures.size(),
        m.bones.size(), humanoid, m.bounds.size().y);
    return std::make_shared<const model::Model>(std::move(*result.model));
}

void showError(const std::string& message) {
    const std::wstring text = platform::win32::widen(message);
    MessageBoxW(nullptr, text.c_str(), L"DeskPet", MB_ICONERROR | MB_OK);
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

    logging::info("DeskPet {} ({}) 시작", DESKPET_VERSION_STRING, DESKPET_GIT_HASH);
    for (const std::string& warning : loaded.warnings) {
        logging::warn("설정: {}", warning);
    }

    // WIC(텍스처 디코딩)는 COM 객체라, 쓰는 스레드에서 COM을 먼저 초기화해야 합니다.
    // application보다 먼저 선언해 렌더러가 해제된 뒤에 CoUninitialize되게 합니다.
    const ComScope com;

    app::Application application(loaded.config,
                                 std::make_unique<platform::win32::Win32Window>(instance),
                                 std::make_unique<renderer::d3d11::D3D11Renderer>());
    if (auto model = loadModel(loaded.config.model, baseDir)) {
        application.setModel(std::move(model));
    }

    const int exitCode = application.run();
    if (exitCode == 0) {
        // 공중에서 종료했어도 높이는 바닥으로 저장 (화면 위쪽 밖 좌표는 복원 시 버려지므로)
        const auto& character = application.character();
        saveLastPosition(configFile, {character.position().x, character.ground()});
    }
    if (exitCode != 0) {
        const std::u8string logPath = logFile.u8string();
        showError(
            std::format("DeskPet을 실행하지 못했습니다 (코드 {}).\n자세한 내용은 로그를 "
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
