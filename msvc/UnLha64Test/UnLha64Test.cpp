// UnLha64Test.cpp - UnLha64x.dll 総合テストツール
// ※ このファイルは UTF-8 (BOM) で保存してください

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <locale.h>
#include <stdio.h>
#include "resource.h"
#include "../../Header/UNLHA32.H"
#include "../../Header/UNLHA64EX.H"

// コモンコントロールライブラリの使用
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// DLL 関数の型定義: UnLha64x.dll の API に準拠
typedef int (WINAPI* UnlhaPtr)(HWND, LPCSTR, LPSTR, DWORD);
typedef WORD (WINAPI* UnlhaGetVersionPtr)(void);
typedef HARC (WINAPI* UnlhaOpenArchivePtr)(HWND, LPCSTR, DWORD);
typedef int (WINAPI* UnlhaCloseArchivePtr)(HARC);
typedef int (WINAPI* UnlhaFindFirstPtr)(HARC, LPCSTR, INDIVIDUALINFO*);
typedef int (WINAPI* UnlhaFindNextPtr)(HARC, INDIVIDUALINFO*);
typedef int (WINAPI* UnlhaSetOwnerWindowPtr)(HWND);
typedef BOOL (WINAPI* UnlhaSetOwnerWindowExTotalPtr)(HWND, LPARCHIVERPROC, BOOL);

// グローバル変数: DLL 関連の関数ポインタ
HMODULE g_hLib = NULL;
UnlhaPtr pUnlha = NULL;
UnlhaGetVersionPtr pGetVersion = NULL;
UnlhaOpenArchivePtr pOpen = NULL;
UnlhaCloseArchivePtr pClose = NULL;
UnlhaFindFirstPtr pFindFirst = NULL;
UnlhaFindNextPtr pFindNext = NULL;
UnlhaSetOwnerWindowPtr pSetOwnerWindow = NULL;
UnlhaSetOwnerWindowExTotalPtr pSetOwnerWindowExTotal = NULL;

// グローバル変数: アプリケーション状態管理
UINT g_wm_arcextract = 0;              // DLL から送られてくるプログレス通知メッセージ ID
std::string g_currentArc;              // 現在処理中のアーカイブパス（解凍時）
int g_progressPercentExt = 0;          // 解凍タブの個別ファイル進捗率
int g_progressPercentArc = 0;          // 圧縮タブの個別ファイル進捗率
int g_progressTotalPercentExt = 0;     // 解凍タブの全体進捗率
int g_progressTotalPercentArc = 0;     // 圧縮タブの全体進捗率

__int64 g_totalBytes = 0;              // 処理対象の全ファイルサイズの合計
__int64 g_processedBytes = 0;          // すでに処理が完了したファイルサイズの合計
__int64 g_currentFileBytes = 0;        // 現在処理中のファイルの処理済みサイズ
__int64 g_currentFileTotalBytes = 0;   // 現在処理中のファイルの全サイズ（通知から取得）

std::string g_currentFileNameExt;      // 現在解凍中のファイル名
std::string g_currentFileNameArc;      // 現在圧縮中のファイル名

bool g_extracting = false;             // 処理実行中フラグ
bool g_cancelRequested = false;        // キャンセル要求フラグ

int g_currentTab = 0;                  // 現在表示中のタブ (0: 解凍, 1: 圧縮)
std::vector<std::string> g_arcFiles;   // 圧縮対象のファイルリスト（相対パス）
std::string g_arcBaseDir;              // 圧縮対象ファイルのベースディレクトリ

// プロトタイプ宣言
INT_PTR CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void UpdateFileList(HWND hwndList, const std::string& arcPath);
void ExtractArchive(HWND hwnd, const std::string& arcPath, const std::string& outDir);
void CompressArchive(HWND hwnd, const std::string& arcPath);
void UpdateTabUI(HWND hwnd);
bool InitUnLhaDLL();
void UpdateProgressDisplay(HWND hwnd, int percent, int progressCtrlId);
__int64 GetArchiveTotalSize(const std::string& arcFile);
__int64 GetFilesTotalSize(const std::string& baseDir, const std::vector<std::string>& files);
int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData);

/**
 * アーカイブ内の全ファイルの合計サイズを取得する（全体進捗計算用）
 */
__int64 GetArchiveTotalSize(const std::string& arcFile) {
    if (!pOpen || !pFindFirst || !pFindNext || !pClose) return 0;
    HARC harc = pOpen(NULL, arcFile.c_str(), 0);
    if (!harc) return 0;
    __int64 total = 0;
    INDIVIDUALINFOA info;
    if (pFindFirst(harc, "*.*", &info) == 0) {
        do {
            total += info.dwOriginalSize;
        } while (pFindNext(harc, &info) == 0);
    }
    pClose(harc);
    return total;
}

/**
 * ディレクトリ内の全ファイルサイズを再帰的に合計する
 */
