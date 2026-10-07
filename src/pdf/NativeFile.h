// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QFile>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pdf::detail
{
// Validate the opened object, rather than a filename that can change between
// checking and opening. QFile takes ownership of the validated descriptor.
inline bool openRegularInput(QFile& file, const QString& path)
{
#ifdef Q_OS_WIN
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(handle, &size) || size.QuadPart <= 0 || size.QuadPart > 64 * 1024 * 1024)
    {
        CloseHandle(handle);
        return false;
    }
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (fd < 0)
    {
        CloseHandle(handle);
        return false;
    }
    if (file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle))
        return true;
    _close(fd);
#else
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat info{};
    if (fd < 0)
        return false;
    if (::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
        info.st_size <= 64 * 1024 * 1024 &&
        file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle))
        return true;
    ::close(fd);
#endif
    return false;
}

inline bool sameFile(const QString& left, const QString& right)
{
#ifdef Q_OS_WIN
    auto identity = [](const QString& path, BY_HANDLE_FILE_INFORMATION& info)
    {
        const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), 0,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return false;
        const bool ok = GetFileInformationByHandle(handle, &info);
        CloseHandle(handle);
        return ok;
    };
    BY_HANDLE_FILE_INFORMATION a{}, b{};
    return identity(left, a) && identity(right, b) &&
           a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
           a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
#else
    struct stat a{}, b{};
    return ::stat(QFile::encodeName(left).constData(), &a) == 0 &&
           ::stat(QFile::encodeName(right).constData(), &b) == 0 && a.st_dev == b.st_dev &&
           a.st_ino == b.st_ino;
#endif
}
} // namespace pdf::detail
