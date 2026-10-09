// SPDX-License-Identifier: GPL-3.0-only
// Native Windows broker. Only the worker receives document bytes. It runs in
// an AppContainer without capabilities and a one-process, memory/CPU bounded
// job. Killing this broker closes the job and kills the worker as well.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <aclapi.h>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <objbase.h>
#include <sddl.h>
#include <stdexcept>
#include <string>
#include <userenv.h>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;
namespace
{
void trace(const char* message)
{
    static const ULONGLONG started = GetTickCount64();
    // Trusted broker only: the AppContainer worker never receives this path.
    wchar_t path[32768]{};
    const DWORD size = GetEnvironmentVariableW(L"PDF_EDITOR_SANDBOX_LOG", path, 32768);
    if (size == 0 || size >= 32768)
        return;
    std::ofstream log(fs::path(path), std::ios::app);
    log << GetCurrentProcessId() << ": elapsed_ms=" << GetTickCount64() - started << " " << message
        << '\n';
}
struct Handle
{
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle()
    {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(std::string(message) + " (Windows error " +
                                 std::to_string(GetLastError()) + ")");
}
std::wstring quote(const std::wstring& argument)
{
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t character : argument)
    {
        if (character == L'\\')
            ++slashes;
        else
        {
            result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            result += character;
            slashes = 0;
        }
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
void grantRead(const fs::path& path, PSID sid)
{
    PACL oldAcl = nullptr, newAcl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD query =
        GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                              nullptr, &oldAcl, nullptr, &descriptor);
    require(query == ERROR_SUCCESS, "Cannot inspect private runtime permissions");
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    access.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    const DWORD merge = SetEntriesInAclW(1, &access, oldAcl, &newAcl);
    DWORD apply = merge;
    if (merge == ERROR_SUCCESS)
        apply = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                      DACL_SECURITY_INFORMATION, nullptr, nullptr, newAcl, nullptr);
    if (newAcl)
        LocalFree(newAcl);
    LocalFree(descriptor);
    require(apply == ERROR_SUCCESS, "Cannot grant read-only sandbox runtime access");
}
struct Sid
{
    PSID value = nullptr;
    ~Sid()
    {
        if (value)
            FreeSid(value);
    }
};
struct PrivateRuntime
{
    fs::path path;
    ~PrivateRuntime()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
        trace(ignored ? "private runtime cleanup failed" : "private runtime cleanup completed");
    }
};
struct WindowStation
{
    HWINSTA value = nullptr;
    ~WindowStation()
    {
        if (value)
            CloseWindowStation(value);
    }
};
struct Desktop
{
    HDESK value = nullptr;
    ~Desktop()
    {
        if (value)
            CloseDesktop(value);
    }
};
struct LocalMemory
{
    HLOCAL value = nullptr;
    ~LocalMemory()
    {
        if (value)
            LocalFree(value);
    }
};
struct PrivateDesktop
{
    WindowStation station;
    Desktop desktop;
    std::wstring name;
    PrivateDesktop(PSID appSid, const wchar_t* identity)
    {
        Handle token;
        require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value),
                "Cannot inspect desktop owner");
        DWORD size = 0;
        GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
        std::vector<unsigned char> owner(size);
        require(GetTokenInformation(token.value, TokenUser, owner.data(), size, &size),
                "Cannot read desktop owner");
        LPWSTR ownerText = nullptr, appText = nullptr;
        require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(owner.data())->User.Sid,
                                       &ownerText),
                "Cannot format desktop owner");
        LocalMemory ownerString{ownerText};
        require(ConvertSidToStringSidW(appSid, &appText), "Cannot format desktop isolation SID");
        LocalMemory appString{appText};
        const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(ownerText) + L")(A;;GRGWGX;;;" +
                                  std::wstring(appText) + L")S:(ML;;NW;;;LW)";
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                                     &descriptor, nullptr),
                "Cannot create private desktop permissions");
        LocalMemory security{descriptor};
        SECURITY_ATTRIBUTES access{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        const std::wstring requested = std::wstring(L"PdfEditor-") + identity;
        wchar_t testLogonStation[2]{};
        const bool useLogonStation =
            GetEnvironmentVariableW(L"PDF_EDITOR_TEST_LOGON_STATION", testLogonStation, 2) > 0;
        if (!useLogonStation)
            station.value = CreateWindowStationW(requested.c_str(), CWF_CREATE_ONLY,
                                                 WINSTA_ALL_ACCESS, &access);
        // Unprivileged users may only request an automatically named station.
        if (useLogonStation || (!station.value && GetLastError() == ERROR_ACCESS_DENIED))
        {
            trace("using standard-user logon window station");
            station.value = CreateWindowStationW(
                nullptr, 0, WINSTA_ALL_ACCESS | READ_CONTROL | WRITE_DAC, &access);
            require(station.value != nullptr, "Cannot open logon window station");
            // The automatic name is shared by this logon session. Its supplied
            // security descriptor is ignored when it already exists. Preserve
            // its ACL and grant only station inspection, never desktop creation
            // or clipboard access. The worker gets a unique, low-integrity
            // desktop below with its own explicit security descriptor.
            PACL oldAcl = nullptr, newAcl = nullptr;
            PSECURITY_DESCRIPTOR existing = nullptr;
            DWORD result =
                GetSecurityInfo(station.value, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                nullptr, &oldAcl, nullptr, &existing);
            require(result == ERROR_SUCCESS, "Cannot inspect logon station permissions");
            LocalMemory existingSecurity{existing};
            EXPLICIT_ACCESSW grant{};
            grant.grfAccessPermissions = WINSTA_READATTRIBUTES | WINSTA_ENUMDESKTOPS;
            grant.grfAccessMode = GRANT_ACCESS;
            grant.grfInheritance = NO_INHERITANCE;
            grant.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            grant.Trustee.ptstrName = static_cast<LPWSTR>(appSid);
            result = SetEntriesInAclW(1, &grant, oldAcl, &newAcl);
            LocalMemory mergedSecurity{newAcl};
            require(result == ERROR_SUCCESS, "Cannot merge logon station permissions");
            result = SetSecurityInfo(station.value, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION,
                                     nullptr, nullptr, newAcl, nullptr);
            require(result == ERROR_SUCCESS, "Cannot grant logon station inspection");
        }
        require(station.value != nullptr, "Cannot create private window station");
        const auto original = GetProcessWindowStation();
        struct Restore
        {
            HWINSTA original;
            ~Restore() { SetProcessWindowStation(original); }
        } restore{original};
        require(SetProcessWindowStation(station.value), "Cannot select private window station");
        const std::wstring desktopName = std::wstring(L"Renderer-") + identity;
        desktop.value =
            CreateDesktopW(desktopName.c_str(), nullptr, nullptr, 0,
                           STANDARD_RIGHTS_REQUIRED | DESKTOP_READOBJECTS | DESKTOP_CREATEWINDOW |
                               DESKTOP_CREATEMENU | DESKTOP_HOOKCONTROL | DESKTOP_JOURNALRECORD |
                               DESKTOP_JOURNALPLAYBACK | DESKTOP_ENUMERATE | DESKTOP_WRITEOBJECTS |
                               DESKTOP_SWITCHDESKTOP,
                           &access);
        require(desktop.value != nullptr, "Cannot create private worker desktop");
        wchar_t stationName[512]{};
        require(GetUserObjectInformationW(station.value, UOI_NAME, stationName, sizeof(stationName),
                                          &size),
                "Cannot read private window station name");
        name = std::wstring(stationName) + L"\\" + desktopName;
    }
};
struct Attributes
{
    std::vector<unsigned char> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    Attributes()
    {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 3, 0, &size);
        storage.resize(size);
        list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        require(InitializeProcThreadAttributeList(list, 3, 0, &size),
                "Cannot initialize worker launch attributes");
    }
    ~Attributes()
    {
        if (list)
            DeleteProcThreadAttributeList(list);
    }
};
} // namespace

