# Research & Design Decisions

## Summary
- **Feature**：`markdown-renderer`
- **Discovery Scope**：Complex Integration（greenfield のコードだが、`syntax-tree` の構文トークンと編集の記録、`block-parser` と `inline-parser` の役割の番号・文字の宣言・走査、`html-renderer` のスロットの形の上に作る。上流の 5 つの spec との継ぎ目がすべて関わる）
- **Key Findings**：
  - 書き戻しの正しさの難しさは 3 か所に集まる：(1) テキストと周りの表記の境目（記号、行頭のブロックの構文、行頭と行末の空白、区切り文字の flanking）、(2) ブロックの境目（空行、遅延継続、同じ種類のリストの結合、閉じていないフェンス）、(3) 行頭の接頭辞（祖先のコンテナ、部分的なタブ、遅延継続）。どれも「ノードごとの出力」ではなく「隣り合う出力の組み合わせ」で決まるため、エンジンが行と葉の単位でまとめて判断し、ノードの出力の処理はそれを使うだけにするのがよい。
  - 構文トークンは「所有するノード」と「ソース上の位置」を別々に持つので、接頭辞は「そのコンテナがその元の行に持つトークン」として行ごとに引ける。移動したノードの行では移動先のコンテナがその行のトークンを持たないため、自然に合成に切り替わる。この性質で、接頭辞の付け直しを一般的な規則 1 つで書ける。
  - 強調の正しさは、区切り文字の並びの flanking と CommonMark の照合（3 の倍数の規則など）の両方で決まる。照合を出力側で作り直すと規則の重複になるため、`inline-parser` の照合を純粋な関数として公開し、出力側は「選んだ表記を照合で検証し、だめなら構成的に安全な表記へ落とす」方式にする。

## Research Log

### 上流の構文トークンと編集の記録
- **Context**：要件 2、3、5、8。何を元の表記として出せ、何を作り直すべきかの判定の材料。
- **Sources Consulted**：`.kiro/specs/syntax-tree/design.md`（TokenStore、EditTracking、TokenCheck、Equality、CopyImport、System Flows の判定図）、`.kiro/specs/block-parser/design.md`（BlockEngine、Builtin syntaxes の役割の表、Fragment）、`.kiro/specs/inline-parser/design.md`（InlineDelimiters、InlineBrackets、InlineText、LinkCommon、InlineSpecial、CommonMarkSet の役割の表）
- **Findings**：
  - トークンの種類は 9 つの汎用の分類（TEXT、MARKER、SPACE、LINE_ENDING、ESCAPE、CHAR_REF、REPLACED、BOM、LAZY_CONTINUATION）、印は CONTAINER_PREFIX と PARTIAL_TAB、役割は 16 ビットで構文ごとに決まる（ブロックは 1–0x9F、インラインは 0x0100–0x01FF、エンジンは 0xFF00–0xFF0F と 0xFF10–0xFF1F）。
  - 編集の記録は CONTENT、CHILDREN、DESCENDANTS、RELOCATED、CREATED。未知のビットは CONTENT として扱う。編集してもトークンは残る（設定はトークンを変えない）。テキストの一括設定は改行を softbreak にし、新しい子は CREATED。リテラルの改行は LF に揃えられる。
  - 空行のトークンは、その空行を含む最も内側の開いているブロックが持つ。ブロックの間の空行が、深い子孫（入れ子のリストの最後の項目）に属することがある。遅延継続は長さ 0 の LAZY_CONTINUATION を接頭辞が省かれたコンテナが持つ。部分的なタブは、同じタブを複数のノードが列を分けて持つ。
  - インラインでは、成立しなかった区切り文字とブラケットは text の `UNMATCHED`（0xFF11）の役割を持つ。softbreak は直前の行末の空白を自分のトークンとして引き取る。断片のパースで挿入したノードはトークンを持ち、RELOCATED になる。
  - 比較（`amark_node_equal` と `AMARK_EQUAL_NORMALIZE_TEXT`）は、隣り合う text の分け方の違いを吸収する。プロパティテストの等しさの定義にそのまま使える。
