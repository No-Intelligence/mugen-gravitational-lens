# 内部構成

## 2 DLLの役割

`src/math/math.cpp`はMUGENの内部構造に依存せず、固定640×480画像の参照座標を計算します。半径・強度・軟化半径の変更時に座標カーネルを作り、中心移動では再利用します。画像のバイト列をコピーするため、色の補間はありません。

`src/adapter/mod.cpp`はローダーv1から初期化され、対応するexeの.textとALLEG40.DLLをSHA256で照合します。元のCALLも検査してからCNS整形と描画処理に接続します。固定アドレスはこのビルド専用です。他ビルドにそのまま流用しないでください。

## 制御と描画

1. CNSのDisplayToClipboardから`GL1:shape`、`GL1:set`、`GL1:off`を受理します。
2. 所有者IDと更新期限を管理し、1個のレンズを有効にします。
3. 戦闘中のソート済みキューで、内部優先度が境界以上になる直前に画像を退避して歪めます。
4. 境界以降のノードは元のMUGEN処理で描画します。

診断用の最終画面モードは提示後に元バッファを復元します。通常のレイヤー指定とは用途が異なります。

## 公開境界

- `include/gravitational_lens.h`：GlMathとGlModのC ABI、引数と戻り値。
- `include/mugen_mod.h`：[ローダーv1 ABI](LOADER_ABI.md)。ローダー実装は別リポジトリです。
- `src/*/*.def`：x86でも装飾されないエクスポート名。

同一数学コンテキストへの並行呼び出しは避け、作成したDLLで破棄します。GlModの制御はMUGENメインスレッドから行います。

## 検証の区分

`tests/native`は数学DLLを検査します。`tests/integration/verify_lens_evidence.py`はDLLを呼ばず、保存RAWからPythonで座標を再計算し、画素・順序・時間を照合します。実機用の素材とRAWはGit管理しません。`docs/verification`は元の実測の集計記録で、新しいビルドの動作保証とは区別します。
