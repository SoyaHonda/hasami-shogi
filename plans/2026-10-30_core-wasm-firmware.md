# ソフトウェア計画: 共通C++ロジック、WebAssemblyシミュレータ、実機ファーム

2026-10-30 / 対象: リポジトリ `SoyaHonda/hasami-shogi` のソフトウェア全体。ハードの組み立て・部品・週ごとの予定は `plans/2026-10-30_realtime-hasami-board.md` に従い、そのうちソフトの構成に関する記述はこの計画で置き換える。

## 1. 背景

リアルタイムはさみ将棋の自動盤では、同じゲームのロジック（はさみ判定、クールダウン、勝敗、機械の読み、機械の駒の移動手順）を、ブラウザのシミュレータとESP32のファームの両方で使う。今の `sim/hasami-sim.html` はJavaScriptで書いた試作で、ファームをC++で書くと同じロジックを二重に持つことになり、手を調べる順番や小数の扱いの違いで、シミュレータと実機で選ぶ手がずれるおそれがある。

そこでロジックをC++17で一本化し、PlatformIOでESP32向けとPC向け（単体テスト）に、EmscriptenでWebAssembly向けにビルドする。シミュレータの画面は素のHTMLとJavaScriptのまま、中身をWebAssemblyに差し替える。Node.jsから同じWebAssemblyを呼び、人間役のボットと機械を自動で何千局も対戦させて、設定ごとの勝率を集計する。GitHub Actionsでテストとビルドを回し、シミュレータをGitHub Pagesに公開する。

## 2. 方針

- ロジックは `lib/core` に置き、Arduinoや時計やハードに依存させない。時刻は呼び出し側から渡す。乱数は自動対戦のボットだけが使い、種を固定して再現できるようにする。
- 機械の駒の移動手順（空送り、吸着、1マスずつ引く、取った駒の運び出し）は `MachineController` にまとめ、XYステージは `IGantry` という抽象で受ける。シミュレータとNode.jsは仮想のステージ `VirtualGantry`、ファームは実物の `Gantry` を渡す。これで、移動の手順と時間のかかり方までシミュレータと実機で同じコードになる。
- 機械の読み `Ai::think` は局面の写し（`Snapshot`）だけを読む純粋関数にする。シミュレータは同じスレッドで呼び、ファームはコア0のタスクで呼ぶ。
- 実測で決まる数値（引く速度、空送り速度、吸着時間、クールダウンなど）は `Params` 構造体に集め、シミュレータの画面、Node.jsの集計、ファームで同じ既定値を使う。
- 旧シミュレータ `sim/hasami-sim.html` は、新しいシミュレータで同じ遊び方ができることを確かめるまで残し、確認後に削除する。

## 3. 仕様（ロジックが満たすこと）

ルール: 7×7（`Rules::n` で9まで可）、各7枚、人間は最下段、機械は最上段。縦横に何マスでも、飛び越えなし。動いた側だけがはさみ判定をする（自分から入った駒は取られない）。一直線に並んだ複数枚はまとめて取る。盤端に達する列は取らない。隅の囲み取りはなし。駒ごとのクールダウン（人間3秒、機械1秒）。人間は持ち上げて置き、元のマスに戻すと取り消し（クールダウンなし）。持ち上げ中の駒は盤上にないものとして扱い、はさまれない。機械が引いている途中の駒も同じく盤上にないものとして扱い、はさまれない（2026-10-31 変更）。持ち上げ中は最大2枚。取りが起きたら Removing に入り、機械が取られた駒を全部盤外へ運び終えたら Countdown（1秒）を経て Play に戻る。4枚先取（`diffOn` で枚数差の勝ちも可）。勝負が決まった取りでは運び出さずに Over。ゲーム時計は Play 中だけ進み、クールダウンもこの時計で測る。ジョブ中に取りが起きたら、引いている途中の1マスだけ引き終えてジョブを打ち切り（到着処理はしない、確保は解く。駒が1マスも動いていなければクールダウンも付けない。2026-10-31 変更）、運び出しに移る。取られた駒は、奥（行 −1）と手前（行 n）の置き場のうち経路が短いほうへ運ぶ（同じなら近い辺、それも同じならその駒の側。2026-10-31 変更）。

機械の移動: 一度に一つのジョブだけ。ジョブは、ヘッドを駒の下へ空送り（速度 `freeSpeed`、マスの境目に沿う折れ線。経路は `Path.h` の `travelPath` で決め、実物と仮想のステージで共通）→ 吸着（`engageMs`）→ 1マスずつ引く（速度 `dragSpeed`）。次のマスへ入る前に空きを確かめて確保し、確保したマスには人間は置けない。行く手が埋まったら手前で止まり、止まったマスで到着処理（はさみ判定）をする。

運び出し: 取られた駒ごとに、空きマスの中心だけを通る経路を幅優先探索で探し、人間の駒は手前の置き場（行 n）、機械の駒は奥の置き場（行 −1）の空いた位置へ運ぶ。人間の駒を運ぶときは電磁石の向きを反転する。経路がない駒しか残っていなければ、その駒を `stuck` として Paused にし、人が手で置き場へ移したことを `onManualRemoval` で受けて続ける。

機械の読み: 旧シミュレータと同じ点数式。候補は、クールダウンが終わっている機械の駒の合法手すべてと「動かない」。点数 = 取れる枚数×100 + 評価 − 所要時間×4 − 2（動かないときは評価のみ）。評価 = −危険度×80×(1−0.5×攻め気) + 圧力×(20+60×攻め気) + 攻め気×(隣接数×8 + 前進度×30)。危険度は人間の駒が次の一手で取れる枚数の最大値＋0.25×合計で、持ち上げ中の駒は重み1.6（`readLift` が真のとき）、ほかの駒は強さで決める（Easy 0、Normal 動かせる駒0.35、Hard 動かせる駒1.0・残り1.5秒以内0.7・それより長いクールダウン中0.35）。2026-10-31 追加: 人間の別々の駒2枚が続けて動けば取れる形も危険度に数える。機械の駒の列の両端が空きでそれぞれに別の駒が1手で入れる形と、両端が人間の駒で片方がどいて別の駒が入れる形で、値は 列の枚数×2枚の重みの小さいほう×0.8。危険度は 1手と2手の値の最大値＋0.25×1手の合計。圧力は、機械の駒（クールダウンを問わない）が次の一手で取れる枚数の最大値。所要時間 = ヘッドから駒までの直線距離/空送り速度 + 吸着 + 引く距離/引く速度（旧版と同じ見積もり）。駒は id 順、行き先は下・上・右・左の順に調べ、`s > best + 0.01` のときだけ更新する。計算は float で、距離は `sqrtf` で求める（`hypotf` はライブラリごとに結果が違いうる）。読むタイミングは、前回から `max(150, reactMs)` 経ったとき、または `readLift` が真で人間が駒を持ち上げてから `reactMs` 経ったとき（早い方）。ジョブ中と、読みの結果を待っている間は読まない。

## 4. リポジトリ構成

