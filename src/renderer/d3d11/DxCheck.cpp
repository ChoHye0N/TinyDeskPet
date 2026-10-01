#include "renderer/d3d11/DxCheck.h"

#include "core/Log.h"

#include <d2d1.h>
#include <dxgi.h>

#include <format>

namespace deskpet::renderer::d3d11 {
namespace {

std::string_view knownName(HRESULT hr) {
    switch (hr) {
        case DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
        case DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
        case DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
        case DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
        case DXGI_ERROR_UNSUPPORTED: return "DXGI_ERROR_UNSUPPORTED";
        case static_cast<HRESULT>(D2DERR_RECREATE_TARGET): return "D2DERR_RECREATE_TARGET";
        case E_INVALIDARG: return "E_INVALIDARG";
        case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
        case E_NOINTERFACE: return "E_NOINTERFACE";
        case E_FAIL: return "E_FAIL";
        default: return {};
    }
}

std::string_view fileName(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

}  // namespace

std::string hresultToString(HRESULT hr) {
    const auto code = static_cast<unsigned long>(hr);
    const std::string_view name = knownName(hr);
    if (!name.empty()) {
        return std::format("0x{:08X} ({})", code, name);
    }
    return std::format("0x{:08X}", code);
}

bool check(HRESULT hr, std::string_view what, std::source_location where) {
    if (SUCCEEDED(hr)) {
        return true;
    }
    core::logging::error("{} 실패: {} [{}:{}]", what, hresultToString(hr),
                         fileName(where.file_name()), where.line());
    return false;
}

}  // namespace deskpet::renderer::d3d11
