# Research & Design Decisions

## Summary
- **Feature**: `block-parser`
- **Discovery Scope**: New Feature（前の版の design は旧モデルの上にあり、実装はない。上流の `syntax-tree` は新しいモデルで実装済み、`html-renderer` は新しいモデルで承認済み）
- **Key Findings**:
  - `syntax-tree` の記録の API（行の範囲、帯、空行、レイアウトのトークン、中身の部分、中身のトークン、行の移し替えと取り除き、中身の先頭の削り）は、CommonMark のブロック構造の記録に足りる。前の版にあった前処理、特例のトークン、自前の定義の表は要らない。
  - 帯と空行の記録には「その行の持ち主が決まるまで待つ」必要がある。ブロックの行の範囲は末尾の空行を含めず、範囲の外に記録できないので、空行を含む行がどのブロックの範囲に入るかは、次の空でない行（か入力の終わり）まで決まらない。
  - 段落の中身の末尾の空白は、cmark ではインラインの段階（`cmark_parse_inlines` の rtrim）で除かれる。ブロックの段階で除かずに中身に残し、インラインの解析に綴らせると、記録が 1 行ずつ確定し、保留の仕組みが要らない。

## Research Log

### 前の版の design が使っていた、もうない API
- **Context**：ユーザーの指示「旧設計を半端に持ち込まない」。
- **Sources Consulted**：前の版の `design.md`（2026-11-26 承認）、`include/amark/` の今の公開ヘッダ、`syntax-tree` の tasks.md の Implementation Notes。
- **Findings**：前の版は `token.h`（ソースの絶対位置のトークン）、`amark_document_check_tokens`、`AMARK_TOKEN_FLAG_CONTAINER_PREFIX`、`PARTIAL_TAB`、`LAZY_CONTINUATION`、エンジンの役割の番号（BLANK_SPACE、BOM など）、編集の追跡の有効化、`literal_mode`、`transfer_tokens`、区間の並びの TextInput、自前の `amark_label_normalize` と case fold と LabelMap を使っていた。どれも今の公開ヘッダにない。
- **Implications**：requirements、design、tasks をすべて書き直す。残す考え方は、モデルに依存しないもの（構文定義駆動のエンジン、スロットの種類 1–4、優先度の順の開始の試行、段落の消費、後処理の 2 つの段階、深さの上限）だけで、新しい言葉で書き直す。

### `syntax-tree` の Implementation Notes のうち、この spec への義務
- **Sources Consulted**：`.kiro/specs/syntax-tree/tasks.md` の Implementation Notes、`include/amark/spelling.h`、`document.h`、`value.h`、`extension.h`。
- **Findings**：
  - ラベルの正規化と case fold と文字参照の解読を自前で持たない。`amark_label_normalize`、`amark_document_find_definition`、`src/base`（`amark_unicode_case_fold`、`amark_char_ref_decode`）を使う。ラベルの空白は空白、タブ、LF、CR だけ。
  - ロスレス性の検証の中身の被覆は、根につながる中身を持つすべての葉に掛かる。コードブロックと HTML ブロック（とリンク参照定義）の中身の全体にトークンを置く。CONTENT の literal は REPLACED の範囲だけを U+FFFD にする。
  - 空の PRESENT の帯は、行の残りが空白のときに MARGINS のどの型にも許される。LAZY の帯の列は外側の祖先の帯の終わり（なければ 0）で、記録の時点では検査されない。
  - 項目を綴るレイアウトのトークンは、記録の時点で種類とバイトを照合される。ESCAPE、CHAR_REF、REPLACED はタブを切れない。
  - 中身のトークンの記録は、葉の覚え（構造の世代ごとに有効）を使う。構造を組み立ててから記録するか、浅いノードに記録して移す。深い木を上から組み立てると O(n·深さ) になる。
  - list の tight は、導出に任せるなら記録しない（記録は上書き）。
  - 行は add / append / finish で逐次に足す。BOM はソースの印。ソースの番号は 0 から。
  - 中身の先頭の削り（`amark_block_trim_content_start`）を使う後処理は、インラインの解析の前に行う。
  - 表のセルの `\|` は、セル自身のレイアウトのトークン（ESCAPE、項目なし、役割）を 2 つの中身の部分の間に置けば表せる。
- **Implications**：design の BlockContext、BlankResolver、Syntax の記録の規約、InlineStage、GfmParseExt に反映した。