```
platformio.ini
package.json                     Node.jsのスクリプト（WASMビルド、JSテスト、自動対戦、ローカル表示）。"type": "module"、package-lock.json もコミット（npm ci に必要）
lib/core/                        共通ロジック（C++17、標準ライブラリのみ）
  Types.h                        Cell、Side、Phase、Winner、Level、MagnetMode、座標
  Rules.h  Params.h              ルールと実測パラメータ（既定値）
  Board.h/.cpp                   盤、dests、capturesAt
  Game.h/.cpp                    状態機械
  Ai.h/.cpp                      Snapshot と think
  Path.h/.cpp                    運び出し経路の幅優先探索、空送りの経路 travelPath
  IGantry.h                      XYステージと電磁石の抽象
  VirtualGantry.h/.cpp           シミュレータ用の仮想ステージ（速度と時間だけを模擬）
  MachineController.h/.cpp       読むタイミング、ジョブ、運び出し
  HumanBot.h/.cpp                自動対戦用の人間役（種つき乱数）
  SimDriver.h/.cpp               Game＋MachineController＋VirtualGantry＋（任意で）HumanBot をまとめて時間で回す
  LedPlan.h/.cpp                 ゲーム状態→各マスのLEDの色（ファームとシミュレータで同じ色規則）
lib/hwpure/                      ハード周りの純粋関数（標準ライブラリのみ）
  HallClassify.h/.cpp            センサ値→空/N/S の判定（ヒステリシス）
  BoardWatcher.h/.cpp            センサの盤面の変化→持ち上げ・置き・反則のイベント
  LedIndex.h/.cpp                マス→LED番号（蛇行）
lib/hw/                          ESP32専用
  Pins.h  HallScanner  Gantry（IGantryの実装、FastAccelStepper）  Magnet  Leds（FastLED）
src/main.cpp                     本番ファーム
src/rig/rig_main.cpp             1マス試作の測定用ファーム
test/test_board/ test_game/ test_ai/ test_path/ test_machine/ test_sim/ test_hwpure/   PC用単体テスト（Unity）
wasm/bindings.cpp                Emscriptenのembindで SimDriver をJavaScriptへ公開
web/index.html  web/app.js  web/style.css      シミュレータの画面（旧版の見た目と操作を移植）
web/hasami.js                    WASMビルドの出力（SINGLE_FILE、git管理しない）
tools/build-wasm.mjs             emcc を呼ぶスクリプト（Windows/Linux共通）
tools/autoplay.mjs               自動対戦と集計
tools/test/*.test.mjs            WASMをNode.jsから呼ぶテスト（node --test）
.github/workflows/ci.yml         テスト、ファームのビルド、WASMのビルド、Pages公開
```

`.gitignore` に `.pio/`、`node_modules/`、`web/hasami.js` を入れる。

`platformio.ini` は `native`、`esp32dev`（`build_src_filter = +<*> -<rig/>`）、`rig`（`build_src_filter = -<*> +<rig/>`）の3環境（分けないと `setup`/`loop` が二重定義になる）。共通で `build_unflags = -std=gnu++11`、`build_flags = -std=gnu++17 -ffp-contract=off`（ESP32のArduinoは既定がC++11で、FPUの積和命令による丸めの違いでWASMと選ぶ手がずれるのを防ぐ）。

## 5. コードの要点

### 5.1 core の主な型

```cpp
// Types.h
enum Cell : int8_t { EMPTY = 0, HUMAN = 1, MACHINE = 2 };
enum class Phase { Ready, Countdown, Play, Paused, Removing, Over };
enum class Winner { None, Human, Machine };
enum class Level { Easy, Normal, Hard };
enum class MagnetMode { Off, PullMachine, PullHuman, RepelWeak };
constexpr int MAX_N = 9;

// Rules.h
struct Rules { int n = 7; int target = 4; bool diffOn = false; int diff = 2;
  uint32_t humanCooldownMs = 3000, machineCooldownMs = 1000, countdownMs = 3000, resumeCountdownMs = 1000; };

// Params.h（実測で更新する値。単位はマス）
struct Params { float freeSpeed = 4.0f, dragSpeed = 1.5f; uint32_t engageMs = 200, reactMs = 250;
  Level level = Level::Normal; float aggression = 0.5f; bool readLift = true; };
```

### 5.2 Board と Game

`Board` は `n` と `int8_t cells[MAX_N*MAX_N]`。`int dests(const Board&, int r, int c, int out[])` は下・上・右・左の順に空きマスを返す。`int capturesAt(const Board&, int8_t side, int r, int c, int out[])` は動いた側の取りだけを返す。

`Game` は駒の配列（id、side、r、c、cooldownUntil、alive、held）と、Play 中だけ進む時計を持つ。主なメソッド:

```cpp
void reset(); void start(uint32_t now); void tick(uint32_t now); void togglePause(uint32_t now);
LiftResult onHumanLift(int r, int c, uint32_t now);    // Ok / InCooldown / NotHumanPiece / NotPlaying / TooMany
PlaceResult onHumanPlace(int r, int c, uint32_t now);  // Ok / Cancelled / Illegal / NoHeldPiece
bool machineCanEnter(int pieceId, int r, int c);       // 空きなら確保して true
void onMachineStep(int pieceId, int r, int c);
CaptureResult onMachineArrive(int pieceId, uint32_t now);
int removals(Removal out[]) const;                      // Removing 中に運ぶ駒（id、元のマス、運び先）
void onRemoved(int pieceId); void onManualRemoval(int pieceId); // 全部終われば Countdown へ
Snapshot snapshot(float headR, float headC) const;      // Ai へ渡す写し
```

置きの対応付け: 持ち上げ中の駒のうち、元のマスから置いたマスへ合法に動ける駒を選ぶ。元のマスと同じなら取り消し（先に判定）、両方合法なら元のマスが近い方（同じなら id の小さい方）、どちらも不合法なら Illegal で状態を変えない。確保中のマスへの置きは Illegal。元のマスが埋まり合法な行き先も一つもない駒は、どの空きマスに置いてもよい（旧版と同じ逃げ道。ないと対局が止まる）。Game は直近の反則（マスと時刻）を持ち、LedPlan がそれを読む。

### 5.3 Ai

```cpp
struct Snapshot { Board board; /* 持ち上げ中を除く */ PieceView pieces[2*MAX_N]; int count;
  uint32_t clockMs; float headR, headC; int heldIds[2]; int heldCount; };
struct AiMove { bool none; int pieceId; int toR, toC; float score; };
AiMove think(const Snapshot&, const Params&);
```

### 5.4 IGantry と MachineController

```cpp
class IGantry { public: virtual ~IGantry() = default;
  virtual void travelTo(float r, float c) = 0;   // 空送り（travelPath の折れ線をたどる）
  virtual void dragTo(int r, int c) = 0;         // 隣のマスの中心へ引く
  virtual void magnet(MagnetMode m) = 0;
  virtual bool idle() const = 0; virtual float headR() const = 0; virtual float headC() const = 0;
  virtual void update(uint32_t now) = 0; };

class MachineController { public:
  MachineController(Game&, IGantry&, const Params&);
  bool wantsThink(uint32_t now) const;           // 読むタイミング（3章）
  Snapshot takeSnapshot(uint32_t now);           // 読みの時刻と結果待ちの印を記録（ファームで写しを何度も送らないため）
  void onThinkResult(const AiMove&, uint32_t now); // 結果待ちを解く。古い手は合法性を確かめ、不合法なら捨てる
  void onHumanLift(uint32_t now);                // 読むタイミングを早める
  void update(uint32_t now);                     // ジョブと運び出しを進める
  JobView job() const;                           // 画面とLED用（行き先、段階）
};
```

`VirtualGantry` は `travelTo` で `travelPath` の折れ線の長さ/`freeSpeed`、`dragTo` で1マス/`dragSpeed` の時間をかけてヘッド座標を補間する（加減速は模擬しない）。

### 5.5 SimDriver と HumanBot

```cpp
class SimDriver { public:
  SimDriver(const Rules&, const Params&, uint32_t seed);
  void setParams(const Params&); void setRules(const Rules&); // Rules は次の reset で反映
  void reset(); void start(); void togglePause();
  void advance(uint32_t dtMs);                   // 時刻を進め、機械は同じスレッドで think
  LiftResult lift(int r, int c); PlaceResult place(int r, int c);
  void enableBot(const BotParams&);              // 人間側をボットにする（自動対戦）
  State state() const;                           // 画面用: 盤、駒、ヘッド、ジョブ、LEDの色、時計、取った数、先回り成功数
};
struct BotParams { uint32_t reactMs = 400, holdMs = 600; float greed = 0.8f; };
```

`HumanBot` は動かせる駒からランダムに1枚を選んで持ち上げ（`reactMs` 後）、`holdMs` だけ持ってから、取れる行き先があれば確率 `greed` でその手、なければ合法手から乱数で選んで置く（合法手がなければ取り消すか、5.2の逃げ道）。種が同じなら同じ対局になる。運び出しが `stuck` で Paused になったら、`SimDriver` は人が手で移した扱いで `onManualRemoval` を呼ぶ（シミュレータと自動対戦が止まらないように）。

