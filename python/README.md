# 学習の環境（hasami_env）

C++ のコア（`lib/core`）で N 局をまとめて進める環境を、pybind11 で Python から呼ぶ。配列の約束は `plans/2026-11-12_ai-enshu-scope.md` の 4 章、入力のチャンネルと行動の番号は `lib/core/Features.h`。

## 準備とテスト

C++17 のコンパイラ（g++ か clang++）が要る。リポジトリの根で:

```bash
pip install -r python/requirements.txt
```

```bash
python -m pytest python
```

テストの前に `python/conftest.py` が `hasami_env/_core*.so` をその場でビルドする（初回は数十秒。`lib/core` が変わっていなければ1秒ほど）。

## 学習のスクリプトから使う

ビルドしたモジュールを読むには、どちらかにする。

```bash
pip install --no-build-isolation -e python
```

```bash
python python/setup.py build_ext --inplace && export PYTHONPATH="$PWD/python"
```

```python
import hasami_env
env = hasami_env.BatchEnv(n_envs=256, seed=1, opponent="b1", speed_scale=1.0)
obs, scal, mask = env.reset()
obs, scal, mask, reward, dt_ms, done, info = env.step(actions)
```

`lib/core` を変えたら、`pip install -e` の場合も入れ直すか `build_ext --inplace` をやり直す。

## 学習のひな形（python/train）

`model.py`（ネットワーク）、`ppo.py`（PPO の学習）、`evaluate.py`（今の読みとの比較、200 局）、`plot_curve.py`（学習曲線）、`record_game.py`（1 局の GIF）。PyTorch が要るので、演習の VM で使う。`python/tests/test_train.py` は PyTorch があるときだけ動き（ないときはスキップ）、小さな設定で学習・評価・曲線・録画を一通り回す。`try_random.py` は環境を試すためのランダムな機械（マニュアルの第2章）。手順は学習担当マニュアル（`docs/training-manual.md`）にある。

## リーグ（python/train/league.py）

計画 2026-11-12 の 7.2〜7.3。人間側も Python から指せる `hasami_env.DuelEnv` を使い、機械の主役と人間の主役を同時に PPO で学習する。機械は B1 相手の BatchEnv と DuelEnv の両方で指し、DuelEnv の人間は半分が人間の主役、残りが人間の過去の版（PFSP で選ぶ）。`--exploit` は、止めた機械（`machine.pt` や `model.pt`、または `read` で手書きの読み）だけを狙う人間を学習する。

```bash
python python/train/league.py --init-machine runs/b1_s10/model.pt --out runs/league
```

```bash
python python/train/league.py --out runs/league --resume
```

```bash
python python/train/league.py --exploit read --out runs/exploit_read
```

50 回の更新ごとと最後に `machine.pt`（`evaluate.py --model` で読める）、`human.pt`、`checkpoint.pt`、`pool/human_XXXXX.pt` を保存し、`log.csv` に更新ごとの成績（B1 への勝率、人間の主役への勝率と枚数、過去の版への勝率）を書く。`--exploit` のときの main の列は、攻略役を相手にした機械の成績。

## 速さ

```bash
python python/bench_env.py
```

ランダムな方策で、相手ごと・スレッド数ごとの決定点／秒を出す。

## コンパイラのない機械で使う（演習の Azure VM）

演習の VM（aiexp-XX）には g++ も Python.h もなく、sudo もないので、その場ではビルドできない（2026-11-12 に aiexp-03 で確認）。そこで、同じ Ubuntu 24.04・Python 3.12 でビルドした wheel を `python/wheels/` に置いてある。VM では、`~/venv` を有効にしたうえで、リポジトリの根で:

```bash
git pull
```

```bash
python -m pip install -r python/requirements.txt && python -m pip install --force-reinstall --no-deps python/wheels/hasami_env-0.1.0-cp312-cp312-linux_x86_64.whl
```

```bash
python -m pytest python -q
```

コンパイラか Python.h がなければ、`python/conftest.py` はビルドせず、入れた wheel をテストする（`HASAMI_ENV_NO_BUILD=1` でも同じ）。`test_build_matches_sources` は、wheel に埋め込んだソースのハッシュ（`hasami_env.SOURCE_HASH`）が checkout の `lib/core` と `_core.cpp` と同じかを確かめるので、C++ を変えたのに wheel を作り直していなければ落ちる。VM で学習のスクリプトを動かすときは、`PYTHONPATH` に `python/` を入れない（ソースのディレクトリが入れた wheel を隠す）。

wheel の作り直し（g++ と python3.12-dev がある Ubuntu 24.04 で。クラウドの開発環境でよい）:

```bash
python3.12 -m venv /tmp/v312 && /tmp/v312/bin/pip install pybind11 setuptools wheel numpy
```

```bash
cd python && rm -f wheels/*.whl && /tmp/v312/bin/python setup.py -q bdist_wheel -d wheels
```

wheel には glibc 2.38 以上と libstdc++ の GLIBCXX_3.4.29 以上が要る（Ubuntu 24.04 は満たす）。
