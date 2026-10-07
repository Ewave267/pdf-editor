// SPDX-License-Identifier: GPL-3.0-only
#include "WorkerPolicy.h"
#include <stdexcept>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <algorithm>
#include <dlfcn.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace pdf::detail
{
void installWorkerPolicy(bool allowArtifactFiles)
{
    if (allowArtifactFiles)
        throw std::runtime_error("Native workers do not permit artifact-file writes.");
#if defined(_WIN32)
    HANDLE token = nullptr;
    DWORD appContainer = 0, size = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw std::runtime_error("Cannot inspect worker isolation.");
    const bool isolated = GetTokenInformation(token, TokenIsAppContainer, &appContainer,
                                              sizeof(appContainer), &size) &&
                          appContainer;
    GetTokenInformation(token, TokenCapabilities, nullptr, 0, &size);
    std::vector<unsigned char> capabilities(size);
    const bool noCapabilities =
        size > 0 &&
        GetTokenInformation(token, TokenCapabilities, capabilities.data(), size, &size) &&
        reinterpret_cast<TOKEN_GROUPS*>(capabilities.data())->GroupCount == 0;
    CloseHandle(token);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (!isolated || !noCapabilities ||
        !QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits,
                                   sizeof(limits), nullptr) ||
        !(limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) ||
        !(limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) ||
        !(limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_ACTIVE_PROCESS) ||
        !(limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_TIME) ||
        limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart > 300LL * 10000000 ||
        limits.ProcessMemoryLimit > 768ULL * 1024 * 1024 ||
        limits.BasicLimitInformation.ActiveProcessLimit != 1)
        throw std::runtime_error("Worker requires its AppContainer and bounded job sandbox.");
#else
    // Direct worker invocation must not become an unsandboxed fallback.
    // sandbox_check is a Darwin SPI, absent from public SDK headers. Resolve
    // it explicitly and fail closed if the host no longer provides it.
    const auto library = dlopen("/usr/lib/libsandbox.dylib", RTLD_NOW | RTLD_LOCAL);
    using SandboxCheck = int (*)(pid_t, const char*, int, ...);
    const auto check =
        library ? reinterpret_cast<SandboxCheck>(dlsym(library, "sandbox_check")) : nullptr;
    constexpr int noFilter = 0, pathFilter = 1;
    if (!check || check(getpid(), "network-outbound", noFilter) <= 0 ||
        check(getpid(), "process-fork", noFilter) <= 0 ||
        check(getpid(), "file-write-data", pathFilter, "/private/tmp/pdf-editor-policy-probe") <= 0)
        throw std::runtime_error("Worker requires the macOS sandbox policy.");
    auto limit = [](int resource, rlim_t maximum)
    {
        rlimit current{};
        if (getrlimit(resource, &current) != 0)
            throw std::runtime_error("Cannot inspect worker resource limits.");
        const auto ceiling = std::min(current.rlim_max, maximum);
        const rlimit restricted{ceiling, ceiling};
        if (setrlimit(resource, &restricted) != 0)
            throw std::runtime_error("Cannot restrict worker resources.");
    };
    limit(RLIMIT_CPU, 300);
    limit(RLIMIT_CORE, 0);
    limit(RLIMIT_NOFILE, 128);
    limit(RLIMIT_FSIZE, 64 * 1024 * 1024);
    limit(RLIMIT_DATA, 768 * 1024 * 1024);
#endif
}
} // namespace pdf::detail
