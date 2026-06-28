#include <windows.h>
#include "UNLHA32.H"
#include "UNLHA64EX.H"

#pragma comment(lib, "version.lib")
#include <stdio.h>
#include <stdarg.h>



// DLLのモジュールハンドルを保持するグローバル変数
static HMODULE g_hModule = NULL;


extern "C" {
#include "lha.h"
#include "prototypes.h"
}

#include <shellapi.h>
#include <ctype.h>
#include <algorithm>
#include <string>
#include <vector>
#include <setjmp.h>
#include <fcntl.h>
#include <io.h>
#include <crtdbg.h>

// CRTの無効パラメータハンドラ（アサーションダイアログを抑制する）
static void lha_invalid_parameter_handler(
    const wchar_t* expression, const wchar_t* function,
    const wchar_t* file, unsigned int line, uintptr_t pReserved) {
    // 何もしない - アサーションダイアログを抑制する
}

// DLLロード時にstdout/stderrをNULにリダイレクトする。
// GUI(WPF)アプリではstdout/stderrに有効なハンドルがないため、
// CRTの終了時フラッシュでアサーション失敗(Debug Assertion Failed)が発生する。
// NULデバイスに紐づけることで安全にフラッシュできるようにする。
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hModule = hModule; // DLLのモジュールハンドルを保存
        // CRTの無効パラメータハンドラを設定してアサーションを抑制
        _set_invalid_parameter_handler(lha_invalid_parameter_handler);
#ifdef _DEBUG
        // Debugビルドでのアサーションダイアログを無効化
        _CrtSetReportMode(_CRT_ASSERT, 0);
#endif

        // 標準出力・標準エラーが有効かチェックする
        // 有効なコンソール出力がない（WPF等のGUIアプリ）場合のみ、NULにリダイレクトする
        HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hStdOut == NULL || hStdOut == INVALID_HANDLE_VALUE || GetFileType(hStdOut) == FILE_TYPE_UNKNOWN) {
            FILE* fp = nullptr;
            freopen_s(&fp, "NUL", "w", stdout);
            fp = nullptr;
            freopen_s(&fp, "NUL", "w", stderr);
        }
    }
    return TRUE;
}

// 文字列変換ユーティリティ
static std::string WStringToString(const std::wstring& wstr, bool* pUsedDefaultChar = nullptr) {
    if (wstr.empty()) {
        if (pUsedDefaultChar) *pUsedDefaultChar = false;
        return std::string();
    }
    int size_needed = WideCharToMultiByte(932, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    BOOL usedDefaultChar = FALSE;
    char defaultChar = '_';
    WideCharToMultiByte(932, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, &defaultChar, &usedDefaultChar);
    if (pUsedDefaultChar) *pUsedDefaultChar = (usedDefaultChar == TRUE);
    return strTo;
}

static std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(932, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(932, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

static std::string ReadResponseFile(const std::string& path) {
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp) {
        return "";
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        fclose(fp);
        return "";
    }
    std::vector<unsigned char> buffer(size);
    size_t read_bytes = fread(&buffer[0], 1, size, fp);
    fclose(fp);
    if (read_bytes < (size_t)size) {
        size = (long)read_bytes;
    }

    // BOMのチェック
    if (size >= 2 && buffer[0] == 0xFF && buffer[1] == 0xFE) {
        // UTF-16LE
        std::wstring wstr(reinterpret_cast<wchar_t*>(&buffer[2]), (size - 2) / 2);
        return WStringToString(wstr);
    }
    if (size >= 3 && buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF) {
        // UTF-8 with BOM
        std::string utf8_str(reinterpret_cast<char*>(&buffer[3]), size - 3);
        int wsize = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, NULL, 0);
        std::wstring wstr(wsize, 0);
        MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, &wstr[0], wsize);
        return WStringToString(wstr);
    }

    std::string raw_str(reinterpret_cast<char*>(&buffer[0]), size);
    int wsize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw_str.c_str(), -1, NULL, 0);
    if (wsize > 0) {
        std::wstring wstr(wsize, 0);
        MultiByteToWideChar(CP_UTF8, 0, raw_str.c_str(), -1, &wstr[0], wsize);
        return WStringToString(wstr);
    }
    
    return raw_str;
}