### cmark のブロックの手順（0.31.2 に対応する `blocks.c`）
- **Sources Consulted**：cmark の `blocks.c`（`S_process_line`、`parse_list_marker`、`resolve_reference_link_definitions`、`S_parse_code_block` 相当の継続の分岐）、`inlines.c`（`cmark_parse_inlines` の rtrim）、CommonMark 0.31.2 spec、`INPUT.md` の 4.3 と 6.4。
- **Findings**：
  - 項目の継続：字下げが項目の幅以上なら幅だけ進め、そうでなく空行で子があるなら最初の空白でない位置まで進め、どちらでもなければ照合しない。commonmark.js は空行の判定を先に行う。どちらも spec の例は通るが、項目の中のコードの空白だけの行の値は cmark の順でないと表せない（`INPUT.md` 6.4）。
  - 段落の中断の判定は「最後に照合したコンテナが段落か」で、インデントされたコードの開始の禁止は「末端のブロックが段落か（遅延継続になりうるか）」で行う。2 つは別の問い合わせとして公開する。
  - setext の下線では、先にリンク参照定義を取り出し、中身が残ったときだけ見出しにする。残らなければ、その行は普通の行として扱われる。
  - 段落の中身の末尾の空白は、インラインの段階で除かれる。ATX 見出しの閉じの記号と末尾の空白は、ブロックの段階で除かれる。
  - HTML ブロックの種類 1–5 は終わりの条件を含む行まで、6 と 7 は空行の前まで。種類 7 は段落を中断しない。
  - リンクの宛先のエスケープされていない括弧の入れ子は 32 まで。
- **Implications**：Syntax の各構文と、BlockContext の問い合わせ（container、tip、all_matched、maybe_lazy）の形を決めた。

### 空行の持ち主と、記録を待つ理由
- **Context**：`syntax-tree` は、空行を「その行を含み、その行を範囲に含む子を持たない一番内側のブロック」が持つと定め、範囲に末尾の空行を含めない。記録は範囲の中にしか置けない。
- **Findings**：空行 ℓ の時点では、リスト項目が続くか（ℓ は項目の中の空行）、新しい項目が始まるか（ℓ はリストの空行）、リストが終わるか（ℓ は親の空行）がわからない。インデントされたコードの空行も、後に中身の行が続くかで中身になるかが変わる。一方、引用の `>` は空でない帯なので、ℓ は必ずその引用の範囲に入る。空でない帯を持つコンテナの祖先は、その行に空でない帯を持つ（空行でない深さでは、項目は幅だけ進むので帯は空でない）。
- **Implications**：BlankResolver を置く。空でない帯はすぐ記録し、空の帯と空行の記録とインデントされたコードへの追加を、次の空でない行の置き場所が決まった時点で確定させる。ℓ を含むのは「その時点でまだ開いているブロック」か「ℓ に空でない帯を持つブロック」で、持ち主はその最も深いものになる。これは `INPUT.md` 4.3 のタイトの規則と cmark の結果に一致する（下の例）。
  - `- a` / 空行 / `- b`：項目 a は閉じ、リストは開いたまま → リストが持つ → ルーズ
  - `- a` / 空行 / `  b`：項目 a は開いたまま → 項目が持つ（子の間）→ ルーズ
  - `- a` / `- b` / 空行（終わり）：文書が持つ → タイト
  - `> - a` / `>` / `> - b`：引用は空でない帯、リストは開いたまま、項目 a は閉じる → リストが持つ → ルーズ

