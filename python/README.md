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