static std::vector<std::string> TokenizeCommandLine(const std::string& cmdLine) {
    std::vector<std::string> args;
    std::string current;
    bool inQuote = false;
    for (size_t i = 0; i <= cmdLine.length(); ++i) {
        char c = (i < cmdLine.length()) ? cmdLine[i] : '\0';
        if (c == '\"') {
            inQuote = !inQuote;
        } else if ((c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0') && !inQuote) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    return args;
}


// グローバル状態
static bool g_running = false;
static int g_lha_exit_status = 0;
static int g_last_error_code = 0;

// LhaCoreのcallback.cで定義されているキャプチャ用グローバル変数への参照
extern "C" {
    extern volatile char* g_capture_buffer;
    extern volatile size_t g_capture_buffer_size;
    extern volatile size_t g_capture_buffer_written;
}


// ライブラリレベルの終了呼び出しを処理するためのジャンプバッファ
jmp_buf g_lha_exit_jmp_buf;
int g_lha_exit_jmp_enabled = 0;

std::wstring g_last_error;

extern "C" {
    void set_lha_error(const char* msg) {
        if (!msg) return;
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, msg, -1, NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, msg, -1, &wstrTo[0], size_needed);
        g_last_error = wstrTo;
    }

    void set_lha_error_invalid_char(const char* filename_sjis) {
        if (!filename_sjis) return;
        std::wstring nameW = StringToWString(filename_sjis);
		g_last_error = L"ファイル名にShift_JISに変換できない文字が含まれています: " + nameW;
	}
}

extern "C" void lha_exit_handler(int status) {
    /* テンポラリファイルなどのリソースをクリーンアップ */
    cleanup();
    g_lha_exit_status = status;
    if (g_lha_exit_jmp_enabled) {
        longjmp(g_lha_exit_jmp_buf, status == 0 ? -1 : status);
    }
}

// 進捗報告の状態
typedef BOOL (CALLBACK *LHA_ARCHIVERPROC)(HWND, UINT, UINT, LPVOID);
static HWND g_hwndOwner = NULL;
static UINT g_uMsgArcExtract = 0;
static LHA_ARCHIVERPROC g_lpArcProc = NULL;
static BOOL g_bEnableTotalProgress = FALSE;


extern "C" {
    HWND Lha_GetHwndOwner() { return g_hwndOwner; }
    UINT Lha_GetMsgArcExtract() { return g_uMsgArcExtract; }
    void Lha_SetMsgArcExtract(UINT u) { g_uMsgArcExtract = u; }
    LHA_ARCHIVERPROC Lha_GetArcProc() { return g_lpArcProc; }
    BOOL Lha_GetEnableTotalProgress() { return g_bEnableTotalProgress; }
}

extern "C" {

#undef Unlha
#undef UnlhaCheckArchive
#undef UnlhaConfigDialog
#undef UnlhaGetFileCount
#undef UnlhaOpenArchive
#undef UnlhaFindFirst
#undef UnlhaFindNext
#undef UnlhaGetArcFileName
#undef UnlhaSetOwnerWindow
#undef UnlhaClearOwnerWindow
#undef UnlhaSetOwnerWindowEx
#undef UnlhaKillOwnerWindowEx

WORD WINAPI UnlhaGetVersion() {
    if (g_hModule == NULL) {
        return 100; // ハンドルが取得できない場合のフォールバック値 (v1.00)
    }

    // DLLの絶対パスを取得する
    wchar_t szPath[MAX_PATH];
    if (GetModuleFileNameW(g_hModule, szPath, MAX_PATH) == 0) {
        return 100;
    }

    // バージョン情報のサイズ確認
    DWORD dwHandle = 0;
    DWORD dwSize = GetFileVersionInfoSizeW(szPath, &dwHandle);
    if (dwSize == 0) {
        return 100;
    }

    // バージョン情報データを取得
    std::vector<BYTE> buffer(dwSize);
    if (!GetFileVersionInfoW(szPath, dwHandle, dwSize, buffer.data())) {
        return 100;
    }

    // 固定情報（VS_FIXEDFILEINFO）のクエリ
    VS_FIXEDFILEINFO* pFileInfo = nullptr;
    UINT uLen = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", (LPVOID*)&pFileInfo, &uLen) || pFileInfo == nullptr) {
        return 100;
    }

    // dwFileVersionMS には上位にメジャーバージョン、下部にマイナーバージョンが格納されている
    // 例: バージョンが 1.14.0.0 の場合、major = 1, minor = 14 となり、114 が返る
    WORD major = HIWORD(pFileInfo->dwFileVersionMS);
    WORD minor = LOWORD(pFileInfo->dwFileVersionMS);

    return (major * 100) + minor;
}

BOOL WINAPI UnlhaGetRunning() {
    return g_running;
}

// PEファイルの物理サイズ（セクションデータの終端）を計算する
static DWORD GetExeSize(FILE* fp) {
    if (!fp) return 0;
    
    // ファイルサイズの取得
    _fseeki64(fp, 0, SEEK_END);
    __int64 fileSize = _ftelli64(fp);
    if (fileSize < sizeof(IMAGE_DOS_HEADER)) {
        return 0;
    }
    
    IMAGE_DOS_HEADER dosHeader;
    if (fseek(fp, 0, SEEK_SET) != 0) return 0;
    if (fread(&dosHeader, sizeof(dosHeader), 1, fp) != 1) return 0;
    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) return 0; // 'MZ'
    
    // e_lfanew が妥当かチェック
    if (dosHeader.e_lfanew < 0 || dosHeader.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) > (ULONGLONG)fileSize) {
        return 0;
    }
    
    // PEシグネチャのチェック
    DWORD peSignature;
    if (fseek(fp, dosHeader.e_lfanew, SEEK_SET) != 0) return 0;
    if (fread(&peSignature, sizeof(peSignature), 1, fp) != 1) return 0;
    if (peSignature != IMAGE_NT_SIGNATURE) return 0; // 'PE\0\0'
    
    // FileHeaderの読み込み
    IMAGE_FILE_HEADER fileHeader;
    if (fread(&fileHeader, sizeof(fileHeader), 1, fp) != 1) return 0;
    
    // セクションテーブルの開始位置
    // PEシグネチャ(4バイト) + IMAGE_FILE_HEADER(20バイト) + SizeOfOptionalHeader
    DWORD sectionOffset = dosHeader.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + fileHeader.SizeOfOptionalHeader;
    
    // セクションテーブル全体がファイルサイズに収まっているかチェック
    if (sectionOffset + (DWORD)fileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > (ULONGLONG)fileSize) {
        return 0;
    }
    
    DWORD exeSize = 0;
    for (WORD i = 0; i < fileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER sectionHeader;
        if (fseek(fp, sectionOffset + i * sizeof(IMAGE_SECTION_HEADER), SEEK_SET) != 0) return 0;
        if (fread(&sectionHeader, sizeof(sectionHeader), 1, fp) != 1) return 0;
        
        DWORD sectionEnd = sectionHeader.PointerToRawData + sectionHeader.SizeOfRawData;
        if (sectionEnd > exeSize) {
            exeSize = sectionEnd;
        }
    }
    
    return exeSize;
}

#define CHECK_HEADER_SIZE           0
#define CHECK_HEADER_CHECKSUM       1
#define CHECK_METHOD                2
#define CHECK_ATTRIBUTE             19
#define CHECK_HEADER_LEVEL          20

// fp の startOffset から最大 maxScanLength バイトの範囲で LZH ヘッダを探索する
static BOOL FindLhaHeaderLimit(FILE* fp, __int64 startOffset, DWORD maxScanLength) {
    if (fseeko(fp, startOffset, SEEK_SET) != 0) return FALSE;
    
    unsigned char buf[4096];
    __int64 currentOffset = startOffset;
    __int64 limitOffset = startOffset + maxScanLength;

    while (currentOffset < limitOffset) {
        __int64 remaining = limitOffset - currentOffset;
        size_t readSize = sizeof(buf);
        if ((__int64)readSize > remaining) {
            readSize = (size_t)remaining;
        }
        
        if (readSize < 24) {
            break;
        }

        if (fseeko(fp, currentOffset, SEEK_SET) != 0) {
            break;
        }

        size_t n = fread(buf, 1, readSize, fp);
        if (n < 24) {
            break;
        }

        for (size_t i = 0; i <= n - 24; ++i) {
            unsigned char* p = buf + i;
            if (p[CHECK_METHOD] == '-' &&
                (p[CHECK_METHOD + 1] == 'l' || p[CHECK_METHOD + 1] == 'p') &&
                p[CHECK_METHOD + 4] == '-') {

                // レベル0または1のヘッダ
                if ((p[CHECK_HEADER_LEVEL] == 0 || p[CHECK_HEADER_LEVEL] == 1)
                    && p[CHECK_HEADER_SIZE] > 20
                    && p[CHECK_HEADER_CHECKSUM] == calc_sum(p + 2, p[CHECK_HEADER_SIZE])) {
                    return TRUE;
                }

                // レベル2のヘッダ
                if (p[CHECK_HEADER_LEVEL] == 2
                    && p[CHECK_HEADER_SIZE] >= 24
                    && p[CHECK_ATTRIBUTE] == 0x20) {
                    return TRUE;
                }
            }
        }

        // 次の読み込み位置へ移動（境界のまたがりを考慮して重複して読み込む）
        currentOffset += (n - 24 + 1);
    }

    return FALSE;
}

BOOL WINAPI UnlhaCheckArchive(LPCSTR _szFileName, int _iMode) {
    if (!_szFileName) return FALSE;
    
    FILE* fp = fopen(_szFileName, "rb");
    if (!fp) return FALSE;

    // 展開処理と同一の基準（拡張子 .x や .exe、または中身が MZ）でSFXか判定する
    if (archive_is_msdos_sfx1((char*)_szFileName)) {
        // PE形式のEXEファイルか確認（PEヘッダサイズによるピンポイント走査のため）
        unsigned char sig[2];
        size_t sigRead = fread(sig, 1, 2, fp);
        if (sigRead == 2 && sig[0] == 0x4D && sig[1] == 0x5A) { // 'M', 'Z'
            DWORD exeSize = GetExeSize(fp);
            if (exeSize > 0) {
                // EXEサイズの直後から64KBの範囲を走査
                if (FindLhaHeaderLimit(fp, exeSize, 65536)) {
                    fclose(fp);
                    return TRUE;
                }
            }
        }
        // PE形式ではない場合（X68000の.xやCOMファイル）、またはPE解析失敗時は
        // フォールバックとして先頭から最大1MBの範囲を走査する
        if (FindLhaHeaderLimit(fp, 0, 1024 * 1024)) {
            fclose(fp);
            return TRUE;
        }
        fclose(fp);
        return FALSE;
    }

    // 通常のLZHファイルのチェック
    // 簡易的なチェックとして "-[lp]??-" を探す
    char buf[10];
    fseek(fp, 2, SEEK_SET); // 1バイト目（ヘッダサイズ）と2バイト目（チェックサム）をスキップ
    size_t n = fread(buf, 1, 5, fp);
    fclose(fp);

    if (n < 5) return FALSE;
    if (buf[0] == '-' && (buf[1] == 'l' || buf[1] == 'p') && buf[4] == '-') return TRUE;

    return FALSE;
}

