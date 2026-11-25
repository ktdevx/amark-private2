# Project Structure

## Organization Philosophy

**レイヤードな構成と、構文拡張の単位でのまとまり**を組み合わせる。

- 依存の方向は、次の一方向に限る。
  - 共通ランタイム（runtime）→ 構文木（tree）→ パースエンジン（parse）とレンダラ（render）→ 組み込みの構文（syntax）
- エンジンは、個別の構文の知識を持たない。CommonMark の各構文は、構文ごとに 1 つのファイルとして実装する。そのファイルが、パースの規則と HTML/Markdown の出力をまとめて持つ。
- 「構文を 1 つ追加する」作業が、1 つのファイルとその登録で終わる状態を保つ。

> 現時点ではコードはまだない。以下は、これから作るときに従う規約である。実装時に合理的な理由があって変える場合は、このファイルを更新する。

## Directory Patterns

### 公開ヘッダ
**Location**：`include/amark/`
**Purpose**：利用者に見せる API だけを置く。`amark.h` がすべてをまとめるヘッダになり、機能ごとのヘッダ（`node.h`、`parser.h`、`render.h`、`extension.h` など）を含める。
**Example**：`#include <amark/amark.h>`

### ライブラリの実装
**Location**：`src/`
**Purpose**：レイヤーごとのサブディレクトリに分ける。
- `src/runtime/`：アロケータ、バッファ、UTF-8、文字の分類、エラー
- `src/tree/`：ノード、構文トークン、編集、拡張のレジストリ
- `src/parse/`：ブロックとインラインのエンジン
- `src/render/`：HTML と Markdown のエンジン
- `src/syntax/`：CommonMark の組み込み構文（1 つの構文につき 1 ファイル。例：`atx_heading.c`、`emphasis.c`）

内部でだけ使うヘッダは、`src/` の中に置く。ファイル名には `_internal.h` などの接尾辞を付けて、公開ヘッダと区別する。

### テスト
**Location**：`tests/`
**Purpose**：Naos のテストを置く。モジュールごとのテスト（`tests/tree/`、`tests/parse/` など）、CommonMark の準拠テスト（`tests/spec/`。spec.txt とテストを生成するスクリプト）、往復のプロパティテスト、テスト専用の検証用拡張（`tests/extensions/`。GFM 相当）に分ける。

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
- **公開型**：`amark_<名前>`。不透明型は `typedef struct amark_node amark_node;` の形で宣言する。
- **列挙と定数**：`AMARK_<分類>_<名前>`（`AMARK_NODE_PARAGRAPH`、`AMARK_OK`）
- **内部関数**：ファイル内でだけ使うなら `static` にする。モジュールをまたいで使う内部関数には、`amark__` 接頭辞（アンダースコア 2 つ）を付けて、公開 API と区別する。
- **マクロ**：`AMARK_` 接頭辞を付け、大文字の snake_case にする。

## Include Organization

```c
/* 1. 自身の対応ヘッダ */
#include "tree/node_internal.h"
/* 2. 公開ヘッダ */
#include <amark/node.h>
/* 3. 他の内部ヘッダ（src/ からの相対パス） */
#include "runtime/buffer.h"
/* 4. 標準ライブラリ */
#include <string.h>
```

- `src/` を include のパスに加え、内部ヘッダはそこからの相対パスで書く。
- 公開ヘッダは、ほかの公開ヘッダだけを include できる。内部ヘッダに依存してはならない。

## Code Organization Principles

- **下位のレイヤーは上位のレイヤーを参照しない**。例えば、tree は parse や render を知らない。
- **組み込みの構文も公開 API だけを使う**。`src/syntax/` から `src/parse/` や `src/render/` の内部ヘッダに依存するのは禁止する。必要なら API を公開する。
- **すべてのソースファイルの先頭にライセンスヘッダを付ける**。

  ```c
  /* SPDX-License-Identifier: MIT
   * Copyright (c) The Amark Authors */
  ```

- **spec の境界は、作業を分けるための手段である**。後の spec で前のモジュールの設計に問題が見つかったら、その spec に戻って直す。回避のためのラッパーや重複した実装は作らない。

---
_ファイル一覧ではなく、配置と命名のパターンを記す。パターンに従った新しいファイルを足すたびに、このファイルを更新する必要はない_