int wmain(int argc, wchar_t** argv)
{
    try
    {
        trace("broker entered");
        require((argc == 3 || argc == 4) && std::wstring(argv[1]) == L"--input" &&
                    (argc == 3 || std::wstring(argv[3]) == L"--save"),
                "Invalid sandbox launch arguments");
        const fs::path input = fs::absolute(argv[2]);
        require(fs::is_regular_file(input) && fs::file_size(input) > 0 &&
                    fs::file_size(input) <= 64 * 1024 * 1024,
                "Invalid worker input snapshot");
        std::vector<wchar_t> filename(32768);
        const DWORD length =
            GetModuleFileNameW(nullptr, filename.data(), static_cast<DWORD>(filename.size()));
        require(length > 0 && length < filename.size(), "Cannot locate native runtime");
        const fs::path installation = fs::path(filename.data()).parent_path();
        Sid sid;
        HRESULT result =
            CreateAppContainerProfile(L"PdfEditor.Renderer", L"PDF Editor Renderer",
                                      L"Isolated offline PDF renderer", nullptr, 0, &sid.value);
        if (result == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
            result = DeriveAppContainerSidFromAppContainerName(L"PdfEditor.Renderer", &sid.value);
        require(SUCCEEDED(result), "Cannot create worker AppContainer");
        trace("AppContainer profile ready");
        GUID guid{};
        require(SUCCEEDED(CoCreateGuid(&guid)), "Cannot create private runtime identity");
        wchar_t identity[40]{};
        require(StringFromGUID2(guid, identity, 40) > 0, "Cannot format runtime identity");
        PrivateDesktop desktop(sid.value, identity);
        trace("private window station and desktop ready");
        PrivateRuntime runtime{input.parent_path() / (std::wstring(L"runtime-") + identity)};
        require(fs::create_directory(runtime.path), "Cannot create private worker runtime");
        grantRead(runtime.path, sid.value);
        const fs::path worker = runtime.path / L"pdf-render-worker.exe";
        fs::copy_file(installation / L"pdf-render-worker.exe", worker);
        grantRead(worker, sid.value);
        uintmax_t copiedBytes = 0;
        unsigned copiedDlls = 0;
        for (const auto& entry : fs::directory_iterator(installation))
        {
            if (!entry.is_regular_file())
                continue;
            auto extension = entry.path().extension().wstring();
            for (auto& character : extension)
                character = std::towlower(character);
            if (extension == L".dll")
            {
                fs::copy_file(entry.path(), runtime.path / entry.path().filename());
                grantRead(runtime.path / entry.path().filename(), sid.value);
                copiedBytes += entry.file_size();
                ++copiedDlls;
            }
        }
        fs::copy_file(input, runtime.path / L"input.pdf");
        grantRead(runtime.path / L"input.pdf", sid.value);
        trace("private runtime and read permissions ready");
        trace(
            ("runtime DLLs=" + std::to_string(copiedDlls) + " bytes=" + std::to_string(copiedBytes))
                .c_str());
        Handle job(CreateJobObjectW(nullptr, nullptr));
        require(job.value != nullptr, "Cannot create worker job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
            JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY |
            JOB_OBJECT_LIMIT_PROCESS_TIME;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = 300LL * 10000000;
        limits.ProcessMemoryLimit = 768ULL * 1024 * 1024;
        require(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits,
                                        sizeof(limits)),
                "Cannot restrict worker job");
        HANDLE inherited[3]{};
        Handle standard[3];
        const DWORD identifiers[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        for (int index = 0; index < 3; ++index)
        {
            require(DuplicateHandle(GetCurrentProcess(), GetStdHandle(identifiers[index]),
                                    GetCurrentProcess(), &standard[index].value, 0, TRUE,
                                    DUPLICATE_SAME_ACCESS),
                    "Cannot prepare worker protocol pipes");
            inherited[index] = standard[index].value;
        }
        Attributes attributes;
        SECURITY_CAPABILITIES capabilities{};
        capabilities.AppContainerSid = sid.value;
        DWORD childPolicy = PROCESS_CREATION_CHILD_PROCESS_RESTRICTED;
        require(UpdateProcThreadAttribute(attributes.list, 0,
                                          PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                          &capabilities, sizeof(capabilities), nullptr, nullptr),
                "Cannot apply worker AppContainer");
        require(UpdateProcThreadAttribute(attributes.list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                          inherited, sizeof(inherited), nullptr, nullptr),
                "Cannot restrict inherited handles");
        require(UpdateProcThreadAttribute(attributes.list, 0,
                                          PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, &childPolicy,
                                          sizeof(childPolicy), nullptr, nullptr),
                "Cannot prohibit worker child processes");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.lpDesktop = desktop.name.data();
        startup.StartupInfo.hStdInput = inherited[0];
        startup.StartupInfo.hStdOutput = inherited[1];
        startup.StartupInfo.hStdError = inherited[2];
        startup.lpAttributeList = attributes.list;
        std::wstring command = quote(worker.wstring()) + L" --input " +
                               quote((runtime.path / L"input.pdf").wstring()) +
                               (argc == 4 ? L" --save" : L"");
        // Do not inherit Qt/plugin configuration or search paths from the GUI.
        wchar_t systemRoot[32768]{};
        require(GetEnvironmentVariableW(L"SystemRoot", systemRoot, 32768) > 0,
                "Cannot locate Windows system libraries");
        // AppContainer creation needs the user profile environment so Windows
        // can redirect LOCALAPPDATA/TEMP into the container's own profile.
        // These path values confer no access: the zero-capability token and
        // file ACLs still govern every read/write. Never inherit the full env.
        std::map<std::wstring, std::wstring> variables{
            {L"PATH", std::wstring(systemRoot) + L"\\System32"},
            {L"SystemRoot", systemRoot},
            {L"windir", systemRoot}};
        for (const auto* name : {L"LOCALAPPDATA", L"USERPROFILE", L"SystemDrive"})
        {
            wchar_t value[32768]{};
            const DWORD size = GetEnvironmentVariableW(name, value, 32768);
            require(size > 0 && size < 32768, "Missing Windows profile environment");
            variables.emplace(name, value);
        }
        wchar_t diagnostics[2]{};
        if (GetEnvironmentVariableW(L"PDF_EDITOR_WORKER_DIAGNOSTICS", diagnostics, 2) > 0)
            variables.emplace(L"PDF_EDITOR_WORKER_DIAGNOSTICS", L"1");
        std::wstring environment;
        for (const auto& [name, value] : variables)
        {
            environment += name + L"=" + value;
            environment.push_back(L'\0');
        }
        environment.push_back(L'\0');
        PROCESS_INFORMATION process{};
        require(CreateProcessW(worker.c_str(), command.data(), nullptr, nullptr, TRUE,
                               EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT |
                                   CREATE_SUSPENDED | CREATE_NO_WINDOW,
                               environment.data(), runtime.path.c_str(), &startup.StartupInfo,
                               &process),
                "Cannot launch AppContainer worker");
        trace("suspended worker created");
        Handle child(process.hProcess), thread(process.hThread);
        if (!AssignProcessToJobObject(job.value, child.value))
        {
            TerminateProcess(child.value, 1);
            WaitForSingleObject(child.value, INFINITE);
            throw std::runtime_error("Cannot assign worker to its bounded job.");
        }
        trace("worker assigned to bounded job");
        if (ResumeThread(thread.value) == static_cast<DWORD>(-1))
            throw std::runtime_error("Cannot resume isolated worker.");
        require(WaitForSingleObject(child.value, INFINITE) == WAIT_OBJECT_0,
                "Cannot wait for worker completion");
        DWORD exitCode = 1;
        require(GetExitCodeProcess(child.value, &exitCode), "Cannot read worker exit status");
        trace(("worker exit " + std::to_string(exitCode)).c_str());
        if (exitCode != 0)
            std::cerr << "Native PDF sandbox: worker exit " << exitCode << " (0x" << std::hex
                      << exitCode << ")\n";
        return static_cast<int>(exitCode);
    }
    catch (const std::exception& error)
    {
        trace(error.what());
        std::cerr << "Native PDF sandbox: " << error.what() << '\n';
        return 1;
    }
}
