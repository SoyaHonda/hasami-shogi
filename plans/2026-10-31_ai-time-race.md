# ソフトウェア計画: 人間側のルールの釣り合いと、機械の読みの作り直し

2026-10-31（同日、専門家レビュー2回を受けて改訂）/ 対象: 人間の操作に関するルール（持ち上げは1枚、置く間隔、持ち上げの上限）、実機で盤とゲームの認識がずれたときの一時停止、`lib/core` の機械の読み（`Ai`）、読みを測る人間役ボットと自動対戦。`plans/2026-10-30_core-wasm-firmware.md` の3章のルールと「機械の読み」、5.3、5.5、5.8 のうち該当する記述はこの計画で置き換える。運び出し、LED の色規則、ファームの構成は変えない（ファームは一時停止の扱いだけ足す）。

## 1. 背景

いまの読みは旧シミュレータの点数式を移植したもので、機械の1手のあとの盤だけを見て、人間が次の1手で取れる枚数を危険度として引く。2026-10-31 に2手の形を足したが、すぐに次の罠が見つかった。再現した罠は次の4つ（マスは画面の表記。7×7で人間は七段、機械は一段）。

- A: 初手で機械が4六へ出る。人間が3六と5六に続けて置くと取れる。
- B: 人間が3六と5六に置くと、機械は必ず4一から4六へ入る。入った瞬間はルール上取られないが、人間は片方をよそへ動かし、クールダウン（3秒）後に戻せば取れる。機械はその間に逃げない。
- C: 機械の駒が人間2枚のあいだにいて、片方がどいたあと別の人間の駒がそこへ入る。
- D: クールダウン中の人間の駒を残り1.5秒を切るまで数えないため、ヘッドが別の駒を動かしていると逃げが間に合わない。

専門家レビューで、読みだけでは直らない原因が見つかった。人間を縛っているのは駒ごとのクールダウンだけで、駒を持ち替えれば続けて何手でも、2枚同時にでも動ける。人間の駒は飛車のように動くので、盤端でないマスはたいてい0.65秒で両隣を埋められる。機械は気づいてから引き始めるまでに最低でも約1秒（読みの間隔0.25秒＋空送り＋吸着0.2秒）かかり、ヘッドは1つしかない。この差はどんな読みでも埋まらない。そこで、人間側のルールに置く間隔と持ち上げ1枚を足して釣り合いを取り、そのうえで読みを時間の比較で作り直す。

レビューで受けたほかの指摘（機械が動いているあいだ人間が止まる前提、実際のルールと式のずれ、無限大どうしの差、足し算の重みでは安全が保証されない、列を崩したあとの残り、受け身、両取り、持ち上げ上限での実機と認識のずれ、ESP32 の計算量、Snapshot の形式、ボットの作り、統計の取り方）も反映した。

## 2. 方針

- ルール: 人間が同時に持ち上げられる駒は1枚。人間が駒を置いた時刻の間隔は1秒以上（早すぎた置きは間隔が過ぎた時点で成立する）。持ち上げたまま待てるのは1秒まで。
- 実機で、センサが見た盤とゲームが思っている盤が食い違ったまま続いたら、ゲームを一時停止し、元どおりになったら再開する。反則で受け付けなかった持ち上げ、持ち上げ上限の時間切れ、置けない場所への置きは、すべてこの仕組みで扱う。
- 読みは「人間がその駒を挟み終えられる最短の時間」と「機械がそれを避け始められる最短の時間」を比べる。手は、ヘッドの順番まで含めて守れない列がない手（安全な手）を優先し、その中から攻めと形の点で選ぶ。安全な手がないときだけ、失う見込みが最小の手を選ぶ。
- 人間の時間は今を起点に数え、機械が動いているあいだも人間は動けるものとして扱う。
- 読みは Snapshot だけを読む純粋関数。計算は float、`exp`・`hypotf` などは使わない。同点は Snapshot から作る整数のハッシュで選ぶ。
- 計算量の上限を仕様で決める（3.10）。
- 強さを測る主な相手は、読みと同じ時間の計算を人間の側から使うボット（RaceBot）。罠 A〜D を仕掛けるボット（TrapBot）は回帰の確認に使う。主な指標は失った枚数で、作り直しの前後を種の対で比べる。

