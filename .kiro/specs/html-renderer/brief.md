# Brief: html-renderer

> 2026-11-27 改訂：`syntax-tree` を「行 × 帯」と「中身の上の CST」のモデルで作り直した（入力資料は `INPUT.md`、契約は `syntax-tree` の design と `include/amark/` の公開ヘッダ）。この brief から requirements、design、tasks を作り直す。前の版（2026-11-26 承認、構文トークンのモデル）の design と tasks は引き継がない。

## Problem
利用者は、パースした（あるいは編集した）構文木を HTML として出力したい。CommonMark の準拠テストは HTML 出力で判定するため、準拠を検証するためにも、CommonMark の参照実装（cmark）と互換な HTML レンダラが必要である。さらに、拡張したノードや既存のノードの出力を、利用者が差し替えられなければならない。

## Current State
- `html-renderer` の実装はない（`src/render/`、`src/syntax/`、`include/amark/html.h` はまだない）。
- 前の版の spec は、旧モデルの API の上で書かれていた。ソース位置は構文トークンの範囲（`amark_node_source_span`、`amark_document_position`）から、値は型ごとの取得関数から、リンクの宛先はノードにコピーした値から取っていた。これらはもうない。
- 今の `syntax-tree` が提供するもの：
  - 値と style は項目（facet）として持ち、`amark_node_get` と `value.h` の項目の番号のマクロで読む。値は綴りから導く。
  - 参照の形のリンクと画像の宛先とタイトルは、定義の索引で解決する。定義がなければ「解決できない」が返る。
  - リストのタイトは空行から導く（利用者が上書きできる）。
  - ソース上の範囲は `amark_node_source_range` が、行と列（1 から）で返す。行の範囲を持たないブロックや、自由な中身の上のインラインは、位置を持たない。
  - スロットの種類の番号 `AMARK_SLOT_HTML_NODE`（11）と `AMARK_SLOT_HTML_HOOKS`（12）は、すでに `extension.h` にある。
  - 綴りを捨てた後に残る値は、設定の関数が拒む値（改行を含む text の literal、空の code の literal など）でもありうる。

## Desired Outcome
- 任意の構文木（パースしたもの、手で作ったもの、編集したもの）を HTML に出力できる。
- 出力は、CommonMark spec の期待値と一致する形式になる（`inline-parser` の準拠テストで検証する）。
- 安全モードを持つ。既定では、生の HTML と危険な URL（`javascript:` など）を出力しない。生の HTML を許可するオプションも用意する。
- ノード型ごとの HTML 出力のスロットを、構文拡張から登録できる。既存の出力を差し替えたり、出力全体をフィルタしたりもできる（タグフィルタ相当を実装できること）。
- 出力に使う値、タイト、位置は、すべて `syntax-tree` の公開 API から取る。綴りの解析や、自前の定義の表、自前のタイトの判定は持たない。

## Approach
- `amark_render_html(node, options, ...)` の形の API を用意する。出力先は、`foundation` のバッファまたはコールバックとする。
- 組み込みノードの HTML 出力も、構文拡張のスロットとして登録する。利用者の拡張と同じ API を使う。
- HTML のエスケープ、URL のエスケープ、URL の安全判定のユーティリティを持つ。
- 出力の文脈を拡張から使えるようにする。具体的には、タイトな（tight）リストの判定や、画像の alt 属性の中でテキストだけを出すモードなどを指す。
- ソース位置の属性は `amark_node_source_range` の範囲から作り、書式（終わりの列の扱い）はこの spec が cmark に合わせて決める。
- リンクと画像の宛先とタイトルは `amark_node_get` で読み、解決できない参照の扱いをこの spec で決める。

## Scope
- **In**：
  - HTML レンダラの本体、オプション（安全モード、ソース位置の属性、改行の扱い）
  - HTML 出力のスロットと、出力の前後のフック、フィルタのインタフェース
  - CommonMark の全ノードの HTML 出力（組み込みの出力の構文拡張）
  - GFM 相当の HTML 出力を公開のスロットだけで書けることを確かめる、テスト専用の検証用拡張（ノード型の記述子を含む）
- **Out**：
  - CommonMark spec の準拠テストのランナー（`inline-parser` が持つ）。この spec では、手で組み立てた木を使ったテストで検証する。
  - 位置の求め方、値の導出、定義の索引（`syntax-tree`）

## Boundary Candidates
- HTML 出力のエンジン
- ノード型ごとの出力のスロット
- 組み込みの HTML 出力の構文拡張

## Out of Boundary
- パース
- Markdown への出力
- 木の変更（出力は木を変えない）

## Upstream / Downstream
- **Upstream**：`syntax-tree`（`node.h`、`document.h`、`value.h`、`extension.h`）、`foundation`
- **Downstream**：`block-parser`（CommonMark の構文セットに組み込みの HTML 出力を含める）、`inline-parser`（準拠テストで使う）、`markdown-renderer`（スロットの形と出力先の型をそろえる）、利用者の拡張

## Existing Spec Touchpoints
- **Extends**：なし（`syntax-tree` が割り当てたスロットの種類の番号 11、12 の中身を定める）
- **Adjacent**：`markdown-renderer`（レンダラのスロットの設計は揃える）

## Constraints
- 出力の形式は、CommonMark の参照実装（cmark）と一致させる。
- 脚注のように「文書の末尾に出力を足す」構文を、拡張だけで実装できるようにする。例えば、文書の出力の前後にフックを置く。
- ソース位置の問い合わせはノードの部分木をたどるので、すべてのブロックに位置の属性を付けると、時間がノードの数 × 深さになりうる（`syntax-tree` の Implementation Notes）。この性能の範囲を requirements で明記する。
