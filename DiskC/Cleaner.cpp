#include "Cleaner.h"
#include "SystemProtector.h"

const UINT Cleaner::WM_CLEAN_PROGRESS = WM_APP + 10;
const UINT Cleaner::WM_CLEAN_COMPLETE = WM_APP + 11;

Cleaner::Cleaner() : m_hThread(NULL), m_cancelled(false), m_cleaning(false),
                     m_hNotifyWnd(NULL), m_notifyMsg(0) {
    ZeroMemory(&m_result, sizeof(m_result));
}

Cleaner::~Cleaner() {
    CancelClean();
}

void Cleaner::StartClean(const std::wstring& targetPath, CleanMode mode,
                         HWND hNotifyWnd, UINT notifyMsg) {
    CancelClean();
    m_targetPath = targetPath;
    m_mode = mode;
    m_hNotifyWnd = hNotifyWnd;
    m_notifyMsg = notifyMsg;
    m_cancelled = false;
    m_cleaning = true;
    ZeroMemory(&m_result, sizeof(m_result));

    m_hThread = CreateThread(NULL, 0, CleanThreadProc, this, 0, NULL);
}

void Cleaner::CancelClean() {
    m_cancelled = true;
    if (m_hThread) {
        WaitForSingleObject(m_hThread, 10000);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
    m_cleaning = false;
}

bool Cleaner::IsCleaning() const { return m_cleaning; }
CleanResult Cleaner::GetResult() const { return m_result; }

std::vector<std::wstring> Cleaner::GetTempPatterns(CleanMode mode) {
    std::vector<std::wstring> patterns;

    if (mode == CleanMode::CleanTemp || mode == CleanMode::DeepClean) {
        patterns.push_back(L"*.tmp");
        patterns.push_back(L"*.temp");
        patterns.push_back(L"*.log");
        patterns.push_back(L"*.bak");
        patterns.push_back(L"*.old");
        patterns.push_back(L"*.etl");
        patterns.push_back(L"*.dmp");
        patterns.push_back(L"*.chk");
        patterns.push_back(L"*.wbcat");
        patterns.push_back(L"*.gid");
        patterns.push_back(L"*.fts");
        patterns.push_back(L"*.~*");
    }

    if (mode == CleanMode::DeepClean) {
        patterns.push_back(L"*.cache");
        patterns.push_back(L"*.blf");
        patterns.push_back(L"*.regtrans-ms");
        patterns.push_back(L"*.log1");
        patterns.push_back(L"*.log2");
        patterns.push_back(L"*.jrs");
        patterns.push_back(L"*.pf");
        patterns.push_back(L"*.db");
        patterns.push_back(L"*.thumb");
        patterns.push_back(L"thumbcache_*.db");
        patterns.push_back(L"$LogFile");
        patterns.push_back(L"$TxfLog");
    }

    return patterns;
}

bool Cleaner::IsTempFile(const std::wstring& path, CleanMode mode) {
    if (SystemProtector::IsSystemPath(path)) return false;

    std::wstring lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    auto patterns = GetTempPatterns(mode);
    for (const auto& pattern : patterns) {
        std::wstring patLower = pattern;
        std::transform(patLower.begin(), patLower.end(), patLower.begin(), ::towlower);

        if (patLower.find(L'*') != std::wstring::npos) {
            std::wstring ext = patLower.substr(1);
            if (lower.length() >= ext.length() &&
                lower.substr(lower.length() - ext.length()) == ext) {
                return true;
            }
        } else {
            size_t pos = lower.find(patLower);
            if (pos != std::wstring::npos) return true;
        }
    }

    return false;
}

ULONG64 Cleaner::GetDirectorySize(const std::wstring& path) {
    ULONG64 size = 0;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((path + L"\\*").c_str(), &fd);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring fullPath = path + L"\\" + fd.cFileName;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                size += GetDirectorySize(fullPath);
            } else {
                ULARGE_INTEGER fs;
                fs.LowPart = fd.nFileSizeLow;
                fs.HighPart = fd.nFileSizeHigh;
                size += fs.QuadPart;
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    return size;
}

DWORD WINAPI Cleaner::CleanThreadProc(LPVOID param) {
    Cleaner* self = (Cleaner*)param;
    self->CleanDirectory(self->m_targetPath, self->m_mode);
    self->m_cleaning = false;

    if (!self->m_cancelled) {
        PostMessageW(self->m_hNotifyWnd, self->WM_CLEAN_COMPLETE,
                     (WPARAM)&self->m_result, 0);
    }
    return 0;
}

void Cleaner::CleanDirectory(const std::wstring& dirPath, CleanMode mode) {
    if (m_cancelled) return;
    if (!SystemProtector::IsSafeToClean(dirPath)) return;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dirPath + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (m_cancelled) break;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring fullPath = dirPath + L"\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

            if (mode == CleanMode::DeleteFolder) {
                CleanDirectory(fullPath, mode);
                if (SystemProtector::IsSafeToDelete(fullPath)) {
                    if (RemoveDirectoryW(fullPath.c_str())) {
                        m_result.deletedFolders++;
                    } else {
                        m_result.errors++;
                    }
                }
            } else {
                CleanDirectory(fullPath, mode);
                if (RemoveDirectoryW(fullPath.c_str())) {
                    m_result.deletedFolders++;
                }
            }
        } else {
            DeleteItem(fullPath, mode);
        }

        if (m_result.deletedFiles % 100 == 0) {
            PostMessageW(m_hNotifyWnd, WM_CLEAN_PROGRESS, 0, 0);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

void Cleaner::DeleteItem(const std::wstring& path, CleanMode mode) {
    if (SystemProtector::IsSystemPath(path)) return;

    if (mode == CleanMode::DeleteFolder) {
        if (DeleteFileW(path.c_str())) {
            ULARGE_INTEGER fs;
            WIN32_FIND_DATAW fd;
            HANDLE hF = FindFirstFileW(path.c_str(), &fd);
            if (hF != INVALID_HANDLE_VALUE) {
                fs.LowPart = fd.nFileSizeLow;
                fs.HighPart = fd.nFileSizeHigh;
                m_result.deletedSize += fs.QuadPart;
                FindClose(hF);
            }
            m_result.deletedFiles++;
        } else {
            DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND) m_result.errors++;
        }
    } else {
        if (IsTempFile(path, mode)) {
            if (DeleteFileW(path.c_str())) {
                ULARGE_INTEGER fs;
                WIN32_FIND_DATAW fd;
                HANDLE hF = FindFirstFileW(path.c_str(), &fd);
                if (hF != INVALID_HANDLE_VALUE) {
                    fs.LowPart = fd.nFileSizeLow;
                    fs.HighPart = fd.nFileSizeHigh;
                    m_result.deletedSize += fs.QuadPart;
                    FindClose(hF);
                }
                m_result.deletedFiles++;
            } else {
                DWORD err = GetLastError();
                if (err != ERROR_FILE_NOT_FOUND) m_result.errors++;
            }
        }
    }
}