## 3. 仕様

### 3.1 人間の操作のルール

- `Rules::maxHeld = 1`。持ち上げ中の駒があるときの持ち上げは `TooMany`（反則）。
- `Rules::humanIntervalMs = 1000`。人間が駒を置いて成立した時刻（`lastPlaceAt`、ゲーム時計）から1秒たたないうちの置きは、`PlaceResult::Pending` を返して保留にする。保留中の駒は持ち上げ中のまま扱い（盤上になく、はさまれない）、`tick` で間隔が過ぎた時点に、その時点の盤で合法なら置きを成立させる（クールダウン、はさみ判定）。合法でなくなっていれば保留を捨て、反則を記録する（駒は持ち上げ中のまま）。保留中に同じ駒を元のマスへ置けば取り消し、別のマスへ置けば保留先を置き換える。取り消し（元のマスへ戻す）は置きとして数えない。
- `Rules::maxHoldMs = 1000`。持ち上げてから（ゲーム時計で、Play 中だけ数える）1秒を過ぎても置きが成立も保留もしていなければ、駒を元のマスへ戻したものとして扱い、人間のクールダウンを付け、元のマスで反則を記録する。元のマスが埋まっているとき（機械が入ったなど）は、持ち上げたまま反則を記録し、`stuckHeld` を立てて一時停止する（3.2）。保留中の駒は時間切れにしない。
- Removing、Paused、Countdown のあいだは、持ち上げの時間と置く間隔の時計は止まる（ゲーム時計が止まるため）。

### 3.2 盤の食い違いによる一時停止（実機）

Game に `pauseForMismatch(now)` と `resumeFromMismatch(now)` を足す。Play と Removing から Paused へ移り、移る前のフェーズを覚え、`mismatch()` を立てる。`mismatch()` が立っている間は `togglePause` で再開しない。`resumeFromMismatch` で元のフェーズへ戻る（Play へ戻るときは `resumeCountdownMs` の Countdown を挟む）。3.1 の `stuckHeld` も同じ一時停止で、元のマスが空いて駒を戻せる状態になったら解く。

ファーム（`src/main.cpp`）は、BoardWatcher の処理のあと、センサの盤と `expected`（持ち上げ中の駒を除いた Game の盤）を、`machineCells` を除いて比べる。食い違いが 300ms 続いたら `pauseForMismatch`、一致が 300ms 続いたら `resumeFromMismatch`。一時停止中は LED で、駒があるべきマスを白、あってはいけないマスを赤で点滅させる（LedPlan に `mismatch` の色を足す）。BoardWatcher の `liftReported_`・`placeReported_` は、一時停止から再開するときに盤の状態から作り直す（`BoardWatcher::resync(sensed, expected)` を足す）。保留中の置き（3.1）は、実機では駒がすでにマスにあるので、`expected` には含めないが食い違いとして数えない（保留先のマスを比較から外す）。シミュレータでは食い違いが起きない（反則の操作は受け付けないだけ）ので、この仕組みは使わない。

### 3.3 人間の時間のパラメータ

`Params` に足す: `humanMoveMs = 500`（持ち上げて置くまで）、`humanLiftMs = 150`（持ち上げてマスを空けるまで）、`humanPlaceMs = 350`（持ち上げている駒を置くまで）、`humanReactMs = 300`（機械の動きに気づくまで）。画面の設定から変えられる。人間の駒 h の `ready(h)` はクールダウンの残り。持ち上げ中の駒は、置くまでの時間を `humanPlaceMs` とし、持ち上げ上限の残りを超える手順は数えない。置く間隔の残り `gap = max(0, lastPlaceAt + humanIntervalMs − 今)`。

### 3.4 列

列は、縦か横に連続した1枚以上の機械の駒の最大の並び。1枚の駒は縦と横の2つの列に入る。列の両端 A、B がどちらも盤内でなければ取られない。失う見込みは駒ごとに、その駒が入る列の取られる見込みの大きいほうを使う。人間の駒の列（攻め）も同じ定義。

