# ソフトウェア計画: 機械の読みを時間の競争で作り直す

2026-10-31 / 対象: `lib/core` の機械の読み（`Ai`）と、それを測る自動対戦。`plans/2026-10-30_core-wasm-firmware.md` の3章「機械の読み」と5.3、5.5、5.8のうち、読みと人間役ボットに関する記述はこの計画で置き換える。ほかの章（Game、MachineController、運び出し、ファーム、画面）は変えない。

## 1. 背景

いまの読みは旧シミュレータの点数式を移植したもので、機械の1手のあとの盤だけを見て、人間が次の1手で取れる枚数を危険度として引く。2026-10-31 に2手の形（両隣が空いて別々の駒がそれぞれ入れる形、両隣が人間で片方がどいて別の駒が入れる形）を足したが、遊ぶとすぐに次の罠が見つかった。

再現した罠は次の4つ。マスは画面の表記（筋＋段、7×7で人間は七段、機械は一段）。

- A: 初手で機械が4六へ出る。人間が3六と5六に続けて置くと取れる（両隣が空き、別々の駒がそれぞれ1手で入る）。2026-10-31 の変更で防げた。
- B: 人間が3六と5六に置くと、機械は必ず4一から4六へ入る。入った瞬間はルール上取られないが、人間は片方を一度どかし、クールダウン（3秒）を待って戻せば取れる。機械はその間に逃げない。
- C: 機械の駒が人間2枚のあいだにいて、片方がどいたあと別の人間の駒がそこへ入る。2026-10-31 の変更で防げた。
- D: 片方がどいて残り1.5秒を切るまでクールダウン中の駒を数えなかったため、ヘッドが別の駒を動かしていると逃げが間に合わない。

B が残った理由は、読みが「どいて戻る」という同じ駒の2手を見ていないこと、そして入った瞬間の隣接と前進の点を取りにいくことにある。形を一つずつ足す直し方では、形を少し変えた罠に追いつけない。このゲームで効くのは時間（駒ごとのクールダウン、機械はヘッドが1つで一度に1枚しか動かせないこと、人間は駒を持ち替えれば続けて動けること）なので、読みを時間の比較で作り直す。

もうひとつの問題は測り方で、いまの自動対戦の人間役はほぼでたらめに動くため、勝率が上がっても罠に強くなった証拠にならない。

## 2. 方針

- 読みは「人間がその駒を挟み終えられる最短の時間」と「機械がそれを避け終えられる最短の時間」を比べる。人間のほうが早い駒は取られる見込みの駒として扱う。攻めも同じ比べ方をする（機械が人間の駒を挟み終える時間と、人間が逃げ終える時間）。
- 時間の見積もりは、人間の手順として次をすべて含める: 1枚が1手で入る、別々の2枚が続けて入る、挟んでいる駒がどいて別の駒が入る、挟んでいる駒がどいてクールダウン後に戻る、1枚が2手かけて入る。3枚以上を使う手順と3手以上かける手順は見ない（見積もりは保守的になりすぎないように、この範囲で止める）。
- 人間が1手にかける時間は `Params::humanMoveMs`（既定400ms、持ち上げて置くまで）、人間が機械の動きに気づくまでの時間は `Params::humanReactMs`（既定300ms）として持つ。実際に遊んで合わせる値なので、画面の設定から変えられるようにする。
- 機械の時間は、駒のクールダウンの残り、読みの間隔（`reactMs`）、ヘッドの空送り、吸着（`engageMs`）、引く時間（`dragSpeed`）から出す。ヘッドは1つなので、取られる見込みの駒が複数あれば、早い順にヘッドが回ったときの時間で判定する。
- 読みは引き続き Snapshot だけを読む純粋関数で、計算は float、`exp` や `hypotf` のようにライブラリで結果が変わりうる関数は使わない（ESP32、WASM、PC で同じ手を選ぶため）。
- 強さは「人間のどの手順まで見るか」で分ける。やさしいは持ち上げ中の駒が1手で入る手順だけ、ふつうは1枚が1手で入る手順と別々の2枚が続けて入る手順、つよいは2章の手順すべて。
- 罠をねらって仕掛ける人間役ボット（TrapBot）を作り、その成績で強さを判断する。

