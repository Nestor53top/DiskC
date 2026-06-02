#include "SystemProtector.h"

bool SystemProtector::s_initialized = false;
std::vector<std::wstring> SystemProtector::s_protectedDirs;
SYSTEM_INFO SystemProtector::SystemInfo;

void SystemProtector::Init() {
    if (s_initialized) return;
    GetNativeSystemInfo(&SystemInfo);
    s_protectedDirs = GetSystemRootDirs();
    s_initialized = true;
}

std::vector<std::wstring> SystemProtector::GetProtectedDirs() {
    Init();
    return s_protectedDirs;
}

bool SystemProtector::IsSystemDriveRoot(const std::wstring& path) {
    wchar_t root[4] = { path[0], L':', L'\\', 0 };
    return (_wcsicmp(path.c_str(), root) == 0) ||
           (_wcsicmp(path.c_str(), std::wstring(1, path[0]) + L":") == 0) ||
           (_wcsicmp(path.c_str(), std::wstring(1, path[0]) + L":/") == 0);
}

std::vector<std::wstring> SystemProtector::GetSystemRootDirs() {
    std::vector<std::wstring> dirs;
    wchar_t sysRoot[MAX_PATH];
    GetSystemDirectoryW(sysRoot, MAX_PATH);
    std::wstring sysRootStr(sysRoot);
    std::wstring drive = sysRootStr.substr(0, 2);

    dirs.push_back(drive + L"\\Windows");
    dirs.push_back(drive + L"\\WinNT");
    dirs.push_back(drive + L"\\WinXS");
    dirs.push_back(drive + L"\\Program Files");
    dirs.push_back(drive + L"\\Program Files (x86)");
    dirs.push_back(drive + L"\\ProgramData");
    dirs.push_back(drive + L"\\System Volume Information");
    dirs.push_back(drive + L"\\$Recycle.Bin");
    dirs.push_back(drive + L"\\$WinREAgent");
    dirs.push_back(drive + L"\\$SysReset");
    dirs.push_back(drive + L"\\Recovery");
    dirs.push_back(drive + L"\\Documents and Settings");
    dirs.push_back(drive + L"\\Config.Msi");
    dirs.push_back(drive + L"\\MSOCache");
    dirs.push_back(drive + L"\\PerfLogs");
    dirs.push_back(drive + L"\\Intel");
    dirs.push_back(drive + L"\\$GetCurrent");
    dirs.push_back(drive + L"\\$Windows.~BT");
    dirs.push_back(drive + L"\\$Windows.~WS");

    return dirs;
}

std::vector<std::wstring> SystemProtector::GetCriticalExtensions() {
    return {
        L".sys", L".dll", L".exe", L".drv", L".ocx",
        L".cpl", L".scr", L".mui", L".cat", L".man",
        L".mof", L".ppd", L".inf", L".pnf", L".ini",
        L".dat", L".bin", L".cfg", L".pol", L".admx",
        L".adml", L".ttf", L".fon", L".otf", L".nls",
        L".ime", L".kbdx", L".hlp", L".chm", L".msc",
        L".msi", L".msp", L".mst", L".pdb", L".dbg",
    };
}

bool SystemProtector::IsProtectedDirectory(const std::wstring& path) {
    Init();
    for (const auto& dir : s_protectedDirs) {
        if (_wcsicmp(path.c_str(), dir.c_str()) == 0) return true;
        if (_wcsnicmp(path.c_str(), dir.c_str(), dir.length()) == 0) {
            if (path.length() > dir.length() &&
                (path[dir.length()] == L'\\' || path[dir.length()] == L'/')) {
                return true;
            }
        }
    }

    return false;
}

bool SystemProtector::IsUnderProtectedDir(const std::wstring& path) {
    return IsProtectedDirectory(path);
}

bool SystemProtector::IsSystemAttribute(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN |
                     FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE |
                     FILE_ATTRIBUTE_INTEGRITY_STREAM)) != 0;
}

bool SystemProtector::IsCriticalFile(const std::wstring& path) {
    Init();
    std::wstring lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    if (lower.find(L"pagefile.sys") != std::wstring::npos) return true;
    if (lower.find(L"hiberfil.sys") != std::wstring::npos) return true;
    if (lower.find(L"swapfile.sys") != std::wstring::npos) return true;
    if (lower.find(L"bootmgr") != std::wstring::npos) return true;
    if (lower.find(L"bootnxt") != std::wstring::npos) return true;
    if (lower.find(L"bootsect.bak") != std::wstring::npos) return true;
    if (lower.find(L"ntldr") != std::wstring::npos) return true;
    if (lower.find(L"ntdetect.com") != std::wstring::npos) return true;

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return true;

    size_t dot = path.rfind(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        auto critExts = GetCriticalExtensions();
        for (const auto& ce : critExts) {
            if (ext == ce) {
                if (IsUnderProtectedDir(path)) return true;
            }
        }
    }

    return false;
}

bool SystemProtector::IsSystemPath(const std::wstring& path) {
    Init();
    if (IsProtectedDirectory(path)) return true;
    if (IsSystemAttribute(path)) return true;
    if (IsCriticalFile(path)) return true;
    return false;
}

bool SystemProtector::IsSafeToDelete(const std::wstring& path) {
    Init();
    if (IsSystemDriveRoot(path)) return false;
    if (IsProtectedDirectory(path)) return false;
    if (IsSystemAttribute(path)) return false;
    if (IsCriticalFile(path)) return false;
    return true;
}

bool SystemProtector::IsSafeToClean(const std::wstring& path) {
    Init();
    if (IsProtectedDirectory(path)) return false;
    if (IsCriticalFile(path)) return false;
    return true;
}