### 3.5 人間が挟み終える時間 `T_h(列)`

今を起点に、次の手順のうち最短のもの。h がマス s へ「届く」とは、縦か横の一直線で途中が空き、s が空きで、機械が確保中のマスでないこと。h が s に置ける最短の時刻 `e(h,s)` は、1手なら `max(ready(h), 0) + humanMoveMs`（持ち上げ中なら `humanPlaceMs`）、2手なら `ready(h) + humanMoveMs + humanCooldownMs + humanMoveMs`。置きは、直前の置きから `humanIntervalMs` 以上あけ、最初の置きは `gap` 以降。先に動いた駒が後の駒の通り道を開け閉めする影響は見ない（近似）。

- H1（片方が人間、もう片方が空き）: 空きの端への置き1回。`max(e(h,s), gap)` の最小。
- H2（両端が空き）: 別々の2枚 h1→A、h2→B を順に置く（同時に持てない）。`t1 = max(e(h1,A), gap)`、`t2 = max(t1 + humanIntervalMs, t1 + humanMoveMs, ready(h2) + humanMoveMs)` を両方の順で求めた小さいほう（2枚目は1枚目を置いてから持ち上げる）。
- H3（端 v が人間）: v をよそへ置き（`tv = max(ready(v) + humanMoveMs, gap)`、v の行き先が必要）、別の駒 g がそのマスへ入る: `max(tv + humanIntervalMs, tv + humanMoveMs, ready(g) + humanMoveMs)`。g が届くかは v を除いた盤で判定する。
- H4（端 v が人間）: v をよそへ置き、クールダウン後に戻す: `tv + humanCooldownMs + humanMoveMs`（`humanIntervalMs` より長いので間隔は自動で満たす）。持ち上げてすぐ元のマスへ戻すのは取り消しで、取りの判定がないので数えない。
- どれもできなければ `INF`。

### 3.6 機械が避け始める時間と安全な行き先

機械の駒 p を動かし始める時刻 `S(p) = max(cooldownRemain(p), headFree) + reactMs + 空送り（ヘッド→p）/ freeSpeed + engageMs`。引き始めた駒は取られないので、避けは引き始めた時点で済む。`reactMs` は最悪の値を使う。

p の安全な行き先 d: p を d へ動かした盤で、(1) d を含む列の `T_h ≥ 到着時刻 + machineCooldownMs + reactMs + engageMs + margin`、(2) 元の列に残った駒を含む列の `T_h ≥ S(p) + margin`。到着時刻は `S(p) + 距離 / dragSpeed`。計算を省くため、p が動いたことによる人間の通り道の変化は無視し、元の盤で作った到達時刻表をそのまま使う。ただし d 自身と p の元のマスの占有は反映する。この判定は再帰しない。

列 L を守る時刻 `save(L)` は、L の駒のうち安全な行き先を持つ駒の `S(p)` の最小。どの駒も持たなければ `INF`。

### 3.7 ヘッドの順番と取られる見込み

`T_h < 6000ms` の列（危ない列）だけを扱う。危ない列が3つ以下なら、ヘッドを回す部分集合と順番を総当たりし、守れる駒の枚数が最大になる組を選ぶ（同じなら `T_h` の早い順）。4つ以上なら `T_h` の早い順に貪欲に割り当てる。列 L が守れるのは、割り当てた順で `save(L) + margin ≤ T_h(L)` のとき（`headFree` は直前に割り当てた列の到着時刻）。

取られる見込み `risk(L)`: `T_h ≥ 6000` または `INF` なら0。守れるなら0。守れないなら、`save = INF` のとき `clamp((2500 − T_h) / 1000, 0, 1)`、それ以外は `clamp(0.5 + (save + margin − T_h) / 2000, 0, 1)`（ms）。

### 3.8 攻め

