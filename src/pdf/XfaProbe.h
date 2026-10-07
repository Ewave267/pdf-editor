// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <string>

namespace pdf
{
struct ProbeOptions
{
    std::filesystem::path input;
    std::filesystem::path outputDirectory;
    std::string value;
};

// Calls PDFium on one thread. Must run inside the isolated probe worker.
void runXfaProbe(const ProbeOptions& options);
} // namespace pdf
