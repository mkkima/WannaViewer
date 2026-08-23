#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <mpv/client.h>

namespace wannaviewer {

class MpvApi final {
public:
    MpvApi() = default;
    ~MpvApi();
    MpvApi(const MpvApi&) = delete;
    MpvApi& operator=(const MpvApi&) = delete;

    void Load(const std::filesystem::path& libraryPath);
    [[nodiscard]] bool IsLoaded() const noexcept;

    [[nodiscard]] mpv_handle* Create() const;
    [[nodiscard]] int Initialize(mpv_handle* handle) const;
    void TerminateDestroy(mpv_handle* handle) const noexcept;
    [[nodiscard]] int SetOptionString(mpv_handle* handle, const char* name, const char* value) const;
    [[nodiscard]] int SetOption(mpv_handle* handle, const char* name, mpv_format format, void* value) const;
    [[nodiscard]] int SetPropertyString(mpv_handle* handle, const char* name, const char* value) const;
    [[nodiscard]] int SetProperty(mpv_handle* handle, const char* name, mpv_format format, void* value) const;
    [[nodiscard]] int CommandAsync(mpv_handle* handle, std::uint64_t requestId, const char** args) const;
    [[nodiscard]] int ObserveProperty(mpv_handle* handle, std::uint64_t replyId, const char* name, mpv_format format) const;
    [[nodiscard]] int RequestLogMessages(mpv_handle* handle, const char* minimumLevel) const;
    [[nodiscard]] mpv_event* WaitEvent(mpv_handle* handle, double timeout) const;
    void Wakeup(mpv_handle* handle) const noexcept;
    [[nodiscard]] const char* ErrorString(int error) const noexcept;

    [[nodiscard]] std::string GetString(mpv_handle* handle, const char* name) const;
    [[nodiscard]] double GetDouble(mpv_handle* handle, const char* name, double fallback = 0.0) const;
    [[nodiscard]] std::int64_t GetInt64(mpv_handle* handle, const char* name, std::int64_t fallback = 0) const;
    [[nodiscard]] bool GetFlag(mpv_handle* handle, const char* name, bool fallback = false) const;

private:
    template <typename T> T Import(const char* name);

#ifdef _WIN32
    void* module_{nullptr};
#else
    void* module_{nullptr};
#endif
    decltype(&mpv_create) create_{nullptr};
    decltype(&mpv_initialize) initialize_{nullptr};
    decltype(&mpv_terminate_destroy) terminateDestroy_{nullptr};
    decltype(&mpv_set_option_string) setOptionString_{nullptr};
    decltype(&mpv_set_option) setOption_{nullptr};
    decltype(&mpv_set_property_string) setPropertyString_{nullptr};
    decltype(&mpv_set_property) setProperty_{nullptr};
    decltype(&mpv_get_property) getProperty_{nullptr};
    decltype(&mpv_get_property_string) getPropertyString_{nullptr};
    decltype(&mpv_free) free_{nullptr};
    decltype(&mpv_command_async) commandAsync_{nullptr};
    decltype(&mpv_observe_property) observeProperty_{nullptr};
    decltype(&mpv_request_log_messages) requestLogMessages_{nullptr};
    decltype(&mpv_wait_event) waitEvent_{nullptr};
    decltype(&mpv_wakeup) wakeup_{nullptr};
    decltype(&mpv_error_string) errorString_{nullptr};
};

} // namespace wannaviewer