void SumDirectorySize(const std::string& path, __int64& total) {
    std::string searchPath = path + "\\*.*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                std::string fullPath = path + "\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    SumDirectorySize(fullPath, total);
                } else {
                    total += ((__int64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

/**
 * 指定されたファイル・ディレクトリリストの合計サイズを取得する
 */
__int64 GetFilesTotalSize(const std::string& baseDir, const std::vector<std::string>& files) {
    __int64 total = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        std::string path = baseDir;
        if (!path.empty() && path.back() != '\\') path += "\\";
        path += files[i];
        // ディレクトリ指定の場合の末尾バックスラッシュを一時的に除去して属性取得
        std::string checkPath = path;
        if (!checkPath.empty() && checkPath.back() == '\\') checkPath.pop_back();
        
        DWORD attr = GetFileAttributesA(checkPath.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES) {
            if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                SumDirectorySize(checkPath, total);
            } else {
                WIN32_FIND_DATAA fd;
                HANDLE hFind = FindFirstFileA(checkPath.c_str(), &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    total += ((__int64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                    FindClose(hFind);
                }
            }
        }
    }
    return total;
}

/**
 * プログレスバーのカスタム描画プロシージャ
 * ファイル名と進捗率(%)を中央にオーバーレイ表示する
 */
LRESULT CALLBACK ProgressSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        // ダブルバッファリング用のメモリ DC 作成
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

        // 背景描画（システムのコントロール色）
        HBRUSH hBg = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
        FillRect(hdcMem, &rc, hBg);
        DeleteObject(hBg);

        // ID に基づいて表示内容を選択
        int ctrlId = GetDlgCtrlID(hwnd);
        int percent = 0;
        std::string fileName = "";
        if (ctrlId == IDC_PROGRESS_ARC) { percent = g_progressPercentArc; fileName = g_currentFileNameArc; }
        else if (ctrlId == IDC_PROGRESS) { percent = g_progressPercentExt; fileName = g_currentFileNameExt; }
        else if (ctrlId == IDC_PROGRESS_ARC_TOTAL) percent = g_progressTotalPercentArc;
        else if (ctrlId == IDC_PROGRESS_TOTAL) percent = g_progressTotalPercentExt;

        // 進捗バーの描画（ライトブルー）
        if (percent > 0) {
            RECT rcProg = rc;
            rcProg.right = rcProg.left + (int)((double)w * percent / 100.0);
            HBRUSH hProg = CreateSolidBrush(RGB(135, 206, 250));
            FillRect(hdcMem, &rcProg, hProg);
            DeleteObject(hProg);
        }

        // 表示テキストの構築
        char buf[520] = { 0 };
        if (ctrlId == IDC_PROGRESS || ctrlId == IDC_PROGRESS_ARC) {
            if (!fileName.empty()) {
                sprintf_s(buf, "%s (%d%%)", fileName.c_str(), percent);
            } else if (percent > 0) {
                // ファイル名がない場合は数値のみ（0%時は描画しない）
                sprintf_s(buf, "%d%%", percent);
            }
        } else {
            // 全体進捗バーは常に数値のみを表示
            sprintf_s(buf, "%d%%", percent);
        }

        // テキストの描画
        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(0, 0, 0));
        HFONT hFont = (HFONT)SendMessage(GetParent(hwnd), WM_GETFONT, 0, 0);
        if (!hFont) hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);
        DrawTextA(hdcMem, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        // 画面への転送と後片付け
        SelectObject(hdcMem, hOldFont);
        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    // 背景消去メッセージを無視してちらつき防止
    if (uMsg == WM_ERASEBKGND) return 1;
    // ウィンドウ破棄時にサブクラスを解除
    if (uMsg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ProgressSubclassProc, uIdSubclass);
    
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

/**
 * フォルダブラウザのコールバック
 * 初期選択ディレクトリを設定する
 */
int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED) {
        // 初期ディレクトリのパス文字列を送信して選択状態にする
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, lpData);
    }
    return 0;
}

/**
 * エントリポイント
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    setlocale(LC_ALL, "Japanese"); // 日本語ロケールの設定
    InitCommonControls();          // コモンコントロールの初期化

    if (!InitUnLhaDLL()) return 1;

    // DLL との通信用メッセージ ID の取得 (拡張プログレスバーメッセージを使用)
    g_wm_arcextract = RegisterWindowMessageA(WM_ARCEXTRACT_EX);

    // メインダイアログの起動
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAINDIALOG), NULL, DialogProc);

    if (g_hLib) FreeLibrary(g_hLib);
    return 0;
}

/**
 * UnLha64x.dll のロードと関数ポインタの初期化
 */
