# ビルドと配布

Windows 10以降、Visual Studioの「C++によるデスクトップ開発」が必要です。C++17、x86、静的CRT（/MT）でコンパイルします。`scripts/build.cmd`はvswhereでVisual Studioを探します。既に開いた開発者プロンプトがx64の場合は、x86 Native Toolsプロンプトを開き直してください。

リポジトリのルートで次を実行します。

```bat
scripts\build.cmd
build\lens-math-test.exe
```

| 出力 | 用途 |
|---|---|
| `build/mod.dll` | WinMUGEN描画アダプター |
| `build/mugen-lens-math.dll` | 参照座標計算DLL |
| `build/lens-math-test.exe` | 数学DLL単体検査 |

テストは1/2/3/4 bytes/pixel、全画素、行パディング、ガード領域、無効引数、強度0、画面端、中心移動を検査します。テストの計算時間はMUGEN全体のFPSとは異なります。

配布ZIPはビルドと単体テストの後で生成します。

```powershell
.\scripts\package.ps1
```

`dist/MUGEN-Gravitational-Lens.zip`に2 DLL、仕様書、SDK、CNS例、LICENSE、ファイルごとのSHA256を収録します。MUGEN本体や画像素材は収録しません。
