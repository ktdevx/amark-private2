# Brief: inline-parser

> 2026-11-27 改訂：`syntax-tree` を「行 × 帯」と「中身の上の CST」のモデルで作り直した（入力資料は `INPUT.md`、契約は `syntax-tree` の design、`include/amark/` の公開ヘッダ、`syntax-tree` の tasks.md の Implementation Notes）。`html-renderer` と `block-parser` も同じモデルで作り直した（承認済み）。この brief から requirements、design、tasks を作り直す。前の版（2026-11-25 承認、ソースの絶対位置の構文トークンと編集の追跡のモデル）の requirements、design、research、tasks は引き継がない。

## Problem
ブロック構造が確定した後、段落や見出しの中身を、強調、リンク、コードスパン、オートリンク、生の HTML などのインライン要素として解析する必要がある。CommonMark の区切り文字の処理（delimiter run と process emphasis）とブラケットの処理は複雑である。拡張（新しい区切り文字、きっかけの文字がない構文、ブラケットの後処理）が同じ仕組みに乗れるよう、エンジンとして汎用化しなければならない。

さらに、インラインの解析は葉の中身の上の綴りの層（中身のトークン）を作る。無編集の往復でバイト単位で同じ出力を得ること、msgstr の断片を表記のまま葉に差し込めることは、この綴りが中身を過不足なく覆い、意味の値を正しく導くことに懸かっている。Amark が CommonMark に完全準拠し、拡張の組み合わせで GFM 相当を表現できることを最終的に確かめる場所も、この spec である。

## Current State
- `inline-parser` の実装はない（`include/amark/inline.h`、`src/parse/` のインラインのエンジン、`src/syntax/` のインライン構文はまだない）。
- 前の版の spec は、旧モデルの API の上で書かれていた。構文トークン（`token.h`、ソースの絶対位置）、`amark_document_check_tokens`、編集の追跡、リテラルのモード、インラインへの「区間の並び」、自前のラベルの表、参照の形のリンクへの宛先とタイトルの写しを使っていた。これらはもうない。
- 今の `syntax-tree` が提供するもの：
  - 葉の中身（配置済み：行の列の範囲を仮想の `\n` でつないだもの、自由：自分のバイト列で改行は LF）と、その上の中身のトークン（9 種類：text、marker、space、delimiter、escape、char_ref、replaced、line_break、tab_remainder）。トークンは中身の上のバイトの範囲だけを指し、葉自身かそのインラインの子孫に記録する。種類とバイトの一致は記録の時点で検査する（text は NUL と不正な UTF-8 を含めない）。
  - トークンの移動（`amark_node_move_content_tokens`）と、text のトークンだけの分割。トークンの種類は変えられない。
  - 意味の値はトークンから導く（escape は 1 文字、char_ref は解読、line_break は改行、構造のトークンは寄与しない）。トークンのない値は記録する（`amark_node_record_*`。記録は編集ではない）。style は記録する。
  - 定義の索引：参照の形（full、collapsed、shortcut）のリンクと画像の宛先とタイトルは、ラベルから `amark_document_find_definition` で引いて導く。ラベルの正規化は `syntax-tree` にある。
  - ロスレス性の検証（中身の被覆を含む）と、元の表記のまま出せるかの判定（`amark_block_content_verbatim`、`amark_node_verbatim`）。
  - `syntax-tree` の早期検証（tasks.md の 10、`tests/tree/msgstr_flow_test.c`）が、インラインの組み立ての手順を CommonMark の例で確かめた。区切り文字の連続は 1 文字ずつの delimiter のトークンで記録すれば、一部だけを使う照合を表せる。使われずに残った区切り文字は、delimiter のトークンと記録した literal を持つ text のノードとして残し、隣の text と 1 つにしない。
  - 中身のトークンの記録は、構造（ノードの親子）を組み立ててから行うと、葉の確認が償却で定数になる。