bool InitUnLhaDLL() {
    g_hLib = LoadLibraryA("UnLha64x.dll");
    if (!g_hLib) {
        // カレントディレクトリにない場合はビルド出力先を探索
#ifdef _DEBUG
        g_hLib = LoadLibraryA("../bin/Debug/UnLha64x.dll");
#else
        g_hLib = LoadLibraryA("../bin/Release/UnLha64x.dll");
#endif
    }
    if (!g_hLib) {
        MessageBoxW(NULL, L"UnLha64x.dll が見つかりません。", L"Error", MB_ICONERROR);
        return false;
    }

    // 各 API 関数のアドレス取得
    pUnlha = (UnlhaPtr)GetProcAddress(g_hLib, "Unlha");
    pGetVersion = (UnlhaGetVersionPtr)GetProcAddress(g_hLib, "UnlhaGetVersion");
    pOpen = (UnlhaOpenArchivePtr)GetProcAddress(g_hLib, "UnlhaOpenArchive");
    pClose = (UnlhaCloseArchivePtr)GetProcAddress(g_hLib, "UnlhaCloseArchive");
    pFindFirst = (UnlhaFindFirstPtr)GetProcAddress(g_hLib, "UnlhaFindFirst");
    pFindNext = (UnlhaFindNextPtr)GetProcAddress(g_hLib, "UnlhaFindNext");
    pSetOwnerWindow = (UnlhaSetOwnerWindowPtr)GetProcAddress(g_hLib, "UnlhaSetOwnerWindow");
    pSetOwnerWindowExTotal = (UnlhaSetOwnerWindowExTotalPtr)GetProcAddress(g_hLib, "UnlhaSetOwnerWindowExTotal");

    if (!pUnlha || !pOpen || !pFindFirst) {
        MessageBoxW(NULL, L"DLL の必須関数が見つかりません。", L"Error", MB_ICONERROR);
        FreeLibrary(g_hLib);
        return false;
    }
    return true;
}

/**
 * 指定したプログレスバーの値を更新し、再描画を促す
 */
void UpdateProgressDisplay(HWND hwnd, int percent, int progressCtrlId) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int* pCurrentPercent = NULL;
    if (progressCtrlId == IDC_PROGRESS_ARC) pCurrentPercent = &g_progressPercentArc;
    else if (progressCtrlId == IDC_PROGRESS) pCurrentPercent = &g_progressPercentExt;
    else if (progressCtrlId == IDC_PROGRESS_ARC_TOTAL) pCurrentPercent = &g_progressTotalPercentArc;
    else if (progressCtrlId == IDC_PROGRESS_TOTAL) pCurrentPercent = &g_progressTotalPercentExt;

    if (pCurrentPercent) {
        // 値が変わっていない場合も再描画のみ行う（カスタム描画同期のため）
        if (percent == *pCurrentPercent) {
            InvalidateRect(GetDlgItem(hwnd, progressCtrlId), NULL, FALSE);
            return;
        }
        *pCurrentPercent = percent;
    }

    // 標準のプログレスバー位置を更新
    SendDlgItemMessage(hwnd, progressCtrlId, PBM_SETPOS, percent, 0);
    // カスタム描画を強制
    InvalidateRect(GetDlgItem(hwnd, progressCtrlId), NULL, FALSE);
    UpdateWindow(GetDlgItem(hwnd, progressCtrlId));
}

/**
 * ダイアログプロシージャ: メイン UI のイベント処理
 */