人間の駒の列 L について、機械が挟み終える時間 `T_m(L)` を、機械の駒1枚か2枚（別々の駒、またはクールダウンを挟んだ同じ駒）がヘッドを順に使って両端を埋める時間で求める（各手 = クールダウンの残りを待つ、空送り、吸着、引く距離）。人間が逃げ終える時間は `ready(h) + humanReactMs + humanLiftMs`。ただし持ち上げて逃げられるのは `maxHoldMs` までなので、`T_m − (ready(h) + humanReactMs + humanLiftMs) > maxHoldMs` のときは、持ち上げで逃げるには遅すぎる（早く持ち上げると時間切れで元のマスへ戻りクールダウンが付く）ものとして、逃げは「よそへ置く」`ready(h) + humanReactMs + humanMoveMs` とし、行き先がなければ逃げられない。取れる見込み `gain(L) = len × clamp(0.5 + (逃げ − T_m) / 2000, 0, 1)`。

### 3.9 手の選び方

候補: クールダウンが終わった機械の駒の合法手すべてと「動かない」（最初の候補）。手 m（駒 p を d へ）について:

- 盤 b' は p を d へ動かし、取れる駒を取り除いたもの。`headFree` は m の到着時刻 D（動かないなら0）から始める。p は引き始めるまで元の列で、到着後は d の列で評価する。
- 安全の条件: (a) p の元の列が引き始めまで守れる（`T_h(元の列) ≥ 引き始め + margin`）、(b) d が 3.6 の安全な行き先、(c) b' で 3.7 を行い守れない列がない。動かない手は (c) だけ。
- 点数 = 今取れる枚数 × 100 + Σgain × (40 + 40 × 攻め気) + 攻め気 × 形 − D(秒) × 4 − 2 − 両取りの減点（動かない手は −D×4−2 なし）。形 = 前進度 × 30 ＋ 人間の駒と縦横に隣り合う数 × 8 ＋ 盤端に接する機械の駒の数 × 4 ＋ 縦に2枚並んだ組の数 × 4（前進度は機械の駒の `r/(n−1)` の和）。
- 安全な候補があればその中で最高点、なければ全候補で 点数 = 今取れる枚数 × 100 − Σ(駒ごとの最大 risk) × 100 − D(秒) × 4。
- 両取りの減点（つよいだけ）: 安全な候補を点数順に並べ、上位3手について、人間の脅威の手（下記）それぞれを指した盤で (c) を確かめる。破れる脅威の手があれば、その手の点数から 60 を引く。上位3手を確かめ終えた時点で、減点後の最高点を選ぶ（4位以下の手は減点なしで比べる）。
- 人間の脅威の手: 人間の駒1枚の1手で、(i) 機械の列の端へ置く手、(ii) 機械の列の端にいる人間の駒をよそへ置く手（H3・H4 の準備）、(iii) 危ない列の駒の行き先をふさぐ手。`e` の早い順、同じなら人間の駒の id 順、次に行き先の `dests` の順で最大12手。脅威の手を指した盤では、人間の時計を `e(h,s)` 進め（各駒の `ready` をその分減らし、`gap` を `humanIntervalMs` にする）、機械の `headFree` と各駒の `cooldownRemain` も同じだけ進める。
- margin: やさしい0、ふつう200ms、つよい300ms（自動対戦の結果で調整する。3.12）。
- 同点: `s > best + 0.01` なら更新、`|s − best| ≤ 0.01` なら、Snapshot の盤・駒・時計（250ms 単位）と候補の番号から作る32ビットの FNV-1a ハッシュが小さいほうを選ぶ。

### 3.10 計算量

- 人間の駒ごとに、各マスへの1手・2手の到達時刻を、読みの最初に1回だけ表にする（49マス × 7駒 × 2）。候補ごとの盤では作り直さず、3.6 の近似のとおり表引きにする。候補の盤で p が人間の通り道を新たにふさぐ・開ける影響は見ない。
- 危ない列（`T_h < 6000`）だけで `save` を求める。
- 両取りの確認は上位3手 × 脅威の手12手まで。
- 最悪の計算量を test_ai で測る局面（駒が多く、危ない列が多い局面）を用意する。

### 3.11 強さ