- `block-parser` が提供するもの：インラインへの委譲の取り決め（`amark_inline_parse_fn_t`、葉と中身の定義、パースの文脈）、既定の委譲先を選ぶ 1 か所（`amark_parse_default_inline()`）、単純な委譲先、インラインの断片のパース（自由な中身の一時の葉と `amark_block_replace_content`）、走査の関数（HTML のタグ、リンクのラベル・宛先・タイトル、値と中身の単位）、CommonMark の構文セットの構築関数（インライン構文の拡張を足す表）、テストの補助と共有の生成手順、ブロックの GFM 相当の検証用の部品。
- `html-renderer` が提供するもの：組み込みのノード型の HTML 出力（`amark.core.html`）と、GFM 相当の HTML の検証用の部品（`amark_test_html_gfm`。取り消し線と脚注の参照のノード型を含む）。

## Desired Outcome
- インライン構文エンジンと公開インライン構文 API がある。この API で次のものを定義できる。
  - きっかけの文字に反応する構文
  - きっかけの文字がない構文（拡張オートリンク相当）
  - 区切り文字の種類（`*`、`_` に加えて、拡張で `~` なども登録できる）とその規則
  - ブラケットの後処理（リンク、画像、脚注の参照）
  - 拡張の構文も、組み込みと同じ方法で、中身の上の位置でトークンを記録する。
- エンジンの入口は葉の中身である。配置済みの中身でも自由な中身でも同じ木と同じトークンを作る。既定の委譲先をこのエンジンにする。
- CommonMark 0.31.2 の全インライン構文を、構文ごとの組み込み拡張として実装し、CommonMark の構文セットに含める。
  - 全インライン構文：バックスラッシュのエスケープ、文字参照、コードスパン、強調、リンク、画像、オートリンク、生の HTML、ハードブレーク、ソフトブレーク、テキスト
- インラインの綴りを、中身の上の位置のトークンで過不足なく記録する。表記の区別は style にも記録する（強調の区切り文字、コードスパンのバッククォートの数と内側の空白、改行の形、リンクの形・山括弧・タイトルの引用符）。参照の形のリンクは、ラベルと形だけを記録し、宛先とタイトルは定義の索引に任せる。
- 断片のパースで、自由な中身とインラインの CST を作り、葉の中身を置き換えられる（msgstr の差し込み）。
- **CommonMark spec の準拠テスト**：spec.txt の全例を Naos のテストとして実行し、HTML 出力で 100% パスする。あわせて、各例のロスレス性の検証、元の表記のまま出せるかの判定、木の不変条件、逐次の入力との一致を確かめる。
- **拡張 API の表現力の検証**：GFM 相当のフックを使う小さな拡張を、テストの中にだけ置く。これらを組み合わせて、それぞれが期待どおり動くことを確かめる。
  - 表、取り消し線、タスクリスト、拡張オートリンク、タグフィルタ、脚注
  - 配布するライブラリには含めない。
- インラインのファジングターゲットと、病的な入力（深い入れ子、大量の区切り文字）に対する計算量のテストがある。

## Approach
- 区切り文字のスタックとブラケットのスタックを汎用化する。区切り文字の種類ごとの規則（開く/閉じるの判定、長さの一致の規則、生成するノード）を拡張から登録できるようにする。
- 組み込みの構文も公開 API だけを使って実装し、内部専用の近道を作らない。
- エンジンは、走査と照合を内部の並びの上で行い、木の構造を組み立て終えてから、各ノードにトークンを中身の順に記録する（`syntax-tree` の「構造を先に」の規則）。構文の拡張が出すトークンも、エンジンが預かって同じ時点で記録する。
- 中身の単位（text、replaced、line_break、tab_remainder、escape、char_ref）は `block-parser` の走査の関数の規則で決め、text のトークンが NUL と不正な UTF-8 を含まないようにする。
- 参照の形のリンクの成立は、パースの文脈の定義の検索（`amark_parse_context_find_definition`）で判断する。自前の定義の表は持たない。
- 準拠テストは、spec.txt から例を取り出してテストを生成する。生成の手順は `block-parser` の共有の手順を使い、検査の関数だけをこの spec が持つ。spec.txt のバージョンは固定する。