## 3. 仕様（読みが満たすこと）

### 3.1 人間が挟み終える時間 `humanCaptureMs`

機械の駒の列（縦か横に連続した機械の駒）ごとに求める。列の両端のマス A、B がどちらも盤内でなければ取られない（盤端の列は取らない規則）。人間の駒 h について、`ready(h)` はクールダウンの残り（持ち上げ中なら0）、`move = humanMoveMs`、`cd = rules.humanCooldownMs`。h があるマスへ1手で入れるのは、縦か横の一直線で途中が空いているとき。

- A、B がどちらも空き: 別々の駒 h1→A、h2→B。人間の手は一度に1つなので、先に動かすほうを両方の順で試し、`t1 = ready(h1) + move`、`t2 = max(t1, ready(h2)) + move` の小さいほう。h が1手で届かず2手で届く場合は `ready + move + cd + move` を1手ぶんの時間として使う。
- 片方が人間、もう片方が空き: 空きのほうへ入る最短の時間（1手か2手）。
- 両方が人間: 片方 v がどいて（v の行き先が少なくとも1つ必要）、(a) 別の駒 g がそのマスへ入る `max(ready(v) + move, ready(g)) + move`、または (b) v が戻る `ready(v) + move + cd + move`。両端それぞれについて小さいほう。
- どれもできなければ無限大。

強さが見ない手順は無限大として扱う。

### 3.2 機械が避け終える時間 `machineSaveMs`

列の駒のどれか1枚を動かして列を崩すか、取られない位置へ動かすまでの時間。駒 p について `max(cooldownRemain(p), headFree) + reactMs + 空送り（ヘッド→p）/ freeSpeed + engageMs + 1 / dragSpeed × 1000`。`headFree` は、ヘッドがそれより先に別の駒を避けに行くときにかかる時間で、取られる見込みの列を `humanCaptureMs` の早い順に並べ、ヘッドを順に回して求める。p に安全な行き先（動いた先の列の `humanCaptureMs` が動き終えた時点の `machineSaveMs` より長い）がひとつもなければ、その列は避けられない（無限大）。

### 3.3 取られる見込み

列の取られる見込み `risk = clamp(0.5 + (machineSaveMs − humanCaptureMs) / (2 × 1000), 0, 1)`（差が −1秒なら0、+1秒なら1、間は直線）。失う枚数の見込みは 列の枚数 × risk の合計。

### 3.4 攻め

人間の駒の列について、機械が挟み終える時間（ヘッドが順に駒を動かす時間、機械の駒のクールダウンを含む。2手まで）と、人間が逃げ終える時間（`max(ready(h), humanReactMs) + move`）を3.3と同じ式で比べ、取れる見込みの枚数を出す。

### 3.5 手の選び方

候補は、クールダウンが終わった機械の駒の合法手すべてと「動かない」。手 m の所要時間を D（空送り＋吸着＋引く距離）とし、m のあとの盤で、時計を D 進めた状態（人間の駒のクールダウンは D 減り、動いた駒には機械のクールダウン、ヘッドは行き先）について 3.3 と 3.4 を求める。動かしている途中の駒は取られない規則（2026-10-31）なので、動かす駒自身は D の間は列から外して見る。

点数 = 今取れる枚数 × 100 + 取れる見込みの枚数 × (40 + 40 × 攻め気) − 失う見込みの枚数 × (100 − 40 × 攻め気) − D(秒) × 4 − 2（動かないときは D = 0 で最後の −2 もなし）。前進と隣接の点はやめる。候補の順と同点の扱いは今と同じ（駒は id 順、行き先は `dests` の順、`s > best + 0.01` のときだけ更新）。

### 3.6 TrapBot

`HumanBot` に戦い方の切り替え `BotParams::style`（`Random` が今のボット、`Trap` が新しいボット）を足す。Trap は、反応時間（`BotParams::reactMs`）ごとに次の順で手を探す。