BOOL WINAPI UnlhaCheckArchiveW(LPCWSTR _szFileName, int _iMode) {
    if (!_szFileName) return FALSE;
    std::string szFileNameA = WStringToString(_szFileName);
    return UnlhaCheckArchive(szFileNameA.c_str(), _iMode);
}

// 大文字小文字を無視して文字列比較するためのユーティリティ
static bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (tolower(a[i]) != tolower(b[i])) return false;
    }
    return true;
}

static bool HasWildcard(const std::string& path) {
    return path.find('*') != std::string::npos || path.find('?') != std::string::npos;
}

static void SplitPath(const std::string& fullPath, std::string& dir, std::string& pattern) {
    size_t lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash == std::string::npos) {
        dir = "";
        pattern = fullPath;
    } else {
        dir = fullPath.substr(0, lastSlash + 1);
        pattern = fullPath.substr(lastSlash + 1);
    }
}

static void GlobRecursive(const std::string& baseDir, const std::string& searchPattern, std::vector<std::string>& results) {
    std::string findPath = baseDir + searchPattern;
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(findPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                results.push_back(baseDir + fd.cFileName);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    std::string subDirFind = baseDir + "*";
    hFind = FindFirstFileA(subDirFind.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                    GlobRecursive(baseDir + fd.cFileName + "\\", searchPattern, results);
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

static void GlobSingle(const std::string& baseDir, const std::string& searchPattern, std::vector<std::string>& results) {
    std::string findPath = baseDir + searchPattern;
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(findPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                results.push_back(baseDir + fd.cFileName);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

int WINAPI Unlha(HWND _hwnd, LPCSTR _szCmdLine, LPSTR _szOutput, DWORD _dwSize) {
    if (!_szCmdLine) return -1;

    // キャプチャバッファを設定
    if (_szOutput && _dwSize > 0) {
        g_capture_buffer = _szOutput;
        g_capture_buffer_size = _dwSize;
        g_capture_buffer_written = 0;
        g_capture_buffer[0] = '\0';
    } else {
        g_capture_buffer = nullptr;
        g_capture_buffer_size = 0;
        g_capture_buffer_written = 0;
    }

    g_hwndOwner = _hwnd;
    g_running = true;
    g_last_error.clear();

    // コマンドラインをトークンに分解する（ダブルクォート考慮）
    std::vector<std::string> raw_tokens = TokenizeCommandLine(_szCmdLine);

    if (raw_tokens.empty()) {
        g_capture_buffer = nullptr;
        g_capture_buffer_size = 0;
        g_capture_buffer_written = 0;
        g_running = false;
        return -1;
    }

    // レスポンスファイル展開とレスポンス設定スイッチ(--)の処理
    std::vector<std::string> expanded_args;
    char response_char = '@';
    bool enable_response = true;
    bool enable_switch = true;

    for (size_t i = 0; i < raw_tokens.size(); ++i) {
        std::string arg = raw_tokens[i];
        if (arg.empty()) continue;

        // スイッチの判定（enable_switchが真の場合のみ）
        if (enable_switch && (arg[0] == '-' || arg[0] == '/')) {
            std::string sw = arg.substr(1);
            std::string sw_lower = sw;
            std::transform(sw_lower.begin(), sw_lower.end(), sw_lower.begin(), ::tolower);

            // -- で始まるスイッチ（またはスイッチ文字が / で /- で始まる場合）の処理
            if (sw_lower.find("-") == 0) {
                std::string val = sw_lower.substr(1); // "--"の後の部分
                if (val.empty() || val == "1") {
                    enable_response = false;
                } else if (val == "0") {
                    response_char = '@';
                    enable_response = true;
                    enable_switch = true;
                } else if (val == "2") {
                    enable_response = false;
                    enable_switch = false;
                } else if (val == "3") {
                    enable_switch = false;
                } else {
                    // --<文字> の場合（例：--!）
                    if (sw.length() >= 2) {
                        response_char = sw[1]; // 大文字小文字等を維持するため元の文字列から取得
                        enable_response = true;
                    }
                }
                continue; // レスポンス制御スイッチ自体は引数に含めない
            }
            expanded_args.push_back(arg);
        } else if (enable_response && arg[0] == response_char) {
            // レスポンスファイル展開
            std::string filepath = arg.substr(1);
            std::string file_content = ReadResponseFile(filepath);
            if (!file_content.empty()) {
                std::vector<std::string> file_args = TokenizeCommandLine(file_content);
                for (const auto& fa : file_args) {
                    expanded_args.push_back(fa);
                }
            }
        } else {
            expanded_args.push_back(arg);
        }
    }

    // 連続オプション指定の分解
    std::vector<std::string> raw_args;
    for (const std::string& arg : expanded_args) {
        if (arg.empty()) continue;

        if (enable_switch && (arg[0] == '-' || arg[0] == '/') && arg.length() >= 2) {
            char sw_char = arg[0];
            std::string content = arg.substr(1);
            size_t idx = 0;
            while (idx < content.length()) {
                char c = content[idx];
                std::string sw_name;

                // 2文字スイッチの判定
                if ((c == 'j' || c == 'g') && idx + 1 < content.length() && isalpha((unsigned char)content[idx + 1])) {
                    sw_name = content.substr(idx, 2);
                    idx += 2;
                } else {
                    sw_name = std::string(1, c);
                    idx += 1;
                }

                std::string sw_name_lower = sw_name;
                std::transform(sw_name_lower.begin(), sw_name_lower.end(), sw_name_lower.begin(), ::tolower);

                // 文字列を引数にとるスイッチの判定
                bool is_str_arg = false;
                if (sw_name_lower == "w" || sw_name_lower == "z") {
                    is_str_arg = true;
                } else if (sw_name_lower == "jx" || sw_name_lower == "gl" || sw_name_lower == "gb" ||
                           sw_name_lower == "gr" || sw_name_lower == "jz" || sw_name_lower == "jo" ||
                           sw_name_lower == "jtz" || sw_name_lower == "gyd" || sw_name_lower == "gye" ||
                           sw_name_lower == "gyt" || sw_name_lower == "gyw") {
                    is_str_arg = true;
                }

                if (is_str_arg) {
                    std::string val = content.substr(idx);
                    raw_args.push_back(std::string(1, sw_char) + sw_name + val);
                    break;
                } else {
                    std::string val = "";
                    while (idx < content.length()) {
                        char next_c = content[idx];
                        if (isalpha((unsigned char)next_c)) {
                            break;
                        }
                        val += next_c;
                        idx++;
                    }
                    raw_args.push_back(std::string(1, sw_char) + sw_name + val);
                }
            }
        } else {
            raw_args.push_back(arg);
        }
    }

    // パラメータ解析
    char cmd_char = '\0';
    std::vector<std::string> file_list;
    std::vector<std::string> raw_switches;
    std::string log_file_path = "";
    std::vector<std::string> exclude_patterns;

    // スイッチのデフォルト設定
    // UNLHA32.DLL の初期設定: -d0 -r0 -x0 -jm2
    bool is_d_specified = false;
    bool is_d_val = false;
    int recursive_mode = 0; // 0: -r0, 1: -r1, 2: -r2
    bool is_x_specified = false;
    bool is_x_val = false;
    int jm_val = 2; // -jm2
    bool is_y_val = false;

    // コマンド（命令）とスイッチ、ファイルリストの分類
    for (size_t i = 0; i < raw_args.size(); ++i) {
        std::string arg = raw_args[i];
        if (arg.empty()) continue;

        if (arg[0] == '-' || arg[0] == '/') {
            raw_switches.push_back(arg);
        } else {
            // スイッチでない引数
            if (cmd_char == '\0') {
                // 最初の非スイッチ引数が1文字（または2文字でオプション付きの可能性もあるが基本1文字）
                // コマンド文字であるかを判定
                if (arg.length() == 1 || (arg.length() == 2 && isalpha(arg[0]))) {
                    char c = tolower(arg[0]);
                    if (c == 'a' || c == 'c' || c == 'd' || c == 'e' || c == 'f' || c == 'j' || 
                        c == 'l' || c == 'm' || c == 'n' || c == 'p' || c == 's' || c == 't' || 
                        c == 'u' || c == 'v' || c == 'x') {
                        cmd_char = c;
                        continue;
                    }
                }
                // コマンド文字が省略されているとみなして、デフォルトの 'l' とする
                cmd_char = 'l';
            }
            file_list.push_back(arg);
        }
    }

    if (cmd_char == '\0') {
        cmd_char = 'l';
    }

    // スイッチの解析
    for (const std::string& sw : raw_switches) {
        if (sw.length() <= 1) continue;
        std::string s = sw.substr(1); // 先頭の '-' または '/' を除く
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);

        if (s.empty()) continue;

        // 非対応スイッチで即時エラーとするもの
        // j, jr, jw, gw, gj
        if (s[0] == 'j') {
            if (s.length() == 1) {
                g_capture_buffer = nullptr;
                g_capture_buffer_size = 0;
                g_capture_buffer_written = 0;
                g_running = false;
                return 87; // ERROR_INVALID_PARAMETER
            }
            std::string sub = s.substr(0, 2);
            if (sub == "w" || s.find("jw") == 0U || s.find("gw") == 0U || s.find("gj") == 0U || s.find("jr") == 0U) {
                g_capture_buffer = nullptr;
                g_capture_buffer_size = 0;
                g_capture_buffer_written = 0;
                g_running = false;
                return 87; // ERROR_INVALID_PARAMETER
            }
        }
        if (s.find("gw") == 0U) {
            g_capture_buffer = nullptr;
            g_capture_buffer_size = 0;
            g_capture_buffer_written = 0;
            g_running = false;
            return 87;
        }

        // 個別対応スイッチのパース
        if (s[0] == 'd') {
            is_d_specified = true;
            if (s.length() == 1 || s == "d1") {
                is_d_val = true;
            } else if (s == "d0") {
                is_d_val = false;
            }
        } else if (s[0] == 'r') {
            if (s == "r0") {
                recursive_mode = 0;
            } else if (s == "r1" || s == "r") {
                recursive_mode = 1;
            } else if (s == "r2") {
                recursive_mode = 2;
            }
        } else if (s[0] == 'x') {
            is_x_specified = true;
            if (s == "x0") {
                is_x_val = false;
            } else if (s == "x1" || s == "x") {
                is_x_val = true;
            }
        } else if (s.find("jm") == 0U) {
            if (s.length() >= 3 && isdigit(s[2])) {
                jm_val = s[2] - '0';
            }
        } else if (s.find("jx") == 0U) {
            std::string pat = sw.substr(3); // 元の文字列から大文字小文字を維持して切り出す
            if (!pat.empty()) {
                exclude_patterns.push_back(pat);
            }
        } else if (s.find("gl") == 0U) {
            log_file_path = sw.substr(3);
        } else if (s[0] == 'y') {
            is_y_val = true;
        }
    }

    // スイッチ設定の適用
    if (is_d_specified) {
        if (is_d_val) {
            recursive_mode = 2;
            is_x_val = true;
        } else {
            recursive_mode = 0;
            is_x_val = false;
        }
    }

    // ライブラリの初期化
    lha_init_variable();
    Lha_ResetAbort();
    g_infp = NULL;
    g_outfp = NULL;

    // LhaCore のグローバル変数に適用
    if (is_y_val) {
        force = TRUE;
    } else {
        force = FALSE;
    }

    if (cmd_char == 'e') {
        if (is_x_specified && is_x_val) {
            ignore_directory = FALSE;
        } else if (is_d_specified && is_d_val) {
            ignore_directory = FALSE;
        } else {
            ignore_directory = TRUE;
        }
    } else if (cmd_char == 'x') {
        if (is_x_specified && !is_x_val) {
            ignore_directory = TRUE;
        } else if (is_d_specified && !is_d_val) {
            ignore_directory = TRUE;
        } else {
            ignore_directory = FALSE;
        }
    }

    if (recursive_mode > 0) {
        recursive_archiving = TRUE;
    } else {
        recursive_archiving = FALSE;
    }

    if (!is_x_val) {
        generic_format = TRUE;
    } else {
        generic_format = FALSE;
    }

    switch (jm_val) {
        case 0: compress_method = LZHUFF0_METHOD_NUM; break;
        case 1: compress_method = LZHUFF1_METHOD_NUM; break;
        case 2: compress_method = LZHUFF5_METHOD_NUM; break;
        case 3: compress_method = LZHUFF6_METHOD_NUM; break;
        case 4: compress_method = LZHUFF7_METHOD_NUM; break;
        case 5: compress_method = LZHUFF2_METHOD_NUM; break;
        case 6: compress_method = LZHUFF3_METHOD_NUM; break;
        case 7: compress_method = LARC_METHOD_NUM; break;
        case 8: compress_method = LARC5_METHOD_NUM; break;
        default: compress_method = LZHUFF5_METHOD_NUM; break;
    }

    // 圧縮系コマンドの場合のみワイルドカードを展開する
    std::vector<std::string> expanded_file_list;
    bool is_compress_cmd = (cmd_char == 'a' || cmd_char == 'u' || cmd_char == 'm' || cmd_char == 'f' || cmd_char == 'c');

    if (file_list.size() > 0) {
        expanded_file_list.push_back(file_list[0]); // 書庫名は展開しない

        for (size_t i = 1; i < file_list.size(); ++i) {
            std::string path = file_list[i];
            if (is_compress_cmd && HasWildcard(path)) {
                std::string dir, pattern;
                SplitPath(path, dir, pattern);
                std::vector<std::string> matches;
                if (recursive_mode >= 1) {
                    GlobRecursive(dir, pattern, matches);
                } else {
                    GlobSingle(dir, pattern, matches);
                }
                if (matches.empty()) {
                    expanded_file_list.push_back(path);
                } else {
                    for (const std::string& m : matches) {
                        expanded_file_list.push_back(m);
                    }
                }
            } else {
                expanded_file_list.push_back(path);
            }
        }
    }

    // UNIXベースのライブラリ向けにパスを正規化する（バックスラッシュをスラッシュに変換）
    // また、引数リスト argv を再構成する
    std::vector<std::string> final_argv_strs;
    final_argv_strs.push_back("lha");
    final_argv_strs.push_back(std::string(1, cmd_char));

    // 除外パターンの追加 (-jx -> -x)
    for (const std::string& pat : exclude_patterns) {
        final_argv_strs.push_back("-x" + pat);
    }

    // 書庫名とファイルリストの追加
    for (const std::string& f : expanded_file_list) {
        final_argv_strs.push_back(f);
    }

    // UNLHA32.DLL 互換レイヤー: e/x/a/u/f/m/c archive [基準ディレクトリ/] [ファイル...]
    // アーカイブ名 (final_argv_strs[2 + exclude_patterns.size()]) の後の引数がディレクトリのように見えるかチェックし、
    // 展開先ディレクトリ、または圧縮時の基準ディレクトリとして扱う。
    char* compress_base_directory = nullptr;
    size_t archive_idx = 2 + exclude_patterns.size();
    if (final_argv_strs.size() >= archive_idx + 2) {
        if (cmd_char == 'e' || cmd_char == 'x' || cmd_char == 'a' || cmd_char == 'u' || cmd_char == 'f' || cmd_char == 'm' || cmd_char == 'c') {
            int dest_idx = -1;
            size_t i = archive_idx + 1;
            if (i < final_argv_strs.size()) {
                size_t len = final_argv_strs[i].length();
                if (len > 0U) {
                    char last_char = final_argv_strs[i][len - 1];
                    if (last_char == '/' || last_char == '\\' || last_char == ':') {
                        dest_idx = (int)i;
                    }
                }
            }
            if (dest_idx != -1) {
                if (cmd_char == 'e' || cmd_char == 'x') {
                    // 静的にメモリ確保された文字列に複製して LhaCore の extract_directory に設定
                    char* dest_dir = _strdup(final_argv_strs[dest_idx].c_str());
                    extract_directory = dest_dir;
                } else {
                    compress_base_directory = _strdup(final_argv_strs[dest_idx].c_str());
                }
                final_argv_strs.erase(final_argv_strs.begin() + dest_idx);
            }
        }
    }

    // 圧縮時の基準ディレクトリ指定がある場合、書庫名を絶対パスに変換する（移動先で作成されるのを防ぐため）
    if (compress_base_directory && final_argv_strs.size() > archive_idx) {
        char szFullPath[MAX_PATH];
        if (GetFullPathNameA(final_argv_strs[archive_idx].c_str(), MAX_PATH, szFullPath, NULL) != 0) {
            final_argv_strs[archive_idx] = szFullPath;
        }
    }

    // パスの区切り文字を正規化
    if (extract_directory) {
        char* p = extract_directory;
        while (*p) {
            if (IsDBCSLeadByte((BYTE)*p)) {
                if (*(p + 1)) p += 2;
                else p++;
                continue;
            }
            if (*p == '\\') *p = '/';
            p++;
        }
        size_t len = strlen(extract_directory);
        if (len > 0U && extract_directory[len - 1] == '/') {
            extract_directory[len - 1] = '\0';
        }
    }

    for (size_t i = 0; i < final_argv_strs.size(); ++i) {
        for (size_t j = 0; j < final_argv_strs[i].length(); ++j) {
            // SJISのマルチバイト文字に配慮しながらバックスラッシュをスラッシュに変換
            if (IsDBCSLeadByte((BYTE)final_argv_strs[i][j])) {
                if (j + 1U < final_argv_strs[i].length()) j++;
                continue;
            }
            if (final_argv_strs[i][j] == '\\') {
                final_argv_strs[i][j] = '/';
            }
        }
    }

    // argv 配列（Cスタイル）を構築
    std::vector<char*> argv;
    std::vector<char*> original_argv;
    for (const std::string& arg_str : final_argv_strs) {
        char* arg = _strdup(arg_str.c_str());
        argv.push_back(arg);
        original_argv.push_back(arg);
    }
    argv.push_back(NULL);
    int argc = (int)argv.size() - 1;
    char** argv_ptr = argv.data();

    // 圧縮時の基準ディレクトリへ一時的に移動
    char szOriginalDir[MAX_PATH] = "";
    bool dir_changed = false;
    if (compress_base_directory) {
        if (GetCurrentDirectoryA(MAX_PATH, szOriginalDir) != 0) {
            if (SetCurrentDirectoryA(compress_base_directory)) {
                dir_changed = true;
            }
        }
    }

    int result = 0;
    g_lha_exit_jmp_enabled = 1;
    if (setjmp(g_lha_exit_jmp_buf) == 0) {
        if (lha_parse_option(argc, argv_ptr) == 0) {
            result = lha_execute();
        } else {
            result = -1; // パースエラー
        }
    } else {
        // longjmp (lha_exit) からの復帰
        result = -g_lha_exit_status;
    }
    g_lha_exit_jmp_enabled = 0;

    // カレントディレクトリを復元
    if (dir_changed) {
        SetCurrentDirectoryA(szOriginalDir);
    }

    // extract_directory 用に複製した領域のクリーンアップ
    if (extract_directory) {
        free(extract_directory);
        extract_directory = nullptr;
    }

    // compress_base_directory 用に複製した領域のクリーンアップ
    if (compress_base_directory) {
        free(compress_base_directory);
        compress_base_directory = nullptr;
    }

    // 元のポインタを使用して安全にargvをクリーンアップする
    for (char* arg : original_argv) {
        if (arg) free(arg);
    }

    g_running = false;

    if (!g_last_error.empty() && _szOutput && _dwSize > 0) {
        int size_needed = WideCharToMultiByte(CP_ACP, 0, g_last_error.c_str(), -1, NULL, 0, NULL, NULL);
        if (size_needed > 0 && size_needed <= (int)_dwSize) {
            WideCharToMultiByte(CP_ACP, 0, g_last_error.c_str(), -1, _szOutput, size_needed, NULL, NULL);
        } else if (size_needed > (int)_dwSize) {
            std::string tempBuffer(size_needed, 0);
            WideCharToMultiByte(CP_ACP, 0, g_last_error.c_str(), -1, &tempBuffer[0], size_needed, NULL, NULL);
            strncpy(_szOutput, tempBuffer.c_str(), _dwSize - 1);
            _szOutput[_dwSize - 1] = '\0';
        }
    }

    // ログファイルの書き出し
    if (!log_file_path.empty() && _szOutput && _dwSize > 0) {
        FILE* fpLog = fopen(log_file_path.c_str(), "w");
        if (fpLog) {
            fputs(_szOutput, fpLog);
            fclose(fpLog);
        }
    }

    // 実行終了時にキャプチャバッファを解除
    g_capture_buffer = nullptr;
    g_capture_buffer_size = 0;
    g_capture_buffer_written = 0;

    g_last_error_code = result;
    return result;
}

int WINAPI UnlhaW(HWND _hwnd, LPCWSTR _szCmdLine, LPWSTR _szOutput, DWORD _dwSize) {
    if (!_szCmdLine) return -1;
    
    // 引数ごとにパースして、SJISに変換できない文字が含まれているかチェックする
    int argc;
    LPWSTR* argv = CommandLineToArgvW(_szCmdLine, &argc);
    if (argv != NULL) {
        std::wstring errorFile = L"";
        for (int i = 0; i < argc; ++i) {
            bool usedDefault = false;
            WStringToString(argv[i], &usedDefault);
            if (usedDefault) {
                // コマンド（a, e など）やオプション（-r など）は通常アスキーなので、ここで引っかかるのはファイル名のはず
                errorFile = argv[i];
                break;
            }
        }
        LocalFree(argv);
        
        if (!errorFile.empty()) {
            if (_szOutput && _dwSize > 0) {
                std::wstring errMsg = L"ファイル名にShift_JISに変換できない文字が含まれています: " + errorFile;
                wcsncpy(_szOutput, errMsg.c_str(), _dwSize - 1);
                _szOutput[_dwSize - 1] = L'\0';
            }
            return 87; // ERROR_INVALID_PARAMETER
        }
    }

    std::string szCmdLineA = WStringToString(_szCmdLine);

    g_last_error.clear();

    int result = Unlha(_hwnd, szCmdLineA.c_str(), NULL, 0);

    if (!g_last_error.empty() && _szOutput && _dwSize > 0) {
        wcsncpy(_szOutput, g_last_error.c_str(), _dwSize - 1);
        _szOutput[_dwSize - 1] = L'\0';
    }

    g_last_error_code = result;
    return result;
}

// その他のエクスポート関数のスタブ
int WINAPI UnlhaConfigDialog(HWND _hwnd, LPSTR _szOpt, int _iMode) { return 0; }
int WINAPI UnlhaGetFileCount(LPCSTR _szArcFile) { return -1; }
int WINAPI UnlhaQueryFunctionList(int _iFunction) { return 0; }

#include <new>

struct ArcHandleContext {
    union {
        wchar_t wpath[FNAME_MAX32 + 1];
        char apath[(FNAME_MAX32 + 1) * sizeof(wchar_t)];
    } sharedPath;

    std::string filename;
    ULHA_INT64 originalSizeTotal;
    ULHA_INT64 compressedSizeTotal;
    ULHA_INT64 archiveSize;
    std::vector<LzHeader> headers;
    size_t currentIndex;
    std::string searchPattern;

    ArcHandleContext() : originalSizeTotal(0), compressedSizeTotal(0), archiveSize(0), currentIndex(0) {
        memset(&sharedPath, 0, sizeof(sharedPath));
    }
};

class ArcHandleLock {
    HGLOBAL m_hMem;
    ArcHandleContext* m_ctx;

public:
    ArcHandleLock(HARC harc) : m_hMem((HGLOBAL)harc), m_ctx(NULL) {
        if (m_hMem) {
            UINT flags = GlobalFlags(m_hMem);
            if (flags != GMEM_INVALID_HANDLE) {
                m_ctx = (ArcHandleContext*)GlobalLock(m_hMem);
            }
        }
    }

    ~ArcHandleLock() {
        if (m_ctx && m_hMem) {
            GlobalUnlock(m_hMem);
        }
    }

    bool IsValid() const {
        return m_ctx != NULL;
    }

    ArcHandleContext* GetContext() const {
        return m_ctx;
    }

    ArcHandleContext* operator->() const {
        return m_ctx;
    }
};

static bool WildcardMatch(const char* pattern, const char* str) {
    if (!pattern || !str) return false;
    if (strcmp(pattern, "*.*") == 0 || strcmp(pattern, "*") == 0) return true;
    return fnmatch(pattern, str, FNM_PATHNAME | FNM_NOESCAPE | FNM_PERIOD) == 0;
}

// ハンドルベースAPIのスタブ
HARC WINAPI UnlhaOpenArchive(HWND _hwnd, LPCSTR _szFileName, DWORD _dwMode) {
    if (!_szFileName) {
        g_last_error_code = -1;
        return NULL;
    }
    FILE* fp = fopen(_szFileName, "rb");
    if (!fp) {
        g_last_error_code = -1;
        return NULL;
    }

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(ArcHandleContext));
    if (!hMem) {
        fclose(fp);
        g_last_error_code = -1;
        return NULL;
    }

    void* ptr = GlobalLock(hMem);
    if (!ptr) {
        GlobalFree(hMem);
        fclose(fp);
        g_last_error_code = -1;
        return NULL;
    }

    ArcHandleContext* ctx = ::new (ptr) ArcHandleContext();
    ctx->filename = _szFileName;
    
    // ANSIファイル名を先頭部分に格納
    strncpy(ctx->sharedPath.apath, _szFileName, FNAME_MAX32);
    ctx->sharedPath.apath[FNAME_MAX32] = '\0';
    
    _fseeki64(fp, 0, SEEK_END);
    ctx->archiveSize = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);

    if (archive_is_msdos_sfx1((char*)_szFileName)) {
        seek_lha_header(fp);
    }

    LzHeader hdr;
    while (get_header(fp, &hdr)) {
        ctx->headers.push_back(hdr);
        // ※ originalSizeTotal, compressedSizeTotal は FindFirst/FindNext 時に計算するため、ここでは加算しません
        _fseeki64(fp, hdr.packed_size, SEEK_CUR);
    }
    fclose(fp);

    if (ctx->headers.empty()) {
        ctx->~ArcHandleContext();
        GlobalUnlock(hMem);
        GlobalFree(hMem);
        g_last_error_code = -1;
        return NULL;
    }
    GlobalUnlock(hMem);
    g_last_error_code = 0;
    return (HARC)hMem;
}

HARC WINAPI UnlhaOpenArchiveW(HWND _hwnd, LPCWSTR _szFileName, DWORD _dwMode) {
    if (!_szFileName) {
        g_last_error_code = -1;
        return NULL;
    }
    std::string szFileNameA = WStringToString(_szFileName);
    HARC ret = UnlhaOpenArchive(_hwnd, szFileNameA.c_str(), _dwMode);
    if (ret) {
        ArcHandleContext* ctx = (ArcHandleContext*)GlobalLock((HGLOBAL)ret);
        if (ctx) {
            wcsncpy(ctx->sharedPath.wpath, _szFileName, FNAME_MAX32);
            ctx->sharedPath.wpath[FNAME_MAX32] = L'\0';
            GlobalUnlock((HGLOBAL)ret);
        }
    }
    return ret;
}


int WINAPI UnlhaCloseArchive(HARC _harc) { 
    if (!_harc) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return -1;
    }
    HGLOBAL hMem = (HGLOBAL)_harc;
    UINT flags = GlobalFlags(hMem);
    if (flags == GMEM_INVALID_HANDLE) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return -1;
    }

    ArcHandleContext* ctx = (ArcHandleContext*)GlobalLock(hMem);
    if (ctx) {
        ctx->~ArcHandleContext();
        GlobalUnlock(hMem);
    }
    GlobalFree(hMem);
    g_last_error_code = 0;
    return 0; 
}
static void MapHeaderToInfo(const LzHeader& hdr, INDIVIDUALINFO* _lpSearch) {
    if (!_lpSearch) return;
    memset(_lpSearch, 0, sizeof(INDIVIDUALINFO));
    _lpSearch->dwOriginalSize = (DWORD)hdr.original_size;
    _lpSearch->dwCompressedSize = (DWORD)hdr.packed_size;
    _lpSearch->dwCRC = hdr.crc;
    _lpSearch->uOSType = hdr.extend_type;
    if (hdr.original_size > 0) {
        _lpSearch->wRatio = (WORD)((hdr.packed_size * 1000) / hdr.original_size);
    }
    
    // ファイル名をコピー
    strncpy(_lpSearch->szFileName, hdr.name, sizeof(_lpSearch->szFileName) - 1);
    
    // 日付/時刻の変換（簡易版）
    struct tm* t = localtime(&hdr.unix_last_modified_stamp);
    if (t) {
        _lpSearch->wDate = ((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday;
        _lpSearch->wTime = (t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2);
    }
}

static void MapHeaderToInfoW(const LzHeader& hdr, INDIVIDUALINFOW* _lpSearch) {
    if (!_lpSearch) return;
    memset(_lpSearch, 0, sizeof(INDIVIDUALINFOW));
    _lpSearch->dwOriginalSize = (DWORD)hdr.original_size;
    _lpSearch->dwCompressedSize = (DWORD)hdr.packed_size;
    _lpSearch->dwCRC = hdr.crc;
    _lpSearch->uOSType = hdr.extend_type;
    if (hdr.original_size > 0) {
        _lpSearch->wRatio = (WORD)((hdr.packed_size * 1000) / hdr.original_size);
    }
    
    // ファイル名をコピー（SJISからUTF-16へ変換）
    std::wstring wname = StringToWString(hdr.name);
    wcsncpy(_lpSearch->szFileName, wname.c_str(), sizeof(_lpSearch->szFileName) / sizeof(wchar_t) - 1);
    
    struct tm* t = localtime(&hdr.unix_last_modified_stamp);
    if (t) {
        _lpSearch->wDate = ((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday;
        _lpSearch->wTime = (t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2);
    }
}


int WINAPI UnlhaFindFirst(HARC _harc, LPCSTR _szWildCard, INDIVIDUALINFO* _lpSearch) {
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return ERROR_HARC_ISNOT_OPENED;
    }
    ArcHandleContext* ctx = lock.GetContext();
    ctx->searchPattern = _szWildCard ? _szWildCard : "*";
    
    // ダブルクォーテーションで括られていた場合にそれを取り除く
    if (ctx->searchPattern.size() >= 2 && ctx->searchPattern.front() == '"' && ctx->searchPattern.back() == '"') {
        ctx->searchPattern = ctx->searchPattern.substr(1, ctx->searchPattern.size() - 2);
    }
    
    // ワイルドカード内のパス区切り文字を正規化（LhaForUnix内部はスラッシュを使用するため）
    for (char& c : ctx->searchPattern) {
        if (c == '\\') c = '/';
    }
    
    ctx->currentIndex = 0;
    ctx->originalSizeTotal = 0;
    ctx->compressedSizeTotal = 0;
    
    for (; ctx->currentIndex < ctx->headers.size(); ++ctx->currentIndex) {
        if (WildcardMatch(ctx->searchPattern.c_str(), ctx->headers[ctx->currentIndex].name)) {
            if (_lpSearch) {
                MapHeaderToInfo(ctx->headers[ctx->currentIndex], _lpSearch);
            }
            ctx->originalSizeTotal += ctx->headers[ctx->currentIndex].original_size;
            ctx->compressedSizeTotal += ctx->headers[ctx->currentIndex].packed_size;
            ctx->currentIndex++;
            g_last_error_code = 0;
            return 0;
        }
    }
    g_last_error_code = -1;
    return -1;
}

int WINAPI UnlhaFindFirstW(HARC _harc, LPCWSTR _szWildCard, INDIVIDUALINFOW* _lpSearch) {
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return ERROR_HARC_ISNOT_OPENED;
    }
    ArcHandleContext* ctx = lock.GetContext();
    ctx->searchPattern = _szWildCard ? WStringToString(_szWildCard) : "*";
    
    // ダブルクォーテーションで括られていた場合にそれを取り除く
    if (ctx->searchPattern.size() >= 2 && ctx->searchPattern.front() == '"' && ctx->searchPattern.back() == '"') {
        ctx->searchPattern = ctx->searchPattern.substr(1, ctx->searchPattern.size() - 2);
    }
    
    // ワイルドカード内のパス区切り文字を正規化（LhaForUnix内部はスラッシュを使用するため）
    for (char& c : ctx->searchPattern) {
        if (c == '\\') c = '/';
    }
    ctx->currentIndex = 0;
    ctx->originalSizeTotal = 0;
    ctx->compressedSizeTotal = 0;
    
    for (; ctx->currentIndex < ctx->headers.size(); ++ctx->currentIndex) {
        if (WildcardMatch(ctx->searchPattern.c_str(), ctx->headers[ctx->currentIndex].name)) {
            if (_lpSearch) {
                MapHeaderToInfoW(ctx->headers[ctx->currentIndex], _lpSearch);
            }
            ctx->originalSizeTotal += ctx->headers[ctx->currentIndex].original_size;
            ctx->compressedSizeTotal += ctx->headers[ctx->currentIndex].packed_size;
            ctx->currentIndex++;
            g_last_error_code = 0;
            return 0;
        }
    }
    g_last_error_code = -1;
    return -1;
}

int WINAPI UnlhaFindNext(HARC _harc, INDIVIDUALINFO* _lpSearch) {
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return ERROR_HARC_ISNOT_OPENED;
    }
    ArcHandleContext* ctx = lock.GetContext();
    
    for (; ctx->currentIndex < ctx->headers.size(); ++ctx->currentIndex) {
        if (WildcardMatch(ctx->searchPattern.c_str(), ctx->headers[ctx->currentIndex].name)) {
            if (_lpSearch) {
                MapHeaderToInfo(ctx->headers[ctx->currentIndex], _lpSearch);
            }
            ctx->originalSizeTotal += ctx->headers[ctx->currentIndex].original_size;
            ctx->compressedSizeTotal += ctx->headers[ctx->currentIndex].packed_size;
            ctx->currentIndex++;
            g_last_error_code = 0;
            return 0;
        }
    }
    g_last_error_code = -1;
    return -1;
}

int WINAPI UnlhaFindNextW(HARC _harc, INDIVIDUALINFOW* _lpSearch) {
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return ERROR_HARC_ISNOT_OPENED;
    }
    ArcHandleContext* ctx = lock.GetContext();
    
    for (; ctx->currentIndex < ctx->headers.size(); ++ctx->currentIndex) {
        if (WildcardMatch(ctx->searchPattern.c_str(), ctx->headers[ctx->currentIndex].name)) {
            if (_lpSearch) {
                MapHeaderToInfoW(ctx->headers[ctx->currentIndex], _lpSearch);
            }
            ctx->originalSizeTotal += ctx->headers[ctx->currentIndex].original_size;
            ctx->compressedSizeTotal += ctx->headers[ctx->currentIndex].packed_size;
            ctx->currentIndex++;
            g_last_error_code = 0;
            return 0;
        }
    }
    g_last_error_code = -1;
    return -1;
}

