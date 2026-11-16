# 対局の記録（棋譜）

Web のシミュレーターで「棋譜」の「ダウンロード」を押して得た `.jsonl` を、ここに置く（1 行が 1 局）。形式は `web/record.js` の先頭に書いてある。人の名前などは入っていない。

確かめるとき（同じ局が再生できるか）と、人の手を取り出すとき:

```bash
npm run build:wasm
```

```bash
node tools/replay.mjs data/games/*.jsonl --moves moves.jsonl
```

ページの「GitHub に自動で送る」を使うと、1 局ずつ `<フォルダー>/<日付>/<時刻>-<id>.json` として送り先のリポジトリにコミットされる。送り先を棋譜専用のリポジトリ（例: `SoyaHonda/hasami-games`）にしたときは、それを clone してフォルダーごと再生ツールに渡す。

```bash
node tools/replay.mjs ../hasami-games/games --moves moves.jsonl
```

このリポジトリに直接送るときは、フォルダーを `data/games` にする（コミットに `[skip ci]` が付くので CI は回らない）。

記録は、記録したときと同じ版の lib/core でだけ正確に再生できる。再生できない記録があれば、ツールが記録の `build.commit` を表示するので、そのコミットで WASM を作り直して再生する。
