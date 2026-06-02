#include "Framework.h"
#include "SystemProtector.h"
#include "Scanner.h"
#include "Cleaner.h"
#include "resource.h"

HINSTANCE g_hInst = NULL;
HWND g_hWnd = NULL;
HWND g_hListFolders = NULL;
HWND g_hBtnScan = NULL;
HWND g_hBtnDelete = NULL;
HWND g_hBtnClean = NULL;
HWND g_hBtnDeepClean = NULL;
HWND g_hProgress = NULL;
HWND g_hStaticStatus = NULL;
HWND g_hStaticPath = NULL;
HWND g_hEditPath = NULL;
HWND g_hBtnBrowse = NULL;

Scanner g_scanner;
Cleaner g_cleaner;
std::vector<ScanEntry> g_scanResults;
std::mutex g_scanMutex;

HICON g_hIconSmall = NULL;
HICON g_hIconBig = NULL;

std::wstring FormatSize(ULONG64 bytes) {
    wchar_t buf[64];
    if (bytes >= 1073741824ULL) {
        swprintf(buf, L"%.2f GB", (double)bytes / 1073741824.0);
    } else if (bytes >= 1048576) {
        swprintf(buf, L"%.2f MB", (double)bytes / 1048576.0);
    } else if (bytes >= 1024) {
        swprintf(buf, L"%.2f KB", (double)bytes / 1024.0);
    } else {
        swprintf(buf, L"%llu B", bytes);
    }
    return std::wstring(buf);
}

void UpdateListView() {
    ListView_DeleteAllItems(g_hListFolders);

    std::vector<ScanEntry> topFolders;
    {
        std::lock_guard<std::mutex> lock(g_scanMutex);
        topFolders = g_scanner.GetTopFolders(200);
    }

    LVITEMW lvi = { 0 };
    int idx = 0;
    for (const auto& entry : topFolders) {
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = idx;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)entry.path.c_str();
        lvi.lParam = (LPARAM)idx;
        ListView_InsertItem(g_hListFolders, &lvi);

        ListView_SetItemText(g_hListFolders, idx, 1,
                             (LPWSTR)FormatSize(entry.totalSize).c_str());
        ListView_SetItemText(g_hListFolders, idx, 2,
                             (LPWSTR)std::to_wstring(entry.fileCount).c_str());
        ListView_SetItemText(g_hListFolders, idx, 3,
                             (LPWSTR)(entry.isProtected ? L"Protected" :
                                      entry.isSystem ? L"System" : L"User").c_str());

        idx++;
    }

    wchar_t buf[128];
    swprintf(buf, L"Folders: %d | Total: %s | Scanned: %llu",
             idx, FormatSize(g_scanner.GetTotalScannedSize()).c_str(),
             g_scanner.GetTotalScannedFolders());
    SetWindowTextW(g_hStaticStatus, buf);
    SetWindowTextW(g_hStaticPath, g_scanner.GetCurrentPath().c_str());
}

void SetUIState(bool scanning) {
    EnableWindow(g_hBtnScan, !scanning);
    EnableWindow(g_hBtnDelete, !scanning);
    EnableWindow(g_hBtnClean, !scanning);
    EnableWindow(g_hBtnDeepClean, !scanning);
    EnableWindow(g_hEditPath, !scanning);

    if (scanning) {
        SendMessageW(g_hProgress, PBM_SETMARQUEE, 1, 0);
        SetWindowTextW(g_hStaticStatus, L"Scanning...");
    } else {
        SendMessageW(g_hProgress, PBM_SETMARQUEE, 0, 0);
    }
}

void StartScan() {
    wchar_t pathBuf[MAX_PATH];
    GetWindowTextW(g_hEditPath, pathBuf, MAX_PATH);
    std::wstring scanPath(pathBuf);
    if (scanPath.empty()) scanPath = L"C:\\";

    if (scanPath.back() != L'\\') scanPath += L'\\';

    SetUIState(true);
    g_scanner.StartScan(scanPath, g_hWnd, 0);
}

void StopScan() {
    g_scanner.CancelScan();
    SetUIState(false);
    SetWindowTextW(g_hStaticStatus, L"Scan cancelled");
}

