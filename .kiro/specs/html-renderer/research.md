# Research & Design Decisions

---
**Purpose**：`html-renderer` の design（2026-11-27 版、`syntax-tree` の「行 × 帯」と「中身の上の CST」のモデルに合わせた作り直し）を決めるための調査と判断を記録する。
**Usage**：design.md の判断の背景と、採らなかった案の理由を残す。前の版（2026-11-26 承認、構文トークンのモデル）の調査記録は git の履歴にある。この版はそれを引き継がない。
---

## Summary
- **Feature**：`html-renderer`
- **Discovery Scope**：Extension（実装のない spec を、作り直した上流の契約に合わせて作り直す。light discovery に、cmark の出力の規則の確認を加えた）
- **Key Findings**：
  - roadmap の改訂の範囲（「ソース位置の属性と、リンクと画像の宛先とタイトルの取り方だけ」）より、実際の変更は広い。前の版が使っていた `syntax-tree` の公開 API のうち、残っているのはノードの関係とレジストリの一部だけである。値の取得、タイト、位置、スロットの種類の番号、検証用のノード型の作り方が変わる（下の「変更の範囲の確認」）。
  - 値（宛先、タイトル、literal、レベル、タイト）は、すべて `amark_node_get` と `value.h` の項目の番号で読める。参照の解決とタイトの導出は `syntax-tree` が持つので、レンダラは綴りも定義の表も持たずに済む。
  - `amark_node_source_range` の範囲は、cmark の `data-sourcepos` の書式（1 から数える行、最初のバイトの列、最後のバイトの列）にそのまま一致する。ただし、1 回の問い合わせが部分木の大きさに比例するので、すべてのブロックに属性を付けると最大でノードの数 × 深さになる。

## Research Log

### 変更の範囲の確認（前の版の design が使っていた上流の API）
- **Context**：roadmap は改訂の範囲を「ソース位置の属性と、リンクと画像の宛先とタイトルの取り方だけ」とする。実際の上流のヘッダと照らし、変更が要る範囲を確かめるため。
- **Sources Consulted**：前の版の `design.md`（git の履歴）、`include/amark/node.h`、`document.h`、`value.h`、`iterator.h`、`extension.h`、`spelling.h`、`syntax-tree` の design.md と tasks.md の Implementation Notes
- **Findings**：

| 前の版が使っていたもの | 今の上流 | この版での扱い |
|---|---|---|
| ノードの関係（parent、first_child、next）、`amark_node_get_type`、`amark_node_document` | `node.h` に同じ名前である | そのまま使う |
| 型ごとの属性とリテラルの取得関数 | ない。`amark_node_get(node, facet, &value)` と `value.h` の項目の番号（`AMARK_LIST_TYPE`、`AMARK_CODE_BLOCK_INFO` など） | すべて `amark_node_get` で読む。取得は確保に失敗しうるので、エラーを返す経路を入れた |
| リンクの宛先とタイトル（ノードにコピーした値） | 参照の形は定義の索引で解決。定義がなければ `AMARK_VALUE_UNRESOLVED` | 解決した値を使う。解決できないときの規則を新しく決めた（要件 3.5） |
| リストのタイト（属性） | `AMARK_LIST_TIGHT` は空行から導く。利用者は上書きできる | item の処理が親の list の値を読む（要件 3.6） |
| `amark_node_source_span` と `amark_document_position` と `amark_document_source_data` | ない。`amark_node_source_range`（行と列を直接返す。位置がなければ `has_position` が偽） | 範囲をそのまま書式にする。改行のバイトを取り除く処理は要らない |
| スロットの種類の番号を `extension.h` に追記 | 11、12 はすでに `extension.h` にある | 追記しない。ファイルの変更の一覧から外した |
| 拡張の記述子とスロットの基底 | `amark_extension_t { struct_size, name, node_types, node_type_count, slots (const amark_slot_t *const *), slot_count, user_data }`、`amark_slot_t { struct_size, kind }` | 同じ形で使う |
| `amark_iter_t` | `iterator.h`。非 const の根を取り、確保する | 使わない（前の版と同じ判断。関係の関数でたどる） |
| 検証用のノード型の型のデータ（列の揃え、ラベル、チェックの有無） | ノード型は値と style を項目として宣言できる。型のデータは `copy` がないとバイトの複製になる | 値を項目として宣言する（下の判断） |
| テスト用の木の組み立て（旧形式の S 式） | `tests/support/tree_builder.h`（新しいダンプの形式、`:lines`、`:content`、`:tokens`、`:spelling`） | 新しい形式で書く。ソース位置のテストはソースを登録してから組み立てる |
| 旧 `amark.h` の順の `entity.h` | `entity.h` は foundation の組にある。3 番目の組は `render.h html.h` | 3 番目の組に入れる |

