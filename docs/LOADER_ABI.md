# MUGEN Open Mod Specification v1

この文書と `include/mugen_mod.h` が規格v1です。仕様・SDK・参照実装はMITライセンスで利用、実装、改変、再配布できます。第三者のmodは独自のライセンスを選べます。ABI互換の他ローダーも実装できます。仕様の拡張を提案する際は既存ABIの意味を変更せず、新しいバージョンを定義してください。

## 1. ディレクトリと一覧

```text
MUGEN/
  winmugen.exe                 # 普段の起動exe。名前による制限はない
  winmugen.exe.mml-backup      # このパッチャーの復元用原本
  mugen-mod-loader.dll
  mods.txt                    # 有効modを列挙
  mugen-mod-loader.log
  mods/
    messagebox/
      mod.dll
    another_mod/
      mod.dll
      optional_dependency.dll
      assets/...
```

基準ディレクトリは起動exeの親ディレクトリです。作業ディレクトリには依存しません。一覧ファイルはUTF-8（BOM可）、LF/CRLF、最大1 MiB。mod IDはASCII英数字で始まり、ASCII英数字・`_`・`-`からなる1～64文字。Windowsのフォルダー名の制約も適用されます。1行1 IDで、空白・タブ・CRを両端から除去します。`;` または `#` 以降はコメントです。無効なIDは記録してスキップし、同じIDの重複は大文字小文字を区別せず初回のみ処理します。ただしDLLのロードやABI確認に失敗して登録されなかったIDは、後続行でも再試行されます。

`mods/<ID>/mod.dll` だけが対象であり、一覧にないフォルダーやDLLを自動走査しません。ネストしたID、絶対パス、`..` は不可です。ゲーム・modディレクトリのUnicodeパスはwide Win32 APIで扱います。

## 2. DLL ABI

- Windows x86（32ビット）、ネイティブPE DLL。
- C ABI、`__cdecl`。C++の名前修飾や`__stdcall`のサフィックスを公開名に付けない。
- 次の名前を正確にエクスポートする。

```c
uint32_t __cdecl MugenMod_GetAbiVersion(void);                /* 必ず 1 を返す */
int32_t  __cdecl MugenMod_Init(const MugenModHostV1 *host);    /* 成功は 0 */
```

エクスポート名は大小文字を区別します。`.def`ファイルの使用を推奨します。

```text
EXPORTS
    MugenMod_GetAbiVersion
    MugenMod_Init
```

`MugenMod_GetAbiVersion` は副作用のない問い合わせとして実装します。不一致・必須エクスポート欠落は初期化せずアンロードします。`DllMain`ではスレッド作成、他DLLのロード、待機、UI表示、ゲームの改変を行わず、`MugenMod_Init`へ移してください。DLLロード時点でDllMain自体はWindowsにより実行されるため、ABIの検査はDLLコードの実行前検査ではありません。

## 3. ホスト構造体

既定の構造体パッキング（8）、32ビットポインター、`wchar_t`はUTF-16の2バイトです。x86における `sizeof(MugenModHostV1)` は36バイト。

| フィールド | offset | 意味 |
|---|---:|---|
| `struct_size` | 0 | 36。modは読み出す前にサイズを確認する |
| `abi_version` | 4 | 1 |
| `phase` | 8 | 1: `MML_PHASE_BEFORE_ENGINE_START` |
| `engine_base` | 12 | exeの実際のロードベース。内部関数アドレスを意味しない |
| `game_directory` | 16 | exeディレクトリの絶対UTF-16パス、末尾区切りなし |
| `mod_directory` | 20 | このmodの絶対UTF-16ディレクトリ |
| `mod_id` | 24 | 一覧に記載されたID、NUL終端ASCII |
| `log` | 28 | `void __cdecl log(const char *mod_id, const char *message_utf8)` |
| `find_export` | 32 | `void * __cdecl find_export(const char *mod_id, const char *symbol)` |

ホスト構造体、文字列、コールバックはプロセス終了まで有効です。読み取り専用として扱います。ホスト所有メモリを解放しないでください。DLLのCRTやアロケーターの違いをまたいでメモリ解放・STLオブジェクト・例外を受け渡してはいけません。独自のmod間APIでも呼出規約、所有権、スレッド要件を文書化してください。

## 4. 初期化と依存関係

元のexe起動入口へ渡す直前、メインスレッド上で、一覧順に同期初期化します。WindowsのDLL初期化処理（loader lock）の外で呼び出します。MUGENのCRT、ウィンドウ、ゲーム状態はまだ初期化されていません。ここで必要な初期化を行い、通常は速やかに戻ってください。モーダルなMessageBoxはエンジンの起動を止め、閉じると再開します。

modは独自のワーカースレッドを作成できます。ただしゲーム状態へのアクセスは、そのmod自身が確認したタイミングで行う必要があります。v1には描画・戦闘・フレーム・正常終了のコールバックも、エンジン準備完了の通知もありません。

正常に初期化された先行modは `find_export` により参照できます。IDは大文字小文字を区別せず、symbolは `GetProcAddress` と同様に区別します。未登録・未初期化・失敗・symbol未公開ではNULL。初期化中の自分自身や後続modは参照できません。依存先は一覧で前に記載し、必須APIがない場合は非0を返してください。自動依存解決やロード順の並び替えはしません。

`log` と `find_export` はスレッドセーフです。これらのコールバック内でmodのコードを呼び出さないため、modの初期化・ログ呼出をまたいで内部ロックは保持されません。

`MugenMod_Init` の非0戻り値を記録し、後続modと本体起動を続けます。失敗したmodのDLLは、既に作成されたスレッドや保持された関数ポインターの不正化を防ぐためプロセス終了まで保持しますが、`find_export`からは公開しません。modは失敗時に自身の途中の変更を戻してください。DLLは通常の実行中にはアンロードしません。ネイティブ例外やアクセス違反に対するプロセス隔離はありません。

## 5. ロード範囲

mod.dllは絶対パスで `LoadLibraryExW` し、依存DLLはmod自身のディレクトリ、exeディレクトリ、System32から探索します。作業ディレクトリやPATHは依存DLLの探索に含めません。既にロードされた同名依存DLLはWindowsの規則が適用されるので、第三者ライブラリのDLL名は衝突を避けてください。Windows 8以降を対象とします。

exe内の起動トランポリンは、アプリケーションディレクトリにある `mugen-mod-loader.dll` を既存の `LoadLibraryA` でロードします。ローダーDLLを標準位置から移動しないでください。DLLとそのエントリーが見つからない場合、本体の元の入口へ戻ります。exeが `MessageBoxA` をインポートしていれば起動時のエラーも表示します。

## 6. エンジンと他modの変更

この規格はMUGEN本体の固定アドレス、構造体、関数の呼出規約を定義しません。その情報は不足しています。native modは自身のコードでWin32 APIやバイナリフックを利用できますが、対応本体、変更する領域、競合対策、初期化・実行・終了タイミングはmodごとに公開してください。共有APIにはバージョンと構造体サイズを用いてください。

最低限の適合性は「一覧に記載したDLLが、同じMUGENプロセス内で、v1ホストを受け取って初期化される」ことです。参照実装のmessagebox modはこれを実際の `MessageBoxW` で示します。
