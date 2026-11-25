# Project Structure

## Organization Philosophy

**レイヤードな構成と、構文拡張の単位でのまとまり**を組み合わせる。

- 依存の方向は、次の一方向に限る。
  - 共通基盤（base）→ 構文木（tree）→ パースエンジン（parse）とレンダラ（render）→ 組み込みの構文（syntax）
- エンジンは、個別の構文の知識を持たない。CommonMark の各構文は、構文ごとに 1 つのファイルとして実装し、パースの規則を持つ（`commonmark.*` 拡張）。
- 出力は構文ではなく**ノード型**に結び付ける。組み込みのノード型の HTML/Markdown 出力は、出力ごとの「コア出力拡張」（`amark.core.html`、`amark.core.markdown`）にまとめる。複数の構文が同じノード型を作る（ATX と setext の見出し、インデントとフェンスのコードブロック）ため、構文を無効化しても出力が消えないようにするためである。
- 拡張が**新しいノード型**を足す場合は、その拡張がパースの規則と出力をまとめて持てる。「構文を 1 つ追加する」作業が、1 つの拡張とその登録で終わる状態を保つ。
- CommonMark の構文セットは `amark.core` → `amark.core.html` → `amark.core.markdown` → ブロック構文 → インライン構文 の順に組み立てる。

> 現時点ではコードはまだない。以下は、これから作るときに従う規約である。実装時に合理的な理由があって変える場合は、このファイルを更新する。

## Directory Patterns

### 公開ヘッダ
**Location**：`include/amark/`
**Purpose**：利用者に見せる API だけを置く。`amark.h` がすべてをまとめるヘッダになり、機能ごとのヘッダ（`node.h`、`parser.h`、`render.h`、`extension.h` など）を含める。
**Example**：`#include <amark/amark.h>`

### ライブラリの実装
**Location**：`src/`
**Purpose**：レイヤーごとのサブディレクトリに分ける。
- `src/base/`：アロケータ、バッファ、UTF-8、文字の分類、エラー
- `src/tree/`：ノード、構文トークン、編集、拡張のレジストリ
- `src/parse/`：ブロックとインラインのエンジン
- `src/render/`：HTML と Markdown のエンジン
- `src/syntax/`：CommonMark の組み込み構文（1 つの構文につき 1 ファイル。例：`atx_heading.c`、`emphasis.c`）と、コア出力拡張（`html_*.c`、`md_*.c`）。同じ層の中だけで共有する内部ヘッダ（`*_internal.h`）は使ってよい。

内部でだけ使うヘッダは、`src/` の中に置く。ファイル名には `_internal.h` などの接尾辞を付けて、公開ヘッダと区別する。

### テスト
**Location**：`tests/`
**Purpose**：Naos のテストを置く。モジュールごとのテスト（`tests/tree/`、`tests/parse/` など）、CommonMark の準拠テスト（`tests/spec/`。spec.txt とテストを生成するスクリプト）、往復のプロパティテスト、テスト専用の GFM 相当の検証用拡張に分ける。検証用拡張は、その部品を使う spec のテストディレクトリに置く（HTML 側：`tests/html/support/`、ブロック側：`tests/parse/`、インライン側と組み合わせ：`tests/extensions/`、Markdown 側：`tests/markdown/support/`）。fuzz からも使う部品は Naos に依存させない。複数のテストで共有する生成スクリプトなどは `tests/support/` に置く。

### ファジングとベンチマーク
**Location**：`fuzz/`、`bench/`
**Purpose**：libFuzzer のターゲットと、病的な入力に対する計算量の確認

### ビルドとパッケージング
**Location**：`CMakeLists.txt`（トップレベル）、`cmake/`
**Purpose**：パッケージ設定のテンプレート（`AmarkConfig.cmake.in`、`amark.pc.in`）や、CMake の補助モジュール

### CI/CD
**Location**：`.github/workflows/`

## Naming Conventions

- **Files**：snake_case（`block_quote.c`、`node_edit.c`）
- **公開関数**：`amark_<対象>_<動作>`（`amark_node_insert_after`、`amark_parser_feed`、`amark_render_html`）
- **生成と破棄の関数の組**：領域は呼び出し側（または埋め込み先、エンジン）が用意し、構造体の中身だけを生成・破棄する組は `_init` / `_deinit` とする（`amark_buffer_init` / `amark_buffer_deinit`）。不透明型のオブジェクトの領域ごと生成・破棄する組は `_create` / `_destroy` とする（`amark_buffer_create` / `amark_buffer_destroy`）。`malloc` と同じ形の `amark_malloc` / `amark_free` は、メモリそのものの確保と解放なので対象外とする。関数表のコールバックも同じ（`init` / `deinit`）。`init` の対は必ず `deinit` とする。0 で埋めるだけで初期化が済むなら、`init` を置かず `deinit` だけでよい（`deinit_block_data`）。破棄するものがなければ `_deinit` は置かない（`amark_parse_options_init`）。
- **typedef した型**：構造体、列挙、関数ポインタ、整数の別名のすべてで、末尾に `_t` を付ける。公開型も、モジュールをまたぐ内部の型も `amark_<名前>_t` とする。構造体と列挙のタグには `_t` を付けない。不透明型は `typedef struct amark_node amark_node_t;` の形で宣言する。
- **列挙と定数**：`AMARK_<分類>_<名前>`（`AMARK_NODE_PARAGRAPH`、`AMARK_ERROR_NONE`）
- **内部関数**：ファイル内でだけ使うなら `static` にする。モジュールをまたいで使う内部関数には、公開関数と同じ `amark_` 接頭辞を付ける。公開 API との区別は名前ではなく、宣言の場所（公開ヘッダか内部ヘッダか）と `AMARK_API` の有無で行う。内部の名前を公開の名前と重複させない。
- **マクロ**：`AMARK_` 接頭辞を付け、大文字の snake_case にする。

## Include Organization

```c
/* 1. 自身の対応ヘッダ */
#include "tree/node_internal.h"
/* 2. 公開ヘッダ */
#include <amark/node.h>
/* 3. 他の内部ヘッダ（src/ からの相対パス） */
#include "base/buffer.h"
/* 4. 標準ライブラリ */
#include <string.h>
```

- `src/` を include のパスに加え、内部ヘッダはそこからの相対パスで書く。
- 公開ヘッダは、ほかの公開ヘッダだけを include できる。内部ヘッダに依存してはならない。

## Code Organization Principles

- **下位のレイヤーは上位のレイヤーを参照しない**。例えば、tree は parse や render を知らない。
- **組み込みの構文も公開 API だけを使う**。`src/syntax/` から `src/tree/`・`src/parse/`・`src/render/` の内部ヘッダに依存するのは禁止する（`src/syntax/` 内の `*_internal.h` は可）。必要なら API を公開する。
- **すべてのソースファイルの先頭にライセンスヘッダを付ける**。

  ```c
  /* SPDX-License-Identifier: MIT
   * Copyright (c) The Amark Authors */
  ```

- **spec の境界は、作業を分けるための手段である**。後の spec で前のモジュールの設計に問題が見つかったら、その spec に戻って直す。回避のためのラッパーや重複した実装は作らない。

---
_ファイル一覧ではなく、配置と命名のパターンを記す。パターンに従った新しいファイルを足すたびに、このファイルを更新する必要はない_