先回り成功数: 持ち上げた時点でその駒に取れる行き先があり、持ち上げ中に機械がジョブを始め、置いた結果取れなかった回数。

### 5.6 LedPlan（ファームとシミュレータで共通の色規則）

`lib/core/LedPlan` の `void ledPlan(const Game&, const MachineController&, uint32_t now, Rgb out[])`（`State` を入力にすると、`State` が色を含むので循環し、core と hwpure のライブラリも相互依存になる）。`SimDriver::state()` はこの結果を色として含める。持ち上げ中の駒の動かせるマス＝青、クールダウン中の駒のマス＝赤（残りに比例）、Removing 中の取られたマス＝白の点滅、ジョブの行き先＝紫、`readLift` 時の危ないマス＝黄、反則＝赤の速い点滅。シミュレータはこの色をマスの光として描く。

### 5.7 WASM と画面

`wasm/bindings.cpp` は embind で `SimDriver` を公開する（`state()` は `emscripten::val` のオブジェクトで返す）。ビルドは `emcc -std=c++17 -O2 -lembind -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,node -sSINGLE_FILE -sALLOW_MEMORY_GROWTH -o web/hasami.js`、ソースは `lib/core/*.cpp wasm/bindings.cpp`。`tools/build-wasm.mjs` は Windows では `emcc.bat` になるので `shell: true` で起動する（Node.jsは .bat を shell なしで起動できない）。

`web/app.js` は旧シミュレータの画面（盤、駒、凡例、設定、記録、オーバーレイ）を移植し、ロジックは `SimDriver` を呼ぶだけにする。`requestAnimationFrame` で経過時間（上限50ms）を `advance` に渡し、`state()` で描く。設定の変更は `setParams` で反映する。ローカルで開くには `npm run serve`（静的サーバ）を使う（ES モジュールは `file://` では読めないため）。

### 5.8 Node.jsの自動対戦

`tools/autoplay.mjs --games 500 --level easy,normal,hard --hcd 2,3,4 --seed 1` のように条件の組を受け、`advance(10)` の固定刻みで最後まで（ゲーム時計が `--max-min`、既定10分を超えたら引き分けとして打ち切り）回し、条件ごとに引き分けの数、機械の勝率、平均の対局時間、先回り成功数の平均を表で出す（`--csv` でCSV）。

### 5.9 ファーム

- `lib/hw/HallScanner`: 49マスを100Hzで走査し、`HallClassify` で判定、2回連続で同じなら採用。電磁石の通電中はヘッドのいるマス（0.75マス以内）だけ直前の判定を保持。
- `lib/hwpure/BoardWatcher`: 判定済みの盤面の変化を、人間の持ち上げ（人間の駒が消えた）、置き（同じマスに200ms続いた）、機械の駒への接触（機械が動かしていないS極の変化）、初期配置との違いのイベントに変える。
- `lib/hw/Gantry`: `IGantry` の実装。FastAccelStepper（パルスはハードウェアが出す）、80 step/mm（GT2、20歯、1/16）、原点出し、行 −1〜n の範囲。
- `lib/hw/Magnet`: Hブリッジの2本のPWMで `MagnetMode` を実現。
- `lib/hw/Leds`: `LedIndex` と `LedPlan` の結果をFastLEDへ。上限1.5A。
- `src/main.cpp`: コア1で走査→`BoardWatcher`→`Game`・`MachineController`→`Gantry::update`→LED（30Hz）。`wantsThink` で写しをキューでコア0へ送り、コア0の `think` の結果をキューで受けて `onThinkResult`。シリアルコマンド（`scan`、`cal`、`home`、`mv`、`pull`、`ai`、`cd`）。
- `src/rig/rig_main.cpp`: 1マス試作の測定（`pull v a`、`hall`、`pass d`、`led b`）。
- ピン割り当ては制作計画に表がないので、支給のESP32を確かめてから `Pins.h` の一か所で決める（ホールセンサはADC1、WROVERなら16/17は使わない）。A4988はMS1〜3をHigh、RESETとSLEEPを接続。

### 5.10 CI（`.github/workflows/ci.yml`）

- `core`: PlatformIOを入れて `pio test -e native`。
- `firmware`: `pio run -e esp32dev` と `pio run -e rig`。
- `web`: `actions/setup-node`（Node 22以上）と `emscripten-core/setup-emsdk` でEmscriptenを入れ、`npm ci`、`npm run build:wasm`、`npm test`（`node --test "tools/test/*.test.mjs"`。Node 21以降はディレクトリを渡すと探さない）、`node tools/autoplay.mjs --games 20`（落ちないことだけ確かめる）。main のときは同じジョブの中で、ビルド後の `web/` を `actions/upload-pages-artifact` で上げる（`web/hasami.js` はgit管理外なので、別ジョブでは存在しない）。
- `pages`（main のみ、`needs: web`）: `actions/deploy-pages` で公開。`permissions` に `pages: write` と `id-token: write`。リポジトリの Settings で Pages のソースを GitHub Actions にしておく。

## 6. 変えないもの・範囲外

- ルールと点数式（3章）。数値は `Rules` と `Params` で変える。
- ハードの寸法、部品、ピン割り当て、週ごとの予定は制作計画のとおり。
- 範囲外: 9×9の画面対応の作り込み（ロジックは対応、画面は7×7と9×9の切り替えのみ）、人間同士の対戦、ネット対戦、二手以上先の読み、外装。

## 7. 異常時の振る舞い

ハード側の注意は制作計画の「落とし穴」のとおり。ロジックで扱うもの: クールダウン中の持ち上げ（InCooldown、LEDで赤の速い点滅）、斜め・飛び越え・確保中のマスへの置き（Illegal、状態を変えない）、3枚目の持ち上げ（TooMany、無視して反則表示）、機械の行く手への置き（手前で止まって到着処理）、ジョブ中の取り（3章のとおり打ち切って運び出し）、運び出しの経路なし（Paused、手で移すのを待つ）、Removing 中の人間の操作（無視）、持ち上げ中の元のマスへの機械の進入（その駒は取り消しできず、合法な行き先に置く。行き先もなければ5.2の逃げ道）。

## 8. テスト

PC用（`pio test -e native`）:
- board: 1枚はさみ、一直線2枚、盤端で取れない、自分から入っても取られない、縦横同時、`dests` が駒の手前で止まる、9×9でも同じ。
- game: 初期配置、クールダウン中の持ち上げ、Paused 中は時計が止まる、取り→Removing→全部運んで Countdown→Play、4枚目で Over、`diffOn`、取り消しはクールダウンなし、斜めは Illegal、確保中のマスは Illegal、2枚持ちの対応付け、3枚目は TooMany、元のマスが埋まり行き先もない駒の逃げ道、行く手に置かれた機械の駒が手前で止まりそこではさみ判定。
- ai: 取れる手を選ぶ、Easy＋readLift で持ち上げた駒の狙いをふさぐか逃げる（手のあとの危険度0）、readLift=false＋Easy＋aggression=0 で何もしない、同点ならヘッドに近い駒。
- path: 空きマスだけを通る、置き場の空き位置を選ぶ、経路がなければ失敗を返す、`travelPath` がマスの境目だけを通る。
- machine: `VirtualGantry` で、空送り→吸着→引くの各段階の所要時間が `Params` どおり、運び出しで人間の駒は `PullHuman`、機械の駒は `PullMachine`。`wantsThink` がジョブ中と結果待ちの間は偽、持ち上げから `reactMs` で真（readLift=false では早まらない）。不合法になった読みの結果を捨てる。引いている途中の取りでジョブを打ち切って運び出す。経路なしで Paused、`onManualRemoval` で Countdown へ。`ledPlan` の色。
- sim: 同じ種の自動対戦2回が同じ結果になる、ボット対機械の100局が Over か時間切れで終わり、Paused や Removing のまま止まらない。
- hwpure: `classify` のしきい値とヒステリシス、`ledIndex` の蛇行（(0,0)=0、(0,6)=6、(1,6)=7、(1,0)=13、(6,0)=42）、`BoardWatcher` の持ち上げ・置き確定（200ms未満は確定しない）・滑らせたときの途中マス無視・機械の駒への接触検知。

