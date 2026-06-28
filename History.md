# 更新履歴 (History)

## Ver. 1.03
- 互換性検証用に32ビット版もビルドできるようにしました。  
- UnlhaCheckArchive()で自己解凍ファイルなども正しく判定できるように修正。  
- UnlhaGetLastError() / UnlhaGetFileName / UnlhaGetOriginalSize / UnlhaGetDate / UnlhaGetTime / UnlhaGetAttribute などの互換APIを追加実装。  
- UnlhaGetAttribute() が LHA for UNIX 等で作成された書庫でも正しい属性（読み取り専用、ディレクトリ）を解釈して返すように修正。  
- 圧縮時に、元のファイル属性（読み取り専用）をヘッダの attribute フィールドに書き込むように対応。  
- 引数の連続指定(-d1n1のような)の対応と、レスポンスファイルに対応。  
- 圧縮時の基準ディレクトリ指定に対応。  
- その他、互換性を高める修正を行いました。  

## Ver. 1.02
- `UnlhaSetOwnerWindowEx()`、`UnlhaSetOwnerWindowExTotal()` で指定したコールバックの返り値の扱いが間違っていたのを修正。  

## Ver. 1.01
- 不足していた詳細な [スイッチ](Switches.md) を実装 (一部未対応/無視しているものもあります)。  
この変更により、一部デフォルトの動作が変わっている場合があるのでご注意ください。  

## Ver. 1.00
- 初版リリース
