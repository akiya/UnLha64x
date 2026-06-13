#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include "../../Header/UNLHA32.H"

#pragma comment(lib, "version.lib")


// DLL 関数の型定義
typedef int (WINAPI* UnlhaPtr)(HWND, LPCSTR, LPSTR, DWORD);
typedef WORD (WINAPI* UnlhaGetVersionPtr)(void);

/**
 * Windows コンソール向けの安全な出力ヘルパー
 * chcp 932 でも chcp 65001 でも、文字化けや文字消失なく日本語を出力する
 */
static void ConsolePrint(const std::string& str, bool isError = false, bool isUtf8 = true) {
    HANDLE hStd = GetStdHandle(isError ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD dwType = GetFileType(hStd);
    if (dwType == FILE_TYPE_CHAR) {
        // コンソールに直接出力する場合はワイド文字列に変換して WriteConsoleW を使用
        UINT cp = isUtf8 ? CP_UTF8 : CP_ACP;
        int size_needed = MultiByteToWideChar(cp, 0, str.c_str(), -1, NULL, 0);
        if (size_needed > 0) {
            std::wstring wstr(size_needed, 0);
            MultiByteToWideChar(cp, 0, str.c_str(), -1, &wstr[0], size_needed);
            // ヌル終端文字を除いた長さを出力
            DWORD written = 0;
            WriteConsoleW(hStd, wstr.c_str(), (DWORD)wcslen(wstr.c_str()), &written, NULL);
        }
    } else {
        // パイプやファイルにリダイレクトされている場合は生バイト列のまま出力
        if (isError) {
            std::cerr << str;
        } else {
            std::cout << str;
        }
    }
}

/**
 * 実行ファイル (UnLha64CLI.exe) のバージョン情報をリソースから取得する
 */
static WORD GetCliVersion() {
    wchar_t szPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, szPath, MAX_PATH) == 0) {
        return 100; // 取得できない場合のフォールバック値 (v1.00)
    }

    DWORD dwHandle = 0;
    DWORD dwSize = GetFileVersionInfoSizeW(szPath, &dwHandle);
    if (dwSize == 0) {
        return 100;
    }

    std::vector<BYTE> buffer(dwSize);
    if (!GetFileVersionInfoW(szPath, dwHandle, dwSize, buffer.data())) {
        return 100;
    }

    VS_FIXEDFILEINFO* pFileInfo = nullptr;
    UINT uLen = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", (LPVOID*)&pFileInfo, &uLen) || pFileInfo == nullptr) {
        return 100;
    }

    WORD major = HIWORD(pFileInfo->dwFileVersionMS);
    WORD minor = LOWORD(pFileInfo->dwFileVersionMS);

    return (major * 100) + minor;
}


/**
 * UnLha64CLI: UnLha64x.dll 動作確認用コマンドラインツール
 * 
 * 使い方: UnLha64CLI <コマンド> [オプション] <アーカイブ> [ファイル...]
 * 例: UnLha64CLI l archive.lzh
 */