- **Implications**：roadmap の 1 行より広く改訂した。要件では、新しく要件 3（構文木の値に従う出力）を立て、要件 5（ソース位置）と 12（性能）を新しいモデルの言葉で書き直した。ほかの要件（出力の入口、安全モード、スロット、文脈、フック、表現力）は、構文トークンのモデルに依存しないので内容を保ち、番号を付け直した。

### `syntax-tree` の Implementation Notes のうち、この spec への義務
- **Context**：`syntax-tree` の tasks.md の Implementation Notes には、下流への義務が含まれる。
- **Findings**：
  - 位置の問い合わせは部分木をたどるので、全ノードを問い合わせると O(n·深さ)。spec に上限はない。html-renderer が問題にするなら、`syntax-tree` を改める（13.1）。
  - 綴りを捨てた後の値は、設定では拒む値（`&#10;` から導いた LF を含む text の literal、構造のトークンだけで綴った空の code の literal）でもよい。書き手はこれも正しく出力する（design の Facets）。
  - 参照リンクの値の取得は、検索の作業用のバッファの確保に失敗しうる（9.2）。
  - オートリンクに title を設定してインラインの形にしても、宛先のトークンは残る（11）。HTML の出力は値だけを使うので影響しない。
- **Implications**：要件 1.5（問い合わせの失敗）、3.7（設定できない値）、12.5（位置の時間）と、design の CoreHtml の値の読み方の表に入れた。

### cmark の HTML 出力の規則（`src/html.c`、0.31.1）
- **Context**：要件 2、4、5 の具体的な出力を確定するため。規則はモデルによらないが、この版で確かめ直した。
- **Sources Consulted**：cmark の `src/html.c`（GitHub の master、2026-11-27 に取得）、`src/houdini_html_e.c`、`src/houdini_href_e.c`、`src/scanners.re`（`_scan_dangerous_url`）
- **Findings**：
  - `S_render_sourcepos` を呼ぶのは、block_quote、list、item、heading、code_block、thematic_break、paragraph（タイトでないとき）だけである。html_block、document、インラインには出さない。書式は ` data-sourcepos="%d:%d-%d:%d"`（開始の行、開始の列、終わりの行、終わりの列）。
  - `cr()`：出力が空でなく、最後のバイトが LF でなければ LF を出す。
  - 各型の出力、エスケープ、href のエスケープ、危険な URL の規則は design の表のとおり。
- **Implications**：design の CoreHtml と HtmlEscape の表に固定した。

### ソース位置の範囲と cmark の書式の対応
- **Context**：`syntax-tree` は「ソース位置の属性の書式（終わりの列を含めるかなど）は html-renderer が決める」とした。
- **Findings**：
  - `amark_source_range_t` の `start_column` は最初のバイトの行の中の位置 + 1、`end_column` は最後のバイトの位置 + 1（範囲の終わりの位置）である。cmark の終わりの列は最後の文字を含む 1 から数える列なので、値をそのまま書けば一致する。
  - ブロックの範囲は、末尾の空行を含まない行の範囲と、最初の行と最後の行の上の記録（帯、空行、表記、中身の部分）の最も左と最も右の列から決まる。引用の中の段落は、帯の後の列から始まる（cmark と同じ）。
  - cmark は、リストやリスト項目の終わりの位置に独自の癖を持つ（後に続く空行や行の長さの扱い）。CommonMark spec の例は sourcepos を照合しないので、この spec は「`syntax-tree` の範囲を cmark の書式で出す」までを約束し、cmark の位置の値との一致は約束しない。
  - 範囲が複数のソースにまたがるとき、`syntax-tree` は始まりのソースで切る。属性はソースの番号を持たない。
- **Implications**：要件 5.1、5.2 を「書式は cmark、値は構文木の範囲」と書いた。

