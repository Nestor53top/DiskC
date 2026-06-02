#include "Scanner.h"
#include "SystemProtector.h"

Scanner::Scanner()
    : m_hThread(NULL), m_hPauseEvent(NULL), m_cancelled(false),
      m_paused(false), m_scanning(false), m_hNotifyWnd(NULL),
      m_notifyMsg(0), m_totalSize(0), m_totalFolders(0), m_scannedFolders(0) {
    m_hPauseEvent = CreateEventW(NULL, TRUE, TRUE, NULL);
}

Scanner::~Scanner() {
    CancelScan();
    if (m_hPauseEvent) CloseHandle(m_hPauseEvent);
}

void Scanner::StartScan(const std::wstring& rootPath, HWND hNotifyWnd, UINT notifyMsg) {
    CancelScan();
    m_rootPath = rootPath;
    m_hNotifyWnd = hNotifyWnd;
    m_notifyMsg = notifyMsg;
    m_cancelled = false;
    m_paused = false;
    m_scanning = true;
    m_results.clear();
    m_totalSize = 0;
    m_totalFolders = 0;
    m_scannedFolders = 0;
    m_currentPath.clear();

    ResetEvent(m_hPauseEvent);
    m_hThread = CreateThread(NULL, 0, ScanThreadProc, this, 0, NULL);
}

void Scanner::CancelScan() {
    m_cancelled = true;
    if (m_hThread) {
        WaitForSingleObject(m_hThread, 5000);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
    m_scanning = false;
}

void Scanner::PauseScan() {
    m_paused = true;
    ResetEvent(m_hPauseEvent);
}

void Scanner::ResumeScan() {
    m_paused = false;
    SetEvent(m_hPauseEvent);
}

bool Scanner::IsScanning() const { return m_scanning; }
bool Scanner::IsPaused() const { return m_paused; }

const std::vector<ScanEntry>& Scanner::GetResults() const { return m_results; }

std::vector<ScanEntry> Scanner::GetTopFolders(size_t count, bool sortBySize) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ScanEntry> sorted = m_results;
    if (sortBySize) {
        std::sort(sorted.begin(), sorted.end(),
            [](const ScanEntry& a, const ScanEntry& b) {
                return a.totalSize > b.totalSize;
            });
    }
    if (sorted.size() > count) sorted.resize(count);
    return sorted;
}

ULONG64 Scanner::GetTotalScannedSize() const { return m_totalSize; }
size_t Scanner::GetTotalScannedFolders() const { return m_totalFolders; }
std::wstring Scanner::GetCurrentPath() const { return m_currentPath; }

DWORD WINAPI Scanner::ScanThreadProc(LPVOID param) {
    Scanner* self = (Scanner*)param;
    self->ScanDirectory(self->m_rootPath);
    self->m_scanning = false;

    if (!self->m_cancelled) {
        PostMessageW(self->m_hNotifyWnd, self->WM_SCAN_COMPLETE, 0, 0);
    }
    return 0;
}

void Scanner::ScanDirectory(const std::wstring& dirPath) {
    if (m_cancelled) return;

    WaitForSingleObject(m_hPauseEvent, INFINITE);

    m_currentPath = dirPath;

    ULONG64 dirSize = 0;
    ULONG64 dirFiles = 0;
    ULONG64 dirSubdirs = 0;
    bool isProtected = SystemProtector::IsProtectedDirectory(dirPath);
    bool isSystem = SystemProtector::IsSystemPath(dirPath);

    std::wstring searchPath = dirPath + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW(searchPath.c_str(), FindExInfoBasic, &fd,
                                     FindExSearchNameMatch, NULL, 0);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring fullPath = dirPath + L"\\" + fd.cFileName;
        m_totalFolders++;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

            DWORD attrs = GetFileAttributesW(fullPath.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                ScanDirectory(fullPath);
                m_scannedFolders++;

                std::lock_guard<std::mutex> lock(m_mutex);
                for (const auto& entry : m_results) {
                    if (_wcsicmp(entry.path.c_str(), fullPath.c_str()) == 0) {
                        dirSize += entry.totalSize;
                        dirFiles += entry.fileCount;
                        dirSubdirs += entry.dirCount + 1;
                        break;
                    }
                }
            }
        } else {
            ULARGE_INTEGER fileSize;
            fileSize.LowPart = fd.nFileSizeLow;
            fileSize.HighPart = fd.nFileSizeHigh;
            dirSize += fileSize.QuadPart;
            dirFiles++;
        }

        if (m_scannedFolders % 50 == 0) {
            NotifyProgress();
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    ScanEntry entry;
    entry.path = dirPath;
    entry.name = dirPath.substr(dirPath.rfind(L'\\') + 1);
    entry.totalSize = dirSize;
    entry.fileCount = dirFiles;
    entry.dirCount = dirSubdirs;
    entry.isSystem = isSystem;
    entry.isProtected = isProtected;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_results.push_back(entry);
        m_totalSize += dirSize;
    }

    NotifyEntry(entry);
}

void Scanner::NotifyProgress() {
    if (m_hNotifyWnd) {
        PostMessageW(m_hNotifyWnd, WM_SCAN_PROGRESS,
                     (WPARAM)m_scannedFolders, (LPARAM)m_totalFolders);
    }
}

void Scanner::NotifyEntry(const ScanEntry& entry) {
    if (m_hNotifyWnd) {
        ScanEntry* pEntry = new ScanEntry(entry);
        PostMessageW(m_hNotifyWnd, WM_SCAN_ENTRY, (WPARAM)pEntry, 0);
    }
}