int WINAPI UnlhaGetArcFileName(HARC _harc, LPSTR _szFileName, int _nSize) { 
    if (!_szFileName || _nSize <= 0) return -1;
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return -1;
    }
    ArcHandleContext* ctx = lock.GetContext();
    strncpy(_szFileName, ctx->filename.c_str(), _nSize - 1);
    _szFileName[_nSize - 1] = '\0';
    return 0; 
}

int WINAPI UnlhaGetArcFileNameW(HARC _harc, LPWSTR _szFileName, int _nSize) { 
    if (!_szFileName || _nSize <= 0) return -1;
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return -1;
    }
    ArcHandleContext* ctx = lock.GetContext();
    std::wstring wname = StringToWString(ctx->filename);
    wcsncpy(_szFileName, wname.c_str(), _nSize - 1);
    _szFileName[_nSize - 1] = L'\0';
    return 0; 
}

DWORD WINAPI UnlhaGetArcFileSize(HARC _harc) { 
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return 0;
    }
    return (DWORD)lock->archiveSize;
}

BOOL WINAPI UnlhaGetArcFileSizeEx(HARC _harc, ULHA_INT64* _pllSize) { 
    if (!_pllSize) return FALSE;
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return FALSE;
    }
    *_pllSize = lock->archiveSize;
    return TRUE;
}