### cmark-gfm の拡張の HTML 出力
- **Context**：要件 11（GFM 相当の出力を公開のスロットだけで書けること）。
- **Sources Consulted**：cmark-gfm の `extensions/table.c`、`strikethrough.c`、`tasklist.c`、`tagfilter.c`、`src/html.c`（脚注）、GFM spec 0.29
- **Findings**：
  - 表：見出しの行を `<thead>`、最初の本体の行の前で `<tbody>`、`<th>` / `<td>` に `align`。
  - 取り消し線：`<del>`。タスクリスト：`<li>` の直後に `<input type="checkbox" disabled="" /> `（チェック済みは `checked="" ` を加える）。
  - タグフィルタ：9 つのタグ名の開始の `<` を `&lt;` にする（生の HTML の出力だけ）。
  - 脚注：参照の出現順の番号、同じ脚注の 2 回目以降の参照は id に `-k` を付ける。末尾の `<section class="footnotes" data-footnotes>` に定義を並べ、最後の段落の終わりの前に戻りのリンクを置く。
- **Implications**：必要なフックは、ノード型ごとの入る/出るの処理、前の処理の呼び出し、子の省略、出力の後のフック、拡張ごとの状態、入れ子の出力、分類つきのフィルタ、子のタイトさの設定である。脚注は木を動かさずに、出力の文脈だけで末尾に出せる。
- **Follow-up**：戻りのリンクの 2 回目以降の書式（`data-footnote-backref-idx="N-k"` と `<sup class="footnote-ref">k</sup>`）は、テストのデータを作るときに cmark-gfm の出力と照らして確かめる。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| スロット駆動のレンダラ ＋ 組み込みの出力の構文拡張 | エンジンは型を知らず、組み込みも公開のスロットで書く | 拡張と組み込みが同じ表現力（steering） | 連鎖の表を出力ごとに作る | 採用（モデルによらない判断で、作り直しても変わらない） |
| レンダラが位置を自分で計算する | `spelling.h` の帯と中身の部分を読んで、ブロックの範囲を下から上へ求める | すべてのブロックで線形になる | `syntax-tree` の位置の規則を重複して持つ（structure.md の「重複した実装を作らない」に反する） | 不採用 |
| レンダラが値を綴りから読む | トークンを読み、エスケープや文字参照を自分で解く | なし | 値と綴りの二重の真実。編集の後の値と食い違う | 不採用 |

## Design Decisions

### Decision: 前の版の design を持ち込まない
- **Context**：利用者の指示（2026-11-26）：中途半端に前の設計を引きずらない。
- **Selected Approach**：requirements、design、tasks、research を新しく書いた。前の版の内容のうち、構文トークンのモデルに依存しないもの（スロットの形、出力の文脈、フック、フィルタ、エスケープの規則、cmark の出力の表）は、新しい上流の API の言葉で書き直して残した。`amark_node_source_span`、`amark_document_position`、型ごとの取得関数、`extension.h` への追記、型のデータを持つ検証用のノード型は、名前も役割も残さない。

### Decision: 値はすべて `amark_node_get` で読み、綴りを見ない
- **Context**：要件 3。値は綴りから導かれ、編集で綴りが捨てられても値は残る。
- **Selected Approach**：組み込みの出力の処理は、`amark_node_get` と `value.h` の項目の番号だけで値を読む。補助の関数 `amark_html_core_get` は `struct_size` を入れて呼ぶだけにする。
- **Rationale**：値の意味を持つのは `syntax-tree` だけになり、パースした木、手で組み立てた木、編集した木が同じ規則で出力される。

### Decision: 解決できない参照のリンクと画像は、宛先を空にして要素を出す
- **Context**：定義のない参照の形のリンクは、パースでは作られないが、編集（定義の削除やラベルの変更）で作れる。`amark_node_get` は `AMARK_VALUE_UNRESOLVED` を返す。
- **Alternatives Considered**：
  1. 宛先を空、タイトルをなしとして `<a href="">` を出す（危険な URL と同じ出力）。
  2. 要素を出さず、子だけを出す。
  3. 元の表記（`[foo][bar]`）をテキストとして出す。
  4. 失敗を返す。
