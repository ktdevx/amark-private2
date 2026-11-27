# Brief: markdown-renderer

> 2026-11-27 改訂：`syntax-tree` を「行 × 帯」と「中身の上の CST」のモデルで作り直した（入力資料は `INPUT.md`、契約は `syntax-tree` の design、`include/amark/` の公開ヘッダ、`syntax-tree` の tasks.md の Implementation Notes）。`html-renderer`、`block-parser`、`inline-parser` も同じモデルで作り直した（承認済み）。この brief から requirements、design、tasks を作り直す。前の版（2026-11-25 承認、ソースの絶対位置の構文トークンと編集の追跡のモデル）の requirements、design、research、tasks は引き継がない。

## Problem
利用者は、Markdown をパースして木を編集し（例えば、翻訳のためにテキストを置き換え）、Markdown に書き戻したい。主な用途は Markdown と po の相互変換である。翻訳者が書いた msgstr を、表記のまま原文の段落に差し込み、翻訳した Markdown を作る。そのとき、次の 3 つを同時に満たす必要がある。
- 編集していない部分は、元の表記のまま残る（訳していないコードブロック、表、HTML、参照の定義、空行、ほかの段落、引用やリストの帯）。
- 編集した部分は、編集後の木と同じ意味の、正しい Markdown として出力される。
- 無編集なら、元の入力とバイト単位で同じ出力になる。

ナイーブに出力すると、編集した木と違う意味の Markdown になる。テキストの記号が構文として読まれる、区切り文字の左右の条件（flanking）が崩れて強調が成り立たない、行頭の文字がブロックの構文になる、引用やリストの中で構造が崩れる、ブロックがつながる、といった問題が起きる。

## Current State
- `markdown-renderer` の実装はない（`include/amark/markdown.h`、`src/render/` の Markdown のエンジン、`amark.core.markdown` はまだない）。
- 前の版の spec は、旧モデルの API の上で書かれていた。構文トークン（`token.h`、ソースの絶対位置）、`amark_document_check_tokens`、編集の追跡（`AMARK_EDIT_*`）、帯のトークンの印（`CONTAINER_PREFIX`、`PARTIAL_TAB`、`LAZY_CONTINUATION`）を使い、トークンのヒープによる合流（MdSource）、行ごとの接頭辞の探索（MdLines）、編集の状態の印の表で書き戻しを組み立てていた。これらはもうない。
- 今の `syntax-tree` が提供するもの：
  - 行の表（ソース、バイトの範囲、改行の種類、列の幅、BOM、文書の既定の改行）と、列とバイトの変換（タブの途中を含む）
  - コンテナの行ごとの帯（`present` / `lazy`、項目の最初の行の `first`）、空行、葉自身の表記（レイアウトのトークン）、葉の中身（配置済み / 自由）とその部分、中身の上のトークン（9 種類）
  - 値（value）と表記の属性（style）。値を変えたノードはその値の綴りだけを捨て、style は残す。綴りを捨てた後に残る値は、設定では拒まれる値（`&#10;` から導いた改行を含むテキストなど）でもありうる。すべての綴りを捨てる操作は、リストのタイトを上書きの値として写す。
  - 元の表記を出せるかの問い合わせ：`amark_node_spelling_state`、`amark_block_find_margin`、`amark_block_content_verbatim`、`amark_node_verbatim`、`amark_node_is_new`、`amark_node_content_base`
  - 定義の索引による参照の解決（参照の形のリンクの宛先とタイトルは `amark_node_get` で解決した値になる）と、意味の比較（`amark_node_equal`）
  - 中身の置き換え（`amark_block_replace_content`）による msgstr の差し込み。差し込んだ段落は行の範囲、帯、style を保ち、インラインは翻訳者のバイトの綴りを保つ（`tests/tree/msgstr_flow_test.c`）。
  - 使われずに残った区切り文字は、DELIMITER のトークンと記録した literal を持つテキストとして残り、元の表記と判定される。オートリンクに title を設定してインラインの形にしても、宛先のトークンは残る。
