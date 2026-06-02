#pragma once
#include "Framework.h"

struct CleanResult {
    ULONG64 deletedSize;
    ULONG64 deletedFiles;
    ULONG64 deletedFolders;
    ULONG64 errors;
    std::wstring lastError;
};

enum class CleanMode {
    DeleteFolder,
    CleanTemp,
    DeepClean
};

class Cleaner {
public:
    Cleaner();
    ~Cleaner();

    void StartClean(const std::wstring& targetPath, CleanMode mode,
                    HWND hNotifyWnd, UINT notifyMsg);
    void CancelClean();
    bool IsCleaning() const;
    CleanResult GetResult() const;

    static const UINT WM_CLEAN_PROGRESS;
    static const UINT WM_CLEAN_COMPLETE;

    static std::vector<std::wstring> GetTempPatterns(CleanMode mode);
    static bool IsTempFile(const std::wstring& path, CleanMode mode);

private:
    static DWORD WINAPI CleanThreadProc(LPVOID param);
    void CleanDirectory(const std::wstring& dirPath, CleanMode mode);
    void DeleteItem(const std::wstring& path, CleanMode mode);
    ULONG64 GetDirectorySize(const std::wstring& path);

    std::wstring m_targetPath;
    CleanMode m_mode;
    HWND m_hNotifyWnd;
    UINT m_notifyMsg;
    std::atomic<bool> m_cancelled;
    std::atomic<bool> m_cleaning;
    HANDLE m_hThread;
    CleanResult m_result;
    mutable std::mutex m_mutex;
};

class CleanProgress {
public:
    std::wstring currentItem;
    ULONG64 itemsProcessed;
    ULONG64 totalItems;
    ULONG64 bytesCleaned;
};
