# Research & Design Decisions: markdown-renderer

## Summary
- **Feature**：`markdown-renderer`
- **Discovery Scope**：Complex Integration（新しいエンジンと公開 API。上流の `syntax-tree` は実装済み、`html-renderer`、`block-parser`、`inline-parser` は改訂した承認済みの design だけがある）
- **Key Findings**：
  - `syntax-tree` の判定は、行ごとの分割と中身の被覆を前提に、部分木を「祖先の帯 ＋ 元の行の c0 から先」として出せるかを返す。祖先の帯（c0 より左）は判定しないので、書き戻しは「部分木が元の表記で出せる」ことと「祖先が元の帯を出せる」ことを分けて確かめる必要がある。
  - 中身の置き換えの後の段落（msgstr）は、元の行と帯を保ち、インラインはすべての綴りを保つが、中身が自由なので `amark_node_verbatim` も `amark_block_content_verbatim` も偽になる。翻訳者のバイトを出すには、「綴りの揃った自由な中身」を書き戻しの側で認める必要がある（公開の問い合わせとトークンの読み取りで確かめられる）。
  - 区切り文字の開閉の判定と照合（`amark_inline_delimiter_flanks`、`amark_inline_match_delimiters`）、文字の宣言（`amark_inline_special_chars`）、HTML ブロックの開始（`amark_scan_html_block_start`）は、上流が「書き戻しが使う」として公開した。これで、区切り文字の選び方と行頭の保護を、規則を重複せずに書ける。

## Research Log

### 前の版の design を持ち込まない
- **Context**：利用者の指示（旧設計を半分だけ引きずらない）。前の版は、構文トークンのヒープによる合流（MdSource）、行ごとの接頭辞の二分探索（MdLines）、編集の状態の印の表（`EDITED_LEAF`、`RELOCATED` など）、トークンの役割による帯の特定を使っていた。
- **Findings**：新しいモデルでは、帯は行ごとの記録、中身は葉ごとの文字列、編集の状態は綴りの有無で表される。合流も接頭辞の探索も状態の印も要らない。
- **Implications**：コンポーネントの名前（MdSource、MdLines）は roadmap の「作り直す」に合わせて残すが、中身はすべて新しい。MdSource は元の行の読み取り、MdLines は行の組み立てと帯の決定、状態の印の表は MdJudge の判断の表（UNIT、KEEP、REBUILD、NEW と中身の 4 種類）になる。