DWORD WINAPI UnlhaGetArcOriginalSize(HARC _harc) { 
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return 0;
    }
    return (DWORD)lock->originalSizeTotal;
}

BOOL WINAPI UnlhaGetArcOriginalSizeEx(HARC _harc, ULHA_INT64* _pllSize) { 
    if (!_pllSize) return FALSE;
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return FALSE;
    }
    *_pllSize = lock->originalSizeTotal;
    return TRUE;
}

DWORD WINAPI UnlhaGetArcCompressedSize(HARC _harc) { 
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return 0;
    }
    return (DWORD)lock->compressedSizeTotal;
}

BOOL WINAPI UnlhaGetArcCompressedSizeEx(HARC _harc, ULHA_INT64* _pllSize) { 
    if (!_pllSize) return FALSE;
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return FALSE;
    }
    *_pllSize = lock->compressedSizeTotal;
    return TRUE;
}

BOOL WINAPI UnlhaSetOwnerWindow(HWND _hwnd) {
    g_hwndOwner = _hwnd;
    g_lpArcProc = NULL;
    g_bEnableTotalProgress = FALSE;
    return TRUE;
}

BOOL WINAPI UnlhaClearOwnerWindow() {
    g_hwndOwner = NULL;
    g_lpArcProc = NULL;
    g_bEnableTotalProgress = FALSE;
    return TRUE;
}

