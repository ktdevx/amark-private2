# Brief: html-renderer

## Problem
利用者は、パースした（あるいは編集した）構文木を HTML として出力したい。CommonMark の準拠テストは HTML 出力で判定するため、準拠を検証するためにも、CommonMark の参照実装と互換な HTML レンダラが必要である。さらに、拡張したノードや既存のノードの出力を、利用者が差し替えられなければならない。

## Current State
まだ存在しない。`syntax-tree` のノードモデルと拡張レジストリの上に作る。

## Desired Outcome
- 任意の構文木（パースしたもの、手で作ったもの、編集したもの）を HTML に出力できる。
- 出力は、CommonMark spec の期待値と一致する形式になる（`inline-parser` の準拠テストで検証する）。
- 安全モードを持つ。既定では、生の HTML と危険な URL（`javascript:` など）を出力しない。生の HTML を許可するオプションも用意する。
- ノード型ごとの HTML 出力のスロットを、構文拡張から登録できる。既存の出力を差し替えたり、出力全体をフィルタしたりもできる（タグフィルタ相当を実装できること）。

## Approach
- `amark_render_html(node, options, ...)` の形の API を用意する。出力先は、`foundation` のバッファまたはコールバックとする。
- 組み込みノードの HTML 出力も、構文拡張のスロットとして登録する。利用者の拡張と同じ API を使う。
- HTML のエスケープ、URL のエスケープ、URL の安全判定のユーティリティを持つ。
- 出力の文脈を拡張から使えるようにする。具体的には、タイトな（tight）リストの判定や、画像の alt 属性の中でテキストだけを出すモードなどを指す。

## Scope
- **In**：
  - HTML レンダラの本体、オプション（安全モード、ソース位置の属性、改行の扱いなど）
  - HTML 出力のスロットのインタフェース
  - CommonMark の全ノードの HTML 出力
- **Out**：
  - CommonMark spec の準拠テストのランナー（`inline-parser` が持つ）。この spec では、手で組み立てた木を使ったテストで検証する。

## Boundary Candidates
- HTML 出力のエンジン
- ノード型ごとの出力のスロット

## Out of Boundary
- パース
- Markdown への出力

## Upstream / Downstream
- **Upstream**：`syntax-tree`
- **Downstream**：`inline-parser`（準拠テストで使う）、利用者の拡張

## Existing Spec Touchpoints
- **Extends**：`syntax-tree` の拡張記述子に、HTML 出力のスロットを足す。
- **Adjacent**：`markdown-renderer`（レンダラのスロットの設計は揃える）

## Constraints
- 出力の形式は、CommonMark の参照実装（cmark）と一致させる。
- 脚注のように「文書の末尾に出力を足す」構文を、拡張だけで実装できるようにする。例えば、文書の出力の前後にフックを置く。