int main(int argc, char* argv[]) {
    // 現在のコンソール出力コードページを取得
    UINT cp = GetConsoleOutputCP();
    bool isJapanese = (cp == 932 || cp == 65001); // Shift-JIS or UTF-8

    // DLL をロード
    HMODULE hLib = LoadLibraryA("UnLha64x.dll");
    if (!hLib) {
        // カレントディレクトリになければビルド出力先を探す
#ifdef _DEBUG
        hLib = LoadLibraryA("../bin/Debug/UnLha64x.dll");
#else
        hLib = LoadLibraryA("../bin/Release/UnLha64x.dll");
#endif
    }

    if (!hLib) {
        if (isJapanese) {
            ConsolePrint("エラー: UnLha64x.dll をロードできませんでした。\n", true);
            ConsolePrint("DLLがパスの通った場所、または実行ファイルと同じディレクトリにあるか確認してください。\n", true);
        } else {
            ConsolePrint("Error: Could not load UnLha64x.dll.\n", true);
        }
        return 1;
    }

    // 関数アドレスを取得
    auto pUnlha = (UnlhaPtr)GetProcAddress(hLib, "Unlha");
    auto pGetVersion = (UnlhaGetVersionPtr)GetProcAddress(hLib, "UnlhaGetVersion");

    if (!pUnlha || !pGetVersion) {
        if (isJapanese) {
            ConsolePrint("エラー: DLL 内に必要な関数 (Unlha, UnlhaGetVersion) が見つかりませんでした。\n", true);
        } else {
            ConsolePrint("Error: Required functions (Unlha, UnlhaGetVersion) not found in DLL.\n", true);
        }
        FreeLibrary(hLib);
        return 1;
    }

    // 引数がない場合はヘルプを表示
    if (argc < 2) {
        WORD dllVer = pGetVersion();
        WORD cliVer = GetCliVersion();
        char buf[256];
        if (isJapanese) {
            sprintf_s(buf, "UnLha64CLI version %d.%02d (using UnLha64x.dll v%d.%02d)\n", (cliVer / 100), (cliVer % 100), (dllVer / 100), (dllVer % 100));
            ConsolePrint(buf);
            ConsolePrint("使用法: UnLha64CLI <command> [options] <archive> [files...]\n");
            ConsolePrint("\n主要なコマンド:\n");
            ConsolePrint("  l <arc> : アーカイブ内のファイル一覧を表示\n");
            ConsolePrint("  e <arc> : アーカイブを展開\n");
            ConsolePrint("  x <arc> : ディレクトリ構造を維持して展開\n");
            ConsolePrint("  a <arc> <files...> : ファイルをアーカイブに追加/作成\n");
            ConsolePrint("  d <arc> <files...> : アーカイブからファイルを削除\n");
        } else {
            sprintf_s(buf, "UnLha64CLI version %d.%02d (using UnLha64x.dll v%d.%02d)\n", (cliVer / 100), (cliVer % 100), (dllVer / 100), (dllVer % 100));
            ConsolePrint(buf);
            ConsolePrint("Usage: UnLha64CLI <command> [options] <archive> [files...]\n");
            ConsolePrint("\nCommands:\n");
            ConsolePrint("  l <arc> : List files in archive\n");
            ConsolePrint("  e <arc> : Extract files\n");
            ConsolePrint("  x <arc> : Extract with paths\n");
            ConsolePrint("  a <arc> <files...> : Add/Create archive\n");
            ConsolePrint("  d <arc> <files...> : Delete files from archive\n");
        }
        
        FreeLibrary(hLib);
        return 0;
    }

    // コマンドライン引数を再構築 (DLL に渡すため)
    std::string commandLine;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        // スペースを含む引数はクォートで囲む
        if (arg.find(' ') != std::string::npos && arg.front() != '\"') {
            commandLine += "\"" + arg + "\"";
        } else {
            commandLine += arg;
        }
        if (i < argc - 1) {
            commandLine += " ";
        }
    }

    // DLL 実行
    char output[32768];
    memset(output, 0, sizeof(output));
    
    // hwnd=NULL でコンソールモード実行
    int result = pUnlha(NULL, commandLine.c_str(), output, sizeof(output));

    // 出力メッセージがあれば表示
    if (output[0] != '\0') {
        ConsolePrint(output, false, false);
        ConsolePrint("\n");
    }

    if (result != 0) {
        // 0 以外はエラーまたは警告
        char buf[256];
        if (isJapanese) {
            sprintf_s(buf, "結果コード: 0x%X\n", result);
            ConsolePrint(buf, true);
        } else {
            sprintf_s(buf, "Result: 0x%X\n", result);
            ConsolePrint(buf, true);
        }
    }

    FreeLibrary(hLib);
    return result;
}
