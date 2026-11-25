# Research & Design Decisions

## Summary
- **Feature**：`html-renderer`
- **Discovery Scope**：New Feature（greenfield。`syntax-tree` のレジストリとノードモデルの上に、HTML の出力のエンジン、スロットのインタフェース、組み込みのノード型の HTML 出力を作る。`block-parser`・`inline-parser`・`markdown-renderer` との継ぎ目を定義する）
- **Key Findings**：
  - cmark（0.31.1、CommonMark 0.31.2 対応）の HTML 出力は、ノードごとの入る/出るイベントの処理と、2 つの文脈（画像の代替テキストの「テキストだけのモード」と、段落の祖父母がタイトなリストかどうか）と、`cr()`（直前が改行でなければ改行）だけで書かれている。これらをすべて公開の出力の文脈として提供すれば、組み込みの出力も拡張と同じスロットで書ける。
  - HTML の出力はノード型に対応し、構文（construct）には対応しない（見出しは ATX と setext、コードブロックはインデントとフェンス、リンクはインライン・参照・オートリンクから作られる）。組み込みの HTML 出力は「構文ごとのファイル」ではなく、`amark.core` のノード型に対応する 1 つの出力の構文拡張としてまとめるのが自然である。
  - cmark-gfm の拡張は、表・取り消し線・タスクリストではノード型ごとの出力の関数、タグフィルタでは HTML の出力の後段の「フィルタ」、脚注では後処理による定義の移動と、専用のノード型の出力を使う。Amark では、ノード型ごとのスロット、前の処理の呼び出し（差し替えと包み込み）、前後のフック、拡張ごとの状態、入れ子の出力、分類つきの出力のフィルタで、すべてを公開 API だけで表せる。

## Research Log

### cmark の HTML 出力の規則（`src/html.c`、0.31.1）
- **Context**：要件 2.1（参照実装と同じ HTML）、2.2–2.9、3.1–3.3、4.1–4.5 の具体的な出力を確定するため。
- **Sources Consulted**：cmark 0.31.1 の `src/html.c`、`src/houdini_html_e.c`、`src/houdini_href_e.c`、`src/scanners.re`（`_scan_dangerous_url`）
- **Findings**：
  - `cr()`：出力が空でなく、最後のバイトが LF でなければ LF を出す。ブロックの開始の前に呼ぶ。
  - document：何も出さない。block_quote：入るとき `cr` `<blockquote` sourcepos `>\n`、出るとき `cr` `</blockquote>\n`。list：入るとき `cr` と `<ul` / `<ol` / `<ol start="N"`（start ≠ 1）、sourcepos、`>\n`。出るとき `</ul>\n` / `</ol>\n`（`cr` なし）。item：入るとき `cr` `<li` sourcepos `>`、出るとき `</li>\n`。heading：入るとき `cr` `<hN` sourcepos `>`、出るとき `</hN>\n`。
  - code_block：`cr`、情報文字列が空なら `<pre` sourcepos `><code>`、空でなければ最初の空白（`cmark_isspace`：空白、タブ、LF、VT、FF、CR）までを語とし、`<pre` sourcepos `><code class="` ＋（情報文字列が `language-` で始まらなければ `language-`）＋ エスケープした語 ＋ `">`。続けてエスケープしたリテラル、`</code></pre>\n`。
  - html_block：`cr`、安全モードなら `<!-- raw HTML omitted -->`、そうでなければリテラルをそのまま、`cr`。thematic_break：`cr` `<hr` sourcepos ` />\n`。
  - paragraph：祖父母が list で tight なら何も出さない。そうでなければ入るとき `cr` `<p` sourcepos `>`、出るとき `</p>\n`。
  - text：エスケープしたリテラル。linebreak：`<br />\n`。softbreak：HARDBREAKS なら `<br />\n`、NOBREAKS なら空白、それ以外は LF（HARDBREAKS を先に判定）。code：`<code>` エスケープ `</code>`。html_inline：安全モードなら `<!-- raw HTML omitted -->`、そうでなければリテラル。emph `<em>`、strong `<strong>`。
  - link：`<a href="` ＋（UNSAFE か危険でなければ）href のエスケープ ＋（タイトルがあれば `" title="` ＋ エスケープ）＋ `">`、出るとき `</a>`。image：`<img src="` ＋ 同じ URL ＋ `" alt="`、テキストだけのモードを開始。出るとき（タイトルがあれば `" title="` ＋ エスケープ）＋ `" />`。
  - テキストだけのモード：text、code、html_inline はエスケープしたリテラル（安全モードでも、エスケープするので安全）、linebreak と softbreak は空白、それ以外は何も出さない。子は通常どおりたどる。開始したノードに戻ったときに終わる。
  - sourcepos はブロックの要素の開始タグにだけ ` data-sourcepos="L:C-L:C"` を出す（終わりの列は最後のバイトを含む）。インラインには出さない。
  - HTML のエスケープ（secure=0）：`"` → `&quot;`、`&` → `&amp;`、`<` → `&lt;`、`>` → `&gt;`。`'` と `/` はそのまま。
  - href のエスケープ：安全な文字（英数字と `-_.+!*(),%#@?=;:/$~`）はそのまま。`&` → `&amp;`、`'` → `&#x27;`、それ以外（`"`、`<`、`>`、`[`、`\`、`]`、`^`、`` ` ``、`{`、`|`、`}`、空白、制御文字、0x80 以上のバイト）は `%XX`（大文字の 16 進）。`%` は検査せずそのまま通す。
  - 危険な URL：大文字と小文字を区別せず、`data:image/png`、`data:image/gif`、`data:image/jpeg`、`data:image/webp` で始まれば安全、`javascript:`、`vbscript:`、`file:`、`data:` で始まれば危険。