| | 見る人間の手順 | margin | 両取りの減点 |
|---|---|---|---|
| やさしい | 持ち上げ中の駒の H1 | 0 | なし |
| ふつう | H1〜H4 | 200ms | なし |
| つよい | H1〜H4 | 300ms | あり |

### 3.12 人間役ボット

`BotParams::style` に `Random`（今のボット）、`Trap`、`Race`、`Race3` を持つ。どのボットも 3.1 のルール（持ち上げ1枚、置く間隔、持ち上げ上限）を守り、1手の時間は `humanMoveMs` を中心に ±40% の範囲で種つき乱数でばらつかせる。

- Trap（回帰用）: 1手で取れれば取る → 罠 A（両端が空いた列へ間隔をあけて2枚置く）→ 罠 C（挟んだ片方をよそへ置き、別の駒を入れる）→ 罠 B（挟んだ片方をよそへ置き、クールダウン後に戻す）→ 罠 B の準備（機械の駒が入りたくなる、両隣に人間がいる空きマスを作る）→ Random。
- Race（主な測定用）: RaceTime を人間の側から使う。人間の合法手それぞれについて、指したあとの盤で機械の列の余裕 `save + margin − T_h` の最小を求め、取れる手 > 余裕を最も減らす手 > 自分の駒の安全を保つ手 の順で選ぶ。自分の駒が機械に取られそうなら逃げる。
- Race3（モデルの外を探す）: Race に、3手の手順（1枚が3手、または3枚を順に使う）で取れる列を探す処理を足したもの。

### 3.13 評価と受け入れ条件

- 罠 A〜D と、その鏡像（左右）・平行移動した局面を、SimDriver で時間どおりに再現するテスト（新しいルールの下で）がすべて通る（機械の駒が取られない、または取られる前に引き始める）。
- 自動対戦は種1〜500、10分打ち切り。記録する指標: 1局あたり機械が失った枚数（主）、取った枚数の差（主）、機械の勝率（引き分けは負け）、引き分け率、安全な候補が1つ以上あった読みの割合、見積もりの当たり具合（機械の駒が取られた場面のうち、取られる margin 以上前に risk ≥ 0.5 だった割合。読みが列ごとの risk を出すデバッグ出力 `AiDebug` を使う）。
- 作り直し前の成績は、新しいルール（3.1）を入れたあと、今の読みで種ごとに記録する（`tools/baseline/` に JSON）。
- 比較は種ごとの対で、主な指標について対のウィルコクソン符号順位検定（またはブートストラップの95%信頼区間）。
- 条件: つよい対 Race・Race3・Trap で、機械が失った枚数が作り直し前より有意に少ない（p < 0.05）。つよい対 Random で、取った枚数の差が非劣性（作り直し後−前の差の95%信頼区間の下限が −0.5枚より上）。引き分け率が各条件で作り直し前 + 5ポイント以内。
- 条件を満たさないときに調整するもの: margin、両取りの減点の大きさ、危ない列の範囲（6000ms）、形の点の重み。調整の結果はこの計画に書き足す。
- 正解表（`test/golden/ai_cases.txt`）を新しい読みで作り直し、PC と WASM で一致する。`think` の時間は PC で平均 2ms 以内、test_ai の最悪局面で 10ms 以内。ESP32 の `golden` コマンドが報告する最悪時間が 50ms 以内（実機での確認は後日）。

## 4. 変えないもの・範囲外

- 運び出し、LED の既存の色規則、ファームの構成（一時停止の扱いと LED の mismatch の色だけ足す）。
- 4手以上の人間の手順、機械側の複数手の計画（ヘッドの順番は 3.7 の範囲）。
- 時間を刻んだミニマックス（ESP32 で現実的でない）。
- 強さとルールの既定値の最終決定（実機で遊んでから）。

## 5. ファイル