- `block-parser` が提供するもの：CommonMark のブロック構文の記録の規約（帯、空行、葉自身の表記、中身、役割の番号）、走査の関数（HTML ブロックの条件、生の HTML のタグ、リンクのラベル・宛先・タイトル）、CommonMark の構文セットの構築関数（出力の拡張の表に `amark.core.markdown` を足す拡張点）、断片のパース、テストの生成の手順（`gen_spec_examples.cmake`）、パースの検査の補助、ブロックの GFM 相当の検証用の部品。
- `inline-parser` が提供するもの：インラインの記録の規約と役割の番号、区切り文字の開閉の判定と照合の純粋な関数、生の HTML の判定、エスケープに関わる文字の宣言（`amark_inline_special_chars`）、CommonMark spec のデータと版の定義、GFM 相当の構文セットの補助（`amark_test_gfm_set_create` に Markdown の部品を足す拡張点）と `test.ext_autolink`。
- `html-renderer` が提供するもの：出力先の型（`render.h` の `amark_write_fn_t`）、スロットの規約（ノード型ごとの処理と前後のフック、名前による型の指定、連鎖、拡張ごとの状態、入口の形）、ケースの生成の手順（`gen_html_cases.cmake`）、検証用のノード型の記述子（`amark_test_html_gfm`）。

## Desired Outcome
- `amark_render_markdown(node, options, buffer)` と書き出しの関数の版で、任意の部分木を Markdown に書き戻せる。
- **往復の保証（必須）**：無編集の文書を書き戻すと、入力とバイト単位で同じ出力になる。根拠は、`syntax-tree` の行ごとの分割と中身の被覆、それに基づく「元の表記のまま出せる」の判定である。
- **意味の保存（必須）**：編集した木を書き戻して再パースすると、編集後の木と意味的に等しい。表現できない木は、黙って違う意味の出力を作らず、専用のエラー（`AMARK_ERROR_UNREPRESENTABLE`）と報告で知らせる。
- **局所性（必須）**：元の表記のまま出せる部分木の行は、元の行のまま出る。変化は、編集した葉の行と、その前後の区切りに限る。
- **msgstr の差し込み**：引用やリストの中の段落の中身を置き換えると、祖先の元の帯と、翻訳者のインラインのバイトで出力される。行が増えれば帯を合成し、減れば元の行を出さない。
- 新しいノード（綴りも行も持たない）も、style かオプションの既定の表記で正しく出力できる。
- ノード型ごとの Markdown 出力のスロット（種類 13、14。`html-renderer` の 11、12 と同じ形）で、拡張のノードも上の保証に参加できる。組み込みの 19 の型の出力は `amark.core.markdown` が持つ。
- 往復と意味の保存を、CommonMark spec の全例、コーパス、ファジングのプロパティテストで確かめる。

## Approach
書き戻しは、行の単位で、部分木ごとに判断して進める。
- **部分木の判断**：`amark_node_verbatim`、`amark_block_content_verbatim`、`amark_node_spelling_state`、`amark_node_is_new` から、ブロックごとに「元の行のまま」「自分の表記を保つ」「合成する」「新しい」を決める。編集の記録は使わない。
- **行の組み立て**：各出力の行で、外側から順に祖先の帯（`amark_block_find_margin` で見つかれば元のバイト、遅延継続なら省いたまま、なければ style から合成）、空行、葉自身の表記、中身を並べる。列がずれた範囲では帯と表記のタブを空白に展開する。
- **中身**：配置済みで綴りが揃っていれば元の部分、自由な中身で綴りが揃っていれば（msgstr）そのバイト、そうでなければインラインの CST から作り直す。作り直した中身の i 行目は、葉の元の i 行目に置く。
- **作り直し**：エスケープは保守的な方式（構文セットが宣言する文字を常にエスケープし、行頭・行の端・区切り文字の隣を位置で保護する）で始める。区切り文字は `inline-parser` の照合の関数で確かめて選ぶ。綴りを捨てた後の値（改行を含むテキストなど）は文字参照で表す。後で必要最小限のエスケープへ改めても、公開 API とスロットは変わらない形にする。
- **ブロックの区切り**：元の文書で隣り合っていたブロックは元の空行のまま、新しい隣接は空行か表記の切り替えで区切る。リストのタイトは、項目の間の空行を足すか取り除いて保つ。

