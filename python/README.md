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

## 速さ

```bash
python python/bench_env.py
```

ランダムな方策で、相手ごと・スレッド数ごとの決定点／秒を出す。

## コンパイラのない機械で使う（ビルド済みの wheel）

演習の Azure VM には sudo がなく、g++ や Python のヘッダーがないと上のビルドができない。そのときは、同じ OS と Python（Ubuntu 24.04、Python 3.12）の別の機械で wheel を作って持っていく。作る側（g++ と python3.12-dev がある機械）で:

```bash
python3.12 -m venv /tmp/v312 && /tmp/v312/bin/pip install pybind11 setuptools wheel numpy
```

```bash
cd python && /tmp/v312/bin/python setup.py bdist_wheel -d dist
```

できた `python/dist/hasami_env-0.1.0-cp312-cp312-linux_x86_64.whl`（glibc 2.38 以上、libstdc++ の GLIBCXX_3.4.29 以上が要る）を VM へ送り、`~/venv` で:

```bash
python -m pip install hasami_env-0.1.0-cp312-cp312-linux_x86_64.whl pytest
```

```bash
HASAMI_ENV_NO_BUILD=1 python -m pytest python --import-mode=importlib
```

`lib/core` を変えたら wheel を作り直す。