```
lib/core/Rules.h                 maxHeld、humanIntervalMs、maxHoldMs
lib/core/Game.h/.cpp             持ち上げ1枚、保留の置き、持ち上げ上限、盤の食い違いの一時停止、lastPlaceAt と持ち上げ時刻の保持
lib/core/Ai.h                    PieceView に liftedAt、Snapshot に lastPlaceAt と保留の情報、AiDebug
lib/core/AiCase.h/.cpp           正解表の形式に gap と持ち上げの経過時間を足す
lib/core/Params.h                humanMoveMs、humanLiftMs、humanPlaceMs、humanReactMs
lib/core/RaceTime.h/.cpp         3.4〜3.8 の見積もり（新規、純粋関数）
lib/core/Ai.cpp                  3.9 の手の選び方に書き換え（danger、danger2、pressure は削除）
lib/core/HumanBot.h/.cpp         style（Random、Trap、Race、Race3）、新しいルールへの対応、手の時間のばらつき
lib/core/LedPlan.h/.cpp          mismatch の色
lib/hwpure/BoardWatcher.h/.cpp   resync、保留先のマスを比較から外す
src/main.cpp                     盤の食い違いの一時停止と再開、保留の置きの扱い
wasm/bindings.cpp                Rules、Params、BotParams、PlaceResult::Pending、thinkCase の新しい形式
web/index.html、web/app.js       保留中の表示、設定（置く間隔、持ち上げの上限、人間の1手の時間）
tools/autoplay.mjs               --bot、指標、--baseline-out／--baseline（対の検定）
tools/gen-golden.mjs             新しい形式の表を埋め込む
tools/baseline/                  作り直し前の種ごとの成績（JSON）
test/golden/gen_cases.cpp        正解表を作るプログラム（新規）
test/test_game/                  ルールと一時停止のテスト
test/test_hwpure/                resync のテスト
test/test_race/                  RaceTime の単体テスト（新規）
test/test_ai/、test/test_sim/    罠の局面と再現テスト、ボットのテスト
```

## Due diligence record

この計画は外部の事実（ライブラリの版、外部サービス）に依存しない。

## Implementation steps

共通の前提: 各ステップは1コミット。PC のテストは `pio test -e native`、WASM は `source ~/emsdk/emsdk_env.sh && npm run build:wasm && npm test`、自動対戦は `node tools/autoplay.mjs`。ESP32 のビルドは `node tools/gen-golden.mjs && pio run -e esp32dev && pio run -e rig`。盤は文字列から作るヘルパー（test_game、test_ai にあるもの）と `Game::onMachineStep` による駒の置き直しで組む。浮動小数のビルドはどこでも `-ffp-contract=off`。

### Step 1: 人間の操作のルール（持ち上げ1枚、置く間隔と保留、持ち上げ上限）

**Files**: `lib/core/Rules.h`、`lib/core/Game.h`、`lib/core/Game.cpp`、`lib/core/HumanBot.cpp`、`test/test_game/test_main.cpp`、`test/test_sim/test_main.cpp`、`wasm/bindings.cpp`、`web/app.js`、`web/index.html`、`plans/2026-10-30_core-wasm-firmware.md`

**Do**:
先にテストを書く。3.1 を Game に実装する（`maxHeld`、`humanIntervalMs` と `PlaceResult::Pending`、保留の成立と破棄、`maxHoldMs` の時間切れ、`stuckHeld`）。今の HumanBot（Random）を新しいルールで動くように直す（1枚だけ持つ、Pending を受けたら待つ）。bindings で新しい Rules と Pending を通す。画面は、Pending のとき駒を保留先に半透明で表示し、ドラッグ中に持ち上げ上限を過ぎて駒が戻ったらドラッグを終える。設定に「置く間隔」（0〜2秒）と「持ち上げの上限」（0.5〜5秒）を足す。`plans/2026-10-30_core-wasm-firmware.md` の3章のルールの「持ち上げ中は最大2枚」を、この計画の 3.1 を指す記述に置き換える。

**Test**: 2枚目の持ち上げは TooMany。置きから999msの置きは Pending で、1000ms の `tick` で成立しクールダウンとはさみ判定が付く。保留中に保留先が埋まれば破棄と反則。保留中に元のマスへ置けば取り消し。取り消しは間隔の起点にならない。持ち上げ999msでは持ち上げたまま、1000ms で元のマスに戻りクールダウンと反則。元のマスが埋まっていれば `stuckHeld` で Paused。Paused・Removing 中は持ち上げと間隔の時計が止まる。ボット対機械の100局（既存のテスト）が新しいルールで最後まで進む。