### リンク参照定義の切り出しの計算量
- **Findings**：`amark_block_move_lines` は移した後の `from` の中身を残りの部分から作り直すので、1 つの段落の k 個の定義を 1 つずつ移すと O(k × 段落の大きさ) になる。`amark_block_remove_lines` も同じ。
- **Implications**：定義の葉は、段落のその行のレイアウトのトークンと中身の部分を写して新しく記録し、段落からは最後に 1 回だけ行を取り除く（全部が定義なら段落を除く）。写しと 1 回の作り直しで、段落の大きさに線形になる。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 汎用エンジン ＋ 構文のスロット（採用） | エンジンは開いているブロックの列と行の照合だけを持ち、構文はスロットのコールバックで開始・継続・行の受け入れ・閉じを決める | 組み込みと拡張が同じ API。構文の追加・除去・置換が構文セットの操作だけで済む | コールバックの契約（いつ記録してよいか）を厳密に書く必要がある | steering の構文定義駆動の方針 |
| 固定の cmark の手順 ＋ フック | cmark の `S_process_line` をそのまま持ち、拡張はフックで割り込む | 実装が速い | 表や脚注を後付けすると歪む（roadmap の却下理由） | 採らない |
| 帯をすぐ記録し、後で取り除く | 空行でも帯をすぐ記録し、末尾の空行とわかったら除く | 保留が要らない | 帯を除く公開の操作（`amark_block_discard_margin`）は構造の綴りを捨てた印を立てる。無編集の文書にならない | 採らない |
| 帯と空行の記録の保留（採用） | 空の帯と空行の記録を、持ち主が決まるまで待つ | 無編集の文書の記録が正しく、行の順の記録の規則も守れる | 保留の列の管理が要る（深さの上限で行あたりの大きさは有界） | BlankResolver |

## Design Decisions

### Decision: 前の版の design を持ち込まない
- **Context**：ユーザーの指示。前の版は旧モデルの API の上に書かれていた。
- **Selected Approach**：requirements、design、research、tasks を新しく書く。スロットの種類の番号 1–4 は `extension.h` のものを使い、中身の構造体を新しく定める。
- **Rationale**：旧モデルの概念（絶対位置、特例のトークン、編集の追跡、区間の並び）は、新しいモデルでは不要か存在しない。

### Decision: 段落の中身は「各行の最初の空白でない文字から行の終わりまで」とし、最後の行の末尾の空白を中身に残す
- **Context**：CommonMark は段落の生の中身から先頭と末尾の空白を除く。cmark では先頭はブロックの段階、末尾はインラインの段階で除く。
- **Alternatives Considered**：
  1. 最後の行の末尾の空白を葉自身の表記にする — 最後の行がどれかは次の行まで決まらないので、中身の部分の記録を 1 行遅らせる保留が要る。setext、表の解釈し直し、定義の切り出しと絡む。
  2. 中身に残し、インラインの解析が構造の空白（SPACE）として綴る — cmark と同じ分担。
- **Selected Approach**：2。各行の先頭の空白は段落のレイアウトのトークン（SPACE）にする。
- **Trade-offs**：インラインの解析が末尾の空白の除去を担う（`inline-parser` との契約）。既定の単純な委譲先は、末尾の空白もテキストに含める。
- **Follow-up**：`inline-parser` の改訂で、段落と setext 見出しの中身の末尾の空白を SPACE で綴ることを要件にする。

### Decision: 項目の記号の行で、記号の後が空白だけのときは、帯を記号の終わりまでにし、残りをその項目の空行にする
- **Context**：`INPUT.md` は続く空行の項目の帯を長さ 0 とするが、記号の行の残りが空白のときを定めていない。
- **Alternatives Considered**：(1) 帯を行の終わりまで広げる、(2) 帯は記号まで、残りは空行。
- **Selected Approach**：2（cmark は記号の後の空白を進めず、その行を空行として扱う）。空行の記録は 0 列でもよい。タイトの導出は「項目の子の間」の空行だけを数えるので、この空行は影響しない。
- **Rationale**：「残りが空白だけの行は空行」という 1 つの規則に揃う。

### Decision: 空行の帯と記録を、持ち主が決まるまで保留する
- **Context**：上の Research Log「空行の持ち主と、記録を待つ理由」。
- **Selected Approach**：BlankResolver。空でない帯（引用の `>`）はその行ですぐ記録し、範囲を広げる。空の帯（項目）、空行の記録、`BLANK_IF_CONTINUED` の葉（インデントされたコード）への追加は、次の空でない行で置き場所が決まった時点（新しいブロックを開き、照合できなかったブロックを閉じた後、その行の帯と中身を記録する前）か、入力の終わりで確定させる。
- **Trade-offs**：保留の列は空行の数 × 深さの大きさを持つ（深さの上限で有界）。

