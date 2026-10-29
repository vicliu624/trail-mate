#include "platform/linux/application_lifecycle.h"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace platform::linux_runtime;
namespace
{
struct Resource
{
    std::string marker;
    int descriptor;
    explicit Resource(std::string path) : marker(std::move(path)), descriptor(open(marker.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600)) { assert(descriptor >= 0); }
    ~Resource()
    {
        const auto pid = std::to_string(getpid());
        assert(write(descriptor, pid.data(), pid.size()) == static_cast<ssize_t>(pid.size()));
        assert(close(descriptor) == 0);
    }
};
void successful(int argc, char** argv, const char* marker)
{
    const auto child = fork();
    assert(child >= 0);
    if (!child)
    {
        char* arguments[] = {argv[0], const_cast<char*>("restarted"), const_cast<char*>(marker), const_cast<char*>("argument ; with $ spaces"), nullptr};
        const int residual = open("/dev/null", O_RDONLY);
        assert(residual >= 3);
        const auto residual_fd = std::to_string(residual);
        assert(setenv("TRAIL_MATE_RESTART_RESIDUAL_FD", residual_fd.c_str(), 1) == 0);
        const int result = run_application(arguments, [&]
                                           {
            Resource resource(marker);
            const auto fd = std::to_string(resource.descriptor);
            assert(setenv("TRAIL_MATE_RESTART_FD", fd.c_str(), 1) == 0);
            assert(setenv("TRAIL_MATE_RESTART_ENV", "preserved", 1) == 0);
            std::thread requester([] { assert(request_application_restart()); });
            requester.join();
            assert(application_restart_requested() && request_application_restart());
            return 0; });
        _exit(result ? result : 99); // A successful restart never returns here.
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    (void)argc;
}
} // namespace
int main(int argc, char** argv)
{
    if (argc == 4)
    {
        assert(std::string(argv[1]) == "restarted" && std::string(argv[3]) == "argument ; with $ spaces");
        assert(std::string(std::getenv("TRAIL_MATE_RESTART_ENV")) == "preserved");
        assert(!application_restart_requested());
        const int descriptor = std::atoi(std::getenv("TRAIL_MATE_RESTART_FD"));
        assert(fcntl(descriptor, F_GETFD) == -1 && errno == EBADF);
        const int residual = std::atoi(std::getenv("TRAIL_MATE_RESTART_RESIDUAL_FD"));
        assert(fcntl(residual, F_GETFD) == -1 && errno == EBADF);
        std::ifstream marker(argv[2]);
        std::string pid;
        marker >> pid;
        assert(pid == std::to_string(getpid()));
        return 0;
    }
    assert(!request_application_restart());
    char root[] = "/tmp/trailmate-lifecycle-XXXXXX";
    assert(mkdtemp(root));
    const auto marker = std::string(root) + "/cleanup marker ; $";
    successful(argc, argv, marker.c_str());
    assert(run_application(argv, []
                           { return 0; }) == 0);
    assert(run_application(argv, []
                           {
        assert(request_application_restart());
        assert(request_application_exit());
        assert(request_application_restart());
        assert(!application_restart_requested());
        return 0; }) == 0);
    assert(run_application(argv, []
                           { assert(request_application_restart()); return 7; }) == 7);
    assert(run_application(nullptr, []
                           { assert(request_application_restart()); return 0; }) == 1);
    assert(unlink(marker.c_str()) == 0 && rmdir(root) == 0);
}
