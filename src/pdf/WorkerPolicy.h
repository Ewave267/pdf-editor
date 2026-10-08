// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace pdf::detail
{
// Install before Qt/PDFium starts threads or reads document bytes. Failure is fatal.
// Only the development probe may write its private, sandbox-mounted artifacts.
void installWorkerPolicy(bool allowArtifactFiles = false);
#if defined(__APPLE__)
// Apply the GUI-provided Seatbelt profile before any input or PDFium initialization.
void applyMacWorkerSandbox(const char* profile);
#endif
} // namespace pdf::detail