### Decision: インラインの既定の委譲先を 1 か所で決め、`inline-parser` がそこを置き換える
- **Context**：`block-parser` は `inline-parser` より先に実装する。インラインのエンジンを直接呼べない。
- **Alternatives Considered**：(1) 構文セットにインラインのスロットがあれば、そのエンジンを使う（呼び出す手段がない）、(2) CommonMark の入口だけで既定を変える（`amark_parser_create` を使う利用者が単純な委譲先になる）、(3) 既定の委譲先を返す内部関数を 1 か所に置き、`inline-parser` の改訂でそこを置き換える。
- **Selected Approach**：3。オプションの委譲先が NULL なら既定の委譲先を使う。この spec の時点では単純な委譲先 `amark_parse_inline_plain`。この spec のテストは単純な委譲先を明示して、既定が変わっても結果が変わらないようにする。
- **Follow-up**：`inline-parser` の design の Downstream Extension Point として明記してもらう。

### Decision: リンク参照定義の葉は、記録を写して作り、段落からは 1 回だけ行を除く
- **Context**：上の Research Log「リンク参照定義の切り出しの計算量」。
- **Selected Approach**：段落の消費の補助（`amark_block_split_lines`）は、次の行のレイアウトのトークン（構造のものだけ）と中身の部分を新しい葉に写して記録し、段落の前に置く。消費の後に、写した行を段落から 1 回で取り除く（全部なら段落を木から除く）。setext の見出しへの置き換えは、行を 1 回で移す `amark_block_move_lines` を使う。

### Decision: ブロックを同じ記録を持つ別の型に置き換える補助を `block-parser` に置く
- **Context**：タスクリストの検証では、item を `test.task_item` に替える必要があるが、`syntax-tree` には型を変える操作がない。帯はノードに付くので、置き換えると失われる。
- **Selected Approach**：`amark_block_retype`（block.h）。新しいノードに行の範囲、帯、空行、レイアウトのトークンを公開の記録の関数で写し、子を移し、元のノードと置き換える。値と style は写さない（型が違う）。中身を持つ葉は対象にしない（`amark_block_move_lines` を使う）。
- **Rationale**：公開 API だけで書ける。記録は編集でないので、無編集の文書のままになる。
- **Follow-up**：型を変える操作が利用者にも要るとわかったら、`syntax-tree` の操作として移す（ユーザーの確認の対象）。

### Decision: 段落の受け皿の構文がないとき、行の残りをエンジンの「取り手のない」表記にする
- **Context**：要件 3.8（組み込みの構文を除いても、他の構文の解析を変えない）とロスレス性。
- **Selected Approach**：FALLBACK のスロットがない構文セットで、どの構文も取らない空でない行の残りは、現在のコンテナのレイアウトのトークン（TEXT、項目なし、役割 `AMARK_BLOCK_ROLE_UNCLAIMED`）にする。

### Decision: 深さの上限
- **Selected Approach**：ブロックの子を持てる開いたブロック（文書を除く）の数を深さとする。既定 128、上限 1024。上限では `AMARK_BLOCK_SYNTAX_CONTAINER` の開始を試さないので、残りは段落になる。
- **Rationale**：帯は覆う行ごとに 1 つ要るので、遅延継続行 1 行の記録は深さに比例する。深さを固定すれば線形。上から木に足す挿入の O(深さ) も有界になる。