BOOL WINAPI UnlhaSetOwnerWindowEx(HWND _hwnd, LPARCHIVERPROC _lpArcProcArg) {
    g_hwndOwner = _hwnd;
    g_lpArcProc = (LHA_ARCHIVERPROC)_lpArcProcArg;
    g_bEnableTotalProgress = FALSE;
    return TRUE;
}

BOOL WINAPI UnlhaKillOwnerWindowEx(HWND _hwnd) {
    if (g_hwndOwner == _hwnd) {
        g_hwndOwner = NULL;
        g_lpArcProc = NULL;
        g_bEnableTotalProgress = FALSE;
    }
    return TRUE;
}

BOOL WINAPI UnlhaSetOwnerWindowExTotal(HWND _hwnd, LPARCHIVERPROC _lpArcProcArg, BOOL _bEnableTotalProgressArg) {
    g_hwndOwner = _hwnd;
    g_lpArcProc = (LHA_ARCHIVERPROC)_lpArcProcArg;
    g_bEnableTotalProgress = _bEnableTotalProgressArg;
    return TRUE;
}

// Windows環境において、ディレクトリやファイルのタイムスタンプを設定するヘルパー関数
// (Cコードから呼び出すためのCリンケージ)
int win32_set_file_time(const char* name, time_t t) {
    ULARGE_INTEGER ull;
    ull.QuadPart = ((ULONGLONG)t * 10000000ULL) + 116444736000000000ULL;
    
    FILETIME ft;
    ft.dwLowDateTime = ull.LowPart;
    ft.dwHighDateTime = ull.HighPart;

    // ディレクトリやファイルを開く (FILE_FLAG_BACKUP_SEMANTICS が必要)
    HANDLE hFile = CreateFileA(name, 
                               GENERIC_WRITE, 
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                               NULL, 
                               OPEN_EXISTING, 
                               FILE_FLAG_BACKUP_SEMANTICS, 
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return 0; // 失敗
    }

    BOOL result = SetFileTime(hFile, NULL, &ft, &ft);
    CloseHandle(hFile);
    return result ? 1 : 0;
}

