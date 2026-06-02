#pragma once
#include "Framework.h"

struct ScanEntry {
    std::wstring path;
    std::wstring name;
    ULONG64 totalSize;
    ULONG64 fileCount;
    ULONG64 dirCount;
    bool isSystem;
    bool isProtected;
};

class Scanner {
public:
    Scanner();
    ~Scanner();

    void StartScan(const std::wstring& rootPath, HWND hNotifyWnd, UINT notifyMsg);
    void CancelScan();
    void PauseScan();
    void ResumeScan();
    bool IsScanning() const;
    bool IsPaused() const;

    const std::vector<ScanEntry>& GetResults() const;
    std::vector<ScanEntry> GetTopFolders(size_t count = 100, bool sortBySize = true) const;
    ULONG64 GetTotalScannedSize() const;
    size_t GetTotalScannedFolders() const;
    std::wstring GetCurrentPath() const;

    enum { WM_SCAN_PROGRESS = WM_APP + 1, WM_SCAN_COMPLETE = WM_APP + 2, WM_SCAN_ENTRY = WM_APP + 3 };

private:
    static DWORD WINAPI ScanThreadProc(LPVOID param);
    void ScanDirectory(const std::wstring& dirPath);
    void NotifyProgress();
    void NotifyEntry(const ScanEntry& entry);

    std::wstring m_rootPath;
    HWND m_hNotifyWnd;
    UINT m_notifyMsg;
    std::atomic<bool> m_cancelled;
    std::atomic<bool> m_paused;
    std::atomic<bool> m_scanning;
    HANDLE m_hThread;
    HANDLE m_hPauseEvent;

    std::vector<ScanEntry> m_results;
    mutable std::mutex m_mutex;
    ULONG64 m_totalSize;
    ULONG64 m_totalFolders;
    ULONG64 m_scannedFolders;
    std::wstring m_currentPath;
};
