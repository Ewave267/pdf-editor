// SPDX-License-Identifier: GPL-3.0-only
#include "pdf/WorkerPolicy.h"
#include "pdf/XfaProbe.h"

#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cerr << "Internal worker: run tools/run_xfa_probe.py instead.\n";
        return 2;
    }
    try
    {
        pdf::detail::installWorkerPolicy(true);
        pdf::runXfaProbe({argv[1], argv[2], argv[3]});
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "XFA probe failed: " << error.what() << '\n';
        return 1;
    }
}