## Scope
- **In**：
  - Markdown レンダラのエンジン（行の単位の書き戻し）と公開 API、出力のオプション（新しいノードの表記、エスケープの方式、報告先）
  - Markdown 出力のスロット（種類 13、14）と出力の文脈の API
  - 組み込みの 19 のノード型の Markdown 出力（`amark.core.markdown`）と、CommonMark の構文セットへの追加
  - テスト専用の GFM 相当の検証用拡張の Markdown 出力の部品（表、取り消し線、タスクリスト、脚注、拡張オートリンク）
  - 往復、意味の保存、局所性のプロパティテストとファジング
- **Out**：
  - フォーマッタとしての整形（編集していない部分を書き換えること）
  - パースと断片のパース（`block-parser`、`inline-parser`）
  - HTML の出力（`html-renderer`）
  - msgid の空白の正規化と po の形式

## Boundary Candidates
- 判断（元の表記を出す部分と合成する部分の決定）と、行の組み立て（帯、空行、表記、中身）
- 中身の作り直し（エスケープ、区切り文字の選び直し、行頭の保護）
- 組み込みの型の出力（`amark.core.markdown`）と、新しいノードの表記の既定

## Out of Boundary
- 記録の形と検査、値の導出、参照の解決、ロスレス性の検証、元の表記の判定（`syntax-tree`）
- パースの規則、記録の規約、区切り文字の規則と照合、文字の宣言（`block-parser`、`inline-parser`）。この spec はそれを使うだけで、同じ規則を作らない。
- 出力先の型と、ケースの生成の手順（`html-renderer`）

## Upstream / Downstream
- **Upstream**：`syntax-tree`（`node.h`、`document.h`、`value.h`、`spelling.h`、`extension.h`）、`block-parser`（`scan.h`、`commonmark.h` の構築関数と役割の番号、テストの補助、生成の手順、ブロックの検証用の部品）、`inline-parser`（`inline.h` の純粋な関数、文字の宣言、区切り文字のスロット、役割の番号、spec のデータ、GFM 相当の構文セット）、`html-renderer`（`render.h`、スロットの規約、`gen_html_cases.cmake`、`amark_test_html_gfm`）、`foundation`
- **Downstream**：利用者（翻訳、リンクの書き換えなどの編集ツール）、拡張の実装者

## Existing Spec Touchpoints
- **Extends**：`syntax-tree` が割り当てたスロットの種類 13、14 の中身を定める。`block-parser` の Downstream Extension Point（`src/syntax/commonmark.c` の出力の拡張の表への `amark.core.markdown` の追加）と、`inline-parser` の Downstream Extension Point（`amark_test_gfm_set_create` への Markdown の部品の追加）に追記する。
- **Adjacent**：`html-renderer`（スロットと入口の形を揃える）

## Constraints
- 無編集の往復でバイト単位で同じ出力になることは必須である。タブ、CRLF と CR、最後の行の改行の有無、行末の空白、遅延継続、NUL と不正な UTF-8、BOM を含む入力でも成り立たなければならない。
- 初期の方式（保守的なエスケープ）を、後で別の方式（必要最小限のエスケープ）へ改めることを妨げないように、公開 API とスロットを設計する。
- 上流の API に不足が見つかったら、回避のためのラッパーを作らず、その spec に戻って直す。
- C99、グローバルのアロケータ、不透明型、`struct_size`、深い木でスタックを深さに比例して使わないこと（steering）。
