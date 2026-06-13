/* lha_progress.c - LHa コアから DLL 側へのプログレス通知ブリッジ */
/* lha.h のマクロ環境下でコンパイルされ、indicator.c から呼び出される */

#include <windows.h>
#include <string.h>
#include <io.h>
#include "lha.h"
#include "UNLHA64EX.H"

/* コールバック関数ポインタ型 */
typedef BOOL (WINAPI *LHA_ARCHIVERPROC_GEN)(HWND, UINT, UINT, LPVOID);

/* アクセサ関数 (unlha64.cpp から) */
extern HWND Lha_GetHwndOwner();
extern UINT Lha_GetMsgArcExtract();
extern void Lha_SetMsgArcExtract(UINT u);
extern LHA_ARCHIVERPROC_GEN Lha_GetArcProc();
extern BOOL Lha_GetEnableTotalProgress();

/* 全体進捗の追跡用グローバル変数 */
static __int64 g_total_bytes = 0;      /* 処理対象の全ファイルの合計サイズ */
static __int64 g_processed_bytes = 0;  /* これまでに処理した累計バイト数 */
static int     g_total_files = 0;      /* 処理対象の全ファイル数 */
static int     g_processed_files = 0;  /* これまでに処理したファイル数 */

static __int64 g_current_file_processed = 0;
static __int64 g_current_file_size = 0;
static char    g_last_filename[513] = {0};
static int     g_lha_aborted = 0;

/* 他の処理から全体サイズなどの情報を設定するためのAPI */
void Lha_SetTotalProgressInfo(__int64 total_bytes, int total_files) {
    g_total_bytes = total_bytes;
    g_processed_bytes = 0;
    g_total_files = total_files;
    g_processed_files = 0;
    g_current_file_processed = 0;
    g_current_file_size = 0;
    memset(g_last_filename, 0, sizeof(g_last_filename));
}

int Lha_CheckAbort() {
    return g_lha_aborted;
}

void Lha_ResetAbort() {
    g_lha_aborted = 0;
}

int Lha_SendProgressMessage(int state, const char* filename, __int64 current_size, __int64 total_size) {
    HWND hwnd = Lha_GetHwndOwner();
    UINT msg = Lha_GetMsgArcExtract();
    LHA_ARCHIVERPROC_GEN proc = Lha_GetArcProc();

    if (g_lha_aborted) return 1;
    if (!hwnd && !proc) return 0;

    /* 1. 全体進捗の自動計算ロジック */
    if (Lha_GetEnableTotalProgress()) {
        if (state == 0) { /* ARCEXTRACT_BEGIN */
            if (filename && strcmp(filename, g_last_filename) != 0) {
                g_processed_files++;
                strncpy(g_last_filename, filename, 512);
            }
            g_current_file_size = total_size;
            g_current_file_processed = 0;
        }
        else if (state == 1) { /* ARCEXTRACT_INPROCESS */
            __int64 delta = current_size - g_current_file_processed;
            if (delta > 0) {
                g_processed_bytes += delta;
                g_current_file_processed = current_size;
            }
        }
        else if (state == 2) { /* ARCEXTRACT_END */
            __int64 delta = total_size - g_current_file_processed;
            if (delta > 0) {
                g_processed_bytes += delta;
            }
            g_current_file_processed = total_size;
        }
    }

    /* 2. 拡張プログレス（wm_arcextract_ex）での通知 */
    if (Lha_GetEnableTotalProgress()) {
        static UINT msg_ex = 0;
        if (msg_ex == 0) {
            msg_ex = RegisterWindowMessageA("wm_arcextract_ex");
            if (msg_ex == 0) return 0;
        }

        EXTRACTINGINFO_TOTAL pi_ex;
        memset(&pi_ex, 0, sizeof(pi_ex));
        pi_ex.dwStructSize = sizeof(pi_ex);
        pi_ex.llFileSize = g_current_file_size;
        pi_ex.llWriteSize = g_current_file_processed;
        pi_ex.llTotalBytes = g_total_bytes;
        pi_ex.llTotalProcessed = g_processed_bytes;
        pi_ex.dwTotalFiles = (DWORD)g_total_files;
        pi_ex.dwFilesProcessed = (DWORD)g_processed_files;

        const char* name_to_use = (filename && filename[0] != '\0') ? filename : g_last_filename;
        if (name_to_use[0] != '\0') {
            MultiByteToWideChar(932, 0, name_to_use, -1, pi_ex.szSourceFileName, FNAME_MAX32);
            MultiByteToWideChar(932, 0, name_to_use, -1, pi_ex.szDestFileName, FNAME_MAX32);
        }

        int abort = 0;
        if (proc) {
            /* コールバック関数の場合は TRUE が継続、FALSE が中断 */
            BOOL ret = proc(hwnd, msg_ex, (UINT)state, (LPVOID)&pi_ex);
            if (!ret) {
                abort = 1;
            }
        } else if (hwnd != NULL) {
            /* メッセージ送信の場合は 0 が継続、非ゼロが中断 */
            LRESULT ret = SendMessageA(hwnd, msg_ex, (WPARAM)state, (LPARAM)&pi_ex);
            if (ret != 0) {
                abort = 1;
            }
        }

        if (abort) {
            g_lha_aborted = 1;
        }
        return abort;
    }

    /* 3. 従来のプログレス（wm_arcextract）での通知 */
    EXTRACTINGINFOA pi;
    if (msg == 0) {
        msg = RegisterWindowMessageA("wm_arcextract");
        if (msg == 0) return 0;
        Lha_SetMsgArcExtract(msg);
    }

    memset(&pi, 0, sizeof(pi));
    pi.dwFileSize = (DWORD)total_size;
    pi.dwWriteSize = (DWORD)current_size;
    
    if (filename) {
        strncpy(pi.szSourceFileName, filename, sizeof(pi.szSourceFileName) - 1);
        strncpy(pi.szDestFileName, filename, sizeof(pi.szDestFileName) - 1);
        strncpy(g_last_filename, filename, 512);
    } else {
        strncpy(pi.szSourceFileName, g_last_filename, 512);
    }

    int abort = 0;
    if (proc) {
        /* コールバック関数の場合は TRUE が継続、FALSE が中断 */
        BOOL ret = proc(hwnd, msg, (UINT)state, (LPVOID)&pi);
        if (!ret) {
            abort = 1;
        }
    } else if (hwnd != NULL) {
        /* メッセージ送信の場合は 0 が継続、非ゼロが中断 */
        LRESULT ret = SendMessageA(hwnd, msg, (WPARAM)state, (LPARAM)&pi);
        if (ret != 0) {
            abort = 1;
        }
    }

    if (abort) {
        g_lha_aborted = 1;
    }
    return abort;
}