- **Implications**：組み込みの 19 の型の出力の表を設計に固定する。`language-` の接頭辞の重複の回避と、`cr` の判定が「この出力の呼び出しで書いた最後のバイト」に基づくことを明記する。リンク参照定義は cmark にないノードなので、何も出さない処理を登録する。

### cmark-gfm の拡張の HTML 出力
- **Context**：要件 10（GFM 相当の出力を公開のスロットだけで書けること）のフックの形を決めるため。
- **Sources Consulted**：cmark-gfm の `extensions/table.c`、`strikethrough.c`、`tasklist.c`、`tagfilter.c`、`src/html.c`（`filter_html_block`、`footnote` の出力）、GFM spec 0.29
- **Findings**：
  - 表：table / table_row / table_cell の出力の関数。見出しの行を `<thead>`、最初の本体の行の前で `<tbody>`、`<th>` / `<td>` に `align="left|center|right"`。行や表の終わりの判定に、ノードの兄弟と型のデータを見る。
  - 取り消し線：`<del>` / `</del>`。
  - タスクリスト：項目の出力を置き換え、`<li` sourcepos `>` の直後に `<input type="checkbox" disabled="" /> `（チェック済みは `checked="" ` を加える）。段落のタイトさの判定は通常の項目と同じ。
  - タグフィルタ：生の HTML の出力（html_block と html_inline のリテラル）に限り、`<title>`、`<textarea>`、`<style>`、`<xmp>`、`<iframe>`、`<noembed>`、`<noframes>`、`<script>`、`<plaintext>` の開始（`<` と任意の `/` の後のタグ名を大文字小文字を区別せずに照合し、直後が空白・`>`・`/>`）の `<` を `&lt;` にする。
  - 脚注：参照は出現順に番号を付け（同じラベルの 2 回目以降は同じ番号）、`<sup class="footnote-ref"><a href="#fn-L" id="fnref-L" data-footnote-ref>N</a></sup>`。定義は文書の末尾に `<section class="footnotes" data-footnotes>\n<ol>\n` ＋ 各 `<li id="fn-L">` ＋ 内容 ＋ 最後の段落の終わりの前に戻りのリンク `<a href="#fnref-L" class="footnote-backref" data-footnote-backref data-footnote-backref-idx="N" aria-label="Back to reference N">↩</a>` ＋ `</li>\n` ＋ `</ol>\n</section>\n`。cmark-gfm は後処理で定義を末尾に移す。