std::wstring GetSelectedPath() {
    int sel = ListView_GetNextItem(g_hListFolders, -1, LVNI_ALL | LVNI_SELECTED);
    if (sel == -1) return L"";

    wchar_t buf[MAX_PATH * 4] = { 0 };
    ListView_GetItemText(g_hListFolders, sel, 0, buf, MAX_PATH * 4);
    return std::wstring(buf);
}

void DoDelete() {
    std::wstring path = GetSelectedPath();
    if (path.empty()) {
        SetWindowTextW(g_hStaticStatus, L"No folder selected");
        return;
    }

    if (!SystemProtector::IsSafeToDelete(path)) {
        SetWindowTextW(g_hStaticStatus, L"Cannot delete - protected system path");
        MessageBoxW(g_hWnd, L"This folder is protected and cannot be deleted.",
                    L"Protected Path", MB_ICONWARNING);
        return;
    }

    wchar_t msg[2048];
    swprintf(msg, L"Delete '%s'?\n\nThis will permanently remove this folder and all contents.",
             path.c_str());

    if (MessageBoxW(g_hWnd, msg, L"Confirm Delete",
                    MB_YESNO | MB_ICONEXCLAMATION) == IDYES) {
        g_cleaner.StartClean(path, CleanMode::DeleteFolder, g_hWnd, 0);
        SetWindowTextW(g_hStaticStatus, L"Deleting...");
        EnableWindow(g_hBtnDelete, FALSE);
        EnableWindow(g_hBtnClean, FALSE);
        EnableWindow(g_hBtnDeepClean, FALSE);
    }
}

