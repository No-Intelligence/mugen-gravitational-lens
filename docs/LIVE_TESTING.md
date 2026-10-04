# 隔離実機テスト

MUGEN本体やKFM素材は同梱しません。利用者が用意した対応WinMUGEN Plusへローダーv1を導入し、元のKFM（`chars/kfm/kfm.def`）を保持したゲームディレクトリを指定します。補助スクリプトはゲームを起動せず、コピーだけを作成します。

```powershell
.\tests\integration\prepare_lens_live.ps1 -GameDirectory 'C:\Games\WinMugenPlus' -Case max -OutputName lens-repro
```

出力は`tests/work/lens-repro/`です。既存出力は上書きしません。元ディレクトリは変更せず、隔離コピーのKFMから診断用キャラクターを作ります。補助スクリプトはオリジナルKFMの`st      = kfm.cns`という定義を前提とします。変更済みキャラクターへの追加CNS登録は手作業で調整してください。事前に既存の`Statedef -2`との競合を確認してください。

`max`は半径512、`layer`は半径180の境界テスト、`diagnostic`は提示前の全画面テストです。いずれも中心(320,240)等の検証用パラメーターです。

作成したコピーの中で次を実行し、少なくとも60秒測定します。

```powershell
Set-Location tests\work\lens-repro
.\winmugen.exe kfm-gravitational-lens-test/lens-test.def kfm -s stage0 -nosound
```

自分で開始したプロセスを停止し、`mods/gravitational-lens/`に保存されたRAW/JSON/CSVを新しい証跡フォルダーへコピーします。ローダーログも保存してください。`verification.enable`は検査用の二重ラスタライズを有効にするため、通常の配布・利用では置かないでください。

リポジトリのルートに戻り、Python 3で検査します。

```powershell
python tests\integration\verify_lens_evidence.py C:\Evidence\lens-repro
```

画素・順序・時間の検査は標準ライブラリだけで実行できます。PNGプレビューにPillowを使用します。スクリプトは証跡フォルダーへ集計結果を出力します。第三者素材を含むRAWやPNGはGitへコミットしないでください。