- **Implications**：
  - 必要なフックは、(1) ノード型ごとの入る/出るの処理、(2) 前の処理の呼び出し（タスクリストやタグフィルタの差し替え・包み込み）、(3) 子の省略（脚注の定義をその場で出さない）、(4) 出力の後のフック（脚注を末尾に出す）、(5) 1 回の出力の間の拡張ごとの状態（参照の番号、定義の記録）、(6) 任意の部分木の入れ子の出力（末尾で定義の内容を出す）、(7) 分類つきの出力のフィルタ（タグフィルタを「生の HTML の出力だけ」に適用する）、(8) 子のタイトさの設定（独自の項目の型）。
  - 脚注は後処理で木を動かさなくても、出力の文脈だけで末尾に出せる（定義の処理は子を省いて状態に記録し、終了の後のフックで出す）。木を変えないので、`markdown-renderer` の往復に影響しない。

### 出力の前後のフック、フィルタ、レンダラの共通の形
- **Context**：要件 8 と、`markdown-renderer` とスロットの設計を揃えるため（brief の Adjacent）。
- **Sources Consulted**：cmark-gfm の `cmark_html_renderer`（`filter_extensions`、`footnote_ix`）、markdown-it の renderer rules（`rules[type] = fn` の置き換えと元の規則の保存）、pulldown-cmark の HTML 出力、`syntax-tree` の design（`amark_slot_header_t`、スロットの種類の番号の範囲、`amark_syntax_set_slot_at`）
- **Findings**：
  - markdown-it の「規則の置き換えと、元の規則の呼び出し」は、拡張の差し替えと包み込みを最小の API で表す。Amark では、構文セットの順を優先度とし、`render_next` で前の処理を呼ぶ形が対応する。
  - cmark-gfm のフィルタは HTML 全体の文字列に対して動くため、テキストのエスケープ後の `&lt;` と、生の HTML の `<` を区別できず、生の HTML の出力の箇所に専用の分岐が入っている。出力に「マークアップ / テキスト / 生の HTML」の分類を付ければ、フィルタは分類で対象を選べる。
  - `syntax-tree` のレジストリはスロットの中身を知らず、種類ごとに構文セットの順で列挙できる（`amark_syntax_set_slot_at`）。スロットの種類の番号は 1–0xFFFF が Amark の spec、0x10000 以上が利用者。
- **Implications**：スロットの種類を 2 つ（ノード型ごとの出力、出力の前後のフックとフィルタ）にする。`markdown-renderer` は同じ形（`AMARK_SLOT_MARKDOWN_NODE` / `AMARK_SLOT_MARKDOWN_HOOKS`、不透明な文脈、`render_next`、子の省略、拡張ごとの状態、入れ子の出力）を使える。出力先の書き出しの関数の型は、両方のレンダラで共有するため `render.h` に置く。

### `syntax-tree` の API で足りるか
- **Context**：上流に不足があれば、回避せずに報告するため。
- **Findings**：
  - ノード型の名前から識別子：`amark_syntax_set_find_node_type`。記述子：`amark_node_desc`。スロットの列挙：`amark_syntax_set_slot_count` / `amark_syntax_set_slot_at`（拡張の記述子も返る）。属性とリテラルの取得：`amark_node_*` の取得関数。親と兄弟：`amark_node_parent` など（const を受け取る）。ソース上の範囲：`amark_node_source_span`、`amark_document_source_data`、`amark_document_position`。確保：`amark_malloc` など（グローバルのアロケータ）。すべて揃っている。
  - `amark_iter_create` は非 const の根を取るため、const の木を出力するエンジンは使わず、親と兄弟のリンクで自分でたどる（反復、確保なし）。
  - スロットの種類の番号の割り当ては、`extension.h` に各 spec が追記する規約だけで、spec ごとの番号の範囲の表がない。`block-parser` と並行して設計しているため、衝突を避ける規約が上流にあるとよい（必須ではない。この spec は 0x0300–0x03FF を使う）。
  - `amark_syntax_set_core()`（文書の既定の構文セット）は HTML の出力のスロットを持たない（レイヤーの規則で `src/tree/` は `src/render/` を知らないため、正しい）。既定の構文セットで手で組み立てた木は、組み込みの HTML 出力の構文拡張を加えた構文セットを使わないと出力されない。
