# Research & Design Decisions: inline-parser

## Summary
- **Feature**：`inline-parser`
- **Discovery Scope**：Complex Integration（新しいエンジンと公開 API。上流の `syntax-tree` は実装済み、`block-parser` と `html-renderer` は承認済みの design だけがある）
- **Key Findings**：
  - `syntax-tree` の早期検証（`tests/tree/msgstr_flow_test.c`）が、中身の上のトークンでインラインを組み立てる手順を CommonMark の例で確かめた。区切り文字の連続を 1 文字ずつの DELIMITER のトークンにすれば、一部だけを使う照合も、使われずに残った区切り文字（DELIMITER のトークンと記録した literal の text）も表せる。この spec はこの手順を、エンジンの内部の並びの上で行い、最後に一度だけ記録する形にする。
  - 元の表記の判定（`amark_block_content_verbatim`）は、すべてのインラインのノードが自分のトークンを持ち、子の範囲が親のトークンの範囲の中にあることを求める。区切りの文字を持たないインラインの包み（区切りのない拡張オートリンクを link と text で表すもの）は、この判定を満たせない。
  - 中身のトークンの記録は、葉の覚え（構造の世代ごと）で葉を確かめる。構造を組み立ててからまとめて記録すれば償却で定数になる。上から順の挿入は祖先をたどるので、インラインの部分木は下から組む。

## Research Log

### 上流の契約の読み取り
- **Context**：旧モデルの概念を持ち込まないため、契約を公開ヘッダと承認済みの design から読み直した。
- **Sources Consulted**：`include/amark/{node,document,value,spelling,extension,entity,unicode}.h`、`.kiro/specs/syntax-tree/design.md`（Facets、Content、Verbatim、DefinitionIndex）、`.kiro/specs/syntax-tree/tasks.md` の Implementation Notes、`tests/tree/msgstr_flow_test.c`、`tests/tree/verbatim_test.c`、`src/tree/verbatim.c`、`src/tree/builtin_types.c`、`.kiro/specs/block-parser/design.md`、`.kiro/specs/html-renderer/design.md`、`INPUT.md` の 4.5–4.8、6.5。
- **Findings**：
  - トークンの種類は 9 つ（text marker space delimiter escape char_ref replaced line_break tab_remainder）。中身のトークンは種類とバイトの一致を記録の時点で検査する。text は NUL と不正な UTF-8 を含めない。分けられるのは text だけで、種類は変えられない。
  - 値の導出：escape は後の 1 文字、char_ref は解読、replaced は U+FFFD、line_break は `\n`（FOLD_LINE_BREAKS なら空白）、tab_remainder は空白、marker と space と delimiter は寄与しない。導出は設定・記録の値より先に効く。
  - 組み込みの型の項目（`value.h`）：text.literal（NO_LINE_BREAK）、code.literal（NO_LINE_BREAK、NONEMPTY、FOLD_LINE_BREAKS）、code.backticks、code.padded、html_inline.literal（RAW、NO_BLANK_LINE）、linebreak.form、emph/strong.delimiter、link/image の destination、title、label（RAW）、form（inline、full、collapsed、shortcut、autolink）、angle、title_quote。
  - 参照の形の destination と title は、label から `amark_document_find_definition` で導かれる。記録した値は使われない。
  - link と image の `on_change`：参照の形かオートリンクに destination か title を設定すると inline の形になる。
  - `amark_block_content_verbatim`：中身が配置済み、綴りを捨てていない、すべてのインラインの子孫が自分のトークンを持つ、被覆がちょうど 1 回、入れ子の順（子の範囲は親の最初と最後のトークンの間、兄弟は順に重ならない、親のトークンは子の範囲の外）。
  - 早期検証の組み立てでは、強調の区切り文字は役割 1（開き）と 2（閉じ）、項目なし（INPUT.md 3.1 のダンプ）。コードスパンの両端の空白は SPACE。インラインのリンクのタイトルの引用符は項目 title_quote を付けて記録していた。`block-parser` のリンク参照定義は、引用符を項目なしの DELIMITER（役割 TITLE_OPEN、TITLE_CLOSE）にしている。
  - オートリンク：`tests/tree/verbatim_test.c` は、`<` と `>` を link の DELIMITER、子の text に TEXT のトークン、destination を記録した値で表し、元の表記と判定される。
  - `block-parser` の委譲：`amark_inline_parse_fn_t(context, leaf, user_data)`、中身は配置済みか自由で、トークンとインラインの子をまだ持たない。段落の最後の行の末尾の空白は中身に残り、インラインが SPACE で綴る。既定の委譲先は `amark_parse_default_inline()` の 1 か所。テストの補助 `amark_test_parse_check` は、委譲先が NULL なら単純な委譲先を使う。
  - `block-parser` の走査の関数：`amark_scan_link_label/destination/title`（括弧の深さ 32、ラベル 999 文字）、`amark_scan_html_open_tag/closing_tag`、`amark_scan_value_run`、`amark_scan_content_run`（中身の単位。TAB_REMAINDER と LINE_BREAK を含む）。
  - `html-renderer`：値だけを `amark_node_get` で読む。解決できない参照は `<a href="">`。検証用のノード型は HTML の部品が登録し、他の spec は記述子を参照するだけ。