Node（`npm test`）: WASMを読み込んで1局のボット対戦が Over で終わる、`lift`/`place` の戻り値が C++ と同じ、`state()` の形（盤の長さ、駒の数）。

実機（`hardware/test-log.md`）: 制作計画の1マス試作の合格基準と、本番の確認（全マスの判定、20回の引っぱりで取り残し0、10局連続）。

## 9. 確認コマンド

```
pio test -e native
pio run -e esp32dev
pio run -e rig
npm ci
npm run build:wasm      # emcc が PATH にあること（Windowsは emsdk_env.ps1 を実行したPowerShellで）
npm test
npm run serve           # http://localhost:8080 でシミュレータ
node tools/autoplay.mjs --games 200 --level easy,normal --hcd 2,3,4
```

Windowsでの準備: VS CodeにPlatformIO拡張、Node.js LTS、emsdk（`git clone https://github.com/emscripten-core/emsdk`、`emsdk install latest`、`emsdk activate latest`）。PC用テストのためにMinGW-w64のgccをPATHに入れる。

## 10. 実装の順番と時期

1. 第1週: core（Types、Rules、Params、Board、Game）と PC用テスト、CI の core ジョブ。
2. 第1〜2週: Ai、Path、IGantry、VirtualGantry、MachineController、LedPlan、HumanBot、SimDriver とテスト。
3. 第2週: WASMのbindings、web の移植、Node.jsのテストと自動対戦、CI の web と pages。新しいシミュレータで旧版と同じ遊び方ができたら旧版を削除。
4. 第2週（部品到着後）: hwpure（HallClassify、LedIndex）と rig ファーム、CI の firmware。
5. 第3〜5週: HallScanner、BoardWatcher、Gantry、Magnet、Leds。
6. 第6週: main の統合。
7. 第7週: 実測値を `Params` の既定値に入れ、自動対戦で強さの既定値を決める。

## Due diligence record

この節は、計画の外部の事実を2026-10-30に一次情報で確かめた結果の記録で、実装時の事実確認のパスが読む。ここにある行は一度確かめた結論であって今も正しいとは限らないので、どのパスもこの節を理由に確認を省いてはならない。対応する事実を人が手で書き換えたときは、この行を直さずに削除する。

- SETTLED: `build_unflags = -std=gnu++11`（Arduino-ESP32 2.0.17 の CXXFLAGS 既定が `-std=gnu++11`、PlatformIO の espressif32 の最新リリースも Arduino 2.0.17 を同梱。espressif/arduino-esp32 2.0.17 `tools/platformio-build-esp32.py` と platform-espressif32 の Releases で確認）
- SETTLED: `-ffp-contract=off`（GCC は ISO C モード以外では既定が fast で積和に縮約しうる。2.0.17 の CCFLAGS に fp-contract の指定はない。GCC Optimize-Options と同上のファイルで確認）
- SETTLED: `build_src_filter = +<*> -<rig/>` と `build_src_filter = -<*> +<rig/>`（構文と src_dir 相対であることを PlatformIO の build_src_filter のドキュメントで確認）
- SETTLED: `emcc -std=c++17 -O2 -lembind -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,node -sSINGLE_FILE -sALLOW_MEMORY_GROWTH`（各設定の存在と `web,node` の書式を Emscripten の settings reference、`src/settings.js`、embind のドキュメントで確認）
- DECIDED: Windows では `spawnSync('cmd.exe', ['/c', 'emcc.bat', ...args])` で起動する（.bat は shell なしでは起動できず、`shell: true` に配列の引数を渡す書き方は DEP0190 で非推奨のため）
- SETTLED: `node --test "tools/test/*.test.mjs"`（glob の指定は Node.js test のドキュメントで確認。Node 22.23.4 で、ディレクトリ指定は失敗し glob 指定は通ることを手元で確認）
- SETTLED: `emscripten-core/setup-emsdk`（旧名 `mymindstorm/setup-emsdk` は emscripten-core へ移っている。GitHub のリポジトリで確認し、計画を修正済み）
- SETTLED: `actions/upload-pages-artifact`、`actions/deploy-pages`、`pages: write`、`id-token: write`（actions/deploy-pages の README で確認）
- SETTLED: Pages のソースを Settings で GitHub Actions にする手順（GitHub Docs の publishing source の記事で確認）
- SETTLED: `emsdk_env.ps1`（emsdk リポジトリの直下にあることを確認）
- SETTLED: FastAccelStepper は ESP32（IDF 4.x）で MCPWM/PCNT か RMT でパルスを出し、最大 200 kStep/s。Arduino core 3.0.x ではコンパイルできない（FastAccelStepper の README で確認）
- SETTLED: 80 step/mm（GT2 2mm×20歯＝40mm/回転、200×16/40＝80。計算で確認）
- SETTLED: A4988 は MS1〜3 がすべて High で 1/16、RESET は SLEEP につなぐ（Pololu の A4988 の製品ページで確認）
- SETTLED: WROVER は GPIO16/17 を PSRAM に使う（Espressif の ESP-IDF v4.4.7 ドキュメントで確認）
- SETTLED: Node 22 は Maintenance LTS で 2027-04-30 まで（nodejs/release の表で確認）
- DECIDED: Actions はメジャー版を固定する。`actions/checkout@v7`、`actions/setup-node@v7`、`emscripten-core/setup-emsdk@v15`、`actions/upload-pages-artifact` と `actions/deploy-pages` は deploy-pages の README の例で組になっている版を使う（実装時に確認）
- DECIDED: `platform = espressif32` と FastAccelStepper、FastLED は実装時点の版を完全な版番号で `platformio.ini` に固定する（`^` や範囲指定は使わない）。FastAccelStepper は Arduino core 3.x で動かないので、espressif32 は Arduino 2.0.x を同梱する版にする
- DECIDED: emsdk は実装時点の版番号に固定し、リポジトリ直下の `.emsdk-version` に書く。手元は `emsdk install <版>`、CI は setup-emsdk の `version` にその値を渡す
- DECIDED: `npm run serve` は Node.js の標準機能だけで書いた `tools/serve.mjs`（ポート8080）
- DECIDED: deploy ジョブには公式の例のとおり `environment: github-pages` を書く
- DECIDED: ESP32 と WASM で同じ手を選ぶことは、局面と期待する手の表 `test/golden/ai_cases.txt` で確かめる。PC用テスト（Step 4）、Node.js のテスト（Step 5）、ESP32 のシリアルコマンド `golden`（Step 9、表から生成したヘッダを埋め込み、食い違った局面を表示）の三か所で同じ表を読む
- DECIDED: リポジトリは公開にし、シミュレータを GitHub Pages で公開する。公開前に計画から氏名などの個人情報を消す

## Implementation steps

共通の前提: 各ステップは1コミットにまとめる。PC用テストは `pio test -e native`（PlatformIO がなければ `pip install platformio` で入れる）、特定のテストだけなら `pio test -e native -f test_xxx`。WASM は emcc が PATH にあること（なければ emsdk をリポジトリの外に clone して `./emsdk install latest`、`./emsdk activate latest`、`source ./emsdk_env.sh`）。テストは各テストディレクトリの `test_main.cpp` に Unity の `RUN_TEST` で書き、テスト用の盤は文字列（例 `".M.\nH.H\n..."`）から作る小さなヘルパーで組むと読みやすい。旧シミュレータ `sim/hasami-sim.html` の `think`、`capturesAt`、`dests`、`arrive`、`cancelHold` は移植元として読む。

### Step 1: 足場、Board、Path、CI の core ジョブ