- **Implications**：上流の変更は必須ではない。報告事項として、(1) スロットの種類の番号の spec ごとの範囲の表を `syntax-tree` の `extension.h` に置くこと、(2) CommonMark の構文セットの構築関数（`block-parser`）が組み込みの HTML 出力の構文拡張を含めること、を挙げる。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 固定の switch ＋ 拡張のフック（cmark-gfm） | 組み込みは switch、拡張は関数表 | 速い、実績がある | 組み込みと拡張の表現力が違う。steering の「内部専用の近道を作らない」に反する | 不採用 |
| ノード型ごとのスロットの連鎖（採用） | 構文セットの順に同じ型の処理を並べ、最後のものを呼び、`render_next` で前を呼ぶ | 差し替え・包み込み・追加が 1 つの仕組み。組み込みも同じ | 連鎖の呼び出しの文脈の管理が要る | markdown-it の規則の置き換えに近い |
| 出力の文字列の後処理だけ | 出力後の HTML を書き換える | 単純 | ノードの文脈がない。脚注やタスクリストが書けない | 不採用（フィルタは補助として採用） |
| 組み込みの出力を構文ごとのファイルに分ける | atx_heading.c などが HTML のスロットを持つ | steering の「1 構文 1 ファイル」に字面で合う | 同じ型の出力の重複、構文の削除で出力が消える非対称、`block-parser` のファイルとの共同所有 | 不採用（Design Decisions を参照） |

## Design Decisions

### Decision: 組み込みのノード型の HTML 出力は、1 つの出力の構文拡張 `amark.core.html` にまとめる
- **Context**：要件 6.7、9.1–9.4。`src/syntax/` の構文のファイルは `block-parser`（ブロック）と `inline-parser`（インライン）が作る。HTML の出力の処理をどこに置き、誰が構文セットに登録するかを、ファイルの共同所有なしに決める必要がある。
- **Alternatives Considered**：
  1. 構文ごとのファイル（`atx_heading.c` など）が HTML のスロットを持ち、この spec は処理の関数だけを提供する。
  2. この spec が、`amark.core` のノード型に対応する出力の構文拡張 `amark.core.html` を `src/syntax/` に置き、公開の取得関数で提供する。CommonMark の構文セットの構築関数がそれを含める。
  3. エンジンが組み込みの出力を暗黙の土台として常に使う。
- **Selected Approach**：2。`amark.core.html` は 19 のノード型の HTML のスロットだけを持ち、ノード型もパースのスロットも持たない。`src/syntax/html_*.c` に置き、公開 API だけを使う。CommonMark の構文セットの構築関数（`block-parser` が所有）は、`amark.core.html` を `amark.core` の直後、すべての構文の構文拡張より前に追加する。構文の構文拡張（`commonmark.*`）は組み込みのノード型の HTML のスロットを持たない。
- **Rationale**：HTML の出力はノード型の関数で、構文の関数ではない。ノード型の定義が `amark.core` にまとまっているのと対になる。構文を無効化しても、他の構文や手で組み立てた木の同じ型のノードの出力が消えない。`block-parser` のファイルを編集せずに済み、登録の箇所は構築関数の 1 行だけになる。3 はエンジン（`src/render/`）が `src/syntax/` を参照することになり、依存の方向と「エンジンは個別の構文を知らない」に反する。
- **Trade-offs**：利用者が自分で構文セットを作る場合は、`amark.core.html` を自分で加える必要がある（要件 9.2、9.4 で明示）。`markdown-renderer` は、出力が表記（ATX か setext か）に依存するため、構文ごとのスロットを選んでもよい（この決定は HTML に限る）。
- **Follow-up**：`block-parser` の構築関数が `amark.core.html` を含めること（報告事項）。これにより `block-parser` の構築関数は `html-renderer` の公開関数にリンクする（roadmap の順では `html-renderer` が先）。

