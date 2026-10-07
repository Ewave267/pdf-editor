// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace pdf::detail
{
// Install before Qt/PDFium starts threads or reads document bytes. Failure is fatal.
// Only the development probe may write its private, sandbox-mounted artifacts.
void installWorkerPolicy(bool allowArtifactFiles = false);
} // namespace pdf::detail
