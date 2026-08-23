#include "wannaviewer/playback/MpvApi.hpp"

#include <format>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace wannaviewer {

MpvApi::~MpvApi() {
#ifdef _WIN32
    if (module_) FreeLibrary(static_cast<HMODULE>(module_));
#else
    if (module_) dlclose(module_);
#endif
}

template <typename T>
T MpvApi::Import(const char* name) {
#ifdef _WIN32
    auto address = reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(module_), name));
#else
    auto address = reinterpret_cast<T>(dlsym(module_, name));
#endif
    if (!address) throw std::runtime_error(std::string("libmpv is missing symbol: ") + name);
    return address;
}

void MpvApi::Load(const std::filesystem::path& libraryPath) {
    if (module_) return;
#ifdef _WIN32
    module_ = LoadLibraryExW(libraryPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    module_ = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (!module_) throw std::runtime_error("Unable to load libmpv from " + libraryPath.string());
    create_ = Import<decltype(create_)>("mpv_create");
    initialize_ = Import<decltype(initialize_)>("mpv_initialize");
    terminateDestroy_ = Import<decltype(terminateDestroy_)>("mpv_terminate_destroy");
    setOptionString_ = Import<decltype(setOptionString_)>("mpv_set_option_string");
    setOption_ = Import<decltype(setOption_)>("mpv_set_option");
    setPropertyString_ = Import<decltype(setPropertyString_)>("mpv_set_property_string");
    setProperty_ = Import<decltype(setProperty_)>("mpv_set_property");
    getProperty_ = Import<decltype(getProperty_)>("mpv_get_property");
    getPropertyString_ = Import<decltype(getPropertyString_)>("mpv_get_property_string");
    free_ = Import<decltype(free_)>("mpv_free");
    commandAsync_ = Import<decltype(commandAsync_)>("mpv_command_async");
    observeProperty_ = Import<decltype(observeProperty_)>("mpv_observe_property");
    requestLogMessages_ = Import<decltype(requestLogMessages_)>("mpv_request_log_messages");
    waitEvent_ = Import<decltype(waitEvent_)>("mpv_wait_event");
    wakeup_ = Import<decltype(wakeup_)>("mpv_wakeup");
    errorString_ = Import<decltype(errorString_)>("mpv_error_string");
}

bool MpvApi::IsLoaded() const noexcept { return module_ != nullptr; }
mpv_handle* MpvApi::Create() const { return create_(); }
int MpvApi::Initialize(mpv_handle* handle) const { return initialize_(handle); }
void MpvApi::TerminateDestroy(mpv_handle* handle) const noexcept { if (handle) terminateDestroy_(handle); }
int MpvApi::SetOptionString(mpv_handle* handle, const char* name, const char* value) const { return setOptionString_(handle, name, value); }
int MpvApi::SetOption(mpv_handle* handle, const char* name, mpv_format format, void* value) const { return setOption_(handle, name, format, value); }
int MpvApi::SetPropertyString(mpv_handle* handle, const char* name, const char* value) const { return setPropertyString_(handle, name, value); }
int MpvApi::SetProperty(mpv_handle* handle, const char* name, mpv_format format, void* value) const { return setProperty_(handle, name, format, value); }
int MpvApi::CommandAsync(mpv_handle* handle, std::uint64_t requestId, const char** args) const { return commandAsync_(handle, requestId, args); }
int MpvApi::ObserveProperty(mpv_handle* handle, std::uint64_t replyId, const char* name, mpv_format format) const { return observeProperty_(handle, replyId, name, format); }
int MpvApi::RequestLogMessages(mpv_handle* handle, const char* minimumLevel) const { return requestLogMessages_(handle, minimumLevel); }
mpv_event* MpvApi::WaitEvent(mpv_handle* handle, double timeout) const { return waitEvent_(handle, timeout); }
void MpvApi::Wakeup(mpv_handle* handle) const noexcept { if (handle) wakeup_(handle); }
const char* MpvApi::ErrorString(int error) const noexcept { return errorString_ ? errorString_(error) : "unknown libmpv error"; }

std::string MpvApi::GetString(mpv_handle* handle, const char* name) const {
    char* value = getPropertyString_(handle, name);
    if (!value) return {};
    std::string result(value);
    free_(value);
    return result;
}

double MpvApi::GetDouble(mpv_handle* handle, const char* name, double fallback) const {
    double value = fallback;
    return getProperty_(handle, name, MPV_FORMAT_DOUBLE, &value) >= 0 ? value : fallback;
}

std::int64_t MpvApi::GetInt64(mpv_handle* handle, const char* name, std::int64_t fallback) const {
    std::int64_t value = fallback;
    return getProperty_(handle, name, MPV_FORMAT_INT64, &value) >= 0 ? value : fallback;
}

bool MpvApi::GetFlag(mpv_handle* handle, const char* name, bool fallback) const {
    int value = fallback ? 1 : 0;
    return getProperty_(handle, name, MPV_FORMAT_FLAG, &value) >= 0 ? value != 0 : fallback;
}

} // namespace wannaviewer