1. 今1手で取れる手があれば指す。
2. 機械の駒の列の両端が空きで、別々の自分の駒が1手ずつで入れるなら、片方を置き、すぐ（`humanMoveMs` 後）もう片方を置く（罠 A）。
3. 機械の駒の列が自分の2枚に挟まれていれば、片方をどかす。別の駒が入れるならそれを入れ（罠 C）、なければクールダウン後に戻す（罠 B）。
4. 機械の駒の両隣が空いた位置へ、別々の2枚を前に出して待つ（罠 B の準備: 3六と5六のような配置）。
5. どれもなければ今の Random と同じ手。

### 3.7 受け入れ条件

- 罠 A〜D を、SimDriver で時間どおりに再現するテストがすべて通る（機械の駒が取られない、または取られる前に逃げる）。
- つよい対 TrapBot（種1〜100、10分打ち切り）の機械の勝率が、作り直し前（Step 1 で測って計画に書く）より高い。
- つよい対 Random の機械の勝率が、作り直し前より下がらない（5ポイント以内）。
- ESP32 の `golden` 用の表（`test/golden/ai_cases.txt`）を新しい読みで作り直し、PC と WASM で一致する。
- 1回の `think` が PC で 1ms 以内（ESP32 では数十ms以内の見込み）。

## 4. 変えないもの・範囲外

- Game、MachineController、運び出し、LED、画面の操作、ファームの構成。
- 3枚以上・3手以上の人間の手順、機械側の複数手の計画（ヘッドの順番は 3.2 の範囲だけ）。
- 強さの既定値の最終決定（実機の測定後）。

## 5. ファイル

```
lib/core/Params.h          humanMoveMs、humanReactMs を足す
lib/core/RaceTime.h/.cpp   3.1〜3.4 の時間の見積もり（新規、純粋関数）
lib/core/Ai.cpp            3.5 の手の選び方に書き換え（danger、danger2、pressure、shape は削除）
lib/core/HumanBot.h/.cpp   BotParams::style と Trap の手順
lib/core/SimDriver.*       変更なし（BotParams を受け渡すだけ）
wasm/bindings.cpp          Params と BotParams の新しい項目を通す
web/index.html、web/app.js 設定に「人間の1手の時間」を足す
tools/autoplay.mjs         --bot random|trap
test/golden/gen_cases.cpp  正解表を作るプログラム（新規。これまで作業用の場所にあったものをリポジトリに入れる）
test/test_race/            RaceTime の単体テスト（新規）
test/test_ai/              罠の局面のテストを足し、古い点数式を前提にしたテストを直す
test/test_sim/             罠 A〜D を時間どおりに再現するテスト、TrapBot のテスト
```

## Due diligence record

この計画は外部の事実（ライブラリの版、外部サービス）に依存しない。

## Implementation steps

共通の前提: 各ステップは1コミット。PC のテストは `pio test -e native`、WASM は `source ~/emsdk/emsdk_env.sh && npm run build:wasm && npm test`、自動対戦は `node tools/autoplay.mjs`。ESP32 のビルドは `node tools/gen-golden.mjs && pio run -e esp32dev`。罠の再現は、盤を文字列から作るヘルパー（test_game や test_ai にあるもの）と、`Game` の `onMachineStep` を使った駒の置き直しで組む。

### Step 1: TrapBot と自動対戦の切り替え、作り直し前の成績の記録

**Files**: `lib/core/HumanBot.h`、`lib/core/HumanBot.cpp`、`lib/core/Params.h`、`wasm/bindings.cpp`、`tools/autoplay.mjs`、`test/test_sim/test_main.cpp`、`plans/2026-10-31_ai-time-race.md`

**Do**:
先に test_sim に TrapBot のテストを書く。`Params` に `humanMoveMs = 400`、`humanReactMs = 300` を足す（このステップでは TrapBot だけが使う）。`BotParams` に `enum class BotStyle { Random, Trap }` の `style`（既定 Random）を足し、3.6 の手順を実装する。2手を続けて置く手順では、1枚目を置いてから `humanMoveMs` 後に2枚目を持ち上げて置く。bindings で `style`（文字列 'random'／'trap' も受ける）と2つの新しい Params を通す。autoplay に `--bot random|trap`（既定 random）を足し、表に bot の列を足す。
最後に、今の読みで `node tools/autoplay.mjs --games 100 --level normal,hard --hcd 3 --bot random,trap` を回し、結果の表をこの計画の 3.7 の下に「作り直し前の成績」として書き足す。

