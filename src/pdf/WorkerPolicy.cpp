// SPDX-License-Identifier: GPL-3.0-only
#include "WorkerPolicy.h"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/sched.h>
#include <linux/seccomp.h>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace pdf::detail
{
namespace
{
void limit(int resource, rlim_t maximum)
{
    rlimit current{};
    if (getrlimit(resource, &current) != 0)
        throw std::runtime_error("Cannot inspect worker resource limits.");
    const auto ceiling = std::min(current.rlim_max, maximum);
    rlimit restricted{ceiling, ceiling};
    if (setrlimit(resource, &restricted) != 0)
        throw std::runtime_error("Cannot restrict worker resources.");
}
} // namespace

void installWorkerPolicy(bool allowArtifactFiles)
{
    limit(RLIMIT_CPU, 300);
    limit(RLIMIT_CORE, 0);
    limit(RLIMIT_NOFILE, 128);
    limit(RLIMIT_FSIZE, 64 * 1024 * 1024);
    // V8 reserves a huge PROT_NONE address range. Limit writable anonymous data,
    // rather than address space, so its reservation remains possible on Linux.
    limit(RLIMIT_DATA, 768 * 1024 * 1024);

#if defined(__x86_64__)
    constexpr auto architecture = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
    constexpr auto architecture = AUDIT_ARCH_AARCH64;
#else
#error "The PDF worker syscall policy needs an audited architecture definition."
#endif
    constexpr unsigned denied = SECCOMP_RET_ERRNO | EPERM;
    std::vector<sock_filter> filter{
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, architecture, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
    };
#if defined(__x86_64__)
    // Reject the x32 ABI, whose syscall numbers bypass an ordinary x86-64 list.
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1));
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS));
#endif
    auto deny = [&](unsigned number, unsigned result = SECCOMP_RET_ERRNO | EPERM)
    {
        filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 1));
        filter.push_back(BPF_STMT(BPF_RET | BPF_K, result));
    };
    for (unsigned number :
         {__NR_execve, __NR_execveat, __NR_socket, __NR_socketpair, __NR_connect, __NR_bind,
          __NR_listen, __NR_ptrace, __NR_setns, __NR_unshare, __NR_mount, __NR_umount2,
          __NR_pivot_root, __NR_process_vm_readv, __NR_process_vm_writev, __NR_bpf,
          __NR_perf_event_open, __NR_io_uring_setup})
        deny(number);
#ifdef __NR_fork
    deny(__NR_fork);
#endif
#ifdef __NR_vfork
    deny(__NR_vfork);
#endif
    // glibc falls back to clone when clone3 is unavailable. V8 threads need clone;
    // process creation does not. Every permitted child inherits this policy.
    deny(__NR_clone3, SECCOMP_RET_ERRNO | ENOSYS);
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 4));
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[0])));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, CLONE_THREAD, 1, 0));
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, denied));
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)));

    if (!allowArtifactFiles)
    {
        auto readOnlyOpen = [&](unsigned number, unsigned argument)
        {
            filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 4));
            filter.push_back(BPF_STMT(
                BPF_LD | BPF_W | BPF_ABS,
                static_cast<unsigned>(offsetof(seccomp_data, args) + argument * sizeof(__u64))));
            filter.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K,
                                      O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND, 0, 1));
            filter.push_back(BPF_STMT(BPF_RET | BPF_K, denied));
            filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)));
        };
#ifdef __NR_open
        readOnlyOpen(__NR_open, 1);
#endif
        readOnlyOpen(__NR_openat, 2);
        deny(__NR_openat2, SECCOMP_RET_ERRNO | ENOSYS);
#ifdef __NR_creat
        deny(__NR_creat);
#endif
        // No writable file descriptors are inherited except the protocol pipes.
        // Prevent filesystem mutations which do not require a writable open.
        for (unsigned number :
             {__NR_unlinkat, __NR_renameat, __NR_renameat2, __NR_mkdirat, __NR_linkat,
              __NR_symlinkat, __NR_mknodat, __NR_fchmodat, __NR_fchownat, __NR_truncate})
            deny(number);
#ifdef __NR_unlink
        for (unsigned number : {__NR_unlink, __NR_rename, __NR_mkdir, __NR_rmdir, __NR_link,
                                __NR_symlink, __NR_mknod, __NR_chmod, __NR_chown})
            deny(number);
#endif
    }
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
    const sock_fprog program{static_cast<unsigned short>(filter.size()), filter.data()};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &program) != 0)
        throw std::runtime_error("Cannot install the PDF worker syscall policy.");
}
} // namespace pdf::detail
