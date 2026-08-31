#include "wannaviewer/network/Process.hpp"
#include "wannaviewer/core/Path.hpp"

#include <algorithm>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace wannaviewer {

ProcessResult RunProcess(const std::filesystem::path& executable,
                         const std::vector<std::string>& arguments,
                         std::chrono::milliseconds timeout,
                         std::size_t maximumOutputBytes,
                         std::stop_token stopToken) {
    if (!std::filesystem::is_regular_file(executable)) throw std::runtime_error("Helper executable is missing");
    int outputPipe[2]{};
    if (pipe(outputPipe) != 0) throw std::runtime_error("pipe failed");
    posix_spawn_file_actions_t actions{};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, outputPipe[0]);
    posix_spawnattr_t attributes{};
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    std::vector<std::string> storage{PathToUtf8(executable)};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& argument : storage) argv.push_back(argument.data());
    argv.push_back(nullptr);
    pid_t process = 0;
    const int spawnResult = posix_spawn(&process, executable.c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(outputPipe[1]);
    if (spawnResult != 0) { close(outputPipe[0]); throw std::runtime_error("Unable to start helper process"); }

    ProcessResult result;
    std::jthread reader([&] {
        char buffer[16U * 1024U];
        ssize_t count = 0;
        while ((count = read(outputPipe[0], buffer, sizeof(buffer))) > 0) {
            const auto available = maximumOutputBytes > result.output.size() ? maximumOutputBytes - result.output.size() : 0;
            const auto append = std::min<std::size_t>(static_cast<std::size_t>(count), available);
            result.output.append(buffer, append);
            if (append < static_cast<std::size_t>(count)) result.outputTruncated = true;
        }
    });
    std::mutex mutex;
    std::condition_variable condition;
    bool exited = false;
    int status = 0;
    std::jthread waiter([&] {
        (void)waitpid(process, &status, 0);
        { std::scoped_lock lock(mutex); exited = true; }
        condition.notify_one();
    });
    std::stop_callback cancellation(stopToken, [&] { condition.notify_one(); });
    {
        std::unique_lock lock(mutex);
        if (!condition.wait_for(lock, timeout, [&] { return exited || stopToken.stop_requested(); })) result.timedOut = true;
        else if (stopToken.stop_requested() && !exited) result.cancelled = true;
    }
    if (result.timedOut || result.cancelled) kill(-process, SIGKILL);
    if (waiter.joinable()) waiter.join();
    kill(-process, SIGTERM);
    if (reader.joinable()) reader.join();
    close(outputPipe[0]);
    result.exitCode = WIFEXITED(status) ? static_cast<unsigned>(WEXITSTATUS(status)) : 128U;
    return result;
}

} // namespace wannaviewer