void DoClean() {
    std::wstring path = GetSelectedPath();
    if (path.empty()) {
        SetWindowTextW(g_hStaticStatus, L"No folder selected");
        return;
    }

    if (!SystemProtector::IsSafeToClean(path)) {
        SetWindowTextW(g_hStaticStatus, L"Cannot clean - protected path");
        return;
    }

    wchar_t msg[2048];
    swprintf(msg, L"Clean '%s'?\n\nThis will remove temporary files, logs, and caches.",
             path.c_str());

    if (MessageBoxW(g_hWnd, msg, L"Confirm Clean",
                    MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        g_cleaner.StartClean(path, CleanMode::CleanTemp, g_hWnd, 0);
        SetWindowTextW(g_hStaticStatus, L"Cleaning...");
        EnableWindow(g_hBtnDelete, FALSE);
        EnableWindow(g_hBtnClean, FALSE);
        EnableWindow(g_hBtnDeepClean, FALSE);
    }
}

void DoDeepClean() {
    std::wstring path = GetSelectedPath();
    if (path.empty()) {
        SetWindowTextW(g_hStaticStatus, L"No folder selected");
        return;
    }

    if (!SystemProtector::IsSafeToClean(path)) {
        SetWindowTextW(g_hStaticStatus, L"Cannot clean - protected path");
        return;
    }

    wchar_t msg[2048];
    swprintf(msg, L"Deep clean '%s'?\n\nAggressive cleaning - removes temp files, logs, caches, dumps, and update artifacts.",
             path.c_str());

    if (MessageBoxW(g_hWnd, msg, L"Confirm Deep Clean",
                    MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        g_cleaner.StartClean(path, CleanMode::DeepClean, g_hWnd, 0);
        SetWindowTextW(g_hStaticStatus, L"Deep cleaning...");
        EnableWindow(g_hBtnDelete, FALSE);
        EnableWindow(g_hBtnClean, FALSE);
        EnableWindow(g_hBtnDeepClean, FALSE);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE);

            INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX),
                                          ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS };
            InitCommonControlsEx(&icex);

            g_hEditPath = CreateWindowW(WC_EDITW, L"C:\\",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                10, 10, 300, 24, hWnd, (HMENU)IDC_EDIT_PATH, hInst, NULL);

            g_hBtnScan = CreateWindowW(L"BUTTON", L"Scan",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                320, 10, 80, 24, hWnd, (HMENU)IDC_BTN_SCAN, hInst, NULL);

            g_hBtnBrowse = CreateWindowW(L"BUTTON", L"...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                405, 10, 30, 24, hWnd, (HMENU)IDC_BTN_BROWSE, hInst, NULL);

            g_hListFolders = CreateWindowW(WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                LVS_SHOWSELALWAYS | WS_BORDER,
                10, 40, 580, 340, hWnd, (HMENU)IDC_LIST_FOLDERS, hInst, NULL);

            LVCOLUMNW lvc = { 0 };
            lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            lvc.cx = 280;
            lvc.pszText = L"Folder Path";
            ListView_InsertColumn(g_hListFolders, 0, &lvc);
            lvc.cx = 100;
            lvc.pszText = L"Size";
            ListView_InsertColumn(g_hListFolders, 1, &lvc);
            lvc.cx = 70;
            lvc.pszText = L"Files";
            ListView_InsertColumn(g_hListFolders, 2, &lvc);
            lvc.cx = 80;
            lvc.pszText = L"Type";
            ListView_InsertColumn(g_hListFolders, 3, &lvc);
            ListView_SetExtendedListViewStyle(g_hListFolders,
                LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

            g_hBtnDelete = CreateWindowW(L"BUTTON", L"Delete Folder",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                10, 390, 130, 30, hWnd, (HMENU)IDC_BTN_DELETE, hInst, NULL);

            g_hBtnClean = CreateWindowW(L"BUTTON", L"Clean Temp",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                150, 390, 130, 30, hWnd, (HMENU)IDC_BTN_CLEAN, hInst, NULL);

            g_hBtnDeepClean = CreateWindowW(L"BUTTON", L"Deep Clean",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                290, 390, 130, 30, hWnd, (HMENU)IDC_BTN_DEEPCLEAN, hInst, NULL);

            g_hProgress = CreateWindowW(PROGRESS_CLASS, L"",
                WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
                10, 430, 580, 20, hWnd, (HMENU)IDC_PROGRESS, hInst, NULL);

            g_hStaticPath = CreateWindowW(L"STATIC", L"Ready",
                WS_CHILD | WS_VISIBLE | SS_SUNKEN,
                10, 460, 400, 20, hWnd, NULL, hInst, NULL);

            g_hStaticStatus = CreateWindowW(L"STATIC", L"Ready",
                WS_CHILD | WS_VISIBLE | SS_SUNKEN,
                10, 485, 580, 20, hWnd, (HMENU)IDC_STATIC_STATUS, hInst, NULL);

            SendMessageW(g_hProgress, PBM_SETMARQUEE, 0, 0);
            EnableWindow(g_hBtnDelete, FALSE);
            EnableWindow(g_hBtnClean, FALSE);
            EnableWindow(g_hBtnDeepClean, FALSE);

            SystemProtector::GetProtectedDirs();
            SetWindowTextW(g_hStaticStatus, L"Ready - enter path and click Scan");
            break;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_BTN_SCAN:
                    StartScan();
                    break;

                case IDC_BTN_BROWSE: {
                    wchar_t path[MAX_PATH] = { 0 };
                    BROWSEINFOW bi = { 0 };
                    bi.hwndOwner = hWnd;
                    bi.lpszTitle = L"Select folder to scan";
                    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
                    if (pidl) {
                        if (SHGetPathFromIDListW(pidl, path)) {
                            SetWindowTextW(g_hEditPath, path);
                        }
                        CoTaskMemFree(pidl);
                    }
                    break;
                }

                case IDC_BTN_DELETE:
                    DoDelete();
                    break;

                case IDC_BTN_CLEAN:
                    DoClean();
                    break;

                case IDC_BTN_DEEPCLEAN:
                    DoDeepClean();
                    break;

                case 40000:
                    DestroyWindow(hWnd);
                    break;

                case 40001:
                    MessageBoxW(hWnd,
                        L"DiskC v1.0\n\nIntelligent disk cleaner\nScans C: drive, finds largest folders, safely cleans without touching system files.\n\nMade by Fox & Jack",
                        L"About DiskC", MB_OK);
                    break;
            }
            break;
        }

        case WM_NOTIFY: {
            LPNMHDR nmhdr = (LPNMHDR)lParam;
            if (nmhdr->idFrom == IDC_LIST_FOLDERS && nmhdr->code == LVN_ITEMCHANGED) {
                std::wstring selPath = GetSelectedPath();
                if (!selPath.empty()) {
                    bool isSys = SystemProtector::IsSystemPath(selPath);
                    EnableWindow(g_hBtnDelete, !isSys);
                    EnableWindow(g_hBtnClean, !isSys);
                    EnableWindow(g_hBtnDeepClean, !isSys);
                    if (isSys) {
                        SetWindowTextW(g_hStaticStatus, L"Selected folder is protected");
                    } else {
                        SetWindowTextW(g_hStaticStatus, L"");
                    }
                }
            }
            break;
        }

        case Scanner::WM_SCAN_PROGRESS: {
            wchar_t buf[256];
            swprintf(buf, L"Scanning... %llu folders processed",
                     (ULONGLONG)wParam);
            SetWindowTextW(g_hStaticStatus, buf);
            break;
        }

        case Scanner::WM_SCAN_COMPLETE: {
            SetUIState(false);
            UpdateListView();
            SetWindowTextW(g_hStaticStatus, L"Scan complete");
            break;
        }

        case Scanner::WM_SCAN_ENTRY: {
            ScanEntry* entry = (ScanEntry*)wParam;
            if (entry) {
                delete entry;
            }
            break;
        }

        case Cleaner::WM_CLEAN_PROGRESS: {
            SetWindowTextW(g_hStaticStatus, L"Cleaning in progress...");
            break;
        }

        case Cleaner::WM_CLEAN_COMPLETE: {
            CleanResult* result = (CleanResult*)wParam;
            if (result) {
                wchar_t buf[512];
                swprintf(buf, L"Clean complete: %s removed, %llu files, %llu folders deleted, %llu errors",
                         FormatSize(result->deletedSize).c_str(),
                         result->deletedFiles, result->deletedFolders, result->errors);
                SetWindowTextW(g_hStaticStatus, buf);
                EnableWindow(g_hBtnDelete, TRUE);
                EnableWindow(g_hBtnClean, TRUE);
                EnableWindow(g_hBtnDeepClean, TRUE);
            }
            break;
        }

        case WM_SIZE: {
            int w = LOWORD(lParam);
            int h = HIWORD(lParam);
            if (w < 610) w = 610;
            if (h < 520) h = 520;

            int margin = 10;
            int ctrlW = w - 2 * margin;

            if (g_hEditPath)
                SetWindowPos(g_hEditPath, NULL, margin, 10, ctrlW - 130, 24, SWP_NOZORDER);
            if (g_hBtnScan)
                SetWindowPos(g_hBtnScan, NULL, ctrlW - 120, 10, 70, 24, SWP_NOZORDER);
            if (g_hBtnBrowse)
                SetWindowPos(g_hBtnBrowse, NULL, ctrlW - 40, 10, 30, 24, SWP_NOZORDER);
            if (g_hListFolders)
                SetWindowPos(g_hListFolders, NULL, margin, 40, ctrlW, h - 160, SWP_NOZORDER);
            if (g_hBtnDelete)
                SetWindowPos(g_hBtnDelete, NULL, margin, h - 115, 130, 28, SWP_NOZORDER);
            if (g_hBtnClean)
                SetWindowPos(g_hBtnClean, NULL, margin + 145, h - 115, 130, 28, SWP_NOZORDER);
            if (g_hBtnDeepClean)
                SetWindowPos(g_hBtnDeepClean, NULL, margin + 290, h - 115, 130, 28, SWP_NOZORDER);
            if (g_hProgress)
                SetWindowPos(g_hProgress, NULL, margin, h - 80, ctrlW, 18, SWP_NOZORDER);
            if (g_hStaticPath)
                SetWindowPos(g_hStaticPath, NULL, margin, h - 55, ctrlW / 2 - margin, 18, SWP_NOZORDER);
            if (g_hStaticStatus)
                SetWindowPos(g_hStaticStatus, NULL, margin, h - 30, ctrlW, 20, SWP_NOZORDER);
            break;
        }

        case WM_DESTROY:
            g_scanner.CancelScan();
            g_cleaner.CancelClean();
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInstance;

    g_hIconSmall = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    g_hIconBig = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = g_hIconBig;
    wc.hIconSm = g_hIconSmall;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"DiskCClass";
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU1);

    if (!RegisterClassExW(&wc)) {
        MessageBoxA(NULL, "Failed to register window class", "Error", MB_ICONERROR);
        return 1;
    }

    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"DiskC - Intelligent Disk Cleaner",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 620, 550,
                             NULL, NULL, hInstance, NULL);

    if (!g_hWnd) {
        MessageBoxA(NULL, "Failed to create window", "Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