### Decision: スロットは 2 種類（ノード型ごとの出力、前後のフックとフィルタ）
- **Context**：要件 6、7、8。
- **Alternatives Considered**：1 つの種類に全部を入れる／ノード、フック、フィルタ、状態の 4 種類に分ける。
- **Selected Approach**：`AMARK_SLOT_HTML_NODE`（ノード型の名前、入る/出るの処理、テキストだけのモードの処理）と、`AMARK_SLOT_HTML_HOOKS`（状態の大きさ、開始の前、終了の後、後始末、フィルタ）。後者は 1 つの構文拡張につき 1 つまで（状態を拡張で一意に引くため）。
- **Rationale**：ノード型ごとの処理は 1 つの拡張が複数持つ（表なら 3 つ）。フック・フィルタ・状態は拡張ごとに 1 つで足り、まとめると状態の持ち主が明確になる。
- **Trade-offs**：フィルタだけを持つ拡張も、フックのスロットを 1 つ持つ形になる（不要な関数は NULL）。

### Decision: 前の処理の呼び出し（連鎖）で差し替えと包み込みを表す
- **Context**：要件 6.2、6.3、9.3、10.3、10.5。
- **Selected Approach**：同じ型の処理を構文セットの順に並べ、最後のものを呼ぶ。処理の中から `amark_html_render_next` で 1 つ前を呼ぶ。最初のものより前は「処理がない場合」（何も出さない）。テキストだけのモードの処理も、その処理を持つスロットだけで同じ連鎖を作る。
- **Rationale**：置き換え・前後に足す・条件つきで委ねる、のすべてを 1 つの規則で書ける。
- **Trade-offs**：連鎖の深さは同じ型のスロットの数で、呼び出しのスタックはその数に比例する（ノードの深さには比例しない）。

### Decision: 出力の分類つきのフィルタ
- **Context**：要件 3.5、8.6、8.7、10.5。
- **Selected Approach**：すべての書き出しは分類（MARKUP、TEXT、RAW）を持つ。フィルタは構文セットの順に連なり、`amark_html_filter_forward` で次へ渡す（渡さなければ省く）。安全モードで生の HTML を置き換えたコメントは MARKUP。
- **Rationale**：タグフィルタは RAW だけを対象にでき、テキストのエスケープの結果を誤って書き換えない。1 つの生の HTML のノードのリテラルは 1 回の書き出しで渡るため、タグが書き出しの境目で切れない。
- **Trade-offs**：フィルタの中から通常の書き出しを呼ぶと再帰になるため、禁止する（不正な状態）。

### Decision: タイトさは「親が直接の子に設定する」文脈にする
- **Context**：要件 2.2、7.3、10.3。cmark は段落の祖父母の型と属性を直接見る。
- **Selected Approach**：処理が入るときに `amark_html_set_tight_children` を呼ぶと、そのノードの直接の子から `amark_html_tight` が真になる。組み込みの item の処理は、親が list ならその tight を設定する。組み込みの paragraph の処理はそれを問い合わせる。
- **Rationale**：独自の項目の型（タスクリストの項目など）や独自のリストの型でも、段落の処理を変えずにタイトさを伝えられる。cmark の規則（段落の親が項目で、祖父母がタイトなリスト）と同じ結果になる。
- **Trade-offs**：部分木の根の親の文脈は使わない。タイトなリストの項目の中の段落だけを出力すると `<p>` が付く（文書化する）。