- **Implications**：
  - ノードの扱いを「エンジンが元の表記をそのまま出す（verbatim）」「出力の処理を呼ぶ」の 2 つに分け、処理には編集の状態の印を渡す。元の表記をそのまま出せるのは、部分木が無編集で、周りの文脈も元のままのときに限る。
  - 空行はブロックの間の「区切り」として、子の末尾の空行と親の空行のトークンをまとめて扱う。末尾の空行はトークンの種類（SPACE と LINE_ENDING だけの行）から一般的に求める。

### CommonMark の再パースで意味が変わる表記の境目
- **Context**：要件 3.3–3.6。保守的なエスケープと区切りの選び直しの規則を決めるため。
- **Sources Consulted**：CommonMark Spec 0.31.2（2.4 バックスラッシュのエスケープ、2.5 文字参照、4.1–4.10、5.1–5.3、6.1–6.9。特に 6.2 の flanking と 3 の倍数の規則、4.8 の段落の継続行の先頭の空白の除去、6.7–6.8 の改行）、cmark 0.31.1 の `inlines.c`、mdast-util-to-markdown（remark）の `unsafe` パターンと、強調の前後の文字を文字参照で表す手法
- **Findings**：
  - バックスラッシュでエスケープできるのは ASCII の句読点だけ。それ以外の文字（空白、英数字）は数値文字参照で表せる。文字参照は flanking の判定では句読点（`&` と `;`）として働く。
  - 段落の行頭と行末の空白、ATX の見出しの末尾の `#` の並び、行頭のブロックの構文（`#`、`>`、`-`、`+`、`*`、`=`、`` ` ``、`~`、`<`、数字と `.` / `)`、4 つ以上の空白）は、テキストの中身でも構文として解釈される。
  - 強調は、区切りの並びが単一の役割（開くだけ、閉じるだけ）で、直接隣り合う並びの文字が違えば、構成的に元の構造になる。ただし元の文書には 3 の倍数の規則に頼る表記（`*foo**bar**baz*`）があり、この規則だけで判断すると無編集の強調まで書き換えてしまう。
  - 短縮の参照リンク `[foo]` の直後の `(`・`[`、リンクの直前の `!`、行末の `\`、隣り合うコードスパンのバッククォートは、隣のノードとの組み合わせで意味が変わる。
- **Implications**：
  - エスケープは「構文セットが宣言する文字の和」を常にエスケープし（保守的な方式）、行頭・行末・区切り文字の隣だけを位置に依存して追加で保護する。
  - 区切り文字は、元の文字と希望の文字で組んだ出力を `inline-parser` の照合で検証し、合わなければ構成的に安全な割り当て（隣と違う文字、単一の役割、隣のテキストの文字参照）へ落とす。

### 他の実装の書き戻しの方式
- **Context**：Build vs. Adopt と、方式の選択。
- **Sources Consulted**：mdast-util-to-markdown（remark）、prettier の Markdown の出力、recast（JavaScript の変更部分だけを印刷し直す AST の書き戻し）、cmark の `commonmark.c`
- **Findings**：
  - cmark の `commonmark.c` と remark は木の全体を正規化して出力し、元の表記を保たない。remark はテキストの「unsafe」パターン（文脈つきの文字の並び）でエスケープし、強調の内側の端の空白を文字参照で表す。
  - recast は、変更のないノードは元のソースの範囲をそのまま出し、変更のあるノードだけを印刷し直す。範囲の重なり（祖先の字下げ）は行単位で付け直す。
  - どれも C のライブラリとして Amark のトークンと拡張の仕組みに合わせられないため、作る。考え方（unsafe パターン、文字参照による端の表し方、変更部分だけの印刷し直し、行単位の字下げの付け直し）を採る。
- **Implications**：エンジンは recast の「変更のない部分はソースの範囲」と remark の「unsafe と文字参照」を組み合わせた形にする。

### `html-renderer` のスロットの規約
- **Context**：要件 9.1、9.3（スロットの形を揃える）。
- **Sources Consulted**：`.kiro/specs/html-renderer/design.md`（HtmlApi、HtmlDispatch、HtmlEngine、Renderer slot conventions、CoreHtml の登録の規則）
- **Findings**：ノード型ごとのスロットと前後のフックのスロットの 2 種類、名前でのノード型の指定、構文セットの順の連鎖と `render_next`、子の省略、拡張ごとの状態、`begin`（順）／`end`・`finish`（逆順）、不透明な文脈、バッファと書き出しの関数の 2 つの入口（`amark_write_fn_t` を `render.h` で共有）、`struct_size` と未知のビットの拒否、組み込みの出力は `amark.core.html` 1 つ、構築関数が `amark.core` の直後に足す。
- **Implications**：同じ形で `AMARK_SLOT_MARKDOWN_NODE`（0x0401）と `AMARK_SLOT_MARKDOWN_HOOKS`（0x0402）を定義し、組み込みの出力を `amark.core.markdown` にまとめる。HTML の入れ子の出力とフィルタは、Markdown の要件にないので持たない（後で末尾に足せる）。Markdown に固有の文脈（接頭辞、空行の規則、エスケープ、区切り文字、保護）は文脈の API として足す。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 全体の正規化（cmark 方式） | 木から常に正規の表記を作る | 単純 | 往復と局所性を満たせない | 不採用 |
| 出力の検証つきの作り直し | 編集した部分を出力して再パースし、違えば別の表記を試す | 正しさを実行時に確かめられる | レンダラがパーサに依存し、断片の文脈（接頭辞、定義）の再現と計算量が重い | 検証はテストの受け入れ基準として使う（実行時には使わない） |
| 構成的な作り直し＋境目の規則（採用） | 元の表記の再利用、作り直し、境目の規則（行、葉、ブロックの区切り）をエンジンにまとめる | パーサに依存しない。局所性を保てる。拡張も同じ規則に乗る | 規則の網羅をテストで確かめる必要がある | 強調だけはパーサの照合の純粋な関数で検証する |

## Design Decisions

### Decision: 元の表記の出力はエンジン、作り直しは出力の処理
- **Context**：要件 2、5、9.9。
- **Alternatives Considered**：
  1. すべてのノードで出力の処理を呼び、処理が元の表記を出すか決める
  2. 無編集で文脈も元のままの部分木はエンジンがトークンをそのまま出し、それ以外だけ処理を呼ぶ
- **Selected Approach**：2。処理を呼ぶときは編集の状態の印（トークンの有無、内容の編集、構造の編集、移動、汚れの印、新しい隣接、作り直す葉の中）を渡し、処理は `amark_markdown_write_verbatim` などで元の表記を使える。
- **Rationale**：往復の保証が処理の正しさに依存しない。拡張が処理を持たなくても無編集なら往復できる。
- **Trade-offs**：「文脈が元のまま」の判定をエンジンが持つ（新しい隣接、作り直す葉、汚れの印）。

### Decision: 出力の単位は行、接頭辞は「その行のコンテナのトークン」
- **Context**：要件 8。
- **Selected Approach**：エンジンが現在の行を、内容（断片）と元の行への対応（アンカー）とともにため、行の終わりに祖先のコンテナの接頭辞を外側から付ける。コンテナがアンカーの元の行に接頭辞のトークンを持てばそれを、持たなければ処理の `prefix` で合成する。最初の行のマーカーはコンテナの最初の出力の行にだけ使う。部分的なタブは、同じタブのすべての部分を元のまま出すときだけタブで、それ以外は列の数の空白にする。合成した接頭辞の後ろの列に依存するタブは、元の幅の空白にする。
- **Rationale**：遅延継続、移動、部分的なタブ、マーカーの幅の変化を 1 つの規則で扱える。

### Decision: 保守的なエスケープ（v1）
- **Context**：要件 3.3、3.4、7.5、9.7。
- **Alternatives Considered**：
  1. 文脈を解析して必要最小限だけエスケープする
  2. 構文セットが宣言する文字の和を、作り直す葉のテキストで常にエスケープし、位置に依存する保護（行頭、行末、区切りの隣、拡張の保護の要求）を足す
- **Selected Approach**：2。作り直す葉では、編集していない text も同じ規則でエスケープし直す（元の ESCAPE / CHAR_REF / REPLACED のトークンは表し方を保つ）。
- **Rationale**：作り直す葉の中で、素の記号が新しく構文を作る経路（成立しなかったブラケット、`<` と後ろのテキスト、リンクの外し）をまとめて断てる。判断はエンジンの 1 か所にあるので、後で必要最小限の方式へ変えても出力の処理は変わらない。
- **Trade-offs**：編集した葉の中の、編集していないテキストにもバックスラッシュが増える（局所性は葉の単位）。翻訳の用途では葉の全体を置き換えることが多く、影響は小さい。
- **Follow-up**：必要最小限のエスケープは、オプションのフラグで選べる形で後から足す。

### Decision: 区切り文字は「希望の表記を照合で検証、だめなら構成的な割り当て」
- **Context**：要件 3.5、5.3、6.5。
- **Alternatives Considered**：
  1. 構成的な規則だけ（隣と違う文字、単一の役割、隣のテキストの文字参照）
  2. 出力側に照合を作り直す
  3. `inline-parser` の照合を純粋な関数として公開し、それで検証する
- **Selected Approach**：3 と 1 の組み合わせ。最初に元の文字と希望の文字で組み、照合で意図した対になるかを確かめる。合わなければ 1 で割り当て直して再び確かめる。照合を提供するフックがないときは 1 だけを使う。
- **Rationale**：3 の倍数の規則に頼る無編集の強調を書き換えない（局所性）。構成的な割り当ては照合がなくても正しい。照合の規則を重複して実装しない。
- **Trade-offs**：`inline-parser` の区切り文字の処理を、並びの配列の上の純粋な関数として切り出す上流の変更が要る。

### Decision: ブロックの区切りは「元の隣接は元のまま、新しい隣接は空行と処理の判断」
- **Context**：要件 3.6、5.2(c)。
- **Selected Approach**：2 つのブロックが元の文書でも連続していた（どちらも元の位置で、間の行がトークンの空行だけ）なら元の区切りを出す。新しい隣接では、親の空行の規則（元のまま、なし、空行が要る）に従い、空行が許されれば少なくとも 1 つの空行を置く。空行が許されない（タイトなリスト）ときは、後ろのノードの処理の `can_follow` が真のときだけ空行なしで置き、偽なら表現できないと報告する。空行の後でも残る結合（同じ種類のリスト、インデントのコード、字下げの吸収、閉じていないフェンスと HTML ブロック）は、新しい隣接の印を受けた処理が表記を切り替えて避ける。
- **Rationale**：元の隣接の判定を行の番号だけで行え、削除で生まれた隣接も検出できる。保守的に空行を置くので、ブロックの種類ごとの中断の表はタイトな文脈の `can_follow` に限られる。

### Decision: 文書全体の依存（リンク参照定義）はフックの事前の印付けで扱う
- **Context**：要件 3.7、5.2(e)。
- **Selected Approach**：`amark.core.markdown` のフックの `begin` が、文書に編集があるときだけ、定義の表（先勝ち）を作り、参照の形のリンクのラベルが同じ宛先とタイトルの定義に解決されるかを確かめる。解決されないリンクと、定義に編集があるときの `UNMATCHED` の `[` を含むテキストに汚れの印を付ける。汚れの印は祖先に伝わり、その葉は作り直しになる。
- **Rationale**：非局所の依存を、拡張と同じ公開の仕組み（`amark_markdown_mark_dirty`）で表せる。脚注などの拡張も同じ方法を使える。

### Decision: 表現できない木は専用のエラーコードと報告で知らせる
- **Context**：要件 4。
- **Selected Approach**：`foundation` のエラーコードの末尾に `AMARK_ERROR_UNREPRESENTABLE` を足す（上流の変更）。理由とノードは、オプションの `report` のポインタに書く。入口の形は `html-renderer` と同じに保つ。
- **Rationale**：引数の誤りと区別でき、利用者が分岐しやすい。報告をオプションに置けば入口の形を揃えられる。

### Decision: GFM 相当の拡張の Markdown の部品はテストの中に置く
- **Context**：要件 10、brief の Existing Spec Touchpoints。
- **Selected Approach**：`tests/markdown/support/gfm_md_ext.{h,c}`（INTERFACE ライブラリ `amark_test_md_gfm`）に、表、取り消し線、タスク項目、脚注の Markdown のスロットと、拡張オートリンクと表の区切りの行の保護のフックを置く。`inline-parser` の `tests/extensions/gfm_set.c` がそれを構文セットに加える。
- **Rationale**：HTML（`tests/html/support`）、ブロックのパース（`tests/parse`）、インライン（`tests/extensions`）と同じく、部品を所有する spec のテストに置く。

## Design Synthesis

### Generalization
- 接頭辞の付け直し、遅延継続、移動、部分的なタブ、マーカーの幅の変化を、「行のアンカーと、その元の行のコンテナのトークン」の 1 つの規則にまとめた。
- テキストのエスケープ、行頭のブロックの構文、行頭と行末の空白、ATX の末尾の `#`、表のセルの `|`、拡張オートリンク、短縮の参照リンクの後ろを、「テキストの文字の保護」と「保護の要求」（エンジンの規則、葉の指定、フックの検出、次の文字の要求）にまとめた。
- リンク参照定義と脚注の定義の依存を、`begin` の汚れの印にまとめた。

### Build vs. Adopt
- 再パースによる実行時の検証は採らず、プロパティテストで同じ検証を行う。照合だけは `inline-parser` の純粋な関数を使う（規則を重複させない）。HTML のリテラルの妥当性も上流の走査の関数を使う。

### Simplification
- HTML の入れ子の出力とフィルタは持たない。エスケープの方式は 1 つだけ。新しいノードの表記はオプションの値と、ノード自身のトークンだけから決める（周りの文書から推定しない）。オートリンクと拡張オートリンクは、編集したら常にインラインのリンクの形にする。

## Risks & Mitigations
- 境目の規則の漏れ（意味が変わる出力）— 構文トークンを取り除いた木の作り直しと、編集の手順のプロパティテストを spec.txt の全例・コーパス・ファジングで回し、再パースの比較を受け入れ基準にする。
- 保守的なエスケープで、編集した葉のバックスラッシュが増える — エスケープの判断をエンジンにまとめ、後で必要最小限の方式を足せるようにする。
- 区切り文字の構成的な割り当てで文字参照が増える（`&#111;`）— 照合による検証で、元の表記が成り立つときはそれを保つ。
- 上流の変更（エラーコード、走査と照合の公開、テキストの走査の規則）が遅れる — 統合の節に明記し、この spec の実装の前に上流の spec を更新する。

## Revision Notes

### 2026-11-25：上流の spec による依頼の取り込み（この spec の承認後）
- **Context**：design の Integration & Upstream Changes の依頼は、それぞれの上流の spec に反映された。この spec の承認済みの要件・設計・タスクの意図は変えず、取り込まれた形に記述を合わせた。
- **`foundation`**：`AMARK_ERROR_UNREPRESENTABLE = 6` と `amark_error_string` の文字列が取り込まれた（`foundation` の要件 10.9、タスク 2.1、Downstream Extension Points）。
- **`block-parser`**：`scan.h` のバイト列の HTML ブロックの開始と終わりの条件とタグの走査（`block-parser` の 7.7、7.8）と、構築関数の出力の core 拡張の一覧への `amark.core.markdown` の 1 行（この spec のタスク 3.7 が足す）が取り込まれた。
- **`inline-parser`**：
  - `amark_inline_delimiter_flanks` と `amark_inline_match_delimiters`（5.7）。取り込まれた形では `amark_inline_run_t` が `max_run_length` ではなく `use_mask` を持ち、`capacity` は「長さの合計の半分（切り捨て）以上」になった。この spec の `amark_markdown_run(_match)_t` は形を変えず、対応の表を MdDispatch に置いた（下の 13）。
  - raw HTML のバイト列の走査（7.7）。`inline-parser` の改訂で名前が `amark_scan_raw_html` から `bool amark_inline_scan_raw_html(const char *data, size_t length, size_t *out_end)`（`inline.h`）に決まったので、design の html_inline の行と Integration をその名前にした。
  - テキストの走査の採否の規則（4.5）。
  - `gen_examples.cmake` の `CHECK=` と `amark_test_example_check_fn_t`（14.4。`inline-parser` のタスク 1.4）。`CHECK=` の所有と実装は `inline-parser` にある。
  - `gfm_set.c` の部品の並びは 1 つの配列で、Markdown の部品を加える編集だけをこの spec のタスク 4.2 が行う。

### 2026-11-25：spec 間のレビュー（1 回目）の反映
- **Context**：全 spec を横断したレビューで、この spec に関わる 6 つの指摘（1、2、6、7、10、13）があった。承認は保ち、要件の Adjacent expectations、design、タスクを更新した。
- **1. ファジングと Naos**：`-DAMARK_FUZZ=ON -DAMARK_BUILD_TESTS=OFF` で、ファジングのターゲットがリンクする `amark_test_md` が Naos を引き込んでいた。`foundation` が「ファジングのターゲットがリンクするテストの補助は Naos に依存せず、`AMARK_FUZZ` か `AMARK_BUILD_TESTS` のときに定義される」規則を定めたので、検査の本体（判定を状態と説明で返す。`amark_test_md`）と Naos の検査（`amark_test_md_check`：作り直し、生成テストの検査の関数、`AMARK_CHECK_MD`）に分けた。`amark_test_md` は `AMARK_CHECK_TREE_DUMP` を宣言する `amark_test_tree` にもリンクしない。`amark_test_md_gfm` も Naos に依存しない（`amark_test_gfm` を通じてリンクされる）。タスク 1.3、4.1、5.5 に反映した。
- **2. 所有の整理**：`tests/support/gen_examples.cmake` の `CHECK=` は `inline-parser` のタスク 1.4 が所有するので、Modified Files から外した。Upstream の `inline-parser` の 4 は、この spec が上流のファイルに行う編集が `gfm_set.c` と `tests/extensions/CMakeLists.txt` だけであると書き直した。この Revision Notes を加えた。
- **6. 拡張オートリンクの局所性**：`test.inline.autolink` の link はブラケットも括弧のトークンも持たず、長さ 0 の `TEXT` だけを持つ。CoreMarkdown の `begin` は `LINK_PAREN_OPEN` のない link をすべて短縮の参照とみなしていたので、定義を編集すると無編集の拡張オートリンクに汚れの印が付き、局所性が崩れた。参照の形を「`LINK_REF_*` を持つか、`LINK_OPEN` を持ち `LINK_PAREN_OPEN` を持たない」に限り、ブラケットと括弧のトークンを持たない link を拡張の定めた形として無編集なら元の表記で出す。`inline-parser` が `gfm_inline_ext.h` に公開する拡張オートリンクの役割の定数 `AMARK_TEST_GFM_ROLE_EXT_AUTOLINK`（0x0E50u）で、`amark_test_md_gfm` の `link` の処理が明示的に扱う。組み込みのリンクと画像は必ず `LINK_OPEN` と `LINK_CLOSE` を持ち（短縮の参照はその 2 つだけ）、拡張の定めたリンクは `LINK_OPEN`・`LINK_PAREN_*`・`LINK_REF_*` をどれも持たない（`inline-parser` の確定）ので、分類は役割だけで曖昧なく決まる。`amark_test_gfm` は Naos に依存せず、Naos の補助は `amark_test_gfm_check` に分かれた。拡張の型を知らないエンジンの規則（トークンの役割による分類）に一般化したので、他の拡張の link も同じ扱いになる。
- **7. `amark.h`**：`foundation` が正準の順を 1 つ定めたので、「`html.h` の後」ではなく「正準の順に従い、`markdown.h` を最後」と書いた。
- **10. 生成の手順の重複**：`gen_md_cases.cmake` は `html-renderer` の `gen_html_cases.cmake` の形式を作り直すものだった。`html-renderer` が共有の契約（`### name [options]`、S 式、`---`、期待する出力、`===`。オプションは解釈せず 1 つの文字列。ARGS `CHECK=`、`INCLUDE=`。`CHECK(name, options, sexpr, sexpr_length, expected, expected_length)`）を定めたので、`gen_md_cases.cmake` を削り、`CHECK=amark_test_md_check_case INCLUDE=md_check.h` で再利用する。検査の関数が未知のオプションを拒否する。spec.txt の版と SHA-256 は `inline-parser` の `tests/spec/spec_version.cmake` を include して同じ規則で照合・スキップし、この spec で持たない。
- **13. 照合の対応**：`amark_markdown_run(_match)_t` と `amark_inline_run(_match)_t` は別の型で、上限の表し方（`max_run_length` と `use_mask`）と `capacity` の規則が違う。MdDispatch に要素ごとの対応の表（添字・`struct_size`・`rule` の値・上限から `use_mask` への変換・照合の結果の写し）と、`capacity`（エンジンは長さの合計以上を渡し、上流の半分以上の要求を満たす）・作業の配列（文書のアロケータ、状態に持って使い回し `finish` で解放）・失敗の規則を書いた。`amark_markdown_run_t` の公開の形は変えない（拡張が別の照合を提供できる形を保つ）。
- **Implications**：Revalidation Triggers に、`amark_inline_run(_match)_t` の形と `capacity`、拡張オートリンクの役割の定数、`gen_html_cases.cmake` の契約、`spec_version.cmake`、`foundation` の `amark.h` の順とファジングの規則、`amark_test_md`・`amark_test_md_gfm` が Naos に依存しないことを加えた。

### 2026-11-25：spec 間のレビュー（2 回目、cross-spec review r2）の反映
- `tests/support/gen_examples.cmake` の所有の記述を直した：ファイルと基本の契約は `block-parser` が所有し、`inline-parser` は `EXPECT=html`・`FACTORY=`・`CHECK=` の追加だけを所有する（design の Allowed Dependencies と File Structure Plan の該当箇所）。この spec は引き続きこのファイルを編集しない。承認の状態は変えない。

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — エスケープ、文字参照、ブロックの構造、flanking と照合の規則
- [cmark](https://github.com/commonmark/cmark) — `inlines.c`（照合）、`commonmark.c`（正規化した書き戻し）
- [mdast-util-to-markdown](https://github.com/syntax-tree/mdast-util-to-markdown) — unsafe パターンと文字参照による端の表し方
- [recast](https://github.com/benjamn/recast) — 変更部分だけを印刷し直す書き戻し
- `.kiro/specs/syntax-tree/design.md`、`.kiro/specs/block-parser/design.md`、`.kiro/specs/inline-parser/design.md`、`.kiro/specs/html-renderer/design.md`
