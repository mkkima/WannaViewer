#include "wannaviewer/network/Process.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <windows.h>

namespace wannaviewer {
namespace {

class UniqueHandle final {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() { Reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.Release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept { if (this != &other) Reset(other.Release()); return *this; }
    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
    [[nodiscard]] HANDLE Release() noexcept { return std::exchange(handle_, nullptr); }
    void Reset(HANDLE value = nullptr) noexcept { if (handle_ && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); handle_ = value; }
private:
    HANDLE handle_{nullptr};
};

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) throw std::runtime_error("Invalid UTF-8 process argument");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring Quote(std::wstring_view value) {
    if (value.empty()) return L"\"\"";
    if (value.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) return std::wstring(value);
    std::wstring result = L"\"";
    unsigned backslashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') { ++backslashes; continue; }
        if (ch == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

} // namespace

ProcessResult RunProcess(const std::filesystem::path& executable,
                         const std::vector<std::string>& arguments,
                         std::chrono::milliseconds timeout,
                         std::size_t maximumOutputBytes,
                         std::stop_token stopToken) {
    if (!std::filesystem::is_regular_file(executable)) throw std::runtime_error("Helper executable is missing");

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE rawRead = nullptr;
    HANDLE rawWrite = nullptr;
    if (!CreatePipe(&rawRead, &rawWrite, &security, 0)) throw std::runtime_error("CreatePipe failed");
    UniqueHandle read(rawRead);
    UniqueHandle write(rawWrite);
    if (!SetHandleInformation(read.Get(), HANDLE_FLAG_INHERIT, 0)) throw std::runtime_error("SetHandleInformation failed");

    SIZE_T attributeBytes = 0;
    (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<std::byte> attributeStorage(attributeBytes);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes))
        throw std::runtime_error("InitializeProcThreadAttributeList failed");
    struct AttributeGuard final { PPROC_THREAD_ATTRIBUTE_LIST value; ~AttributeGuard() { DeleteProcThreadAttributeList(value); } } guard{attributes};
    HANDLE inherited[] = {write.Get()};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
        throw std::runtime_error("UpdateProcThreadAttribute failed");

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = write.Get();
    startup.StartupInfo.hStdError = write.Get();
    startup.StartupInfo.hStdInput = nullptr;
    startup.lpAttributeList = attributes;

    std::wstring commandLine = Quote(executable.wstring());
    for (const auto& argument : arguments) {
        commandLine.push_back(L' ');
        commandLine += Quote(Utf8ToWide(argument));
    }
    commandLine.push_back(L'\0');

    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, executable.parent_path().c_str(),
                        &startup.StartupInfo, &processInfo))
        throw std::runtime_error("Unable to start helper process");
    UniqueHandle process(processInfo.hProcess);
    UniqueHandle thread(processInfo.hThread);
    write.Reset();

    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (job.Get()) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        (void)SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        (void)AssignProcessToJobObject(job.Get(), process.Get());
    }
    if (ResumeThread(thread.Get()) == static_cast<DWORD>(-1)) {
        if (job.Get()) (void)TerminateJobObject(job.Get(), ERROR_CANCELLED);
        else (void)TerminateProcess(process.Get(), ERROR_CANCELLED);
        throw std::runtime_error("Unable to resume helper process");
    }

    ProcessResult result;
    std::mutex outputMutex;
    std::jthread reader([&] {
        char buffer[16U * 1024U];
        DWORD bytes = 0;
        while (ReadFile(read.Get(), buffer, sizeof(buffer), &bytes, nullptr) && bytes > 0) {
            std::scoped_lock lock(outputMutex);
            const auto available = maximumOutputBytes > result.output.size() ? maximumOutputBytes - result.output.size() : 0;
            const auto append = std::min<std::size_t>(static_cast<std::size_t>(bytes), available);
            result.output.append(buffer, append);
            if (append < bytes) result.outputTruncated = true;
        }
    });

    UniqueHandle cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    std::stop_callback cancellation(stopToken, [&] { if (cancelEvent.Get()) SetEvent(cancelEvent.Get()); });
    HANDLE waits[] = {process.Get(), cancelEvent.Get()};
    const auto waitMilliseconds = static_cast<DWORD>(std::clamp<std::int64_t>(timeout.count(), 1, MAXDWORD - 1LL));
    const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, waitMilliseconds);
    if (wait == WAIT_OBJECT_0 + 1) result.cancelled = true;
    else if (wait == WAIT_TIMEOUT) result.timedOut = true;
    else if (wait != WAIT_OBJECT_0) result.cancelled = true;
    if (result.cancelled || result.timedOut) {
        if (job.Get()) (void)TerminateJobObject(job.Get(), ERROR_CANCELLED);
        else (void)TerminateProcess(process.Get(), ERROR_CANCELLED);
        (void)WaitForSingleObject(process.Get(), 5'000);
    }
    DWORD exitCode = ERROR_GEN_FAILURE;
    if (GetExitCodeProcess(process.Get(), &exitCode)) result.exitCode = exitCode;
    job.Reset(); // Ends any descendant that could otherwise keep the output pipe open.
    if (reader.joinable()) reader.join();
    read.Reset();
    return result;
}

} // namespace wannaviewer