## Scope
- **In**：
  - インライン構文エンジンと公開インライン構文 API
  - 既定の委譲先の置き換えと、CommonMark の構文セットへのインライン構文の追加
  - CommonMark の全インライン構文の組み込み拡張と、その記録の規約（トークンの種類、項目、役割の番号、style）
  - 書き戻しが使う純粋な関数（区切り文字の開閉の判定と照合、生の HTML の判定）と、エスケープに関わる文字の宣言
  - 断片のパースで作る自由な中身の解析と、msgstr の差し込みの流れの検証
  - CommonMark spec の準拠テスト一式
  - テスト専用の検証用拡張（GFM 相当のインライン側の部品）と、ブロック側・HTML 側の部品との組み合わせ
  - インラインのファジングと計算量のテスト、ベンチマーク
- **Out**：
  - ブロック構造の解析と、断片のパースの入口（`block-parser`）
  - GFM の配布、GFM spec の公式準拠の主張
  - Markdown への書き戻し、エスケープの方式（`markdown-renderer`）

## Boundary Candidates
- インライン構文エンジン（汎用）と、拡張に渡す文脈
- CommonMark のインライン構文の定義（組み込み拡張）と記録の規約
- 準拠テストと拡張 API の表現力の検証（プロジェクト全体の受け入れゲート）

## Out of Boundary
- ブロック構造の解析、委譲の取り決めそのもの、断片のパースの入口（`block-parser`）
- 記録の形と検査、値の導出、定義の索引、ラベルの正規化、ロスレス性の検証と元の表記の判定（`syntax-tree`）
- HTML 出力の規則（`html-renderer`）。ただし、準拠テストの中で HTML 出力の不具合が見つかったら、`html-renderer` に戻って直す。

## Upstream / Downstream
- **Upstream**：`syntax-tree`（`node.h`、`document.h`、`value.h`、`spelling.h`、`extension.h`、`unicode.h`、`entity.h`）、`block-parser`（`parser.h`、`scan.h`、`commonmark.h`、委譲の取り決め、断片、テストの補助、生成の手順、ブロックの検証用の部品）、`html-renderer`（`html.h`、`amark_test_html_gfm`）、`foundation`
- **Downstream**：`markdown-renderer`（記録の規約、役割の番号、純粋な関数、文字の宣言、準拠テストのデータ）、利用者の拡張

## Existing Spec Touchpoints
- **Extends**：`syntax-tree` が割り当てたスロットの種類の番号 5–10（INLINE_SYNTAX、INLINE_DELIMITER、INLINE_BRACKET、INLINE_TEXT_SCANNER、INLINE_NODE_TRAITS、INLINE_SPECIAL）の中身を定める。`block-parser` の Downstream Extension Points（構文セットの構築関数の表、既定の委譲先）に追記する。検証用拡張の実装で、上流の拡張 API に不足が見つかったら、それぞれの spec に戻って補う。
- **Adjacent**：`block-parser`、`html-renderer`、`markdown-renderer`

## Constraints
- CommonMark spec（0.31.2）の例に 100% パスすることを必須にする。
- spec.txt のライセンスは CC-BY-SA 4.0 なので、テストデータとしてだけ扱い、配布するライブラリとソースのアーカイブには含めない。
- 無編集でパースした文書は、ロスレス性の検証に通り、根が元の表記のまま出せると判定される。
- 中身のトークンは、`syntax-tree` の記録の規則（中身の順、重なりなし、種類とバイトの一致、文字の境界）に従う。分けられるのは text のトークンだけで、種類は変えられない。
- 入力は UTF-8 とする。不正なバイト列があってもクラッシュせず、処理を続ける。