**Files**: `platformio.ini`、`.gitignore`、`lib/core/Types.h`、`lib/core/Rules.h`、`lib/core/Params.h`、`lib/core/Board.h`、`lib/core/Board.cpp`、`lib/core/Path.h`、`lib/core/Path.cpp`、`test/test_board/test_main.cpp`、`test/test_path/test_main.cpp`、`.github/workflows/ci.yml`（新規）

**Do**:
先に `test/test_board/test_main.cpp` と `test/test_path/test_main.cpp` を書き、そのあと本体を書く。
`platformio.ini` は共通の `[env]` に `build_unflags = -std=gnu++11` と `build_flags = -std=gnu++17 -ffp-contract=off` を置き、このステップでは `[env:native]`（`platform = native`、`test_framework = unity`、`lib_ignore = hw`）だけを作る。`esp32dev` と `rig` は src がまだないので Step 8・9 で足す。
`.gitignore` に `.pio/`、`node_modules/`、`web/hasami.js`。
`Types.h`、`Rules.h`、`Params.h` は5.1のとおり。`Types.h` には `Rgb { uint8_t r, g, b; }`、盤外も表せる座標 `Sq { int8_t r, c; }` も置く。`Params.h` の既定値は旧シミュレータの `S` と同じ（`freeSpeed 4.0`、`dragSpeed 1.5`、`engageMs 200`、`reactMs 250`、Normal、`aggression 0.5`、`readLift true`）。
`Board.h/.cpp`: `struct Board { int n; int8_t cells[MAX_N*MAX_N]; int8_t at(int r,int c) const; void set(int r,int c,int8_t v); bool inside(int r,int c) const; }`。`int dests(const Board&, int r, int c, int out[])` は下・上・右・左の順（旧版の `DIRS` と同じ）に空きマスの添字 `r*n+c` を返す。`int capturesAt(const Board&, int8_t side, int r, int c, int out[])` は旧版と同じく、各方向に敵が続き、その先が自分の駒のときだけ取り、盤端に達した列は取らない。
`Path.h/.cpp`: `int pickReserveSlot(const bool used[], int n, int preferC)` は置き場の空き列から `preferC` に最も近い列（同じ近さなら小さい列）を返し、空きがなければ −1。`bool removalPath(const Board&, Sq from, int toRow, const bool reserveUsed[], const bool blocked[], Sq out[], int& len, int& slotC)` は `from` から、空きマスの中心（`blocked` のマスは通らない）だけを上下左右に通って、置き場の行 `toRow`（n または −1）の空き列に着く最短経路を幅優先探索で求める（`from` 自身は駒のいるマスなので出発点としてだけ使う）。着く列は最初に届いた空き列、同じ長さなら `pickReserveSlot` の順。経路がなければ false。`int travelPath(float fromR, float fromC, int toR, int toC, float outR[], float outC[])` は空送りの折れ線を返す: まず最寄りのマスの角（行・列とも x.5）へ寄り、マスの境目（x.5 の線）に沿って行方向・列方向の順に進み、最後に目的のマスの中心へ入る。出発点と目的地は折れ線に含める。`float polylineLength(...)` も用意する（VirtualGantry と所要時間の見積もりで使う）。
`ci.yml`: `on: push, pull_request`。ジョブ `core` は checkout、setup-python、`pip install platformio`、`pio test -e native`。Actions のメジャー版は Due diligence の記録にある2026-10-30時点の最新を使う。

**Test**:
board: 1枚はさみ（`H M H` の横で M が取れる）、一直線2枚（`H M M H` で2枚）、盤端で取れない（`M M H` が左端に接する）、自分から入っても取られない（M が `H _ H` の間に入っても `capturesAt(MACHINE, …)` は空で、人間側の判定は呼ばれない）、縦横同時（十字に2方向取れる）、`dests` が駒の手前で止まり下・上・右・左の順で返す、9×9でも同じ結果。
path: 空きマスだけを通る（障害のある盤で経路の全マスが空き）、置き場の空き位置を選ぶ（埋まっている列を避け、`preferC` に近い列）、経路がなければ false、`travelPath` の途中点がすべてマスの境目上（行か列が x.5）にある。

**Verify**: `pio test -e native` で test_board と test_path が全件通る。

### Step 2: Game の状態機械

**Files**: `lib/core/Game.h`、`lib/core/Game.cpp`、`lib/core/Ai.h`（型の定義だけ）、`test/test_game/test_main.cpp`

**Do**:
先に `test/test_game/test_main.cpp` を書く。
`Ai.h` に5.3の `PieceView { int id; int8_t side; int r, c; uint32_t cooldownUntil; bool alive, held; }`、`Snapshot`、`AiMove` と `think` の宣言だけを置く（本体は Step 3）。
`Game.h/.cpp` は5.2のメソッドを実装する。駒の id は旧版と同じく、列ごとに機械（行0）、人間（行 n−1）の順で 1 から振る。`now` は呼び出し側の時刻で、Game は Play 中だけ `clockMs` を進め（前回の `now` との差を足す）、クールダウンはこの時計で測る。
フェーズ: `start` で Ready→Countdown（`countdownMs`）、`togglePause` で Play→Paused、Paused→Countdown（`resumeCountdownMs`）、Countdown が終われば `tick` で Play。Over と Ready からの `start` は `reset` してから始める。
`onHumanLift`: Play 以外は NotPlaying、人間の駒でなければ NotHumanPiece、クールダウン中は InCooldown、すでに2枚なら TooMany。InCooldown と TooMany は反則として記録する。
`onHumanPlace`: Play 以外なら `PlaceResult::NotPlaying`（列挙に足す）。対応付けは5.2のとおり（元のマスなら取り消し、合法な駒が複数なら元のマスが近い方、同じなら id の小さい方、確保中のマスは Illegal、どれも不合法なら Illegal として反則を記録し状態を変えない）。元のマスが埋まり（機械が入った等）合法な行き先もない駒は、どの空きマスにも置ける。置いたら人間のクールダウンを付け、人間側のはさみ判定をする。
機械側: `machineCanEnter` は空きで確保されていなければ確保して true（持ち上げ中の駒の元のマスは空きとみなす）。`onMachineStep` は駒をそのマスへ動かし確保を解く。Removing 中でも呼べて、取られた駒なら運び出しの元のマスを更新する。`onMachineArrive` は機械のクールダウンを付け、機械側のはさみ判定。`void onMachineAbort(int pieceId, uint32_t now)` を足し、ジョブ打ち切りで確保を解きクールダウンを付ける（到着処理はしない）。
取り: 取られた駒を `alive=false` にし、取った数を足す。勝負が決まれば Over（運び出しなし）、そうでなければ Removing に入り、取られた駒を運び出し待ちに積む。`removals` は運び出し待ちの駒ごとに id、元のマス、置き場の行（近い辺の行、同じならその駒の側）と、`pickReserveSlot` で仮に選んだ列を返す。Game は置き場の行ごとの使用状況 `reserveUsed(int toRow)` を持ち、両者で共有する。`onRemoved(pieceId, toRow, slotC)` と `onManualRemoval(pieceId)` で運び出し済みにし、全部終われば Countdown（`resumeCountdownMs`）へ。`void onRemovalStuck(int pieceId, uint32_t now)` を足し、Paused にして `stuckId()` を立てる。`stuckId` が立っている間は `togglePause` で再開しない。
反則: `void reportViolation(int r, int c, uint32_t now)` と `lastViolation()`（マスとゲーム時計でなく `now` の時刻）を持つ。
読み出し: `phase()`、`winner()`、`clockMs()`、`captured(side)`、`pieces()`、`heldIds()`、`reservedCell()`、`boardWithoutHeld()`、`boardWithReserve()`、`snapshot(headR, headC)`。

**Test**:
初期配置（7×7で各7枚、id 順）、クールダウン中の持ち上げが InCooldown で反則が記録される、Paused 中は時計が止まる、取り→Removing→全部 `onRemoved` で Countdown→1秒後に Play、4枚目の取りで運び出しなしに Over、`diffOn` で枚数差の勝ち、取り消しはクールダウンなし、斜めの置きは Illegal で状態が変わらない、確保中のマスへの置きは Illegal、2枚持ちの対応付け（両方合法なら元のマスが近い方）、3枚目は TooMany、元のマスが埋まり行き先もない駒は任意の空きマスへ置ける、行く手に置かれた機械の駒は `machineCanEnter` が false になり手前のマスの `onMachineArrive` ではさみ判定が働く、`snapshot` が持ち上げ中の駒を盤から除き `heldIds` に入れる。