### Decision: 出力の入口はノードと出力先だけを取り、レンダラのオブジェクトを作らない
- **Context**：要件 1、11.4。
- **Alternatives Considered**：構文セットごとに作って使い回すレンダラのオブジェクト。
- **Selected Approach**：`amark_render_html`（バッファ）と `amark_render_html_to`（書き出しの関数）。呼び出しのたびに、文書の構文セットのスロットから型の識別子で引く表を作る（スロットの数に比例）。
- **Rationale**：凍結した構文セットは変わらないが、`syntax-tree` には構文セットに付属の情報を持たせる仕組みがない。表の構築はスロットの数に比例し、出力に比べて小さい（Simplification）。
- **Trade-offs**：小さな部分木を大量に出力する用途では、毎回の表の構築が相対的に目立つ。必要になれば、後からレンダラのオブジェクトを追加できる（入口の関数は残せる）。

### Generalizations（Synthesis）
- 「画像の代替テキスト」を、任意の処理が開始できる「テキストだけのモード」と、型ごとのテキストだけのモードの処理に一般化した。
- 「タイトなリストの判定」を、親が直接の子に設定する文脈に一般化した。
- 「脚注を末尾に出す」を、出力の前後のフック、拡張ごとの状態、入れ子の出力に一般化した。目次の生成なども同じ仕組みで書ける。
- 「タグフィルタ」を、分類つきの出力のフィルタに一般化した。

### Build vs. Adopt
- エスケープの規則と出力の形は cmark に合わせる（準拠の判定の基準そのもの）。cmark のコードは BSD-2-Clause で互換性はあるが、Amark のバッファとアロケータと出力先の抽象に合わないため、規則だけを採り、実装は書く。
- 外部のライブラリは使わない（steering：実行時は C 標準ライブラリだけ）。

### Simplification
- レンダラのオブジェクト、出力の段落ごとの属性の API（`<p class>` を足すなど）、スロットの優先度の数値は作らない（構文セットの順で足りる）。
- フィルタは書き出しの単位で動き、構文の解析をしない（タグフィルタの照合は拡張の責任）。

## Risks & Mitigations
- 連鎖・フィルタ・入れ子の出力の文脈の取り違え（処理中でないノードの `render_next` など） — 文脈が現在の呼び出しの状態を持ち、違反を不正な引数/不正な状態で返す。テストで各違反を確かめる。
- `cr` の判定とフィルタの関係（フィルタが改行を変えると、後の `cr` の判定とずれる） — `cr` はフィルタの前の出力の最後のバイトで判定すると定める。cmark 互換の出力はフィルタがない構成で検証する。
- 深い木での再帰 — エンジンは親と兄弟のリンクで反復し、深さごとの文脈は動的な配列に持つ。深さ 10 万のテストで確かめる。
- 手で組み立てた木を既定の構文セットで出力すると何も出ない — 要件 9.4 と API の文書で明示し、テストでも確かめる。
- CommonMark spec 全体との一致はこの spec では確かめられない — 組み込みの出力の表を cmark の `html.c` と 1 対 1 で照合する手で組み立てた木のテストを持ち、残りは `inline-parser` の準拠テストで確かめる（不具合はこの spec に戻して直す）。

## Revision Notes

