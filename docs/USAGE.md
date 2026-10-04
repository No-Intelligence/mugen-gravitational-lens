# WinMUGEN Plus 重力レンズmod

`mod.dll` が描画とCNSコマンドを接続し、`mugen-lens-math.dll` が画素の参照座標を計算します。既存の [LOADER_ABI.md](LOADER_ABI.md) のx86/C ABI/起動前初期化規格に従います。ローダー本体の仕様変更はありません。

## 配置と起動

```text
MUGEN/
  mods.txt                         # gravitational-lens を追記
  mods/gravitational-lens/
    mod.dll
    mugen-lens-math.dll
```

ローダーが未導入の場合は既存のMugenModPatcherで導入してください。依存DLLはmod.dllと同じディレクトリに配置します。本modにMUGEN本体やKFM素材は同梱しません。DLLは32bit、CRTは静的リンクです。

描画アダプターはWindows 10以降が必要です。ハッシュ照合に使用する [BCryptHashのOS要件](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcrypthash) によるもので、ローダー本体の最低OS条件とは別です。

描画処理は同梱されたWinMUGEN Plusの特定ビルドだけに対応します。他ビルドをアドレスの推測で処理しません。初期化時に本体 `.text` とALLEG40.DLLのSHA256、全フック位置の元CALLを照合し、不一致なら変更を適用せずローダーログに記録します。

| 照合対象 | SHA256 |
|---|---|
| exe `.text`、RVA 0x1000、647168 bytes | `45cdacf01b3aeb25c79497504f397c3d6e6e868f7eb3d1ac3bd25532c8f533ec` |
| ALLEG40.DLL 全体 | `490f5b48f494f51abec8d5cce292b6f2656c921dca5ae724e27126a765afd2fa` |

## CNS API

キャラクターの追加CNSを `.def` の空いている `stN` に登録します。既に `Statedef -2` がある場合は、その定義へ以下のコントローラーを統合してください。別の-2定義で上書きしないでください。

```ini
[Statedef -2]

[State -2, Lens shape]
type = DisplayToClipboard
trigger1 = RoundState = 2
text = "GL1:shape:%d:%d:%d"
params = ID, 180, 18
ignorehitpause = 1

[State -2, Lens position and layer]
type = DisplayToClipboard
trigger1 = RoundState = 2
text = "GL1:set:%d:%d:%d:%d:%d"
params = ID, 320, 240, 0, 70
ignorehitpause = 1

[State -2, Stop]
type = DisplayToClipboard
trigger1 = RoundState != 2
text = "GL1:off:%d"
params = ID
ignorehitpause = 1
```

上記の数値は検証例です。利用者が指定した座標、レイヤー、半径、強度の既定値ではありません。

| コマンド | 引数と範囲 |
|---|---|
| `GL1:shape:%d:%d:%d` | 所有者ID、影響半径1～512 px、中心の軟化半径1～512 px |
| `GL1:set:%d:%d:%d:%d:%d` | 所有者ID、画面x=0～639、画面y=0～479、レイヤー境界-128～128、Einstein半径0～512 px |
| `GL1:off:%d` | 所有者ID |

`shape` の後に `set` を実行し、有効中は毎tick `set` を更新してください。3回の画面提示を超えて更新が途絶えると解除します。`off` は明示解除です。`ID` はコマンドの送信者を識別します。同時に有効にできるレンズは1個で、別IDによる置換は拒否します。強度0は恒等変換です。座標の原点は640×480画面の左上で、ワールド座標への自動変換は行いません。

このAPIは完全一致した上記書式だけを受理します。任意ポインター、ネイティブ関数アドレス、`%n` を渡すAPIではありません。元のDisplayToClipboard整形処理も実行するため、デバッグ用クリップボード表示はこれらのコマンドで更新されます。CNS側には戻り値を返せません。ネイティブAPIには戻り値があります。

## レイヤーの意味

