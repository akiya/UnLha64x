# UnLha64x.dll

UnLha64x.dll は、Windows (x64) 環境で LZH 形式の書庫を操作するためのアーカイバライブラリです。  
**UNLHA32.DLL** の 64 ビット版互換(機能縮小版)ライブラリとして作成されています。  

コアとなる圧縮・展開エンジンには、最新の C 言語による LHa 実装である [LHa for UNIX with Autoconf](https://github.com/jca02266/lha) を使用しています。

## ！注意！

新規にlzh形式の圧縮ファイルを採用することは推奨されていません。  

このプログラムは過去の資産へのアクセス手段として使われることを想定しています。  

通常の利用ではzip形式など、他の圧縮形式を使用することを推奨します。

## 特徴
- **UNLHA32.DLL 互換**: 従来の 32 ビットアプリケーションで使用されていた UNLHA32.DLL の API 名、呼び出し規則、メッセージ仕様を踏襲しています。
- **x64 ネイティブ**: 64 ビットアプリケーションから直接ロードして使用可能です。
- **進捗通知対応**: `WM_ARCEXTRACT` メッセージおよびコールバック関数による進捗状況の取得に対応しています。
- **拡張全体進捗通知対応**: `UNLHA64EX.H` を使用することで、個別ファイルだけでなく書庫全体の合計サイズやファイル数に対する進捗状況を取得できる `UnlhaSetOwnerWindowExTotal` API をサポートしています。

## ダウンロード方法
https://github.com/akiya/UnLha64x  
GitHubのページの右側にある **Releases** セクションから最新版の `UnLha64x-(version).zip`をダウンロードしてください。

## 他のアプリからの利用方法

`UnLha64x.dll` を、対応アプリの指定するディレクトリにコピーしてください。  

詳細は対応アプリのドキュメントなどをご覧ください。  

## 変更履歴

### Ver.1.00
- 初版リリース


---

※ これより下は、主に対応アプリ開発者のための説明となります。

## 実装済み API

以下の API が実装されています（詳細は `UNLHA32.H` を参照してください）。

### 基本 API
- `Unlha` / `UnlhaW`: コマンドライン文字列によるアーカイブ操作 (Ansi / Unicode 版)
- `UnlhaGetVersion`: DLL のバージョン取得
- `UnlhaGetRunning`: 処理実行中かどうかの確認
- `UnlhaCheckArchive` / `UnlhaCheckArchiveW`: ファイルが正しい LZH アーカイブかチェック
- `UnlhaQueryFunctionList`: 実装済み機能の問い合わせ（スタブ）
- `UnlhaConfigDialog`: 設定ダイアログの表示（スタブ）

### ハンドルベース API (個別ファイル操作)
- `UnlhaOpenArchive` / `UnlhaOpenArchiveW`: アーカイブを開きハンドルを取得
- `UnlhaCloseArchive`: ハンドルを解放
- `UnlhaFindFirst` / `UnlhaFindFirstW`: アーカイブ内のファイル検索開始
- `UnlhaFindNext` / `UnlhaFindNextW`: 次のファイルを検索
- `UnlhaGetArcFileName` / `UnlhaGetArcFileNameW`: アーカイブファイル名の取得
- `UnlhaGetArcFileSize` / `Ex`: アーカイブファイルサイズの取得
- `UnlhaGetArcOriginalSize` / `Ex`: 展開後合計サイズの取得
- `UnlhaGetArcCompressedSize` / `Ex`: 圧縮後合計サイズの取得

### 通知設定 API
- `UnlhaSetOwnerWindow`: 通知先ウィンドウの設定
- `UnlhaClearOwnerWindow`: 通知先解除
- `UnlhaSetOwnerWindowEx`: 通知先ウィンドウとコールバック関数の設定
- `UnlhaKillOwnerWindowEx`: 通知先解除
- `UnlhaSetOwnerWindowExTotal`: 書庫全体の合計進捗通知（事前スキャン）を有効化した通知設定 (要 `UNLHA64EX.H` インクルード)


## 進捗状況の取得

`UnlhaSetOwnerWindow` または `UnlhaSetOwnerWindowEx` で設定したウィンドウまたはコールバック関数を通じて、処理の進捗を受け取ることができます。

- **メッセージ**: `RegisterWindowMessage("wm_arcextract")` で取得したメッセージ ID が送信されます。
- **構造体**: `EXTRACTINGINFOA` (またはその互換型) を通じて、処理中のファイル名、ファイルサイズ、書き込み済みサイズが通知されます。

### 拡張全体進捗通知 (UnLha64x 独自拡張)

`UnlhaSetOwnerWindowExTotal` で `_bEnableTotalProgress = TRUE` を指定した場合、処理前に自動的に事前スキャンが行われ、個別ファイルの進捗に加えて書庫全体の進捗情報が得られます。

- **メッセージ**: `RegisterWindowMessage("wm_arcextract_ex")` で取得したメッセージ ID が送信されます。
- **構造体**: `EXTRACTINGINFO_TOTAL` を通じて、処理中個別ファイル名やサイズのほか、処理対象の全ファイル合計サイズ (`llTotalBytes`)、累積処理済みサイズ (`llTotalProcessed`)、合計ファイル数 (`dwTotalFiles`)、処理済みファイル数 (`dwFilesProcessed`) などが通知されます。

※拡張全体進捗通知の仕様や各構造体メンバーの詳細については、[Header/UNLHA64EX.TXT](Header/UNLHA64EX.TXT) を参照してください。


## 仕様上の注意

### パスの相対化（セキュリティ対策）
セキュリティ上の理由（意図しないディレクトリへの書き込み防止）から、本ライブラリではアーカイブ内のファイルパスからドライブレターやルート指定を自動的に削除し、相対パスとして扱います。  
この対策により、ディレクトリトラバーサル脆弱性（JVN#68350834 / CVE-2026-41530 など）を防止しています。

- **展開・一覧表示時**:
  - ドライブレター（`C:` など）、先頭のルート記号（`\` や `/`）、およびUNCパスのルート（`\\server\share`）は自動的に削除されます。
  - 例: `\\server\share\dir\file.txt` -> `dir/file.txt` として扱われます。
- **圧縮時**:
  - デフォルトで上記と同様の相対化処理が行われます。
  - 絶対パスを維持して格納したい場合は、 `--enable-absolute-path` オプションを指定してください(非推奨)

### Unicode API と文字コード変換
本ライブラリは Unicode 版 API (`W` 系関数) をサポートしていますが、LZH ファイルのヘッダフォーマット仕様上、アーカイブ内部のファイル名は **Shift-JIS** として扱われます。

- **引数の変換**: `UnlhaW` 等の Unicode 版 API に渡された文字列 (UTF-16) は、DLL 内部で自動的に Shift-JIS (CP_ACP) に変換されてから処理されます。また、ファイル情報を取得する構造体 (`INDIVIDUALINFOW`) へは Shift-JIS から UTF-16 への変換が行われます。
- **代替文字によるフォールバック**: 圧縮対象のファイル名に Shift-JIS で表現できない Unicode 固有文字（ハングルや環境依存文字など）が含まれている場合、文字化けやファイル作成エラーを防ぐため、内部で該当文字を `_`（アンダースコア）に置換して処理・記録します。

### DLLとしての安全性（リエントラント性とエラーハンドリング）
オリジナルの LHa for UNIX はコマンドラインツールとして設計されていたため、グローバル変数の使用や、致命的なエラー時に `exit()` を呼び出してプロセスを強制終了する動作が含まれていました。  
本ライブラリでは、DLLとして組み込んで使用する際の安全性を確保するため、以下の改良を行っています。  

- **リエントラント性の確保**: API呼び出しごとに内部のグローバル変数状態を正しく初期化し、複数回・連続呼び出し時の不具合を防止しています。
- **ホストプロセスの保護**: 内部エラー発生時に `exit()` でホストアプリケーション（呼び出し元プロセス）を巻き込んでクラッシュさせないよう、適切なエラーハンドリングとリターンコードによる復帰に置き換えています。

## 付属サンプルツール

本プロジェクトには、ライブラリの使用例および動作検証用として以下のツールが同梱されています。

### UnLha64CLI
コマンドラインからアーカイブ操作を行うためのツールです。
- `Unlha` 関数を呼び出し、引数をそのままコマンドとして渡すシンプルな実装です。
- C++ からの動的な DLL ロードと関数呼び出しの最小限のサンプルとして利用できます。

### UnLha64Test
Win32 API を使用した GUI ベースの総合テストツールです。
- **解凍・圧縮タブ**: ドラッグ＆ドロップによる直感的な操作が可能です。
- **進捗通知の可視化**: 1ファイルごとの進捗に加え、拡張API `UnlhaSetOwnerWindowExTotal` と `wm_arcextract_ex` メッセージを利用して書庫全体の進捗状況も取得し、2本のプログレスバーでリアルタイムに表示します（全体進捗表示の実際の実装例となっています）。
- **Unicode API の利用例**: `UnlhaW` や `UnlhaOpenArchiveW` などの Unicode 版 API を使用した実装例となっています。

## コマンドラインオプション

`Unlha` 関数などで指定するコマンドライン文字列において、以下の変更・追加があります。

- **無視されるオプション**:
  - `-r`: 従来の LHA ではディレクトリを再帰的に処理するために使用されていましたが、本エンジンではデフォルトで再帰的に動作するため、互換性のためにエラーにはせず無視します。

## ライセンス

本プロジェクトは、以下のライセンスおよび著作権を尊重して配布・利用される必要があります。

1. **UNLHA32.DLL 互換インターフェース**:
   オリジナルの UNLHA32.DLL の作者である **Ｍｉｃｃｏ** 氏に著作権が帰属します。

2. **LHa 圧縮・展開エンジン**:
   [LHa for UNIX with Autoconf](https://github.com/jca02266/lha) を使用しています。
   このエンジンの利用にあたっては、オリジナルの LHa (by 吉崎栄泰氏) の再配布条件を遵守する必要があります（詳細は `LhaForUnix/man/lha.man` を参照）。
   - 著作権表示を削除しないこと。
   - 改変した場合はその旨を明記すること。
   - バイナリのみの再配布は原則として許可されていません（ソースコードの同梱が必要です）。
   - 他のプログラムに組み込む場合は、そのプログラムの名前を「LHa」にしてはいけません。
   - 商用利用には一定の制限（メイン機能としての利用禁止、サポート義務など）があります。

3. **UnLha64x 実装部**:
   本リポジトリに含まれるラッパー実装（`unlha64.cpp` 等）については、上記のライセンス条項に準じます。

## 制作者
ミコソフト / あきや  
micosoft / akiya  

``micosoft68k [at] gmail.com``  
``[at]``をアットマークに置き換えてください。  

https://github.com/akiya  
https://x.com/akiya193  

> micosoftの名称が **Ｍｉｃｃｏ** 氏の名前に似ておりますが、全くの偶然で一切関係ございません。 

## 開発環境
- Visual Studio 2026
- Antigravity

開発にAIを利用しています。  

## ビルド手順

1. Headerディレクトリに、[Ｍｉｃｃｏ氏のUNLHA32.DLL Ver 3.00](https://micco.blog/mysoft/unlha32.htm)の中にある `UNLHA32.H` をUTF-8に変換して配置します。
2. Visual Studio 2026 で `UnLha64.sln` を開きます。
3. ソリューションプラットフォームを `x64` に設定します。
4. プロジェクトをビルドして、`UnLha64x.dll` を生成します。

## ソースコード管理 (Lha for UNIX)

本プロジェクトのコアとなる LHa エンジン（`LhaForUnix` ディレクトリ以下）は、上流の [LHa for UNIX with Autoconf](https://github.com/jca02266/lha) リポジトリから `git subtree` を使用して統合・管理されています。
これにより、上流リポジトリの更新の取り込みや、Windows向け独自修正の維持を容易にしています。