**Test**: TrapBot が罠 A の配置（機械の駒の両隣が空き、別々の2枚が入れる）で2枚を続けて置いて取る、罠 B の配置で片方をどかしてクールダウン後に戻す、罠 C の配置で別の駒を入れる。同じ種で同じ結果になる。

**Verify**: `pio test -e native` と `npm test` が通る。autoplay が bot ごとの表を出す。

### Step 2: RaceTime（時間の見積もり）

**Files**: `lib/core/RaceTime.h`、`lib/core/RaceTime.cpp`、`test/test_race/test_main.cpp`

**Do**:
先にテストを書く。`RaceTime` は Snapshot と Params と強さから、3.1 の `humanCaptureMs`（列ごと）、3.2 の `machineSaveMs`（ヘッドの順番を含む）、3.3 の risk、3.4 の取れる見込みを返す純粋関数にする。列は `struct Line { int8_t side; Sq cells[MAX_N]; int len; Sq endA, endB; }` で表し、盤から縦横の列をすべて列挙する関数を用意する。時間は uint32 の ms ではなく float の秒で持ち、無限大は大きな定数で表す。

**Test**: 罠 A〜D の局面それぞれで `humanCaptureMs` が手で計算した値になる（例: 罠 B で両隣が人間、どいて戻る手順が `ready + 400 + 3000 + 400`）。盤端に達する列は無限大。強さで見ない手順が無限大になる。ヘッドが1つなので、同時に2列が危ないとき2列目の `machineSaveMs` が1列目の処理時間ぶん遅れる。安全な行き先がない駒の列は避けられない。

**Verify**: `pio test -e native -f test_race` が通る。

### Step 3: Ai を時間の競争で書き換え、罠のテスト、正解表の作り直し

**Files**: `lib/core/Ai.cpp`、`test/test_ai/test_main.cpp`、`test/test_sim/test_main.cpp`、`test/golden/gen_cases.cpp`、`test/golden/ai_cases.txt`

**Do**:
先に罠 A〜D を SimDriver で時間どおりに再現するテストを test_sim に書き（人間の操作は lift/place を時刻どおりに呼ぶ）、今の読みで失敗することを確かめる。`think` を 3.5 で書き換える。`gen_cases.cpp` は作業用の場所にあった生成プログラムをもとに、`think` と同じ候補の点数を全部出して最高点と次点の差が0.1以上の局面だけを選ぶ。生成のしかたをファイルの先頭に書く（`g++ -std=gnu++17 -ffp-contract=off -Ilib/core test/golden/gen_cases.cpp lib/core/*.cpp -o /tmp/gen && /tmp/gen 40`）。表を作り直す。test_ai の古い点数式を前提にしたテスト（`test_easy_read_lift_blocks_or_flees` など）は、新しい式で同じ意図が確かめられるように直す。

**Test**: 罠 A〜D の再現テスト（機械の駒が取られない、または取られる前に逃げる）。取れる手を選ぶ、クールダウン中の駒は動かさない、同点ならヘッドに近い駒。正解表のテスト。

**Verify**: `pio test -e native` と `npm test` が通る。`node tools/autoplay.mjs --games 100 --level normal,hard --hcd 3 --bot random,trap` で 3.7 の勝率の条件を満たす。`think` の時間を test_ai で測り、1ms 以内。

### Step 4: 画面の設定と計画の書き換え

**Files**: `web/index.html`、`web/app.js`、`plans/2026-10-30_core-wasm-firmware.md`

**Do**:
設定に「人間の1手の時間」（200〜1000ms、`humanMoveMs`）を足し、`setParams` で渡す。強さの説明文を 2章の分け方に合わせて書き直す。`plans/2026-10-30_core-wasm-firmware.md` の3章「機械の読み」と5.3、5.5、5.8 を、この計画を指す短い記述に置き換える。

**Test**: 自動テストは足さない。`npm test` の既存テストが通ること。

**Verify**: `node --check web/app.js`。`npm run serve` で画面を開き、設定の変更が反映される（ヘッドレスブラウザで確認してよい）。
