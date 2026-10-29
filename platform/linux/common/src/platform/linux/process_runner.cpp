#include "platform/linux/process_runner.h"
#include <algorithm>

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace platform::linux_runtime
{
ProcessResult run_process(const std::vector<std::string>& arguments,
                          std::chrono::milliseconds timeout, const std::atomic_bool* cancel)
{
    ProcessResult result;
#if defined(__linux__)
    if (arguments.empty() || arguments.front().empty()) return result;
    int descriptors[2];
    if (pipe2(descriptors, O_CLOEXEC) != 0) return result;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, descriptors[0]);
    posix_spawn_file_actions_addclose(&actions, descriptors[1]);
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    std::vector<std::string> environment;
    for (char** entry = environ; entry && *entry; ++entry)
    {
        if (std::string(*entry).rfind("LC_ALL=", 0) != 0) environment.emplace_back(*entry);
    }
    environment.emplace_back("LC_ALL=C");
    std::vector<char*> envp;
    for (auto& entry : environment) envp.push_back(entry.data());
    envp.push_back(nullptr);
    pid_t child = -1;
    const int spawned = posix_spawnp(&child, argv.front(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    close(descriptors[1]);
    if (spawned != 0)
    {
        close(descriptors[0]);
        return result;
    }
    fcntl(descriptors[0], F_SETFL, fcntl(descriptors[0], F_GETFL) | O_NONBLOCK);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    bool finished = false;
    while (!finished)
    {
        char buffer[4096];
        ssize_t count;
        while ((count = read(descriptors[0], buffer, sizeof(buffer))) > 0)
        {
            constexpr std::size_t limit = 256 * 1024;
            if (result.output.size() < limit)
                result.output.append(buffer, std::min<std::size_t>(count, limit - result.output.size()));
        }
        const pid_t waited = waitpid(child, &status, WNOHANG);
        finished = waited == child;
        if (waited < 0 && errno != EINTR) break;
        if (finished) break;
        if ((cancel && cancel->load()) || std::chrono::steady_clock::now() >= deadline)
        {
            result.timed_out = true;
            // Only this exact child is signalled; unrelated processes survive.
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR)
            {
            }
            finished = true;
            break;
        }
        pollfd descriptor{descriptors[0], POLLIN | POLLHUP, 0};
        poll(&descriptor, 1, 20);
    }
    // A child may exit between the previous read and waitpid; drain its tail.
    char tail[4096];
    ssize_t count;
    while ((count = read(descriptors[0], tail, sizeof(tail))) > 0)
    {
        if (result.output.size() < 256 * 1024)
            result.output.append(tail, std::min<std::size_t>(count, 256 * 1024 - result.output.size()));
    }
    close(descriptors[0]);
    if (finished && WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    else if (finished && WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
#else
    (void)arguments;
    (void)timeout;
    (void)cancel;
#endif
    return result;
}
} // namespace platform::linux_runtime