INT_PTR CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // DLL からのプログレス通知メッセージの処理
    if (g_wm_arcextract != 0 && uMsg == g_wm_arcextract) {
        EXTRACTINGINFO_TOTAL* pEi = (EXTRACTINGINFO_TOTAL*)lParam;
        if (pEi) {
            int state = (int)wParam;
            
            // ファイル処理開始 (BEGIN) 時にファイル名を更新
            if (state == ARCEXTRACT_EX_BEGIN) {
                // Unicode のファイル名から Shift_JIS に変換
                char szFileName[MAX_PATH];
                WideCharToMultiByte(CP_ACP, 0, pEi->szSourceFileName, -1, szFileName, MAX_PATH, NULL, NULL);
                std::string filename = szFileName;
                size_t slash = filename.find_last_of("\\/");
                if (slash != std::string::npos) filename = filename.substr(slash + 1);
                
                if (g_currentTab == 0) g_currentFileNameExt = filename;
                else g_currentFileNameArc = filename;
            }

            // 個別ファイル進捗の計算と表示更新
            if (pEi->llFileSize > 0) {
                int pos = (int)((double)pEi->llWriteSize / pEi->llFileSize * 100.0);
                UpdateProgressDisplay(hwnd, pos, (g_currentTab == 0) ? IDC_PROGRESS : IDC_PROGRESS_ARC);
            } else {
                UpdateProgressDisplay(hwnd, 0, (g_currentTab == 0) ? IDC_PROGRESS : IDC_PROGRESS_ARC);
            }

            // 全体進捗の計算と表示更新
            if (pEi->llTotalBytes > 0) {
                int totalPos = (int)((double)pEi->llTotalProcessed / pEi->llTotalBytes * 100.0);
                UpdateProgressDisplay(hwnd, totalPos, (g_currentTab == 0) ? IDC_PROGRESS_TOTAL : IDC_PROGRESS_ARC_TOTAL);
            } else {
                UpdateProgressDisplay(hwnd, 0, (g_currentTab == 0) ? IDC_PROGRESS_TOTAL : IDC_PROGRESS_ARC_TOTAL);
            }
        }
        
        // メッセージポンプを回して「キャンセル」ボタンの反応を維持
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (!IsDialogMessage(hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        
        // キャンセル要求があれば DLL に中止を通知 (1を返すと中断)
        if (g_cancelRequested) {
            SetWindowLongPtr(hwnd, DWLP_MSGRESULT, 1);
            return TRUE;
        }
        SetWindowLongPtr(hwnd, DWLP_MSGRESULT, 0);
        return TRUE;
    }

    switch (uMsg) {
    case WM_INITDIALOG: {
        DragAcceptFiles(hwnd, TRUE); // ファイルドロップの有効化
        
        // タブコントロールの初期化
        HWND hwndTab = GetDlgItem(hwnd, IDC_TAB);
        TCITEMW tiew = { 0 }; tiew.mask = TCIF_TEXT;
        tiew.pszText = (LPWSTR)L"解凍"; SendMessageW(hwndTab, TCM_INSERTITEMW, 0, (LPARAM)&tiew);
        tiew.pszText = (LPWSTR)L"圧縮"; SendMessageW(hwndTab, TCM_INSERTITEMW, 1, (LPARAM)&tiew);
        
        // リストビューのスタイル設定
        HWND hwndList = GetDlgItem(hwnd, IDC_FILELIST);
        ListView_SetExtendedListViewStyle(hwndList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        HWND hwndListArc = GetDlgItem(hwnd, IDC_FILELIST_ARC);
        ListView_SetExtendedListViewStyle(hwndListArc, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        
        // 解凍タブのリストビューヘッダー設定
        LVCOLUMNW lvc = { 0 }; lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        const wchar_t* headers[] = { L"ファイル名", L"サイズ", L"圧縮サイズ", L"日付" };
        int widths[] = { 250, 80, 80, 120 };
        for (int i = 0; i < 4; ++i) {
            lvc.pszText = (wchar_t*)headers[i]; lvc.cx = widths[i];
            SendMessageW(hwndList, LVM_INSERTCOLUMNW, i, (LPARAM)&lvc);
        }
        
        // 圧縮タブのリストビューヘッダー設定
        const wchar_t* headersArc[] = { L"ファイル名" };
        int widthsArc[] = { 350 };
        for (int i = 0; i < 1; ++i) {
            lvc.pszText = (wchar_t*)headersArc[i]; lvc.cx = widthsArc[i];
            SendMessageW(hwndListArc, LVM_INSERTCOLUMNW, i, (LPARAM)&lvc);
        }
        
        // 出力ディレクトリの初期値をカレントに設定
        char curDir[MAX_PATH]; GetCurrentDirectoryA(MAX_PATH, curDir);
        SetDlgItemTextA(hwnd, IDC_OUTDIR, curDir);
        
        // 拡張プログレスバーを有効化してオーナーウィンドウを設定
        if (pSetOwnerWindowExTotal) {
            pSetOwnerWindowExTotal(hwnd, NULL, TRUE);
        } else if (pSetOwnerWindow) {
            pSetOwnerWindow(hwnd);
        }

        // 解凍設定チェックボックスの初期値設定
        SendDlgItemMessage(hwnd, IDC_CHK_KEEP_DIR, BM_SETCHECK, BST_CHECKED, 0);

        // 圧縮設定チェックボックスの初期値設定
        SendDlgItemMessage(hwnd, IDC_CHK_STORE_DIR, BM_SETCHECK, BST_CHECKED, 0);

        // プログレスバーの描画サブクラス化を設定
        SetWindowSubclass(GetDlgItem(hwnd, IDC_PROGRESS), ProgressSubclassProc, 0, 0);
        SetWindowSubclass(GetDlgItem(hwnd, IDC_PROGRESS_ARC), ProgressSubclassProc, 0, 0);
        SetWindowSubclass(GetDlgItem(hwnd, IDC_PROGRESS_TOTAL), ProgressSubclassProc, 0, 0);
        SetWindowSubclass(GetDlgItem(hwnd, IDC_PROGRESS_ARC_TOTAL), ProgressSubclassProc, 0, 0);
        
        UpdateTabUI(hwnd);
        return TRUE;
    }
    case WM_NOTIFY: {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->idFrom == IDC_TAB && pnmh->code == TCN_SELCHANGE) {
            // タブ切り替え時の UI 更新
            g_currentTab = TabCtrl_GetCurSel(pnmh->hwndFrom);
            UpdateTabUI(hwnd);
        } else if (pnmh->idFrom == IDC_FILELIST_ARC && pnmh->code == NM_RCLICK) {
            // 圧縮対象リストの右クリックメニュー
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 2001, L"対象から外す");
            AppendMenuW(hMenu, MF_STRING, 2002, L"全クリア");
            if (ListView_GetSelectedCount(pnmh->hwndFrom) == 0) {
                EnableMenuItem(hMenu, 2001, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
            }
            POINT pt; GetCursorPos(&pt);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == 2001) {
                HWND hl = pnmh->hwndFrom; int item = -1;
                while ((item = ListView_GetNextItem(hl, -1, LVNI_SELECTED)) != -1) {
                    g_arcFiles.erase(g_arcFiles.begin() + item);
                    ListView_DeleteItem(hl, item);
                    item = -1; // 削除後はインデックスがずれるためリセットして再検索
                }
                if (g_arcFiles.empty()) EnableWindow(GetDlgItem(hwnd, IDC_COMPRESS), FALSE);
            } else if (cmd == 2002) {
                g_arcFiles.clear();
                ListView_DeleteAllItems(pnmh->hwndFrom);
                EnableWindow(GetDlgItem(hwnd, IDC_COMPRESS), FALSE);
            }
        }
        break;
    }
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        char path[MAX_PATH];
        if (g_currentTab == 0) { // 解凍タブ: アーカイブファイルのドロップ
            if (DragQueryFileA(hDrop, 0, path, MAX_PATH)) {
                g_currentArc = path;
                UpdateFileList(GetDlgItem(hwnd, IDC_FILELIST), g_currentArc);
                EnableWindow(GetDlgItem(hwnd, IDC_EXTRACT), TRUE);
            }
        } else { // 圧縮タブ: 対象ファイル・ディレクトリのドロップ
            UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);
            if (count > 0) {
                bool ok = true; std::string newBaseDir; std::vector<std::string> newFiles;
                for (UINT i = 0; i < count; ++i) {
                    DragQueryFileA(hDrop, i, path, MAX_PATH);
                    std::string fullPath = path;
                    size_t slashPos = fullPath.find_last_of("\\/");
                    if (slashPos != std::string::npos) {
                        std::string dir = fullPath.substr(0, slashPos);
                        std::string file = fullPath.substr(slashPos + 1);
                        DWORD attr = GetFileAttributesA(fullPath.c_str());
                        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) file += "\\";
                        
                        if (i == 0) {
                            newBaseDir = dir;
                            // ドライブ直下判定 (D: -> D:\)
                            if (newBaseDir.length() == 2 && newBaseDir[1] == ':') newBaseDir += "\\";
                        } else {
                            std::string nd = dir;
                            if (nd.length() == 2 && nd[1] == ':') nd += "\\";
                            if (newBaseDir != nd) { ok = false; break; }
                        }
                        newFiles.push_back(file);
                    }
                }
                if (ok) {
                    g_arcBaseDir = newBaseDir;
                    g_arcFiles = newFiles;
                    HWND hl = GetDlgItem(hwnd, IDC_FILELIST_ARC);
                    ListView_DeleteAllItems(hl);
                    for (size_t i = 0; i < g_arcFiles.size(); ++i) {
                        LVITEM lvi = { 0 }; lvi.mask = LVIF_TEXT; lvi.iItem = (int)i;
                        lvi.pszText = (LPSTR)g_arcFiles[i].c_str();
                        ListView_InsertItem(hl, &lvi);
                    }
                    if (GetWindowTextLengthA(GetDlgItem(hwnd, IDC_ARCOUT)) > 0) EnableWindow(GetDlgItem(hwnd, IDC_COMPRESS), TRUE);
                } else {
                    MessageBoxW(hwnd, L"同じディレクトリにあるファイルのみ追加できます。", L"エラー", MB_ICONERROR);
                }
            }
        }
        DragFinish(hDrop);
        return TRUE;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_BROWSE: { // 解凍先ディレクトリ選択
            wchar_t szDir[MAX_PATH] = { 0 };
            GetDlgItemTextW(hwnd, IDC_OUTDIR, szDir, MAX_PATH);

            BROWSEINFOW bi = { 0 };
            bi.hwndOwner = hwnd;
            bi.lpszTitle = L"出力ディレクトリを選択";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            bi.lpfn = BrowseCallbackProc;
            bi.lParam = (LPARAM)szDir;

            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t wpath[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, wpath)) {
                    char path[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, wpath, -1, path, MAX_PATH, NULL, NULL);
                    SetDlgItemTextA(hwnd, IDC_OUTDIR, path);
                }
                CoTaskMemFree(pidl);
            }
            break;
        }
        case IDC_BROWSE_ARCOUT: { // 圧縮先ファイル名選択
            OPENFILENAMEA ofn = { 0 }; char szFile[MAX_PATH] = { 0 };
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "LZH Files\0*.lzh\0All Files\0*.*\0"; ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH; ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT; ofn.lpstrDefExt = "lzh";
            if (GetSaveFileNameA(&ofn)) {
                SetDlgItemTextA(hwnd, IDC_ARCOUT, szFile);
                if (!g_arcFiles.empty()) EnableWindow(GetDlgItem(hwnd, IDC_COMPRESS), TRUE);
            }
            break;
        }
        case IDC_ARCOUT: // 圧縮先パス入力欄の変更監視
            if (HIWORD(wParam) == EN_CHANGE) {
                if (!g_arcFiles.empty() && GetWindowTextLengthA(GetDlgItem(hwnd, IDC_ARCOUT)) > 0) {
                    EnableWindow(GetDlgItem(hwnd, IDC_COMPRESS), TRUE);
                } else {
                    EnableWindow(GetDlgItem(hwnd, IDC_COMPRESS), FALSE);
                }
            }
            break;
        case IDC_EXTRACT: { // 解凍開始ボタン
            if (g_extracting) { g_cancelRequested = true; break; }
            if (g_currentArc.empty()) {
                MessageBoxW(hwnd, L"LZHファイルをドロップしてください。", L"情報", MB_ICONINFORMATION);
                break;
            }
            char od[MAX_PATH]; GetDlgItemTextA(hwnd, IDC_OUTDIR, od, MAX_PATH);
            ExtractArchive(hwnd, g_currentArc, od);
            break;
        }
        case IDC_COMPRESS: { // 圧縮開始ボタン
            if (g_extracting) { g_cancelRequested = true; break; }
            if (g_arcFiles.empty()) {
                MessageBoxW(hwnd, L"圧縮するファイルをドロップしてください。", L"情報", MB_ICONINFORMATION);
                break;
            }
            char ao[MAX_PATH]; GetDlgItemTextA(hwnd, IDC_ARCOUT, ao, MAX_PATH);
            if (ao[0] == '\0') {
                MessageBoxW(hwnd, L"圧縮ファイル名を指定してください。", L"情報", MB_ICONINFORMATION);
                break;
            }
            // 上書き確認
            if (GetFileAttributesA(ao) != INVALID_FILE_ATTRIBUTES) {
                int res = MessageBoxW(hwnd, L"出力先ファイルが既に存在します。\n\n[はい] : 削除して新規作成\n[いいえ] : 追加・更新\n[キャンセル] : 中止", L"確認", MB_YESNOCANCEL | MB_ICONQUESTION);
                if (res == IDCANCEL) break;
                else if (res == IDYES) DeleteFileA(ao);
            }
            CompressArchive(hwnd, ao);
            break;
        }
        case IDCANCEL:
            if (g_extracting) g_cancelRequested = true;
            else EndDialog(hwnd, IDCANCEL);
            break;
        }
        return TRUE;
    }
    } // switch (uMsg) の閉じ
    return FALSE;
}

