# MUGEN Gravitational Lens

WinMUGEN Plus向けの重力レンズ効果MODです。描画優先度の境界までに描かれた画像を歪め、その後のスプライトは元の順序で描きます。画素は最近傍コピーで参照し、補間や色変換を行いません。

**Windows 10以降・x86・特定ビルドのWinMUGEN Plus**に対応します。実機検証は640×480・16bitです。MUGEN本体、キャラクター、ステージ素材、ローダー本体は含みません。独自コードは[MITライセンス](LICENSE)です。

## 最初に読む資料

- 利用者：[導入・CNSコマンド・対応ハッシュ・レイヤー仕様](docs/USAGE.md)
- 開発者：[ビルドと単体テスト](docs/BUILD.md)、[内部構成](docs/ARCHITECTURE.md)
- 検証：[元の実機検証記録](docs/VERIFICATION.md)、[整理後の検証結果](docs/REPOSITORY_VALIDATION.md)、[実機テストの準備](docs/LIVE_TESTING.md)

## ファイル構成

```text
include/                 公開C ABI（数学DLL・描画MOD・ローダー）
src/
  math/                  レンズの参照座標計算と画素コピー
  adapter/               対応ビルド照合・描画フック・CNS連携
scripts/                 x86ビルド・配布ZIP作成
examples/cns/            キャラクターへ組み込むCNS例
tests/
  native/                数学DLLの画素・引数・メモリー境界検査
  integration/           隔離実機ケース準備・独立した証跡検査
docs/                    仕様・設計・検証方法
  verification/          素材を含まない集計JSONと元ビルドのハッシュ
build/                   ビルド生成物（Git管理外）
dist/                    配布ZIP（Git管理外）
```

## ビルド

Visual Studio C++ Build Toolsを導入したWindowsで実行します。

```bat
scripts\build.cmd
build\lens-math-test.exe
```

`build/mod.dll`と`build/mugen-lens-math.dll`を、[MUGEN Open Mod Loader](https://github.com/No-Intelligence/mugen-open-mod-loader)を導入したゲームの`mods/gravitational-lens/`に置き、`mods.txt`に`gravitational-lens`を追記します。詳しいCNS登録手順は[利用仕様](docs/USAGE.md)を参照してください。

## レイヤーと性能の注意点

レイヤー引数は内部の描画優先度境界であり、ステージの`layerno`ではありません。HUDの`layerno=0`は変形範囲に入り得ます。同時に有効にできるレンズは1個です。サンプルの中心・半径・強度は検証用の値です。

元の半径512pxの実機測定は平均59.873247 fps、フレーム間隔P99は31.984100 msでした。「平均約60fps」の基準に合格した記録で、全フレーム60fpsの保証ではありません。測定条件と未検証範囲は[検証報告](docs/VERIFICATION.md)に記載しています。