- **Selected Approach**：1。
- **Rationale**：木は「リンク」と言っているので、要素を残すのが木の意味に忠実である。危険な URL の扱いと同じ形なので、新しい規則が増えない。3 は綴りを読むか合成することになり、この spec の範囲を超える。4 は編集の途中の木を出力できなくする。
- **Trade-offs**：Markdown に書き戻して再パースした結果（テキストになる）とは、HTML が一致しない。
- **Follow-up**：利用者の確認を要する判断として報告する。

### Decision: ソース位置の属性は `amark_node_source_range` をブロックごとに 1 回呼んで作る
- **Context**：要件 5、12.5。問い合わせは部分木の大きさに比例する。
- **Alternatives Considered**：
  1. ブロックごとに問い合わせる。
  2. レンダラが位置を下から上へ計算する（上の表で不採用）。
  3. `syntax-tree` に、すべてのブロックの位置を一度に求める API を足す。
- **Selected Approach**：1。要件 12.5 に時間の範囲を明記し、ソース位置の属性なしの出力だけを線形と約束する。
- **Rationale**：位置の規則を重複して持たない。属性はエディタのプレビューのための任意の機能で、深い木で使うことはまれである。3 は必要になったときに `syntax-tree` に戻って足す（Revalidation Triggers）。
- **Trade-offs**：深さ 10 万の鎖に属性を付けると、時間が二次になる。scale_test は属性なしで深さを、属性ありで平らな大きい文書を確かめる。
- **Follow-up**：利用者の確認を要する判断として報告する。

### Decision: 検証用のノード型の値を項目として宣言する
- **Context**：前の版は、列の揃え、脚注のラベル、チェックの有無を型のデータに持っていた。新しいモデルでは、型は値と style を項目として宣言でき、比較、ダンプ、複製、綴りを捨てる操作が型ごとの関数なしで働く。型のデータは `copy` がないとバイトの複製になる。
- **Selected Approach**：`test.table` の align（STRING、列ごとに `l` `c` `r` `n`）、`test.table_row` の header（BOOL）、`test.task_item` の checked（BOOL）と mark（style）、`test.footnote_ref` と `test.footnote_def` の label（STRING、RAW、NONEMPTY）、`test.strikethrough` の tildes（style）を項目にした。型のデータは持たない。
- **Rationale**：パーサの検証用拡張は `amark_node_record_*` で値を記録でき、`markdown-renderer` は style から表記を合成でき、ダンプの S 式で木を組み立てられる。
- **Trade-offs**：表の列の揃えを 1 つの文字列にまとめたので、列の数の上限は項目の文字列の長さだけになるが、列ごとの値を変えるには文字列全体を設定する。
- **Follow-up**：`block-parser` と `inline-parser` の改訂で、型の印（SHARE_LINES、MARGINS、CONTENT）と項目がパースの記録に足りるかを確かめる。足りなければこの spec に戻って直す（Revalidation Triggers）。

### Decision: 脚注のラベルの照合に `amark_label_normalize` を使う
- **Context**：脚注の参照と定義のラベルは、CommonMark のラベルと同じ規則で照合する。
- **Selected Approach**：検証用の拡張は、正規化を自分で持たず、`document.h` の `amark_label_normalize` を使う（`syntax-tree` の Implementation Notes の「自前の正規化を持たない」に揃える）。

### Generalizations（Synthesis）
- 値の読み方は、組み込みの 19 の型も検証用の型も、`amark_node_get` の 1 つの形にまとまる。CoreHtml の補助は 1 つで足りる。
- 「位置がない」「解決できない」「設定できない値」は、どれも失敗にせず、出力の規則で扱う（Error Handling の「失敗にしないもの」）。

### Build vs. Adopt
- cmark と cmark-gfm のコードは取り込まない（ライセンスは BSD-2 だが、エンジンの形が違い、スロット駆動にすると使える部分がない）。規則だけを参照し、テストのデータで一致を確かめる。

### Simplification
- `extension.h` への追記を除いた（番号はすでにある）。
- 位置の計算、タイトの判定、定義の表を持たない。
- レンダラのオブジェクト（表の使い回し）は作らない。出力ごとに表を作る費用はスロットの数に比例し、出力の大きさに比べて小さい。