/**
 * リストビューにアーカイブ内のファイル一覧を表示する
 */
void UpdateFileList(HWND hwndList, const std::string& arcPath) {
    ListView_DeleteAllItems(hwndList);
    HARC hArc = pOpen(hwndList, arcPath.c_str(), 0);
    if (!hArc) {
        MessageBoxW(GetParent(hwndList), L"アーカイブを開けませんでした。", L"エラー", MB_ICONERROR);
        return;
    }
    INDIVIDUALINFO info = { 0 };
    if (pFindFirst(hArc, "*", &info) == 0) {
        int index = 0;
        do {
            LVITEM lvi = { 0 }; lvi.mask = LVIF_TEXT; lvi.iItem = index;
            lvi.pszText = info.szFileName; ListView_InsertItem(hwndList, &lvi);
            
            char buf[64];
            sprintf_s(buf, "%u", info.dwOriginalSize); ListView_SetItemText(hwndList, index, 1, buf);
            sprintf_s(buf, "%u", info.dwCompressedSize); ListView_SetItemText(hwndList, index, 2, buf);
            sprintf_s(buf, "%04d-%02d-%02d %02d:%02d", 
                ((info.wDate >> 9) & 0x7F) + 1980, (info.wDate >> 5) & 0x0F, info.wDate & 0x1F,
                (info.wTime >> 11) & 0x1F, (info.wTime >> 5) & 0x3F);
            ListView_SetItemText(hwndList, index, 3, buf);
            index++;
        } while (pFindNext(hArc, &info) == 0);
    }
    pClose(hArc);
}