### Decision: テストの生成は spec の例の形式の共有の手順を新しく作る
- **Context**：`html-renderer` の `gen_html_cases.cmake` は `---` と `===` の行を区切りにする。Markdown の入力には `---` と `===` の行（thematic break、setext の下線）がよく現れる。
- **Selected Approach**：CommonMark の spec.txt と同じ形式（`````````````````````````````````` example` と `.` の行）を読む `tests/support/gen_spec_examples.cmake` を作る。`CHECK=` と `INCLUDE=` を受け取り、`inline-parser` が spec.txt にそのまま使える。`→` はタブに戻す。
- **Trade-offs**：生成の手順が 2 つになる（S 式の照合と Markdown の照合）。役割が違うので重複ではない。

### Decision: spec のブロックの章の例の期待値は、値だけのダンプにする
- **Context**：インラインのエンジンがないので、spec の期待する HTML とは照合できない。
- **Selected Approach**：`tests/syntax/data/spec_blocks.txt` に、0.31.2 のタブ、ブロックと行、コンテナブロック、葉ブロックの章の例の入力と、spec の HTML から作った値だけのダンプ（単純な委譲先のテキストを含む）を置く。HTML の 100% の照合は `inline-parser` が行う。

### Decision: ブロックの断片の根の空行は取り込まない
- **Context**：断片は一時の文書でパースし、`amark_node_import_children` で子だけを取り込む。根の空行は根の記録なので、子に付いて移らない。
- **Selected Approach**：取り込まない。断片の先頭と末尾の空行は、挿入先で書き戻すときに `markdown-renderer` がブロックの区切りとして合成する。

### Generalizations（Synthesis）
- 葉の中身の全体をトークンで覆う処理（コードブロック、HTML ブロック、単純な委譲先、拡張の literal の葉）は、1 つの走査 `amark_scan_content_run` と補助 `amark_block_cover_content` にまとめた。
- 値を綴る単位への分割（情報文字列、宛先、タイトル、ラベル）は、`amark_scan_value_run` の印の組み合わせ（エスケープ、文字参照）で表す。
- 段落の類のブロックへの操作（消費、置き換え、末尾の行の解放）は、開いているブロックの列を正しく保つためにエンジンの補助として公開した。

### Build vs. Adopt
- cmark と commonmark.js のコードは取り込まない（ライセンスとモデルの違い）。規則とテストの例を参照する。

### Simplification
- 前処理（タブの展開、NUL の置換、改行の正規化）の層を持たない。行の表と記録がすべてを表す。
- 自前の定義の表、ラベルの正規化、文字参照の解読を持たない。
- パーサの所有の入口（構文セットの所有を受け取る生成）は作らない。文書の所有の受け渡し（`amark_document_take_syntax_set`）で足りる。

## Risks & Mitigations
- 保留の空行の解決の誤りで帯や空行の記録が抜ける — すべてのパースのテストでロスレス性の検証、`amark_tree_check`、`amark_node_verbatim`（根が真）を確かめる。タイトの導出を cmark の結果と照合するケースを入れる。
- 拡張のコールバックが記録の規則を破る — 記録の関数は `syntax-tree` の検査で不正な引数を返し、パースはそのエラーで止まる。
- `html-renderer` の実装がまだない — tasks で、CommonMark の構文セットの構築と GFM 相当の検証を、`amark.core.html` と `amark_test_html_gfm` の実装の後に置く。
- `foundation` の Downstream Extension Points の表は、case fold と文字参照の表の追加を `block-parser` に割り当てたままだが、`syntax-tree` がすでに足した。この spec では足さない（`foundation` の改訂で表を直す対象）。

## References
- CommonMark Spec 0.31.2 — ブロックの規則と例（テストデータとしてだけ使う）
- cmark 0.31.x `src/blocks.c`、`src/inlines.c` — 手順の基準（コードは取り込まない）
- `INPUT.md` 3 章、4.2–4.4、4.8、6.4
- `.kiro/specs/syntax-tree/design.md`、`tasks.md` の Implementation Notes
- `.kiro/specs/html-renderer/design.md` の CoreHtml、GfmHtmlExt

## Skills consulted
- `kiro-spec-requirements`（`rules/ears-format.md`、`rules/requirements-review-gate.md`）
- `kiro-spec-design`（`rules/design-principles.md`、`design-discovery-full.md`、`design-synthesis.md`、`design-review-gate.md`）
- `kiro-validate-design`（`rules/design-review.md`）
- `kiro-spec-tasks`（`rules/tasks-generation.md`、`rules/tasks-parallel-analysis.md`）

## 設計のレビュー（`/kiro-spec-design` の Design Review Gate、2026-11-27）
- **機械的な確認**：requirements.md の 91 の ID がすべて design の Requirements Traceability にある。Boundary Commitments の 4 つの節、File Structure Plan の具体的なパスがある。Components の表のすべてのコンポーネントに、ファイルの割り当てがある。
- **1 回目の修正**（3 件）：
  1. 1 行の処理で、受け皿の開始（段落を開く）と `add_line` の順が曖昧だった。開始は置き場所の段階（解決の前）、`add_line` と取り手のない表記は解決と帯の記録の後と書き分けた。
  2. 項目が空行で `amark_block_skip` で進めた列の持ち主が、HTML ブロックで不明だった。葉の記録の規則（INDENT に含めるか、SPACE にする）を BlockApi と記録の規約の表に足した。cmark では、この列は HTML ブロックの中身にならない。
  3. 要件 9.6 の「行末の空白」が、段落の最後の行の末尾の空白（6.2 で中身に残す）と矛盾して読めた。要件 9.6 を「見出しとフェンスと thematic break の行の行末の空白」に直した。
- 2 回目で通った。

## 設計の検証（`/kiro-validate-design`、2026-11-27、inline）
- **判定**：GO。重大な問題（上流の契約との矛盾、要件の欠落、層の違反）は見つからなかった。
- **確かめたこと**：
  - 上流の公開ヘッダとの照合：design が使う関数と定数（`amark_block_set_lines`、`_add_margin`、`_add_blank_line`、`_add_token`、`_add_content`、`_move_lines`、`_remove_lines`、`_trim_content_start`、`amark_block_set_free_content`、`amark_block_replace_content`、`amark_node_add_content_token`、`amark_node_record_int`、`_record_string`、`amark_document_add_source`、`_append_source`、`_finish_source`、`amark_document_check_lossless`、`amark_document_find_definition`、`amark_label_normalize`、`amark_node_import_children`、`amark_node_verbatim`、`AMARK_MARGIN_FIRST`、`AMARK_SLOT_BLOCK_SYNTAX` 1–`AMARK_SLOT_PARSE_STATE` 4、`value.h` の項目の番号）が、`include/amark/` にその名前と意味で存在する。
  - 帯の記録の前提：`amark_block_set_lines` は親の範囲を検査しない（`src/tree/layout.c`）ので、葉の記録の後に親の範囲を広げる順でよい。リストのタイトの導出（`amark_tree_list_loose`）は子の型を問わないので、`test.task_item` を子に持つリストも空行から導かれる。
  - 公開ヘッダの順：`check_public_headers.cmake` の 4 番目の組 `scan.h parser.h block.h inline.h commonmark.h` と一致する（`inline.h` は `inline-parser` が足す）。
  - `html-renderer` の契約：`amark.core.html` を `amark.core` の直後に足すこと、`commonmark.*` が HTML のスロットを持たないこと、検証用の部品が型を登録しないこと、値を `amark_node_record_*` で記録することを守っている。
- **指摘と修正**（1 件、軽微）：インラインの断片では委譲先に自由な中身の葉を渡すのに、InlineStage が「中身は配置済み」とだけ書いていた。通常のパースは配置済み、断片は自由で、委譲先は両方を扱うと書き直した。
- **長所**：記録の分担（帯はエンジン、表記と中身は構文、空行は解決の時点）がはっきりしており、`syntax-tree` の順序の規則と範囲の規則を構造的に守る。記録の規約の表が `markdown-renderer` との契約として独立して読める。
- **残るリスク**：保留の空行の解決は込み入っている。すべてのパースのテストで、ロスレス性、`amark_tree_check`、根の `amark_node_verbatim` を確かめ、タイトを cmark の結果と照合するケースで補う。

## タスクの計画のレビュー（`/kiro-spec-tasks`、2026-11-27）
- **Task Plan Review Gate**：
  - 1 回目で 4 件を直した。(1) インラインの段階のテストがエンジンより前にあり、葉を作れなかった → 開閉と照合のエンジン（3.2）、記録と遅延継続と受け皿（3.3）、インラインの段階（3.4）、検査の補助（3.5）の順にした。(2) エンジンのケースのデータが、後で作る CommonMark の構文セットの検査の関数に依存していた → データのケースは 5.1 に移し、3.3 は手書きのテストにした。(3) 並列の印の構文のタスクが、ほかの構文に依存する組み合わせのケース（引用の中のリスト、項目の中のコード）を持っていた → 5.1 に移した。(4) 並列の印の構文のタスクが共有ヘッダとテストの登録のファイルを編集しうる → 共有ヘッダの補助を 4.2、構文ごとのデータのファイルの登録を 4.1 で先に作り、並列のタスクは自分のファイルだけを編集する形にし、`_Depends:` を明記した。
  - 2 回目で通った。91 の要件の ID はすべてタスクにある。
- **Task-graph sanity review**：mode は inline（このセッションでは新しいレビュー用のサブエージェントを起動できない）。判定は PASS。隠れた前提（`html-renderer` の実装を 4.1 と 5.4 の前に要すること、公開ヘッダに宣言を足すタスクを並列にしないこと、INTERNAL の登録）は計画の冒頭とタスクに書いてある。
- design への反映：タスクに合わせて、構文ごとのケースのデータのファイル（`tests/syntax/data/<構文>_cases.txt` と `combined_cases.txt`）と、受け皿のない構文セットの確認の場所を File Structure Plan と Testing Strategy に反映した。