## Risks & Mitigations
- 上流の値の取得が確保に失敗する → すべての処理が失敗を返し、出力は止まり、バッファを戻す。oom_test の総当たりで確かめる。
- ソース位置の属性ありの出力が深い木で遅い → 要件 12.5 に明記し、必要なら `syntax-tree` の問い合わせを改める。
- 検証用のノード型の項目が、パーサや Markdown の出力に足りない → 下流の spec の改訂で確かめ、この spec に戻って直す。
- cmark-gfm の脚注の戻りのリンクの書式の細部 → テストのデータを作るときに照らす。

## References
- `INPUT.md`（リポジトリの直下）— 構文木のモデルの入力資料
- `.kiro/specs/syntax-tree/design.md`、`requirements.md`、`tasks.md`（Implementation Notes）— 上流の契約
- `include/amark/node.h`、`document.h`、`value.h`、`extension.h`、`iterator.h`、`spelling.h` — 上流の公開ヘッダ
- cmark 0.31.1 `src/html.c`、`houdini_html_e.c`、`houdini_href_e.c`、`scanners.re` — 出力とエスケープの規則
- cmark-gfm 0.29 `extensions/*.c` — GFM 相当の出力
- CommonMark Spec 0.31.2

## Skills consulted
- `kiro-spec-requirements`（`rules/ears-format.md`、`rules/requirements-review-gate.md`）
- `kiro-spec-design`（`rules/design-principles.md`、`design-discovery-light.md`、`design-synthesis.md`、`design-review-gate.md`）
- `kiro-validate-design`（`rules/design-review.md`）
- `kiro-spec-tasks`（`rules/tasks-generation.md`、`rules/tasks-parallel-analysis.md`）

## 設計のレビュー（`/kiro-validate-design`、2026-11-27、inline）
- **判定**：GO。重大な問題（構造の不整合、要件の欠落、上流の契約との矛盾）は見つからなかった。
- **確かめたこと**：
  - 上流の公開ヘッダとの照合：design が使う関数と定数（`amark_node_get`、`AMARK_VALUE_UNRESOLVED`、`AMARK_LIST_TIGHT`、`amark_node_source_range` の `has_position`、`amark_syntax_set_slot_at`、`AMARK_SLOT_HTML_NODE` 11 と `AMARK_SLOT_HTML_HOOKS` 12、`amark_extension_t` の `slots` の型、`amark_label_normalize`）が、`include/amark/` にその名前と意味で存在する。
  - 公開ヘッダの順：`check_public_headers.cmake` の 3 番目の組 `render.h html.h` と一致する。
  - レイヤー：`src/syntax/html_*.c` は公開ヘッダだけを使い、`src/render/` の内部ヘッダに依存しない。
- **指摘と修正**（1 件、軽微）：
  - 切り離した部分木を出力するときの参照の解決が書かれていなかった（入れ子の出力と脚注の定義の出力で起きる）。CoreHtml の値の読み方に、解決は文書の根につながる定義だけを見る（`amark_document_find_definition` の規則）ことを書き足した。
- **長所**：値・タイト・位置の意味を `syntax-tree` に一本化し、レンダラが綴りを読まない。スロットと文脈の契約はモデルに依存しないので、下流（`markdown-renderer`）が同じ形を使える。

## タスクの計画のレビュー（`/kiro-spec-tasks`、2026-11-27）
- **Task Plan Review Gate**：1 回目で 3 件を直した。(1) テストの補助（3.1）が、まだない組み込みの拡張を構文セットに足す前提だった → 3.1 を、組み込みの拡張の骨組み（空の表と取得の関数）を作る統合のタスクにした。(2) 規模のテストと確保の失敗のテストが同じテストの登録のファイルを編集するのに並列だった → 規模のテストの並列の印を外した。(3) 検証用拡張の独立性の確認が単独のタスクとして小さすぎた → 4.1 に含めた。2 回目で通った。
- **Task-graph sanity review**：mode は inline（このセッションでは新しいレビュー用のサブエージェントを起動できない）。判定は PASS。隠れた前提（`amark_test_tree` のリンク、INTERNAL の登録）は各タスクに書いてあり、`html.h` に宣言を足すタスクはどれも並列にしていない。
- design への反映：タスクに合わせて、HtmlOutput と HtmlDispatch の INTERNAL の単体テスト（`output_test.c`、`dispatch_test.c`）を File Structure Plan と Testing Strategy に足した。