**Verify**: `pio test -e native` で test_board、test_path、test_game が全件通る。

### Step 3: Ai、IGantry、VirtualGantry、MachineController

**Files**: `lib/core/Ai.cpp`、`lib/core/IGantry.h`、`lib/core/VirtualGantry.h`、`lib/core/VirtualGantry.cpp`、`lib/core/MachineController.h`、`lib/core/MachineController.cpp`、`test/test_ai/test_main.cpp`、`test/test_machine/test_main.cpp`

**Do**:
先に `test/test_ai/test_main.cpp` と `test/test_machine/test_main.cpp` を書く。
`Ai.cpp`: 旧版 `think` を3章の式どおり移植する。危険度の重み（Easy 0、Normal 動かせる駒0.35、Hard 動かせる駒1.0・残り1.5秒以内0.7・それより長いクールダウン中0.35、持ち上げ中は `readLift` のとき1.6。2手の危険度は3章のとおり）、圧力、形（隣接×8＋前進度×30、前進度は機械の駒の `r/(n−1)` の和）、所要時間（ヘッドから駒までの直線距離 `sqrtf(dr*dr+dc*dc)`/`freeSpeed` ＋ `engageMs/1000` ＋ マス数/`dragSpeed`）、点数（取り×100＋評価−所要時間×4−2、動かないときは評価のみ）。すべて float、`hypotf` は使わない。駒は id 順、行き先は `dests` の順、`s > best + 0.01f` のときだけ更新。何も選ばなければ `none = true`。
`IGantry.h` は5.4のとおり。
`VirtualGantry`: コンストラクタで `Params` を受ける。`travelTo` は `travelPath` の折れ線を長さ/`freeSpeed` 秒で等速にたどり、`dragTo` は1マスを 1/`dragSpeed` 秒で直線補間する（置き場の行 −1 と n へも引ける）。`update(now)` で位置を進め、着いたら `idle()`。`magnet` は現在のモードを覚えるだけ（テスト用に `magnetMode()` を公開）。
`MachineController`: 5.4のメソッドを実装する。読むタイミングはゲーム時計で測り、前回の読みから `max(150, reactMs)`、または `readLift` が真で直近の持ち上げから `reactMs` の早い方。Play でないとき、ジョブ中、結果待ちの間は偽。Countdown から Play に戻ったら、前回の読みを今に置き直す（旧版と同じく `reactMs` 後に最初の読み）。`onThinkResult` は結果待ちを解き、駒が生きていてクールダウンが終わり、今の盤で行き先が合法なときだけジョブを始める（不合法なら捨てる）。
ジョブの段階: Travel（`travelTo` 駒のマス、磁石 Off）→ Engage（`PullMachine`、`engageMs` 待つ）→ Drag（次のマスを `machineCanEnter` で確保できたら `dragTo`、着いたら `onMachineStep`、行き先に着くか確保できなければ `onMachineArrive`）→ 磁石 Off でジョブ終了。Drag 中に Game が Removing になったら、引いている1マスを引き終えて `onMachineStep`、`onMachineAbort` を呼び、運び出しへ移る。
運び出し: Removing の間、`removals` の駒を id 順に見て、`removalPath`（持ち上げ中の駒の元のマスは `blocked`）で経路が見つかった最初の駒を運ぶ。Travel → Engage（人間の駒は `PullHuman`、機械の駒は `PullMachine`）→ 経路を1マスずつ `dragTo` → 磁石 Off → `onRemoved(id, slotC)`。どの駒も経路がなければ最初の駒で `onRemovalStuck`。
`JobView { bool active; int pieceId; int toR, toC; Stage stage; bool removing; }` を `job()` で返す。

**Test**:
ai: 取れる手を選ぶ、Easy＋readLift で持ち上げた駒の狙いをふさぐか逃げる（選んだ手のあとの危険度が0）、readLift=false＋Easy＋aggression=0 で `none`、同点ならヘッドに近い駒を選ぶ。
machine（`VirtualGantry` を使う）: 空送り・吸着・1マス引くの各段階の所要時間が `Params` どおり（±1刻み）、運び出しで人間の駒は `PullHuman`・機械の駒は `PullMachine`、`wantsThink` がジョブ中と結果待ちの間は偽、持ち上げから `reactMs` で真（readLift=false では早まらない）、不合法になった読みの結果を捨てる、引いている途中の取りでジョブを打ち切って運び出す（引いている途中の駒ははさまれない）、経路なしで Paused になり `onManualRemoval` で Countdown へ。

**Verify**: `pio test -e native` で test_ai と test_machine を含む全件が通る。

### Step 4: LedPlan、HumanBot、SimDriver

**Files**: `lib/core/LedPlan.h`、`lib/core/LedPlan.cpp`、`lib/core/HumanBot.h`、`lib/core/HumanBot.cpp`、`lib/core/SimDriver.h`、`lib/core/SimDriver.cpp`、`test/test_sim/test_main.cpp`、`test/test_machine/test_main.cpp`（ledPlan のテストを追加）

**Do**:
先に `test/test_sim/test_main.cpp` と test_machine への ledPlan のテストを書く。
`LedPlan`: 5.6の `void ledPlan(const Game&, const MachineController&, uint32_t now, Rgb out[])`。色は 青 (0,0,255)、赤 (255,0,0)×残り/クールダウン長、白 (255,255,255) の点滅（500msごと）、紫 (160,0,255)、黄 (255,200,0)、反則の赤の速い点滅（100msごと、1秒間）、それ以外は消灯。重なったときの優先は 反則 > 白 > 紫 > 黄 > 青 > 赤。黄は `readLift` のとき、持ち上げ中の駒の行き先のうち人間が置くと取れるマス（旧版の `threat`）。
`HumanBot`: 5.5のとおり。乱数は自前の xorshift32 など、標準ライブラリの分布関数に頼らず環境で結果が変わらないものにする。`BotParams` は5.5の既定値。
`SimDriver`: 5.5のとおり。`advance(dt)` は 時刻を進める → `game.tick` → `controller.update` → `gantry.update` → `wantsThink` なら `takeSnapshot`→`think`→`onThinkResult` → ボット、の順。`Paused` かつ `stuckId` が立っていたら `onManualRemoval` を呼ぶ。先回り成功数は5.5の定義どおり SimDriver が数える。`State` は、フェーズ、勝者、n、盤、駒（id、side、r、c、alive、held、クールダウン残り、表示位置 x/y：磁石で引いている駒はヘッドの位置）、ヘッド、`JobView`、LED の色（n×n）、ゲーム時計、両者の取った数、先回り成功数、機械の手数、`stuckId`、直近の出来事（取り・先回り成功を最大40件、通し番号つき）を持つ。

**Test**:
ledPlan: 持ち上げ中の駒の行き先が青、クールダウン中のマスが残りに比例した赤、ジョブの行き先が紫、反則のマスが点滅する（100ms ずらすと消灯と点灯が入れ替わる）。
sim: 同じ種の自動対戦2回が同じ結果（勝者、時計、取った数、先回り成功数）、ボット対機械の100局（種 1〜100、`advance(10)`、ゲーム時計10分で打ち切り）がすべて Over か時間切れで終わり、最後に Paused や Removing のまま止まっていない。

**Verify**: `pio test -e native` で全件通る。100局のテストが数十秒以内に終わること。

追加（ESP32 と WASM の一致の確認）: `test/golden/ai_cases.txt` を作る。1行1局面で、盤（n×n の `.HM` の文字列）、持ち上げ中の駒、ヘッド位置、クールダウン残り、Params、期待する手を空白区切りで書く。期待する手は最高点と次点の差が0.1以上ある局面だけを選ぶ。`test_ai` にこの表を読んで `think` の結果と突き合わせるテストを足す。

