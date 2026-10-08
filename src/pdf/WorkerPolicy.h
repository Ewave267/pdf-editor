// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace pdf::detail
{
// Install before any document reads. Linux installs before Qt/PDFium startup.
// Only the development probe may write its private, sandbox-mounted artifacts.
void installWorkerPolicy(bool allowArtifactFiles = false);
#if defined(__APPLE__)
// Apply the GUI-provided Seatbelt profile before Qt startup or any document input.
void applyMacWorkerSandbox(const char* profile);
#endif
} // namespace pdf::detail
