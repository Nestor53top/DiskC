#pragma once
#include "Framework.h"

class SystemProtector {
public:
    static bool IsSystemPath(const std::wstring& path);
    static bool IsProtectedDirectory(const std::wstring& path);
    static bool IsCriticalFile(const std::wstring& path);
    static bool IsSystemAttribute(const std::wstring& path);
    static bool IsSafeToDelete(const std::wstring& path);
    static bool IsSafeToClean(const std::wstring& path);
    static bool IsSystemDriveRoot(const std::wstring& path);
    static SYSTEM_INFO SystemInfo;
    static std::vector<std::wstring> GetProtectedDirs();

private:
    static bool IsUnderProtectedDir(const std::wstring& path);
    static std::vector<std::wstring> GetSystemRootDirs();
    static std::vector<std::wstring> GetCriticalExtensions();
    static bool s_initialized;
    static std::vector<std::wstring> s_protectedDirs;
    static void Init();
};
