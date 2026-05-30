#if !defined(UNLHA64EX_H)
#define UNLHA64EX_H

/* UNLHA64EX.H - UnLha64 拡張進捗表示機能用の公開ヘッダ */

#include "UNLHA32.H"

/* 拡張された進捗通知メッセージ ID */
#define WM_ARCEXTRACT_EX "wm_arcextract_ex"

/* 拡張メッセージにおける状態 (_nState) の定義 */
#define ARCEXTRACT_EX_BEGIN      0   /* 該当ファイルの処理の開始 */
#define ARCEXTRACT_EX_INPROCESS  1   /* 該当ファイルの処理中 */
#define ARCEXTRACT_EX_END        2   /* 処理終了、関連メモリーを開放 */
#define ARCEXTRACT_EX_OPEN       3   /* 書庫全体の処理の開始 */
#define ARCEXTRACT_EX_CLOSE      4   /* 書庫全体の処理の終了 */

/* 全体の進捗状況を含む拡張構造体 */
#if defined(__BORLANDC__)
#pragma option -a-
#else
#pragma pack(1)
#endif

typedef struct {
    DWORD        dwStructSize;          /* 構造体のサイズ (互換性用) */
    ULHA_INT64   llFileSize;            /* 処理中ファイルの元のサイズ */
    ULHA_INT64   llWriteSize;           /* 処理中ファイルの書き込み(読み込み)サイズ */
    ULHA_INT64   llTotalBytes;          /* 処理対象の全ファイルの合計サイズ */
    ULHA_INT64   llTotalProcessed;      /* 処理済みの全ファイルの累積サイズ */
    DWORD        dwTotalFiles;          /* 処理対象の全ファイル数 */
    DWORD        dwFilesProcessed;      /* 処理済みのファイル数 */
    WCHAR        szSourceFileName[FNAME_MAX32 + 1]; /* 格納ファイル名 (Unicode) */
    WCHAR        szDestFileName[FNAME_MAX32 + 1];   /* 解凍先または圧縮元パス名 (Unicode) */
} EXTRACTINGINFO_TOTAL, *LPEXTRACTINGINFO_TOTAL;

#if !defined(__BORLANDC__)
#pragma pack()
#else
#pragma option -a.
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
    全体進捗の通知を有効化する新しいオーナーウィンドウ設定API。
    _bEnableTotalProgress = TRUE を指定すると、事前スキャンおよび "wm_arcextract_ex" による拡張通知が有効になります。
*/
BOOL
WINAPI _export
UnlhaSetOwnerWindowExTotal(
    const HWND      _hwnd,
    LPARCHIVERPROC  _lpArcProc,
    const BOOL      _bEnableTotalProgress
);

#ifdef __cplusplus
}
#endif

#endif /* UNLHA64EX_H */