/**
 * アーカイブを解凍する
 */
void ExtractArchive(HWND hwnd, const std::string& arcPath, const std::string& outDir) {
    BOOL overwrite = SendDlgItemMessage(hwnd, IDC_CHK_OVERWRITE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL keepDir = SendDlgItemMessage(hwnd, IDC_CHK_KEEP_DIR, BM_GETCHECK, 0, 0) == BST_CHECKED;
    char szSwExt[256] = { 0 };
    GetDlgItemTextA(hwnd, IDC_SW_EXT, szSwExt, sizeof(szSwExt));
    std::string swExt = szSwExt;

    // 解凍コマンドの決定 (x / e)
    std::string baseCmd = keepDir ? "x" : "e";

    // オリジナルの仕様に合わせて、上書き時には '-y' スイッチを付加するように調整
    std::string switches = swExt;
    if (overwrite) {
        if (!switches.empty()) {
            switches += " ";
        }
        switches += "-y";
    }

    // 解凍コマンドの構築
    std::string cmd = baseCmd;
    if (!switches.empty()) {
        cmd += " " + switches;
    }
    cmd += " \"" + arcPath + "\" \"" + outDir + "\\\"";
    
    // 状態の初期化
    g_extracting = true;
    g_cancelRequested = false;
    SetDlgItemTextW(hwnd, IDC_EXTRACT, L"キャンセル");
    EnableWindow(GetDlgItem(hwnd, IDC_BROWSE), FALSE);
    
    g_progressPercentExt = -1; g_progressTotalPercentExt = -1; g_currentFileNameExt = "";
    UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS);
    UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS_TOTAL);
    
    // 全サイズ計算（DLL側の事前走査機能を使用するため手動計算は廃止）
    // g_totalBytes = GetArchiveTotalSize(arcPath);
    g_totalBytes = 0;
    g_processedBytes = 0; g_currentFileBytes = 0; g_currentFileTotalBytes = 0;

    char szOutput[1024] = { 0 };
    int result = pUnlha(hwnd, cmd.c_str(), szOutput, sizeof(szOutput));

    // 終了後の UI 復帰
    g_extracting = false;
    g_cancelRequested = false;
    SetDlgItemTextW(hwnd, IDC_EXTRACT, L"解凍");
    EnableWindow(GetDlgItem(hwnd, IDC_BROWSE), TRUE);

    if (result == 0) {
        g_progressPercentExt = -1; g_progressTotalPercentExt = -1; g_currentFileNameExt = "";
        UpdateProgressDisplay(hwnd, 100, IDC_PROGRESS);
        UpdateProgressDisplay(hwnd, 100, IDC_PROGRESS_TOTAL);
        MessageBoxW(hwnd, L"解凍が完了しました。", L"完了", MB_ICONINFORMATION);
    } else if (result == 0x8000 + 7 || (result == -1 && strstr(szOutput, "User cancelled"))) {
        g_progressPercentExt = -1; g_progressTotalPercentExt = -1; g_currentFileNameExt = "";
        UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS);
        UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS_TOTAL);
        MessageBoxW(hwnd, L"解凍がキャンセルされました。", L"キャンセル", MB_ICONINFORMATION);
    } else {
        g_progressPercentExt = -1; g_progressTotalPercentExt = -1; g_currentFileNameExt = "";
        UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS);
        UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS_TOTAL);
        wchar_t wszOutput[1024] = { 0 };
        if (szOutput[0] != '\0') {
            MultiByteToWideChar(CP_ACP, 0, szOutput, -1, wszOutput, 1024);
        }
        wchar_t buf[2048];
        if (wszOutput[0] != L'\0') {
            swprintf_s(buf, L"エラーが発生しました。\n%s\n(コード: 0x%X)", wszOutput, result);
        } else {
            swprintf_s(buf, L"エラーが発生しました。\nコード: 0x%X", result);
        }
        MessageBoxW(hwnd, buf, L"エラー", MB_ICONERROR);
    }
}