### Step 5: WASM のバインディング、Node.js のテストと自動対戦、CI の web ジョブ

**Files**: `package.json`、`package-lock.json`、`wasm/bindings.cpp`、`tools/build-wasm.mjs`、`tools/autoplay.mjs`、`tools/test/sim.test.mjs`、`.github/workflows/ci.yml`（web ジョブを追加）

**Do**:
先に `tools/test/sim.test.mjs` を書く。
`package.json`: `"type": "module"`、`engines.node >= 22`、scripts は `build:wasm`（`node tools/build-wasm.mjs`）、`test`（`node --test "tools/test/*.test.mjs"`）、`serve`（`node tools/serve.mjs`。Step 6 で作る）、`autoplay`（`node tools/autoplay.mjs`）。依存パッケージは持たない。`npm install` で `package-lock.json` を作ってコミットする。
`wasm/bindings.cpp`: embind で `SimDriver` を公開する。`enum_` で `LiftResult`、`PlaceResult`、`Phase`、`Winner`、`Level` を登録。コンストラクタと `setParams`/`setRules` は JavaScript の素のオブジェクトを受け、欠けた項目は C++ の既定値で埋める。`defaults()` 関数で `Rules` と `Params` の既定値をオブジェクトで返し、画面と自動対戦はここから既定値を取る（既定値を JavaScript に書かない）。`enableBot(obj)`、`state()` は `emscripten::val` のオブジェクト（`cells`、`leds` は配列）で返す。
`tools/build-wasm.mjs`: 5.7のコマンドを `child_process.spawnSync` で起動（Windows では `spawnSync('cmd.exe', ['/c', 'emcc.bat', ...args])`、ほかは `emcc`）、ソースは `lib/core/*.cpp` と `wasm/bindings.cpp`、`-Ilib/core`。失敗したら終了コード1。
`tools/autoplay.mjs`: 5.8のとおり。`--games`（既定100）、`--level`、`--hcd`（人間のクールダウン秒のリスト）、`--seed`（既定1、局ごとに seed＋局番号）、`--max-min`（既定10）、`--csv`。条件ごとに 局数、引き分け、機械の勝ち、機械の勝率（引き分けを除く）、平均の対局時間、先回り成功数の平均を表で出す。
`ci.yml` に `web` ジョブ: setup-node（22）、`emscripten-core/setup-emsdk`、`npm ci`、`npm run build:wasm`、`npm test`、`node tools/autoplay.mjs --games 20`。

**Test**:
WASM を読み込んで1局のボット対戦が Over（または時間切れ）で終わる、`lift`/`place` の戻り値が C++ と同じ列挙（クールダウン中の持ち上げが `LiftResult.InCooldown`、斜めの置きが `PlaceResult.Illegal`、元のマスが `PlaceResult.Cancelled`）、`state()` の形（7×7で `cells.length === 49`、`leds.length === 49`、駒14枚）、`defaults()` が C++ の既定値（`freeSpeed 4`、`humanCooldownMs 3000` など）を返す。

**Verify**: `npm ci`、`npm run build:wasm` で `web/hasami.js` ができる、`npm test` が全件通る、`node tools/autoplay.mjs --games 20 --level easy,normal --hcd 2,3` が表を出して終わる。`pio test -e native` も通ったまま。

追加: `tools/test/golden.test.mjs` で同じ `test/golden/ai_cases.txt` を読み、WASM に公開した `thinkCase(行)` の結果と突き合わせる。

### Step 6: シミュレータの画面の移植と Pages 公開

**Files**: `web/index.html`、`web/style.css`、`web/app.js`、`tools/serve.mjs`、`.github/workflows/ci.yml`（Pages を追加）

**Do**:
`web/index.html` と `web/style.css` は旧シミュレータのマークアップと CSS（盤、駒、凡例、設定、記録、遊び方、オーバーレイ）をそのまま分けて移す。
`web/app.js`: `import createModule from './hasami.js'` で読み込み、`defaults()` から設定の初期値を作る。旧版の描画（`build`、`render`、`statusText`、`showOverlay`、記録）を移植し、ロジックは `SimDriver` の `lift`/`place`/`start`/`togglePause`/`reset` を呼ぶだけにする。`requestAnimationFrame` で経過時間（上限50ms）を `advance` に渡し、`state()` で描く。マスの光は `state().leds` の色で描く（旧版のクラスによる色分けを置き換える）。クリックは、持ち上げ中でなければ `lift`、持ち上げ中なら `place`（元のマスで取り消し、Esc で持ち上げ中の駒を元のマスに `place`）。設定の変更は `setParams`、盤の大きさと勝ちの枚数は `setRules` のあと `reset`。記録は `state()` の出来事を通し番号で差分を取って足す。Paused で `stuckId` が立つことは SimDriver が処理するので画面では扱わない。
`tools/serve.mjs`: Node.js 標準の `http` だけで `web/` を 8080 番で配る静的サーバ（`.js` は `text/javascript`）。
`ci.yml`: web ジョブで main のときだけ `actions/upload-pages-artifact`（path `web`）、`pages` ジョブ（`needs: web`、main のみ、`permissions: pages: write, id-token: write`、`environment: github-pages`、`actions/deploy-pages`）。
旧版 `sim/hasami-sim.html` はこのステップでは消さない。ユーザーが新版で数局遊び、旧版と同じ感触だと判断したら、別のコミットで削除する。

**Test**: 自動テストは足さない（画面の確認は手で行う）。`npm test` の既存テストが通ること。

**Verify**: `npm run build:wasm`、`npm test` が通る。`npm run serve` を起動し、`curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/` と `.../app.js`、`.../hasami.js` が 200。`node --check web/app.js` が通る。ブラウザでの見た目と操作（持ち上げ、置き、取り消し、取り→運び出し→再開、勝敗、設定の反映）はユーザーが確かめる。

### Step 7: hwpure（HallClassify、LedIndex、BoardWatcher）

**Files**: `lib/hwpure/HallClassify.h`、`lib/hwpure/HallClassify.cpp`、`lib/hwpure/LedIndex.h`、`lib/hwpure/LedIndex.cpp`、`lib/hwpure/BoardWatcher.h`、`lib/hwpure/BoardWatcher.cpp`、`test/test_hwpure/test_main.cpp`

**Do**:
先に `test/test_hwpure/test_main.cpp` を書く。hwpure は標準ライブラリと core の `Types.h` だけを使う（core から hwpure は参照しない）。
`HallClassify`: `struct HallThresholds { int center; int enter; int exit; bool southHigh; }`（値は1マス試作で測って決めるので、既定値は仮で ADC 12bit の中央付近）。`Cell classify(int raw, Cell prev, const HallThresholds&)` は `|raw−center|` が `enter` 以上で N（人間）か S（機械）に、判定済みの駒は `exit` を下回るまで保つ（ヒステリシス）。どちらの向きが S かは `southHigh` で決める。
`LedIndex`: `int ledIndex(int r, int c, int n)` は蛇行（偶数行は左から、奇数行は右から）。
`BoardWatcher`: `struct WatchEvent { enum Kind { Lift, Place, MachineTouch } kind; int r, c; }`。`int update(const int8_t sensed[], const int8_t expected[], const bool machineCells[], uint32_t now, WatchEvent out[])`。`expected` は Game が思っている盤（持ち上げ中の駒を除く）、`machineCells` は機械が今動かしているマス（ジョブの駒のマス、確保中のマス、ヘッドの近く）。人間の駒が `expected` にあるマスから消えたら Lift。`expected` で空のマスに N が現れ、同じマスで200ms続いたら Place（その前に消えたら何も出さないので、滑らせたときの途中のマスは無視される）。`machineCells` 以外で S の有無が `expected` と違ったら MachineTouch（同じマスで一度だけ）。`int diffInitial(const int8_t sensed[], const int8_t expected[], int out[])` は初期配置と違うマスを返す。