int WINAPI UnlhaGetFileNameA(HARC _harc, LPSTR _lpBuffer, const int _nSize) {
    if (!_lpBuffer || _nSize <= 0) {
        g_last_error_code = -1;
        return -1;
    }
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return -1;
    }
    ArcHandleContext* ctx = lock.GetContext();
    if (ctx->currentIndex == 0 || ctx->currentIndex > ctx->headers.size()) {
        g_last_error_code = -1;
        return -1;
    }
    const LzHeader& hdr = ctx->headers[ctx->currentIndex - 1];
    std::string sName = WStringToString(StringToWString(hdr.name));
    strncpy(_lpBuffer, sName.c_str(), _nSize - 1);
    _lpBuffer[_nSize - 1] = '\0';
    g_last_error_code = 0;
    return 0;
}

int WINAPI UnlhaGetFileNameW(HARC _harc, LPWSTR _lpBuffer, const int _nSize) {
    if (!_lpBuffer || _nSize <= 0) {
        g_last_error_code = -1;
        return -1;
    }
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return -1;
    }
    ArcHandleContext* ctx = lock.GetContext();
    if (ctx->currentIndex == 0 || ctx->currentIndex > ctx->headers.size()) {
        g_last_error_code = -1;
        return -1;
    }
    const LzHeader& hdr = ctx->headers[ctx->currentIndex - 1];
    std::wstring wName = StringToWString(hdr.name);
    wcsncpy(_lpBuffer, wName.c_str(), _nSize - 1);
    _lpBuffer[_nSize - 1] = L'\0';
    g_last_error_code = 0;
    return 0;
}