/**
 * タブ切り替えに伴う UI 要素の表示・非表示を制御する
 */
void UpdateTabUI(HWND hwnd) {
    int sE = (g_currentTab == 0) ? SW_SHOW : SW_HIDE;
    int sA = (g_currentTab == 1) ? SW_SHOW : SW_HIDE;
    
    // 解凍用 UI
    ShowWindow(GetDlgItem(hwnd, IDC_LBL_OUTDIR), sE);
    ShowWindow(GetDlgItem(hwnd, IDC_OUTDIR), sE);
    ShowWindow(GetDlgItem(hwnd, IDC_BROWSE), sE);
    ShowWindow(GetDlgItem(hwnd, IDC_FILELIST), sE);
    ShowWindow(GetDlgItem(hwnd, IDC_CHK_KEEP_DIR), sE);
    ShowWindow(GetDlgItem(hwnd, IDC_CHK_OVERWRITE), sE);
    ShowWindow(GetDlgItem(hwnd, IDC_LBL_SW_EXT), sE);
    ShowWindow(GetDlgItem(hwnd, IDC_SW_EXT), sE);
    ShowWindow(GetDlgItem(hwnd, IDC_EXTRACT), sE);
    ShowWindow(GetDlgItem(hwnd, IDC_PROGRESS), sE);
    ShowWindow(GetDlgItem(hwnd, IDC_PROGRESS_TOTAL), sE);
    
    // 圧縮用 UI
    ShowWindow(GetDlgItem(hwnd, IDC_LBL_ARCOUT), sA);
    ShowWindow(GetDlgItem(hwnd, IDC_ARCOUT), sA);
    ShowWindow(GetDlgItem(hwnd, IDC_BROWSE_ARCOUT), sA);
    ShowWindow(GetDlgItem(hwnd, IDC_FILELIST_ARC), sA);
    ShowWindow(GetDlgItem(hwnd, IDC_CHK_STORE_DIR), sA);
    ShowWindow(GetDlgItem(hwnd, IDC_LBL_SW_ARC), sA);
    ShowWindow(GetDlgItem(hwnd, IDC_SW_ARC), sA);
    ShowWindow(GetDlgItem(hwnd, IDC_COMPRESS), sA);
    ShowWindow(GetDlgItem(hwnd, IDC_PROGRESS_ARC), sA);
    ShowWindow(GetDlgItem(hwnd, IDC_PROGRESS_ARC_TOTAL), sA);
}