**Test**:
`classify` のしきい値（`enter` の手前は空、超えたら N か S）、ヒステリシス（`enter` と `exit` の間は直前の判定を保つ）、`ledIndex` の蛇行（(0,0)=0、(0,6)=6、(1,6)=7、(1,0)=13、(6,0)=42）、`BoardWatcher` の持ち上げ検知、置きは200ms続いて確定、199msでは確定しない、滑らせたとき途中のマスに一瞬出た N を無視して最後のマスだけ Place、機械の駒への接触検知（`machineCells` のマスは無視）、`diffInitial`。

**Verify**: `pio test -e native` で test_hwpure を含む全件が通る。

### Step 8: rig ファーム（1マス試作の測定）と CI の firmware ジョブ（実機での確認は後日）

**Files**: `platformio.ini`（`[env:rig]` を追加）、`lib/hw/Pins.h`、`lib/hw/Magnet.h`、`lib/hw/Magnet.cpp`、`lib/hw/Leds.h`、`lib/hw/Leds.cpp`、`src/rig/rig_main.cpp`、`hardware/test-log.md`、`.github/workflows/ci.yml`（firmware ジョブを追加）

**Do**:
`platformio.ini` の `[env:rig]`: `platform = espressif32`（Arduino 2.0.17 を同梱する版に固定）、`board = esp32dev`、`framework = arduino`、`build_src_filter = -<*> +<rig/>`、`lib_deps` に FastAccelStepper と FastLED（その時点の版に固定）、`monitor_speed = 115200`。
`Pins.h`: 5.9のとおり仮の割り当てを一か所に置く。ホールの4本は ADC1（GPIO32〜39）、GPIO6〜11 と 16/17 は使わない、ストラップピン（0、2、5、12、15）はなるべく避ける。A4988 の MS1〜3 は High 固定・RESET と SLEEP は配線でつなぐ前提。
`Magnet`: Hブリッジの2本の PWM（`ledcSetup`/`ledcWrite`、Arduino 2.0 の API）で `MagnetMode` を出す（Off は両方0、PullMachine と PullHuman は逆向き、RepelWeak は弱い逆向き。デューティは定数で持つ）。
`Leds`: FastLED で WS2812B、`FastLED.setMaxPowerInVoltsAndMilliamps(5, 1500)`。`void show(const Rgb colors[], int n)` は `ledIndex` で並べ替えて出す。
`rig_main.cpp`: シリアルコマンド `pull v a`（電磁石を PullMachine にして1軸を v mm/s・加速度 a mm/s² で1マス33.3mm 往復、FastAccelStepper、80 step/mm）、`hall`（試作マスのセンサの生の値と `classify` の結果を10Hzで表示、何か送ると止まる）、`pass d`（電磁石を通電したままヘッドを d mm 動かし、その間のセンサ値の最大の変化を表示）、`led b`（試作マスの LED を明るさ b で赤と青に交互に点ける）。
`hardware/test-log.md`: 制作計画の1マス試作の合格基準の表に、測定値と日付を書く欄を足した雛形。
`ci.yml` に `firmware` ジョブ: PlatformIO を入れて `pio run -e rig`（`esp32dev` は Step 9 で足す）。

**Test**: 自動テストはない（ハードに依存）。実機での測定は1マス試作ができてから `hardware/test-log.md` に記録する。

**Verify**: `pio run -e rig` がビルドに成功する（初回はツールチェーンを取りに行くので時間がかかる）。`pio test -e native` が通ったまま（native で `lib/hw` を拾わないこと）。

### Step 9: HallScanner、Gantry と立ち上げ用の esp32dev ファーム（実機での確認は後日）

**Files**: `platformio.ini`（`[env:esp32dev]` を追加）、`lib/hw/HallScanner.h`、`lib/hw/HallScanner.cpp`、`lib/hw/Gantry.h`、`lib/hw/Gantry.cpp`、`src/main.cpp`（立ち上げ用）、`.github/workflows/ci.yml`（`pio run -e esp32dev` を追加）

**Do**:
`platformio.ini` の `[env:esp32dev]`: rig と同じ platform・board・lib_deps で `build_src_filter = +<*> -<rig/>`。
`HallScanner`: CD74HC4067×4 の選択線とマス→(マルチプレクサ, チャンネル) の表（`Pins.h` に置く）で49マスを100Hzで走査し、`classify` で判定、2回続けて同じなら採用する。`void setHold(bool magnetOn, float headR, float headC)` で、通電中はヘッドから0.75マス以内のマスだけ直前の判定を保つ。`const int8_t* cells() const`、`int raw(int r, int c) const`、`void calibrate()`（駒のない状態で各マスの中心値を測って `HallThresholds.center` をマスごとに持つ）。
`Gantry`: `IGantry` の実装。FastAccelStepper で X・Y、80 step/mm、1マス33.3mm、行 −1〜n の範囲で座標を mm に変換。`travelTo` は `travelPath` の折れ線を1区間ずつ `freeSpeed` で、`dragTo` は隣のマスへ `dragSpeed` で動かす。`magnet` は `Magnet` へ渡す。`bool home()` はリミットスイッチで原点出し。`update(now)` で次の区間を出し、全区間が終われば `idle()`。
`src/main.cpp`（立ち上げ用）: シリアルコマンド `scan`（49マスの判定を7×7で表示）、`cal`（`calibrate`）、`home`、`mv r c`（`travelTo`）、`pull r c`（今のマスの駒を吸着して隣のマス r c へ `dragTo`）。LED は `Leds` で全消灯。ゲームはまだつながない（Step 10）。
`ci.yml` の firmware ジョブに `pio run -e esp32dev` を足す。

**Test**: 自動テストはない（ハードに依存）。全マスの判定と、任意のマス間で駒を引けることは、XYステージと49マスの配線ができてから実機で確かめて `hardware/test-log.md` に書く。

**Verify**: `pio run -e esp32dev` と `pio run -e rig` がビルドに成功する。`pio test -e native` が通ったまま。

追加: `tools/gen-golden.mjs` で `test/golden/ai_cases.txt` から `src/golden_cases.h` を生成し（git 管理しない、ビルド前に生成）、立ち上げ用ファームにシリアルコマンド `golden` を足して、ESP32 上で全局面の `think` を実行し、食い違った局面を表示する。

### Step 10: 本番ファームの統合（実機での確認は後日）

**Files**: `src/main.cpp`

**Do**:
Step 9 の立ち上げ用コマンドを残したまま、ゲームをつなぐ。
コア1（`loop`）: `HallScanner` の走査 → `BoardWatcher::update`（`expected` は `game.boardWithoutHeld()`、`machineCells` は `controller.job()` とヘッドの位置から作る）→ Lift は `game.onHumanLift` と `controller.onHumanLift`、Place は `game.onHumanPlace`、MachineTouch は `game.reportViolation` → `game.tick` → `controller.update` → `gantry.update` → `HallScanner::setHold` → 30Hz で `ledPlan` と `Leds::show`。
読み: `controller.wantsThink(now)` が真なら `takeSnapshot` を FreeRTOS のキュー（長さ1）でコア0へ送る。コア0のタスク（`xTaskCreatePinnedToCore`）が `think` を呼び、結果をもう一つのキューで返し、コア1が受け取って `onThinkResult`。
シリアルコマンドに `ai`（今の局面で `think` を同期で呼び、選んだ手と点数を表示）と `cd`（`game.start`、対局開始のカウントダウン）を足す。開始前に `diffInitial` で初期配置との違いを表示し、違いがあれば開始しない。
`Params` と `Rules` は core の既定値を使う。

**Test**: 自動テストはない。実機で、持ち上げ・置きが Game に届くこと、機械が先回りすること、取った駒の運び出し、10局連続で止まらないことを確かめて `hardware/test-log.md` に書く。実測値の `Params` への反映と強さの既定値の決定（10章の7）は、実機の測定がそろってから別に行う。

**Verify**: `pio run -e esp32dev` と `pio run -e rig` がビルドに成功する。`pio test -e native` と `npm test` が通ったまま。
