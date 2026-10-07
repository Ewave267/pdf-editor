// SPDX-License-Identifier: GPL-3.0-only
// Exercises the same kernel policy used by the production renderer/save worker.
#include "pdf/WorkerPolicy.h"
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        check(argc == 2, "host sentinel path is required");
        pdf::detail::installWorkerPolicy();
        check(std::getenv("PDF_EDITOR_TEST_SECRET") == nullptr, "host environment leaked");
        check(open(argv[1], O_RDONLY) == -1 && errno == ENOENT, "host secret is accessible");
        const int input = open("/input.pdf", O_RDONLY);
        check(input >= 0, "sandbox input cannot be read");
        close(input);
        check(open("/input.pdf", O_WRONLY) == -1 && errno == EPERM, "input write not blocked");
        check(open("/tmp/forbidden", O_WRONLY | O_CREAT, 0600) == -1 && errno == EPERM,
              "file creation not blocked");
        check(socket(AF_INET, SOCK_STREAM, 0) == -1 && errno == EPERM, "network not blocked");
        check(socket(AF_UNIX, SOCK_STREAM, 0) == -1 && errno == EPERM, "local socket not blocked");
#ifdef __NR_fork
        check(syscall(__NR_fork) == -1 && errno == EPERM, "fork not blocked");
#endif
        check(syscall(__NR_clone, SIGCHLD, nullptr) == -1 && errno == EPERM,
              "process clone not blocked");
        char name[] = "/usr/bin/true";
        char* arguments[] = {name, nullptr};
        char* environment[] = {nullptr};
        check(execve(name, arguments, environment) == -1 && errno == EPERM,
              "command execution not blocked");
        check(syscall(__NR_execveat, AT_FDCWD, name, arguments, environment, 0) == -1 &&
                  errno == EPERM,
              "execveat not blocked");
        bool inherited = false;
        std::thread thread(
            [&] { inherited = execve(name, arguments, environment) == -1 && errno == EPERM; });
        thread.join();
        check(inherited, "thread did not inherit syscall restrictions");
        rlimit memory{};
        check(getrlimit(RLIMIT_DATA, &memory) == 0 && memory.rlim_max <= 768 * 1024 * 1024,
              "memory limit not installed");
        void* allocation = mmap(nullptr, 1024UL * 1024 * 1024, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        check(allocation == MAP_FAILED && errno == ENOMEM, "oversized allocation not blocked");
        void* reservation =
            mmap(nullptr, 1024UL * 1024 * 1024, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        check(reservation != MAP_FAILED, "V8-style address reservation failed");
        const int committed = mprotect(reservation, 1024UL * 1024 * 1024, PROT_READ | PROT_WRITE);
        const int commitError = errno;
        munmap(reservation, 1024UL * 1024 * 1024);
        check(committed == -1 && commitError == ENOMEM, "memory commitment bypassed data limit");
        std::cout << "PASS: files, commands, sockets, environment, threads and memory policy\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