### 2026-11-25：spec 横断のレビュー r1（cross-spec review r1）
- **Context**：全 spec を横断したレビューの指摘のうち、この spec に関わるもの（#7、#10、#1 の間接の影響）を反映した。承認済みの要件の受け入れ基準は変えず、設計とタスクの記述を補う。
- **#7 `amark.h` の include の順**：`amark.h` は `foundation` が所有し、正規の順（`foundation` のヘッダ → 木のヘッダ → `render.h`、`html.h` → `entity.h`、`scan.h`、`parser.h`、`block.h`、`inline.h`、`commonmark.h` → `markdown.h`）を定める。「`extension.h` の後に追加する」という書き方をやめ、正規の順の「`render.h`、`html.h`」の位置に置く、という参照に改めた（Modified Files、タスク 1.1）。正規の順の変更を Revalidation Triggers に加えた。
- **#10 ケースの生成スクリプトの共有**：`markdown-renderer` は、`data/*_cases.txt`（S 式と期待する出力の組）を同じ形式で持ち、別の生成スクリプト（`gen_md_cases.cmake`）を書く予定だった。重複を避けるため、`gen_html_cases.cmake` を HTML に固有の知識を持たない共有のスクリプトとし、`markdown-renderer` がそのまま再利用する。再利用のため、(1) 見出しのオプションの語をスクリプトで解釈せず文字列として検査関数に渡す、(2) 検査関数とヘッダを `CHECK=`、`INCLUDE=` で選べる、(3) 生成するテストが呼ぶ検査関数の形を固定する、を設計に契約として書き、変更は `markdown-renderer` の再検証の契機とした（Boundary の This Spec Owns、Revalidation Triggers、HtmlTests、タスク 1.2）。検討した代替：`tests/support/` へ移す案は、この spec が先に実装され、置き場所の変更が他の spec の Modified Files にも波及するため採らなかった（場所は `tests/html/` のまま、契約で共有する）。
- **#1 ファジングとテストの補助ライブラリ（間接）**：`amark_test_html_gfm` は `amark_test_parse_gfm`・`amark_test_gfm` を通してファジングターゲットに間接にリンクされる。`foundation` が定める規則（ファジングが使うテストの補助ライブラリは Naos に依存せず、`AMARK_FUZZ` か `AMARK_BUILD_TESTS` が ON のとき定義する）に合わせ、`amark_test_html_gfm` が Naos に依存しないことと、`AMARK_FUZZ` だけが ON の構成でも定義することを明記した（GfmHtmlExt、タスク 4.1）。
- **Implications**：公開 API・要件・タスクの範囲は変わらない。承認は維持する。`syntax-tree` のスロットの種類の範囲の表（#9）は、この spec の `0x0301`、`0x0302` と一致する。

### 2026-11-25：spec 横断のレビュー r2（cross-spec review r2）
- Revalidation Triggers の `amark_test_html_gfm` の依存の行で、影響先を `block-parser` のファジングターゲット（`amark_test_html_gfm` をリンクしない）から、`inline-parser`（`inline_parse`）と `markdown-renderer`（`md_roundtrip`、`md_edit`）のファジングターゲットに直し、中間のライブラリ `amark_test_parse_gfm`（`block-parser`）と `amark_test_gfm`（`inline-parser`）を明記した。承認の状態は変えない。

### 2026-11-25：内部ヘッダの命名の規則の変更（steering の更新の反映）
- **Context**：steering（structure.md）の規則を、「内部ヘッダにはすべて `_internal.h` を付ける」から「内部ヘッダは原則として接尾辞を付けず、同じ名前の公開ヘッダがあるときだけ `_internal.h` を付ける」に改めた。公開ヘッダとは置き場所（`include/amark/` と `src/`）と include の書き方で区別できるため。
- **変更**：`src/syntax/html_core_internal.h` を `src/syntax/html_core.h` に改名した（File Structure Plan、該当のコンポーネント）。同じ名前の公開ヘッダがある内部ヘッダ（`base/allocator_internal.h`、`base/buffer_internal.h`、`parse/inline_internal.h`、`render/html_internal.h`、`render/markdown_internal.h`）は名前を変えない。
- **Implications**：ファイル名だけの変更で、API・要件・タスクの範囲は変わらない。承認は維持する。

## References
- cmark 0.31.1 `src/html.c`、`src/houdini_html_e.c`、`src/houdini_href_e.c`、`src/scanners.re` — 出力とエスケープと危険な URL の規則
- CommonMark Spec 0.31.2 — 準拠の対象
- cmark-gfm `extensions/table.c`、`strikethrough.c`、`tasklist.c`、`tagfilter.c`、`src/html.c` — GFM 相当の出力の形
- GitHub Flavored Markdown Spec 0.29 — 表、取り消し線、タスクリスト、タグフィルタ、脚注の期待値
- markdown-it の renderer rules — 規則の置き換えと元の規則の呼び出しの先行例
- `.kiro/specs/syntax-tree/design.md` — ノードモデル、レジストリ、スロットの見出し、テストの補助
- `.kiro/specs/foundation/design.md` — エラーコード、アロケータ、バッファ、テストとファジングの仕組み