DWORD WINAPI UnlhaGetOriginalSize(HARC _harc) {
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return 0;
    }
    ArcHandleContext* ctx = lock.GetContext();
    if (ctx->currentIndex == 0 || ctx->currentIndex > ctx->headers.size()) {
        g_last_error_code = -1;
        return 0;
    }
    g_last_error_code = 0;
    return (DWORD)ctx->headers[ctx->currentIndex - 1].original_size;
}

BOOL WINAPI UnlhaGetOriginalSizeEx(HARC _harc, ULHA_INT64 *_lpllSize) {
    if (!_lpllSize) {
        g_last_error_code = -1;
        return FALSE;
    }
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return FALSE;
    }
    ArcHandleContext* ctx = lock.GetContext();
    if (ctx->currentIndex == 0 || ctx->currentIndex > ctx->headers.size()) {
        g_last_error_code = -1;
        return FALSE;
    }
    *_lpllSize = ctx->headers[ctx->currentIndex - 1].original_size;
    g_last_error_code = 0;
    return TRUE;
}

WORD WINAPI UnlhaGetDate(HARC _harc) {
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return 0;
    }
    ArcHandleContext* ctx = lock.GetContext();
    if (ctx->currentIndex == 0 || ctx->currentIndex > ctx->headers.size()) {
        g_last_error_code = -1;
        return 0;
    }
    const LzHeader& hdr = ctx->headers[ctx->currentIndex - 1];
    struct tm* t = localtime(&hdr.unix_last_modified_stamp);
    WORD wDate = 0;
    if (t) {
        wDate = ((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday;
    }
    g_last_error_code = 0;
    return wDate;
}

WORD WINAPI UnlhaGetTime(HARC _harc) {
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return 0;
    }
    ArcHandleContext* ctx = lock.GetContext();
    if (ctx->currentIndex == 0 || ctx->currentIndex > ctx->headers.size()) {
        g_last_error_code = -1;
        return 0;
    }
    const LzHeader& hdr = ctx->headers[ctx->currentIndex - 1];
    struct tm* t = localtime(&hdr.unix_last_modified_stamp);
    WORD wTime = 0;
    if (t) {
        wTime = (t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2);
    }
    g_last_error_code = 0;
    return wTime;
}

int WINAPI UnlhaGetAttribute(HARC _harc) {
    ArcHandleLock lock(_harc);
    if (!lock.IsValid()) {
        g_last_error_code = ERROR_HARC_ISNOT_OPENED;
        return 0;
    }
    ArcHandleContext* ctx = lock.GetContext();
    if (ctx->currentIndex == 0 || ctx->currentIndex > ctx->headers.size()) {
        g_last_error_code = -1;
        return 0;
    }
    const LzHeader& hdr = ctx->headers[ctx->currentIndex - 1];
    
    int attr = 0;
    bool isDir = false;

    // 1. ディレクトリ判定 (ファイル名末尾またはUNIX mode)
    size_t len = strlen(hdr.name);
    if (len > 0 && (hdr.name[len - 1] == '/' || hdr.name[len - 1] == '\\')) {
        isDir = true;
    }
    if (hdr.extend_type == 0x55) { // EXTEND_UNIX ('U')
        if ((hdr.unix_mode & 0xF000) == 0x4000) { // S_IFDIR
            isDir = true;
        }
    }

    if (isDir) {
        attr = 0x30; // FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_ARCHIVE
    } else {
        attr = 0x20; // FILE_ATTRIBUTE_ARCHIVE
        
        // 2. 読込専用判定
        if (hdr.extend_type == 0x55) { // UNIX
            if ((hdr.unix_mode & 0200) == 0) { // 所有者の書き込み権限が無い
                attr |= 0x01; // FILE_ATTRIBUTE_READONLY
            }
        } else { // MS-DOS等、あるいは通常
            if (hdr.attribute & 0x01) { // FA_RDONLY
                attr |= 0x01;
            }
        }
    }

    g_last_error_code = 0;
    return attr;
}

int WINAPI UnlhaGetLastError(LPDWORD _lpdwSystemError) {
    if (_lpdwSystemError) {
        *_lpdwSystemError = ::GetLastError();
    }
    return g_last_error_code;
}

} // extern "C"