**Verify**: `pio test -e native` と `npm test` が通る。

### Step 2: 盤の食い違いによる一時停止（実機）

**Files**: `lib/core/Game.h`、`lib/core/Game.cpp`、`lib/core/LedPlan.h`、`lib/core/LedPlan.cpp`、`lib/hwpure/BoardWatcher.h`、`lib/hwpure/BoardWatcher.cpp`、`src/main.cpp`、`test/test_game/test_main.cpp`、`test/test_hwpure/test_main.cpp`、`test/test_machine/test_main.cpp`

**Do**:
先にテストを書く。3.2 の `pauseForMismatch`・`resumeFromMismatch`・`mismatch()`、LedPlan の mismatch の色（あるべきマスは白、あってはいけないマスは赤の点滅）、`BoardWatcher::resync` と保留先の除外を実装する。`src/main.cpp` に食い違いの検出（300ms）と一時停止・再開、Pending の置きの扱いを足す。

**Test**: Play と Removing から一時停止して元のフェーズへ戻る（Play へは Countdown を挟む）、mismatch 中は `togglePause` で再開しない、`stuckHeld` も同じ扱い、LED の mismatch の色、resync 後に Lift・Place が再び報告される、保留先のマスは食い違いに数えない。

**Verify**: `pio test -e native` が通る。`pio run -e esp32dev` と `pio run -e rig` が通る（実機での確認は後日）。

### Step 3: RaceTime（時間の見積もり）と Snapshot の拡張

**Files**: `lib/core/Params.h`、`lib/core/Ai.h`、`lib/core/Game.cpp`、`lib/core/AiCase.h`、`lib/core/AiCase.cpp`、`lib/core/RaceTime.h`、`lib/core/RaceTime.cpp`、`tools/gen-golden.mjs`、`test/test_race/test_main.cpp`、`test/test_ai/test_main.cpp`、`wasm/bindings.cpp`

**Do**:
先にテストを書く。Params に 3.3 の4項目、PieceView に `liftedAt`、Snapshot に `lastPlaceAt` と保留の情報を足し、`Game::snapshot` で埋める。正解表の形式に gap と持ち上げの経過時間の欄を足し（`AiCase.h` の説明も直す）、既存の表は新しい欄を `-` として読めるようにする。RaceTime を 3.4〜3.8 と 3.10 のとおり純粋関数で作る（到達時刻表、`T_h`、`S(p)` と安全な行き先、危ない列とヘッドの割り当て、risk、`T_m` と gain）。時間は float の ms、`INF` は専用の定数で、差を取る前に必ず判定する。

**Test**: 罠 A〜D の局面で `T_h` が手計算の値になる（例: 罠 A の H2 が `max(500, gap) = 500` と `max(500 + 1000, 500 + 500, 500) = 1500`、罠 B の H4 が `500 + 3000 + 500 = 4000`、罠 C の H3 が `max(500 + 1000, ready(g) + 500)`）。置く間隔の残り `gap` が効く。持ち上げ中の駒は `humanPlaceMs` で、持ち上げ上限の残りを超える手順を数えない。盤端の列は `INF`、`INF` の列の risk は0。危ない列が2つで片方しか守れないとき、守れる駒の多いほうを選ぶ。列の片方を動かすと残りが取られる形は安全な行き先にならない。持ち上げてすぐ戻す手順を数えない。確保中のマスへは届かない。やさしいで H2〜H4 が `INF`。

**Verify**: `pio test -e native` と `npm test` が通る。

### Step 4: 人間役ボット（Trap、Race、Race3）と自動対戦の拡張、作り直し前の成績の記録

**Files**: `lib/core/HumanBot.h`、`lib/core/HumanBot.cpp`、`lib/core/Ai.h`、`lib/core/Ai.cpp`（`AiDebug` の出力だけ）、`wasm/bindings.cpp`、`tools/autoplay.mjs`、`tools/baseline/*.json`、`test/test_sim/test_main.cpp`、`plans/2026-10-31_ai-time-race.md`