- **Implications**：記録の規約は、項目を綴るのは値（STRING）だけ、構造のトークンは項目なしと役割の番号、という `block-parser` の規約に揃える。参照の形では label と form だけを記録する。エンジンは委譲先の形で提供し、既定の委譲先を置き換える。

### CommonMark の参照実装の手順
- **Context**：区切り文字の処理、ブラケット、改行、オートリンク、計算量の対策の基準。
- **Sources Consulted**：CommonMark Spec 0.31.2（Inlines の章、Appendix: A parsing strategy）、cmark 0.31.x `inlines.c`（`handle_newline`、`handle_backslash`、`handle_close_bracket`、`process_emphasis`、`make_autolink`、`cmark_clean_autolink`）、cmark-gfm の `strikethrough.c`、`autolink.c`、脚注の参照の処理。コードは取り込まない。
- **Findings**：
  - 改行：直前のテキストの末尾の空白を除き、改行の直前が 2 つの空白ならハードブレーク、そうでなければソフトブレーク。改行の後の行頭の空白は飛ばす。`\` と改行はハードブレーク。
  - 区切り文字：`openers_bottom` を文字、閉じの長さの 3 の剰余、開けるかで分けて持ち、線形にする。開くことも閉じることもできない並びはスタックに積まない（テキスト）。
  - ブラケット：閉じで最も近い開きだけを見る。無効の開きは text。リンクができたら、それより前の画像でない開きを、すでに無効のものに着くまで無効にする。開きの後に別の開きがあった（`bracket_after`）なら、省略参照とショートカットの扱いをしない。
  - 参照：後に完全なラベルが走査できたら、そのラベルだけで引く。見つからなければリンクにならない。
  - オートリンク（cmark）：子のテキストは文字参照を解読する。宛先も文字参照を解読し、メールアドレスには `mailto:` を足す。エスケープは働かない。
  - 取り消し線（cmark-gfm）：`~` の並びは 1 か 2 文字だけ。開きと閉じの長さが同じときだけ一致する。flanking は `*` と同じ。
  - 脚注の参照（cmark-gfm）：リンクにならなかったブラケットの中が `^` で始まり、後に文字があるとき。
  - 計算量：cmark はバッククォートの長さごとの最後の位置、閉じない HTML の構文ごとの走査済みの印を、対象の文字列（葉）ごとに持つ。
- **Implications**：構文ごとに葉ごとの状態（0 で埋めた領域）を持てるようにする。区切り文字の規則は EMPHASIS（3 の倍数の規則）と EXACT（長さの一致）の 2 つで、組み込みの強調と取り消し線の両方を表す。

### 拡張オートリンクと元の表記の判定
- **Context**：GFM 相当の検証で、元の表記のまま出せるという判定も確かめる（13.3）。
- **Findings**：区切りの文字のない拡張オートリンク（`www.example.com`）を link と子の text で表すと、すべてのバイトは子の text のトークンになり、link は自分のトークンを持てない。`syntax-tree` の判定は、自分のトークンのないインラインのノードを新しいノードとみなすので、その段落は元の表記と判定されない。長さ 0 のトークンも記録できない。
- **Implications**：検証用拡張は、子を持たない独自のインラインのノード型（`test.ext_autolink`：表示の文字列をトークンで綴る値、宛先を記録する値）と、その HTML の処理を、同じ検証用拡張の中に持つ（Design Decisions）。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 木を直接組み立てる（cmark 方式） | 走査しながらノードを作り、照合でノードを動かし、トークンを移す | 早期検証の手順そのまま | 構造を変えるたびに葉の覚えが無効になり、記録が深さに比例する。上からの挿入で祖先をたどる | 採らない |
| 内部の並び → 構造 → 記録 | 走査と照合を内部の並びで行い、最後に下から構造を組み、ノードごとにトークンを中身の順に記録する | 「構造を先に」を満たす。トークンの移動と分割が要らない。失敗の後始末が単純 | 拡張の出すトークンをエンジンが預かる必要がある | 採る |
| 構文ごとに木を組み、記録も構文が行う | 拡張が `syntax-tree` の記録の関数を直接呼ぶ | API が薄い | 記録の順と構造の順を拡張ごとに守らせる必要がある。区切り文字の照合で構造が変わる | 採らない |

## Design Decisions

### Decision: エンジンは内部の並びで解析し、構造を組んでからトークンを記録する
- **Context**：葉の覚えは構造の世代ごとに有効で、上からの挿入は祖先をたどる（syntax-tree の Implementation Notes 14.1、15.5）。
- **Alternatives Considered**：
  1. 早期検証のとおり、走査で text のノードを作り、照合で `move_content_tokens` で移す。
  2. 内部の並び（テキストの単位、区切り文字の並び、ブラケットの開き、構文のノード）で走査と照合を行い、最後に構造、次にトークンの順で反映する。
- **Selected Approach**：2。構文の拡張は `amark_inline_new_node` でノードを作り、`amark_inline_token` でトークンをエンジンに預ける。エンジンは葉の解析の最後に、部分木を下から組み、葉の子にしてから、ノードごとに中身の順でトークンを記録する。
- **Rationale**：記録が償却で定数になる。トークンは最初から正しいノードに記録されるので、移動も分割も要らない。分けられるのは text だけという規則に触れない。
- **Trade-offs**：拡張は記録の関数を直接呼ばず、エンジンの関数を使う。トークンの種類の誤りは、記録の時点（解析の最後）に `syntax-tree` の検査で見つかる。
- **Follow-up**：葉の子への挿入は、上の葉の祖先をたどる（ブロックの深さの上限で有界）。scale_test で確かめる。

### Decision: 記録の規約は block-parser に揃え、項目を綴るのは値だけにする
- **Context**：早期検証はタイトルの引用符に項目 title_quote を付けた。`block-parser` は定義の引用符を項目なしにしている。
- **Alternatives Considered**：
  1. style を表す区切り（引用符、山括弧、強調の区切り文字）にも、その style の項目を付ける。
  2. 項目を付けるのは値（STRING）の綴りだけにし、構造のトークンは項目なしと役割の番号にする。style は `amark_node_record_int` で記録する。
- **Selected Approach**：2。
- **Rationale**：ブロックとインラインで 1 つの規約になり、`markdown-renderer` の読み方が 1 つで済む。INPUT.md 3.1 の強調の記録も 2 の形である。
- **Trade-offs**：style を設定しても、古い区切りのトークンはノードに残る（その style の綴りを捨てた印は立つ）。書き手は印を見て区切りを合成し直す。
- **Follow-up**：`markdown-renderer` の改訂で、この読み方を確かめる（Revalidation Triggers）。

### Decision: 参照の形のリンクの成否は、宛先の値と同じ形のラベルで引く
- **Context**：定義のラベルは REPLACED のトークンから U+FFFD として導かれる。`amark_label_normalize` は NUL を普通の文字として残す。
- **Selected Approach**：成否の検索に使うラベルは、リンクが持つことになるラベルの値と同じ文字列（NUL と不正な UTF-8 を U+FFFD にしたもの）にする。省略参照とショートカットでは、その文字列を記録する。
- **Rationale**：パースの時点の判断と、後の `amark_node_get` の解決が食い違わない。

### Decision: オートリンクの宛先は解読した値を記録する
- **Context**：オートリンクのバイトは子の text が綴るので、宛先は記録するしかない（NO_LINE_BREAK）。cmark は宛先の文字参照を解読する。
- **Selected Approach**：文字参照を解読し、メールアドレスには `mailto:` を足した値を記録する。解読で LF か CR になる文字は `%0A`、`%0D` にして記録する（cmark の URL のエスケープの出力と同じバイトになる）。
- **Trade-offs**：この 2 文字だけ、値が cmark の内部の値と違う。HTML の出力は同じ。

### Decision: 中身の先頭と末尾の空白は、エンジンが葉の綴りにする
- **Context**：`block-parser` は段落の最後の行の末尾の空白を中身に残し、インラインに綴らせる。自由な中身（msgstr）は先頭と末尾に空白や LF を持ちうる。
- **Selected Approach**：エンジンは、解析の前に中身の先頭と末尾の空白（空白、タブ、改行、タブの残り）を葉自身のトークン（SPACE、TAB_REMAINDER、LINE_BREAK、項目なし）にし、その間だけを解析する。
- **Rationale**：段落の仕上げで末尾を除く CommonMark の規則と同じ結果になり、構文がこの範囲を気にしなくてよい。

### Decision: 改行の構文は、直前のテキストの末尾の空白を取り込む
- **Context**：cmark は改行の処理で直前のテキストの末尾の空白を除く。空白をきっかけの文字にすると、語の間のすべての空白で構文を呼ぶ。
- **Selected Approach**：エンジンが `amark_inline_take_trailing_space` を提供し、改行の構文（きっかけの文字は改行だけ）が直前のテキストの末尾の空白を自分の SPACE のトークンにする。
- **Rationale**：呼び出しは改行ごとに 1 回で済む。拡張も同じ仕組みを使える。

### Decision: 区切りのない拡張オートリンクは、検証用拡張の独自のノード型で表す
- **Context**：Research Log の「拡張オートリンクと元の表記の判定」。13.3 は GFM 相当の文書でも元の表記の判定を確かめることを求める。
- **Alternatives Considered**：
  1. link と子の text で表し、拡張オートリンクを含む段落を元の表記の判定から外す。`syntax-tree` に、自分のトークンのない包みのノードの扱いを足す変更を求める。
  2. 子を持たない独自のインラインのノード型 `test.ext_autolink`（表示の文字列を綴る値 `text`、記録する値 `destination`）と、その HTML の処理（`<a href="…">…</a>`）を、インラインの検証用拡張の中に持つ。
- **Selected Approach**：2（推奨。利用者の確認を待つ）。
- **Rationale**：今の `syntax-tree` の契約のまま、HTML、ロスレス性、元の表記の判定のすべてを満たす。拡張が新しいノード型とその出力を 1 つの拡張に持てることの検証にもなる。
- **Trade-offs**：拡張オートリンクは木の上で link の型にならない（リンクを書き換える道具は、この型も扱う必要がある）。`markdown-renderer` の検証用の部品は、この型の Markdown の出力を持つ必要がある。
- **Follow-up**：利用者が 1 を選ぶなら、`syntax-tree` の要件 11.3 の変更として扱う（13.5）。

### Decision: ブラケットの後処理の 2 つの形（包む、置き換える）
- **Context**：リンクと画像は中身を子にする。脚注の参照は子を持たない（`test.footnote_ref` は INLINE を受け入れない）。
- **Selected Approach**：`amark_inline_bracket_wrap`（中の区切り文字を処理してから子にする）と `amark_inline_bracket_replace`（中の解析の結果を捨て、開きから終わりまでをノードのトークンで覆う）を提供する。開きのトークンはどちらもエンジンが記録する（役割 1）。

### Decision: 葉ごとの状態と「テキストとして飛ばす」範囲
- **Context**：閉じないバッククォートや HTML の走査を繰り返すと二次になる（11.1）。
- **Selected Approach**：構文、ブラケット、テキストの走査のスロットは、葉ごとに 0 で埋めた状態（`leaf_state_size`）を持てる。成立しない構文は、続く範囲をテキストとして飛ばせる（`amark_inline_set_text_end`）。

### Decision: エンジンの準備は委譲の呼び出しごとに行う
- **Context**：委譲先の形は `amark_inline_parse_fn_t` で、エンジンの表をパースの間に置く場所が `block-parser` の文脈にない。グローバルな可変状態は持てない。
- **Selected Approach**：委譲の呼び出し（葉）ごとに、構文セットのスロットから反応の表（256 の文字ごとの構文の並び）を作る。費用は葉ごとにスロットの数の対数倍と表の大きさで、中身に比例する作業に比べて小さい。
- **Trade-offs**：小さな葉が非常に多い文書で定数倍が増える。ベンチマークで測り、問題なら `block-parser` の文脈に委譲先の状態を置く変更を求める。

### Decision: 書き戻しのための純粋な関数を公開する
- **Context**：`markdown-renderer` は、合成した区切り文字が同じ強調として読み直されるか、literal が生の HTML として読まれるかを、パーサと同じ規則で確かめる必要がある（前の版で求められた要求のうち、モデルによらないもの）。
- **Selected Approach**：`amark_inline_delimiter_flanks`、`amark_inline_match_delimiters`、`amark_inline_scan_raw_html` と、文字の宣言の問い合わせを公開する。エンジンは同じ内部の関数を使う。

### Synthesis outcomes
- **Generalization**：強調と取り消し線は、区切り文字の種類（文字、flanking、規則、長さの範囲、ノードを作る関数）の 2 つの例である。リンク、画像、脚注の参照は、ブラケットの後処理の例である。改行、エスケープ、文字参照、コードスパン、オートリンク、生の HTML は、きっかけの文字の構文の例である。
- **Build vs. Adopt**：cmark のコードは取り込まない（ライセンスは問題ないが、木のモデルと記録の形が違い、拡張のスロットの上に乗らない）。手順と規則だけを参照する。ラベル、宛先、タイトル、HTML のタグ、値と中身の単位の走査は `block-parser` のものを使う。
- **Simplification**：トークンの移動と分割は使わない（最後に一度だけ記録する）。エンジンの統計は INTERNAL の関数だけにする。脚注の参照は定義の検索だけを使い、自前の表を持たない。

### Consulted skills
- `kiro-spec-requirements`、`kiro-spec-design`（design-principles、design-discovery-full、design-synthesis、design-review-gate）、`kiro-validate-design`、`kiro-spec-tasks` の規則を読み、その手順に従った。対話の確認はできないため、推奨の選択肢を選び、ここと最終の報告に記録した。

## Risks & Mitigations
- 準拠テストのすべての例で根が元の表記と判定されない例が残る — 例ごとの検査で判定の失敗を報告し、記録の規約か上流の検査の問題として直す（`syntax-tree` か `block-parser` に戻す）。
- 拡張の預けたトークンの種類の誤りが解析の最後に見つかる — エンジンは範囲と被覆を出した時点で確かめ、種類の誤りは `syntax-tree` の検査に任せる。エンジンの API のテストで両方を確かめる。
- 葉ごとの準備の費用 — ベンチマークで多数の小さな葉（表のセル）を測る。
- spec.txt の取り込み — 版と SHA-256 を 1 か所に固定し、構成で照合する。

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — 規則と例の基準
- [cmark](https://github.com/commonmark/cmark) — `inlines.c` の手順（参照のみ）
- [cmark-gfm](https://github.com/github/cmark-gfm) — 取り消し線、拡張オートリンク、脚注の参照の手順（参照のみ）
- [GitHub Flavored Markdown Spec](https://github.github.com/gfm/) — 検証用拡張の入力の参考

## 設計レビュー（kiro-validate-design、2026-11-27）
- **Review Summary**：境界、依存の向き、記録の規約が上流（`syntax-tree` の実装、`block-parser` と `html-renderer` の承認済みの design）と一致し、88 の受け入れ基準すべてが部品と契約に対応している。次の 2 つを、レビューの中で design に直した。
- **Issue 1（直した）**：テキストの単位のまとめ方が書かれていなかった。区切り文字の文字でテキストになった並びが別の単位になると、テキストの走査（拡張オートリンク）に渡る範囲が細かく分かれ、`www.a_b.com` のような語を見つけられない（4.1、4.3）。→ InlineItems に、直前の通常のテキストに足し、隣り合う TEXT の単位を 1 つにする規則を足した。
- **Issue 2（直した）**：ブラケットの置き換えで、捨てる範囲の中のブラケットの開きをスタックから除くことと、どちらの形でも開きがスタックから外れることが書かれていなかった（6.3、6.4）。→ InlineBrackets に足した。
- **Design Strengths**：構造を組んでから一度だけ記録する方式で、`syntax-tree` の葉の覚えの規則と「分けられるのは text だけ」の規則を満たし、トークンの移動が要らない。記録の規約の表が、`markdown-renderer` の読み方の契約として 1 か所にまとまっている。
- **Final Assessment**：GO。拡張オートリンクの表し方（独自のノード型）は利用者の確認を待つ決定として残す。

## タスクの計画のレビュー（kiro-spec-tasks、2026-11-27）
- 計画のレビューの門：88 の受け入れ基準すべてがタスクに対応し、design の部品、変更するファイル、検証の項目がすべてタスクにある。
- タスクの依存のレビュー（mode：inline。このセッションには新しいレビューのサブエージェントを起こす道具がないため、同じ文脈で行った）：
  - 1 回目：NEEDS_FIXES。(1) 1.2–1.4 のテストが、3.1 で作る `tests/inline` の CMake を前提にしていた（隠れた前提）。(2) 1.2–1.4 と 6.1–6.2 が同じファイル（`tests/inline/CMakeLists.txt`、`gfm_inline_ext.{h,c}`）を変えるのに並行の印があった。(3) 準拠テストの構成と全例の合格が 1 つのタスクで大きすぎた。
  - 直したこと：`tests/inline` の枠を 1.1 に移した。1.2–1.4 と 6.1–6.2 の並行の印を外し、理由を書いた。5.3 を構成（5.3）と全例の合格（5.4）に分けた。
  - 2 回目：PASS。