### 上流の契約の読み取り
- **Sources Consulted**：`include/amark/{node,document,value,spelling,extension}.h`、`.kiro/specs/syntax-tree/design.md`（Layout、Content、Verbatim、Facets）、`.kiro/specs/syntax-tree/tasks.md` の Implementation Notes、`tests/tree/msgstr_flow_test.c`、`INPUT.md` の 2.5、3.2、4.2–4.9、6.7、`.kiro/specs/{html-renderer,block-parser,inline-parser}/design.md`
- **Findings**：
  - 判定：`amark_node_spelling_state`（`has_spelling`、`complete`、`dropped_facets`、`structure_dropped`）、`amark_block_find_margin`、`amark_block_content_verbatim`（配置済みだけ）、`amark_node_verbatim`（祖先の帯は判定しない、キャッシュあり）、`amark_node_is_new`。
  - 帯：PRESENT / LAZY と FIRST。LAZY の列は外側の帯の終わり。空行での項目の帯は長さ 0 の PRESENT。空行の持ち主は、その行を子に含まない一番内側のブロックの子を持てるブロック。
  - 綴りを捨てた後の値は、設定では拒まれる値（`&#10;` の text、空の code）でもよい。書き手は正しく出力しなければならない（Implementation Notes 15.6、design の 6.13）。
  - すべての綴りを捨てる操作は list の tight を上書きの値として写す（7.2）。記録した tight も上書きとして働く（12.3）。
  - 使われずに残った区切り文字の text は DELIMITER のトークンと記録した literal を持ち、元の表記と判定される（10）。
  - オートリンクに title を設定すると inline の形になるが、宛先のトークンは残る（11）。オートリンクの中では `\` は文字どおりなので、inline の形の宛先にバイトを写すと値が変わる。
  - `block-parser` の記録の規約：段落の字下げは葉の SPACE、項目の FIRST の帯は記号と空白（後が空白だけなら空白なし）、HTML ブロックの中身は先頭の空白を含む、コードと HTML の中身は全体がトークンで覆われる、tight は記録しない。
  - `inline-parser` の記録の規約：区切り文字は 1 文字ずつの DELIMITER、参照の形はラベルと形だけ、オートリンクの宛先は記録した値、ハードブレークの形、葉の先頭と末尾の空白は葉の SPACE。
  - `html-renderer` の規約：2 種類のスロット、名前による型、構文セットの順の連鎖、`render_next`、拡張ごとの状態、`begin` 順・`end` と `finish` 逆順、2 つの入口、`render.h` の `amark_write_fn_t`、`gen_html_cases.cmake` の形式と `CHECK=`・`INCLUDE=`。

### CommonMark の再パースで意味が変わる境目
- **Sources Consulted**：CommonMark Spec 0.31.2（2.4、2.5、4.1–4.10、5.1–5.3、6.1–6.9）、cmark 0.31.1、mdast-util-to-markdown（remark）の unsafe のパターン
- **Findings**：
  - `\` でエスケープできるのは ASCII の句読点だけ。ほかの文字は数値の文字参照で表せる。文字参照は flanking の判定では句読点（`&` と `;`）として働く。
  - 段落の行の端の空白は値から除かれ、行末の 2 つ以上の空白はハードブレークになる。段落と見出しの末尾の `\` と空白のハードブレークは成り立たない。
  - 段落の最初の行で読まれうるブロックの構文と、続く行で段落を中断する構文（ATX、引用、フェンス、区切り線、HTML の種類 1–6、空でない箇条書き、`1.` の開き、setext の下線）は違う。遅延継続の行は setext の下線にならない。
  - 空行があっても、同じ種類と記号のリストはつながる。リストの後の字下げした行は項目に取り込まれる。段落の後に空行なしで置いた段落、引用やリストの後の段落は遅延継続になる。
  - 種類 6・7 の HTML ブロックは空行まで続き、種類 7 は段落を中断しない。リンク参照定義は段落を中断せず、定義の後の行は title として読まれうる。
- **Implications**：エスケープは構文セットが宣言する文字の和で保守的に行い、行の端の空白は文字参照にする（エンジン）。行頭の保護は、最初の行と続く行で別の表にし、CommonMark の規則は `amark.core.markdown` のフィルタに置く（拡張は自分のフィルタで参加する）。ブロックの区切りは、新しい隣接には空行を置き、タイトな文脈では型ごとの `follow` で判断する。

### 他の実装の書き戻しの方式
- **Sources Consulted**：mdast-util-to-markdown、prettier の Markdown、recast、cmark の `commonmark.c`
- **Findings**：cmark と remark は木の全体を正規化する。recast は変更のないノードのソースの範囲をそのまま出し、行の字下げを付け直す。どれも C のライブラリとして、行 × 帯のモデルと拡張のスロットに合わせられない。
- **Implications**：作る。考え方（変更のない部分は元の範囲、unsafe と文字参照、端の空白の文字参照、行の単位の帯の付け直し）を採る。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 行の単位、ブロックごとの判断（採用） | ブロックを判断の表で分け、UNIT は元の行、それ以外は出力の処理が行を書く。帯はエンジンが行ごとに決める | INPUT.md の 4.9 の手順そのまま。帯の持ち主とコンテナの処理が分かれる | 帯の決定に深さの分の時間がかかる | 帯は出力に必ず要るので本質的 |
| 部分木ごとに文字列を作って字下げを付け直す | 子の出力の文字列に親が接頭辞を付ける | 単純 | 元の帯（遅延継続、タブの分け合い）を行ごとに残せない。深さに比例する写し | 不採用 |
| 全体を正規化して出力し、差分を元に戻す | 正規化の出力と元の行を比べる | 正規化の出力は簡単 | 局所性を保証できない | 不採用 |

## Design Decisions

### Decision: 判断の表と UNIT の条件
- **Context**：2.1、2.4、5.1。`amark_node_verbatim` は祖先の帯を判定しない。
- **Alternatives Considered**：
  1. `amark_node_verbatim` が真の部分木を常に元の行で出す
  2. 祖先の枠がその行で元の帯を出せるときだけ UNIT にし、そうでなければ降りる
- **Selected Approach**：2。UNIT の行は「祖先の帯 ＋ 元の行の c0 から行の終わり」で出し、c0 は一番内側の祖先の帯の終わりの列とする。項目の FIRST の帯は最初の出力の行でだけ使う。タイトな list の下で子の間の空行を持つ item は UNIT にしない。
- **Rationale**：移したブロックは自分の綴りを保つが、移した先では遅延継続や行頭の解釈が変わりうる（INPUT.md 4.9 の 6）。降りれば、葉の中身の行がフィルタを通る。
- **Trade-offs**：UNIT の確かめは行の数と深さの積に比例する。
- **Follow-up**：scale_test で行の数と深さの積の比例を確かめる。

### Decision: 作り直しの単位は葉の中身。綴りの揃った自由な中身はそのまま出す
- **Context**：5.2、5.4、6.1。`syntax-tree` の Q8 は「値を変えたノード自身だけの綴りを捨てる」とした。
- **Alternatives Considered**：
  1. 綴りの残るインラインのノードごとに元のバイトを出し、合成した部分とつなぐ
  2. 中身を元の表記で出せないときは葉の中身の全体を作り直し、各インラインの出力の処理が綴りを使うかを決める。自由な中身で綴りが揃っていれば（msgstr）、中身のバイトをそのまま出す
- **Selected Approach**：2。作り直しの中では、text のトークンもエスケープし直し、強調は区切り文字の並びとして選び直す。ESCAPE、CHAR_REF、REPLACED のバイトと、コードスパン・リンクの綴りは使えるときに使う。
- **Rationale**：インラインの意味は葉の中の前後の文字と区切り文字の照合で決まるので、葉の中の 1 か所の変更が遠くの区切り文字の対を変えうる。葉の単位なら、エスケープと照合の確かめが葉の中で閉じる。msgstr の断片は同じ型の葉の中身としてパースされたので、そのバイトは同じ木を作る（行頭と定義だけはフィルタと作り直しの指定で守る）。
- **Trade-offs**：編集した葉の中の編集していないテキストにも `\` が増えうる。翻訳の用途では葉の全体を置き換えるので影響は小さい。

### Decision: 保守的なエスケープと位置の保護、行頭の保護はフィルタ
- **Context**：3.2、3.10、6.5、7.6、9.7。
- **Selected Approach**：エンジンは、宣言された文字（`amark.core.markdown` が `amark_inline_special_chars` で宣言し、拡張が足す）の前に `\` を置き、LF と CR を文字参照にし、行の端の空白を文字参照にする。行頭のブロックの構文の保護は、`AMARK_SLOT_MARKDOWN_HOOKS` の `filter` として、中身の行（作り直した行、綴りの揃った自由な中身の行、移した行）に当てる。CommonMark の表は `amark.core.markdown`、GFM の表の区切りの行は GFM の部品が持つ。
- **Rationale**：エスケープの方式は 1 か所（エンジン）にあり、後でオプションの選択肢を足しても出力の処理は変わらない。行頭の規則は構文ごとに違うので、拡張が参加できる形にする。
- **Trade-offs**：HTML のフィルタ（出力のバイトの分類）とは意味が違う。スロットの形（`begin`、`end`、`finish`、`filter`、状態）は揃える。

### Decision: エンジンから区切り文字の照合を呼ぶ
- **Context**：3.4。`html-renderer` は `src/render/` から `src/parse/` を参照しない規則を置いた。
- **Alternatives Considered**：
  1. エンジンが flanking と照合の規則を自分で持つ（重複する）
  2. 区切り文字の決定を `amark.core.markdown` に置き、エンジンは仮の位置だけを持つ
  3. エンジンが `inline.h` の公開の純粋な関数と区切り文字のスロットを読む
- **Selected Approach**：3。区切り文字の文字ごとの規則は構文セットの区切り文字のスロットから取り、照合は `amark_inline_match_delimiters` で行う。`src/parse/` の内部ヘッダは使わない。
- **Rationale**：steering は重複した実装を禁じる。2 では、拡張の区切り文字（取り消し線）が同じ確かめを使えない。上流（`inline-parser`）はこの関数を「書き戻しが使う純粋な関数」として公開し、`block-parser` の `scan.h` も「書き戻しが共有する走査の規則」としている。structure.md は parse と render を同じ層に置き、互いの公開 API の使用を禁じていない。
- **Trade-offs**：`src/render/` が parse 層の公開 API に依存する。依存は純粋な関数とスロットの構造体の読み取りに限り、Allowed Dependencies に明記する（利用者の確認を待つ決定）。

### Decision: ブロックの区切りとリストの空行
- **Context**：3.6、3.9、4.1。
- **Selected Approach**：
  - 元の隣接（どちらも親の行の範囲の中にあり、間が親の空行だけ）は元の空行のまま。
  - 新しい隣接は、間にある親の空行を出し、なければ空行を 1 つ合成する（保守的）。
  - list の出力の処理が tight を読んで `set_spacing` を置く。TIGHT なら list と直下の item の子の間の空行を出さず、空行が要る隣接（`follow` が偽）は ADJACENT。LOOSE で出す空行がなければ最初の区切りに 1 つ足し、区切りがなければ ATTRIBUTE。
  - 同じ種類のリストの隣接は、空行でも分けられないので記号を切り替える。リストの後のインデントのコードは fenced にし、移したブロックの字下げのトークンは書かない。
- **Rationale**：tight の値（上書き、すべての綴りを捨てたときの写しを含む）を、空行の有無で保つ規則が `syntax-tree` の導出（list と直下の item の空行）と同じ範囲になる。

### Decision: 定義の追加と編集では、残りのブラケットを持つ葉を作り直す
- **Context**：3.8、6.5。参照の形のリンクは索引で解決されるので、定義の変更に追従する。逆に、リンクにならなかった `[foo]` のテキストは、新しい定義で再パースのときリンクになりうる。
- **Selected Approach**：`amark.core.markdown` の `begin` のフックで、文書の link_definition のどれかが元の表記で出せなければ、ブラケットの残りのテキストを持つ葉を `force_rebuild` で指定する。作り直しでは残りのブラケットがエスケープされる。
- **Rationale**：ラベルの照合をせずに安全側に倒せる。定義の編集はまれで、影響は残りのブラケットを持つ葉に限る。
- **Trade-offs**：新しい定義のラベルと関係のない葉も作り直す。後で正規化したラベルで絞れる。

### Decision: 定義で解決できない参照の形のリンクは表現できない
- **Context**：3.7、4.1。定義を消すと、参照の形のリンクの宛先は UNRESOLVED になる。そのまま書くと再パースでテキストになる。
- **Alternatives Considered**：
  1. UNRESOLVED のリンクを空の宛先の inline の形（`[a]()`）で書く（HTML の出力は同じ）
  2. 表現できない（UNRESOLVED）と報告する
- **Selected Approach**：2。
- **Rationale**：1 は値（UNRESOLVED と空の文字列）が変わり、意味の比較で等しくならない。利用者は、定義を消す前にリンクを inline の形にするか、テキストに置き換えられる。
- **Follow-up**：利用者の確認を待つ決定。1 を選ぶならオプションの印で足せる。

### Decision: スロットの形
- **Context**：9.1、9.8。
- **Selected Approach**：ノード型ごとのスロットは `{base, node_type, write, margin, follow, user_data}`、前後のフックのスロットは HTML と同じ `{base, state_size, begin, end, finish, filter, user_data}`。`write` は連鎖し `write_next` で前を呼ぶ。`margin` と `follow` は持つものの最後を使い、連鎖しない。入れ子の出力（HTML の `render_subtree`）は要件がないので持たず、子の葉の中身は `prepare_content` で書ける。
- **Rationale**：HTML の規約（2 種類、名前、連鎖、状態、入口）を揃え、Markdown に固有のもの（帯、隣接）を任意の関数として足す。`html-renderer` の design も「固有の文脈は各レンダラが足す」としている。
- **Trade-offs**：ノード型のスロットのフィールドは HTML と同じではない（`render_plain` の代わりに `margin` と `follow`）。

### Decision: 表現できない木の報告はオプションの報告先
- **Context**：4.1–4.4。
- **Selected Approach**：`foundation` の `AMARK_ERROR_UNREPRESENTABLE` を返し、`options->report` があれば最初の報告のノードと分類を書く。分類は開いた集合（`0x100` から拡張）。
- **Rationale**：入口の形を HTML と揃えたまま、原因を利用者に渡せる。

### Decision: GFM 相当の部品の置き場所と、拡張オートリンクの保護
- **Context**：11.1–11.4。
- **Selected Approach**：`tests/markdown/support/gfm_md_ext.{h,c}`（INTERFACE、Naos なし）。型は登録せず、HTML の部品とインラインの部品の記述子を参照する。`amark_test_gfm_set_create` の末尾で足す（`inline-parser` の拡張点）。拡張オートリンクとして検出されうるテキストは、最初の文字を文字参照にして検出を避ける（text のスロットの連鎖）。
- **Rationale**：text が再パースで `test.ext_autolink` になると意味が変わる。連鎖の `write_next` の使い方の検証にもなる。

## Design Synthesis
- **Generalization**：帯の決定は、引用、項目、脚注の定義、拡張のコンテナの 1 つの規則（元の帯、遅延継続、FIRST、合成の関数）である。強調、強い強調、取り消し線は、区切り文字の並びの 1 つの仕組み（候補の文字、照合の確かめ）である。行頭の保護は、中身の行のフィルタの 1 つの仕組みである。
- **Build vs. Adopt**：既存の書き戻しのライブラリは採らない（C でない、モデルが違う）。照合、文字の宣言、HTML とリンクの部品の判定は上流の関数を使う。
- **Simplification**：入れ子の出力、出力のバイトの分類つきのフィルタ、必要最小限のエスケープは持たない。ブロックの区切りは保守的に空行を置き、`follow` はタイトな文脈でだけ使う。定義の確かめはラベルで絞らない。

## Consulted skills
- `kiro-spec-requirements`（ears-format、requirements-review-gate）、`kiro-spec-design`（design-principles、design-discovery-light と full の考え方、design-synthesis、design-review-gate）、`kiro-validate-design`（design-review）、`kiro-spec-tasks`（tasks-generation、tasks-parallel-analysis）の規則を読み、その手順に従った。対話の確認はできないため、推奨の選択肢を選び、ここと最終の報告に記録した。

## 要件のレビュー（Requirements Review Gate、2026-11-27）
- 機械的な確かめ：13 の要件、79 の受け入れ基準。見出しはすべて数字の ID、すべての基準が EARS の形。技術の名前は Markdown と CommonMark の概念だけ。
- 判断：往復、意味の保存、表現できない木、局所性、msgstr、新しいノード、帯、スロット、組み込み、GFM、プロパティテスト、規約を覆う。1 回目で合格。

## 設計のレビュー（Design Review Gate、2026-11-27）
- 1 回目：機械的な確かめ（79 の ID がすべて design にある、境界の節、File Structure Plan、部品とファイルの対応）は合格。判断で次を直した。
  - MdJudge が MdLines の枠に問い合わせる形が、依存の向き（judge → lines）に反していた。→ 「元の帯を出せる」の判定を MdJudge に置き、MdLines がそれを使う形にした。
  - 遅延継続の帯の条件が UNIT の行に当てはまるかが書かれていなかった。→ UNIT の行では常に成り立つと書いた。
  - 組み込みの `follow` の規則（引用の後の段落、リストの後、定義の後、HTML の種類 6・7、閉じのないフェンス）が不正確だった。→ 表を直し、共通の規則を足した。
  - 項目の続く行の幅（記号だけの FIRST の帯）、タイトな list の下の UNIT の item、LOOSE の空行の数え方、定義の確かめの条件（UNIT は文脈によるので `amark_node_verbatim` にする）、`commonmark.h` の役割の番号の依存を直した。
- 2 回目：合格。

## 設計の検証（`/kiro-validate-design`、2026-11-27、inline）
- **Review Summary**：境界、依存の向き、判断の表、行の組み立てが、上流（`syntax-tree` の実装と Implementation Notes、改訂した `html-renderer`、`block-parser`、`inline-parser` の design）と一致し、79 の受け入れ基準すべてが部品と契約に対応している。msgstr の流れ（`tests/tree/msgstr_flow_test.c` の木）を設計の規則でたどり、`> - いち *に さん* よん\n> - five\n` になることを確かめた。次の 2 つをレビューの中で design に直した。
- **Issue 1（直した）**：出力の根がインラインのノードのときの出力が書かれていなかった（1.1 の「木の途中のノード」）。→ MdEngine に「インラインの根」の規則（元のバイトか作り直し、帯なし、既定の改行）を足し、render_entry のテストに足した。
- **Issue 2（直した）**：作り直した中身に空の行（続く 2 つのソフトブレーク、中身の端の改行、フィルタが渡さなかった行）ができると、再パースで段落が切れる。表には最初と最後の改行しかなかった（3.1、4.1）。→ MdContent に「空の行」の規則（NESTING）を足し、softbreak と linebreak の行とフィルタの説明をそれに合わせた。
- **Design Strengths**：元の帯の決定を「部分木が元の表記で出せる」と「祖先が元の帯を出せる」に分けたので、移動、帯を捨てたコンテナ、項目の記号の行、遅延継続が 1 つの規則で扱える。区切り文字と行頭の保護が、上流の純粋な関数と拡張のフィルタに乗り、規則を重複しない。
- **Final Assessment**：GO。エンジンから parse 層の公開の純粋な関数を呼ぶこと、解決できない参照の形のリンクを表現できないとすることは、利用者の確認を待つ決定として残す。

## Risks & Mitigations
- 行頭の保護とブロックの区切りの規則の漏れで、意味の保存が破れる — spec の全例のプロパティテスト、コーパス、ファジングで見つけ、表に足す。
- 区切り文字の決定が収束しない並び — 候補と文字参照の直し直しの回数を並びごとに有界にし、合わなければ報告する。
- 判断と帯の決定の時間が深さに比例して増える — scale_test で測る。必要なら枠ごとの帯の検索を行の順の走査に変える。
- `src/render/` から parse 層の公開 API への依存 — 純粋な関数とスロットの構造体に限り、レビューで確かめる。利用者が拒めば、照合を `syntax-tree` か共通の層へ移す改訂を上流に求める。

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — 再パースの規則の基準
- [cmark](https://github.com/commonmark/cmark) — 参照実装（参照のみ）
- [mdast-util-to-markdown](https://github.com/syntax-tree/mdast-util-to-markdown) — unsafe のパターンと文字参照による端の表し方（考え方のみ）
- [recast](https://github.com/benjamn/recast) — 変更のない部分のソースの範囲を出す書き戻し（考え方のみ）
- [GitHub Flavored Markdown Spec](https://github.github.com/gfm/) — 検証用の部品の入力の参考

## タスクの計画のレビュー（`/kiro-spec-tasks`、2026-11-27）
- **Task Plan Review Gate**：79 の受け入れ基準すべてがタスクに対応し、design の部品（MdEscape、MdOutput、MdDispatch、MdSource、MdJudge、MdLines、MdContent、MdEngine、CoreMarkdown、MdTestSupport、MdEdits、GfmMdExt、MdTests、MdFuzz）、変更するファイル、テストのファイルがすべてタスクにある。1 回目で合格。
- **Task-graph sanity review**：mode は inline（このセッションには新しいレビュー用のサブエージェントを起こす道具がない）。
  - 1 回目：NEEDS_FIXES。(1) 2.6 の「処理のない型の葉の既定」が、後の 2.8 の中身の行の出力を前提にしていた。(2) 前提の注記が「2.4–2.8 は `syntax-tree` だけで確かめられる」としていたが、2.3 以降は `inline.h` が要る。(3) 3.7 のケースの語 `gfm` が、5.2 の GFM 相当の構文セットを前提にしていた。(4) 3 のテストの構文セット（構築関数に `amark.core.markdown` が入る 4.1 より前）が書かれていなかった。(5) 4.1 が、構築関数の変更とスロット・文脈・オプションの契約のテストを 1 つにしていて大きすぎた。
  - 直したこと：葉の既定を 2.8 に移した。前提の注記を書き直した。`gfm` の語を 5.2 に移した。3.1 にテストの補助の構文セットを足した。4.1 を構築関数の統合（4.1）と契約のテスト（4.2）に分け、番号と `_Depends:_` を直した。
  - 2 回目：PASS。
- 並列の印は 6.3（ファジング。`fuzz/` だけを変える）だけに付けた。2 と 3 のタスクは `md_internal.h`、ソースの一覧、テストの登録を共有するので並列にしない。
