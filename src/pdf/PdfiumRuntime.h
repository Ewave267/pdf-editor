// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <fpdf_formfill.h>
#include <fpdfview.h>
#include <functional>

namespace pdf::detail
{
struct Library
{
    Library()
    {
        FPDF_LIBRARY_CONFIG config{};
        config.version = 2;
        // The pinned shared-library distribution initializes V8 internally.
        FPDF_InitLibraryWithConfig(&config);
    }
    ~Library() { FPDF_DestroyLibrary(); }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
};

// The interface storage must outlive the form environment. No callback grants
// document scripts access to files, network, processes, or application state.
struct Host : FPDF_FORMFILLINFO
{
    IPDF_JSPLATFORM javascript{};
    FPDF_PAGE page = nullptr;
    int deniedRequests = 0;
    int pageIndex = 0;
    bool changed = false, topLeftCoordinates = false;
    std::function<FPDF_PAGE(int)> lookupPage;

    static Host& host(FPDF_FORMFILLINFO* info) { return *static_cast<Host*>(info); }

    Host() : FPDF_FORMFILLINFO{}
    {
        version = 2;
        xfa_disabled = false;
        javascript.version = 3;
        javascript.app_alert = [](IPDF_JSPLATFORM*, FPDF_WIDESTRING, FPDF_WIDESTRING, int, int)
        { return JSPLATFORM_ALERT_RETURN_CANCEL; };
        javascript.app_beep = [](IPDF_JSPLATFORM*, int) {};
        javascript.app_response = [](IPDF_JSPLATFORM*, FPDF_WIDESTRING, FPDF_WIDESTRING,
                                     FPDF_WIDESTRING, FPDF_WIDESTRING, FPDF_BOOL, void*, int)
        { return 0; };
        javascript.Doc_getFilePath = [](IPDF_JSPLATFORM*, void*, int) { return 0; };
        javascript.Doc_mail = [](IPDF_JSPLATFORM*, void*, int, FPDF_BOOL, FPDF_WIDESTRING,
                                 FPDF_WIDESTRING, FPDF_WIDESTRING, FPDF_WIDESTRING,
                                 FPDF_WIDESTRING) {};
        javascript.Doc_print = [](IPDF_JSPLATFORM*, FPDF_BOOL, int, int, FPDF_BOOL, FPDF_BOOL,
                                  FPDF_BOOL, FPDF_BOOL, FPDF_BOOL) {};
        javascript.Doc_submitForm = [](IPDF_JSPLATFORM*, void*, int, FPDF_WIDESTRING) {};
        javascript.Doc_gotoPage = [](IPDF_JSPLATFORM*, int) {};
        javascript.Field_browse = [](IPDF_JSPLATFORM*, void*, int) { return 0; };
        m_pJsPlatform = &javascript;

        FFI_OnChange = [](FPDF_FORMFILLINFO* info) { host(info).changed = true; };
        FFI_Invalidate = [](FPDF_FORMFILLINFO*, FPDF_PAGE, double, double, double, double) {};
        FFI_SetCursor = [](FPDF_FORMFILLINFO*, int) {};
        // Timer-driven forms are deliberately outside this synchronous probe.
        FFI_SetTimer = [](FPDF_FORMFILLINFO*, int, TimerCallback) { return 0; };
        FFI_KillTimer = [](FPDF_FORMFILLINFO*, int) {};
        FFI_GetLocalTime = [](FPDF_FORMFILLINFO*) { return FPDF_SYSTEMTIME{}; };
        FFI_GetPage = [](FPDF_FORMFILLINFO* info, FPDF_DOCUMENT, int index)
        {
            return host(info).lookupPage
                       ? host(info).lookupPage(index)
                       : (index == host(info).pageIndex ? host(info).page : nullptr);
        };
        FFI_GetCurrentPage = [](FPDF_FORMFILLINFO* info, FPDF_DOCUMENT) { return host(info).page; };
        FFI_GetRotation = [](FPDF_FORMFILLINFO*, FPDF_PAGE) { return 0; };
        FFI_ExecuteNamedAction = [](FPDF_FORMFILLINFO*, FPDF_BYTESTRING) {};
        FFI_DoURIAction = [](FPDF_FORMFILLINFO* info, FPDF_BYTESTRING)
        { ++host(info).deniedRequests; };
        FFI_DoGoToAction = [](FPDF_FORMFILLINFO*, int, int, float*, int) {};
        FFI_DisplayCaret = [](FPDF_FORMFILLINFO*, FPDF_PAGE, FPDF_BOOL, double, double, double,
                              double) {};
        FFI_GetCurrentPageIndex = [](FPDF_FORMFILLINFO* info, FPDF_DOCUMENT)
        { return host(info).pageIndex; };
        FFI_SetCurrentPage = [](FPDF_FORMFILLINFO*, FPDF_DOCUMENT, int) {};
        FFI_GotoURL = [](FPDF_FORMFILLINFO* info, FPDF_DOCUMENT, FPDF_WIDESTRING)
        { ++host(info).deniedRequests; };
        FFI_GetPageViewRect = [](FPDF_FORMFILLINFO* info, FPDF_PAGE page, double* left, double* top,
                                 double* right, double* bottom)
        {
            *left = 0;
            *top = host(info).topLeftCoordinates ? 0 : FPDF_GetPageHeight(page);
            *right = FPDF_GetPageWidth(page);
            *bottom = host(info).topLeftCoordinates ? FPDF_GetPageHeight(page) : 0;
        };
        FFI_PageEvent = [](FPDF_FORMFILLINFO*, int, FPDF_DWORD) {};
        FFI_PopupMenu = [](FPDF_FORMFILLINFO*, FPDF_PAGE, FPDF_WIDGET, int, float, float)
        { return 0; };
        FFI_OpenFile = [](FPDF_FORMFILLINFO* info, int, FPDF_WIDESTRING,
                          const char*) -> FPDF_FILEHANDLER*
        {
            ++host(info).deniedRequests;
            return nullptr;
        };
        FFI_EmailTo = [](FPDF_FORMFILLINFO*, FPDF_FILEHANDLER*, FPDF_WIDESTRING, FPDF_WIDESTRING,
                         FPDF_WIDESTRING, FPDF_WIDESTRING, FPDF_WIDESTRING) {};
        FFI_UploadTo = [](FPDF_FORMFILLINFO*, FPDF_FILEHANDLER*, int, FPDF_WIDESTRING) {};
        FFI_GetPlatform = [](FPDF_FORMFILLINFO*, void*, int) { return 0; };
        FFI_GetLanguage = [](FPDF_FORMFILLINFO*, void*, int) { return 0; };
        FFI_DownloadFromURL = [](FPDF_FORMFILLINFO* info, FPDF_WIDESTRING) -> FPDF_FILEHANDLER*
        {
            ++host(info).deniedRequests;
            return nullptr;
        };
        FFI_PostRequestURL = [](FPDF_FORMFILLINFO* info, FPDF_WIDESTRING, FPDF_WIDESTRING,
                                FPDF_WIDESTRING, FPDF_WIDESTRING, FPDF_WIDESTRING, FPDF_BSTR*)
        {
            ++host(info).deniedRequests;
            return 0;
        };
        FFI_PutRequestURL =
            [](FPDF_FORMFILLINFO* info, FPDF_WIDESTRING, FPDF_WIDESTRING, FPDF_WIDESTRING)
        {
            ++host(info).deniedRequests;
            return 0;
        };
    }
};

} // namespace pdf::detail