レイヤー `L` は、**戦闘中のソート済みスプライトキューで内部優先度が初めて `L` 以上になる直前**の描画境界です。優先度が `L` より小さいスプライトと、その時点で描かれている背景・影を変形し、それ以降をMUGENの元の順序で描きます。該当するスプライトがなければキュー末尾で変形します。並べ替え、SprPriorityの変更、スプライトの取り除きは行いません。

検証用KFMでは `sprpriority=-3,0,3` が内部キー `-3,0,3` に対応し、境界0で-3のマーカーだけが変形しました。ただし、複数画像を含むアニメーションでは要素の優先度も内部キーに加味されるため、**キャラクターのsprpriority値だけによる選別を全キャラクターで保証するAPIではありません**。ステージの `layerno=0/1` とも別です。BG1、ontop Explodはこのキューの後に通常描画します。ステージレイヤーそのものを指定するAPIは実装していません。

HUDも元の描画位置を保持します。同梱の `data/fight.def` の説明（562行目以降）では、HUDの `layerno=0` は背景より前・プレイヤーより後ろで、通常の既定値です。このHUDはキューの前に描かれるため、レンズの範囲に入れば変形されます。HUDの `layerno=1/2` はキュー後に描かれるので変形しません。modはHUDを別レイヤーへ移動しません。HUDを常に保護するという意味のAPIではありません。

`2147483647` はレイヤー分離の検証には使わない、診断用の最終画面全体モードです。提示直前に変形し、提示後に元のバッファを戻して繰り返し歪みが蓄積するのを防ぎます。

## 色と変形の計算

目的画素の中心相対座標を `theta`、影響半径を `R`、Einstein半径を `E`、軟化半径を `C` とすると、範囲内の参照元は次式で近似します。

```text
q = dot(theta, theta)
beta = theta * (1 - E² * (1 - q/R²)² / (q + C²))   (q < R²)
beta = theta                                      (q >= R²)
source = center + nearest_integer(beta)
```

画面外参照は端にクランプします。各画素のバイト列を丸ごとコピーする最近傍方式で、RGBの計算、補間、パレット変更、色深度変換はありません。半透明スプライトなどの合成は、その後に元のMUGENが実行します。その場合、変形された背景との合成色が変わることはあります。今回の画素・手前保護検証は通常の不透明KFM素材を対象としています。

半径・強度・軟化半径の変更時に参照座標カーネルを再作成します。中心座標だけの移動は再計算しません。毎tick大きなカーネルを作り直す使用法の60fpsは未検証です。数学DLLは1～4 bytes/pixel、描画アダプターは8/15/16/24/32bitの640×480メモリビットマップを受理します。実機で確認したのは16bitです。

## ネイティブAPIとビルド

[include/gravitational_lens.h](../include/gravitational_lens.h) が公開C ABIです。`GlMath_Create/SetLens/Warp/SourcePixel/Destroy` は計算DLL、`GlMod_Set/Disable/IsLayerSupported` はmod DLLから公開します。`GlMod_Set` はMUGENメインスレッドで呼び、CNSと同様に更新を続けます。コンテキストを作ったDLLで破棄し、同一コンテキストへの並行呼び出しは避けてください。

`scripts/build.cmd` をx86 Visual Studio C++環境で実行します。出力は `build/`。本modと計算DLL、公開SDK、テストソースはリポジトリのMITライセンスに従います。実測と検証条件は [VERIFICATION.md](VERIFICATION.md) を参照してください。

Allegroの構造体定義は [公式gfx.h](https://github.com/liballeg/allegro5/blob/4.4/include/allegro/gfx.h) を参照し、対象ビルドの逆アセンブルと実際の16bitバッファで確認しています。エフェクト有効中は `timeBeginPeriod(1)` を要求し、解除時に `timeEndPeriod(1)` で戻します。[Microsoftの仕様](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod) にあるとおり、不可視状態などで精度の向上が保証されるものではありません。QPCの精度を変更する処理でもありません。