/**
 * 指定されたファイルを圧縮する
 */
void CompressArchive(HWND hwnd, const std::string& arcPath) {
    BOOL storeDir = SendDlgItemMessage(hwnd, IDC_CHK_STORE_DIR, BM_GETCHECK, 0, 0) == BST_CHECKED;
    char szSwArc[256] = { 0 };
    GetDlgItemTextA(hwnd, IDC_SW_ARC, szSwArc, sizeof(szSwArc));
    std::string swArc = szSwArc;

    // スイッチの調整
    std::string switches = swArc;
    if (storeDir) {
        if (!switches.empty()) {
            switches += " ";
        }
        switches += "-d";
    }

    // 圧縮コマンドの構築
    std::string cmd = "a";
    if (!switches.empty()) {
        cmd += " " + switches;
    }
    cmd += " \"" + arcPath + "\"";
    for (size_t i = 0; i < g_arcFiles.size(); ++i) cmd += " \"" + g_arcFiles[i] + "\"";
    
    // 状態の初期化
    g_extracting = true;
    g_cancelRequested = false;
    SetDlgItemTextW(hwnd, IDC_COMPRESS, L"キャンセル");
    EnableWindow(GetDlgItem(hwnd, IDC_BROWSE_ARCOUT), FALSE);
    
    // 圧縮対象ファイルのベースディレクトリへ移動（DLL 内での相対パス維持のため）
    SetCurrentDirectoryA(g_arcBaseDir.c_str());
    
    g_progressPercentArc = -1; g_progressTotalPercentArc = -1; g_currentFileNameArc = "";
    UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS_ARC);
    UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS_ARC_TOTAL);
    
    // 全サイズ計算（DLL側の事前走査機能を使用するため手動計算は廃止）
    // g_totalBytes = GetFilesTotalSize(g_arcBaseDir, g_arcFiles);
    g_totalBytes = 0;
    g_processedBytes = 0; g_currentFileBytes = 0; g_currentFileTotalBytes = 0;

    char szOutput[1024] = { 0 };
    int result = pUnlha(hwnd, cmd.c_str(), szOutput, sizeof(szOutput));

    // 終了後の UI 復帰
    g_extracting = false;
    g_cancelRequested = false;
    SetDlgItemTextW(hwnd, IDC_COMPRESS, L"圧縮");
    EnableWindow(GetDlgItem(hwnd, IDC_BROWSE_ARCOUT), TRUE);

    if (result == 0) {
        g_progressPercentArc = -1; g_progressTotalPercentArc = -1; g_currentFileNameArc = "";
        UpdateProgressDisplay(hwnd, 100, IDC_PROGRESS_ARC);
        UpdateProgressDisplay(hwnd, 100, IDC_PROGRESS_ARC_TOTAL);
        MessageBoxW(hwnd, L"圧縮が完了しました。", L"完了", MB_ICONINFORMATION);
    } else if (result == 0x8000 + 7 || (result == -1 && strstr(szOutput, "User cancelled"))) {
        g_progressPercentArc = -1; g_progressTotalPercentArc = -1; g_currentFileNameArc = "";
        UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS_ARC);
        UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS_ARC_TOTAL);
        MessageBoxW(hwnd, L"圧縮がキャンセルされました。", L"キャンセル", MB_ICONINFORMATION);
    } else {
        g_progressPercentArc = -1; g_progressTotalPercentArc = -1; g_currentFileNameArc = "";
        UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS_ARC);
        UpdateProgressDisplay(hwnd, 0, IDC_PROGRESS_ARC_TOTAL);
        wchar_t wszOutput[1024] = { 0 };
        if (szOutput[0] != '\0') {
            MultiByteToWideChar(CP_ACP, 0, szOutput, -1, wszOutput, 1024);
        }
        wchar_t buf[2048];
        if (wszOutput[0] != L'\0') {
            swprintf_s(buf, L"エラーが発生しました。\n%s\n(コード: 0x%X)", wszOutput, result);
        } else {
            swprintf_s(buf, L"エラーが発生しました。\nコード: 0x%X", result);
        }
        MessageBoxW(hwnd, buf, L"エラー", MB_ICONERROR);
    }
}