**Do**:
先にテストを書く。3.12 の style と手の時間のばらつきを実装し、bindings で通す。`think` に列ごとの risk を返す任意の `AiDebug*` を足す（今の読みでは今の危険度を入れる）。autoplay に `--bot`、3.13 の指標、`--baseline-out <file>`（種ごとの指標を JSON に書く）と `--baseline <file>`（種ごとに対で比べ、ウィルコクソン検定の p 値とブートストラップの信頼区間を出す）を足す。今の読みで、つよい・ふつう × Random・Trap・Race・Race3 を種1〜500で回して `tools/baseline/` に記録し、要約の表をこの計画の 3.13 の下に「作り直し前の成績」として書き足す。

**Test**: Trap が罠 A・B・C の配置でそれぞれの手順を指して取る（新しいルールの下で）。Race が、用意した局面で余裕が最小になる手を選ぶ。Race3 が3手の手順で取れる局面でそれを指す。どのボットも持ち上げ1枚・置く間隔・持ち上げ上限を破らない。同じ種で同じ結果。

**Verify**: `pio test -e native` と `npm test` が通る。autoplay が bot ごとの表と検定の結果を出す。

### Step 5: Ai を時間の競争で書き換え、罠のテスト、正解表の作り直し

**Files**: `lib/core/Ai.cpp`、`test/test_ai/test_main.cpp`、`test/test_sim/test_main.cpp`、`test/golden/gen_cases.cpp`、`test/golden/ai_cases.txt`、`plans/2026-10-31_ai-time-race.md`

**Do**:
先に罠 A〜D とその鏡像・平行移動を SimDriver で時間どおりに再現するテストを書き、今の読みで失敗することを確かめる。`think` を 3.9〜3.11 で書き換える（`AiDebug` に列ごとの risk を出す）。`gen_cases.cpp` は `think` と同じ候補の点数を全部出し、最高点と次点の差が0.1以上の局面だけを選ぶ。先頭にビルドのしかた（`g++ -std=gnu++17 -ffp-contract=off -O2 -Ilib/core test/golden/gen_cases.cpp lib/core/*.cpp -o <出力先>`）を書く。表を作り直す。test_ai の古い点数式を前提にしたテストは、同じ意図を新しい式で確かめるように直す。`think` の平均と最悪局面の時間を test_ai で測る。autoplay で 3.13 の条件を確かめ、満たさなければ 3.13 の調整項目を調整し、結果を計画に書き足す。

**Test**: 罠の再現テスト。取れる手を選ぶ、クールダウン中の駒は動かさない、同点はハッシュで決まり同じ Snapshot なら同じ手、安全な手がないときは失う見込みが最小の手、両取りの減点が上位3手にだけ掛かる。正解表。

**Verify**: `pio test -e native` と `npm test` が通る。`node tools/autoplay.mjs --games 500 --level normal,hard --hcd 3 --bot random,trap,race,race3 --baseline tools/baseline/<file>` で 3.13 の条件を満たす。`think` が PC で平均 2ms 以内・最悪局面 10ms 以内。`pio run -e esp32dev` が通る。

### Step 6: 画面の設定と計画の書き換え

**Files**: `web/index.html`、`web/app.js`、`plans/2026-10-30_core-wasm-firmware.md`

**Do**:
設定に「人間の1手の時間」（0.2〜1.0秒、`humanMoveMs`）を足す。強さの説明文を 3.11 に合わせて書き直す。遊び方に、持ち上げは1枚、置く間隔、持ち上げの上限を書く。`plans/2026-10-30_core-wasm-firmware.md` の3章「機械の読み」と5.3、5.5、5.8 を、この計画を指す短い記述に置き換える。

**Test**: 自動テストは足さない。`npm test` の既存テストが通ること。

**Verify**: `node --check web/app.js`。`npm run serve` の画面をヘッドレスブラウザで開き、設定の変更、保留中の表示、1秒を過ぎたドラッグで駒が戻ることを確かめる。
