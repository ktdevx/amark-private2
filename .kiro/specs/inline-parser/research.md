# Research & Design Decisions

## Summary
- **Feature**：`inline-parser`
- **Discovery Scope**：New Feature / Complex Integration（`block-parser` の委譲の取り決め、走査と解読の関数、ラベルの表、`syntax-tree` のトークンとレジストリ、`html-renderer` の出力と GFM 相当の HTML の部品の上に、汎用のインラインのエンジンと CommonMark の全インライン構文、プロジェクト全体の受け入れゲート（準拠テストと表現力の検証）を作る）
- **Key Findings**：
  - cmark の `inlines.c` の手順（特別な文字で止まる走査、区切り文字のスタック、ブラケットのスタック、`process_emphasis`、`handle_close_bracket`）は、個別の構文の知識を「特別な文字ごとの処理」「区切り文字の種類ごとの規則」「閉じのブラケットでの後処理」の 3 か所に閉じ込められる。エンジンは、この 3 つの登録と、テキストのまとめ、区切り文字とブラケットのスタックだけを持てばよい。
  - 病的な入力で線形の時間を保つ仕組み（`openers_bottom`、`no_link_openers`、`bracket_after`、バッククォートの位置の記憶、生の HTML のコメントなどの「閉じがない」の記憶、宛先の括弧の深さ 32）は、構文ごとの「葉ごとの作業領域」と、エンジンの汎用の印（区切り文字の種類ごとの下限、開きの種類ごとの無効化）で一般化できる。
  - CommonMark 0.31.2 の spec.txt は 652 例で、cmark 0.31.1 は `--no-normalize`（バイト単位の比較）と `--unsafe` で全例を通す。`html-renderer` は cmark の `html.c` と 1 対 1 に対応するので、準拠テストはバイト単位で比較できる。
  - GFM 相当の拡張オートリンクは、cmark-gfm では特別な文字（`:`、`w`）と後処理を混ぜて実装している。本設計では「区切り文字とブラケットの処理が終わった後のテキストのノードを走査する構文」として一般化し、きっかけの文字を持たない構文の受け皿にする。

## Research Log

### CommonMark 0.31.2 と cmark 0.31.1 のインラインの手順
- **Context**：要件 2、5、6、7。エンジンが持つべき仕組みと、構文に分けられる部分を確定するため。
- **Sources Consulted**：CommonMark Spec 0.31.2（6 章、付録「A parsing strategy」の Phase 2、process emphasis、look for link or image）、cmark 0.31.1 `src/inlines.c`（`parse_inline`、`handle_delim`、`scan_delims`、`process_emphasis`、`S_insert_emph`、`push_bracket`、`handle_close_bracket`、`handle_backticks`、`scan_to_closing_backticks`、`handle_newline`、`handle_pointy_brace`、`manual_scan_link_url`）。
- **Findings**：
  - 走査は、特別な文字の表で次の特別な文字まで進み、その間をテキストにする。特別な文字ごとに処理を呼び、処理が成立しなければその文字をテキストにする。
  - 改行の処理は、直前のテキストの末尾の空白を調べる（2 つ以上の空白ならハードブレーク）。行末の空白はテキストから取り除く（`cmark_chunk_rtrim`）。行頭の空白はブロックの段階で除かれている。
  - 区切り文字の並びは、前後の文字（Unicode の空白と句読点。0.31 で句読点は P と S の一般カテゴリ）から left/right flanking を求め、`_` は語の中で開閉しない規則を加える。
  - `process_emphasis` は、閉じの候補ごとに、同じ文字・開ける・「3 の倍数の規則」を満たす最も近い開きを探す。見つからなければ `openers_bottom[文字, 閉じが開けるか, 長さ % 3]` を更新し、以後の探索の下限にする（二次の時間を避ける）。2 つ以上使えれば strong、そうでなければ emph。
  - 閉じのブラケット：最後の開きが無効（`no_link_openers` かつリンクの開き）なら `]` をテキストに。インラインのリンク（`(`、空白、宛先、空白、タイトル、空白、`)`）、完全参照（`[label]`）、省略参照（`[]`）、ショートカット（`bracket_after` が偽のときだけ中身をラベルにする）の順に試す。成立したら、中の区切り文字を処理してから中身を新しいノードへ移し、リンク（画像でない）なら前のリンクの開きをすべて無効にする。
  - 宛先の括弧の入れ子は 32 で打ち切る（`nb_p > 32` で失敗）。spec は「実装が括弧の入れ子に上限を設けてよい（少なくとも 3）」と許している。
  - バッククォート：開きの長さごとに、以後に見つかった閉じの候補の位置を記憶し、閉じがないと分かった長さを再び探さない（cmark は長さ 1000 を上限にした配列。これは spec にない制限である）。
  - 生の HTML：コメント、処理命令、宣言、CDATA の閉じがないと分かったら、その葉の中では以後探さない印を立てる（`FLAG_SKIP_HTML_*`）。
  - オートリンク：テキストの子は文字参照を解読した値（`make_str_with_entities`）、宛先は `cmark_clean_autolink`（メールは `mailto:` を前に付ける）。
- **Implications**：
  - エンジンは「特別な文字の表」「区切り文字の種類の規則」「ブラケットの後処理」と、テキストのまとめ、2 つのスタック、葉ごとの作業領域だけを持つ。
  - 改行の処理が直前のテキストの末尾を取り返す仕組みを一般化し、「まだテキストとして確定していない直前の範囲を構文が引き取る」API（`amark_inline_claim_preceding`）にする。
  - バッククォートは葉ごとの作業領域に、最初の使用時に葉全体のバッククォートの並びの索引を作ることで、長さの上限なしに線形にする。
  - 宛先の括弧の深さ 32 は、`block-parser` の `amark_scan_link_destination` の契約として明記する必要がある（上流の変更）。

### 病的な入力（cmark の pathological_tests）
- **Context**：要件 11。計算量のテストの入力と期待を決めるため。
- **Sources Consulted**：cmark 0.31.1 `test/pathological_tests.py`。
- **Findings**：入れ子の強調（`*a **a ` × 32500）、閉じだけ・開きだけの強調、閉じだけ・開きだけのリンク、`*a_ ` の混在、issue #389、3 の倍数、`[ a_`、`[ (](`、`![[]()`、`**x [a*b**c*](d)`、入れ子のブラケット（25000）、U+0000、バッククォート（1–2500 の並び）、閉じないリンク A/B（`[a](<b`、`[a](b`）、閉じない `<!--`（300000）、深い引用の中の強調。cmark はこれらを時間の上限つきで実行する。
- **Implications**：同じ入力を `tests/inline/pathological_test.c` に移し、時間の上限（CTest の TIMEOUT）と出力の形で確かめる。倍々の時間は `bench/inline_pathological.c` で表示する。エンジンの内部のカウンタ（構文の試行、区切り文字の比較、ブラケットの後処理の呼び出し）で、入力を倍にしたときの比を INTERNAL のテストで確かめる。

### 準拠テストの基準
- **Context**：要件 12。比較の方式と spec.txt の固定の方法。
- **Sources Consulted**：`https://raw.githubusercontent.com/commonmark/commonmark-spec/0.31.2/spec.txt`、cmark 0.31.1 `test/CMakeLists.txt`（`spec_tests.py --no-normalize`）、`test/cmark.py`（`--unsafe`）。
- **Findings**：
  - spec.txt 0.31.2 は 205025 バイト、SHA-256 `257c41ad946f7a1414a499aca402a1aa8fdac3678532266611348c1cf54f4b80`、例は 652。例は 32 個のバッククォートと ` example` の行から `.` の行までが入力、`.` から閉じまでが期待する HTML。入力と出力の `→` はタブ。
  - cmark は正規化なし（バイト単位）、生の HTML を許可して全例を通す。
  - spec.txt は CC-BY-SA 4.0（ファイルの前付けに版と許諾がある）。
- **Implications**：準拠テストは、版と SHA-256 を `tests/spec/spec_version.cmake` の 1 か所に固定し、構成時に照合する。比較はバイト単位、HTML のオプションは `AMARK_HTML_UNSAFE`。spec.txt は `.gitattributes` で `export-ignore` にし、ソースのアーカイブにない場合はテストを「スキップ」として報告する。

### `block-parser` との継ぎ目
- **Context**：要件 1、3、6、10。委譲の取り決めと共有する部品を、重複なく使うため。
- **Sources Consulted**：`.kiro/specs/block-parser/design.md`（ParserAPI、ParseContext、TextInput と Scanners、LabelMap、PlainDelegate、Fragment、CommonMarkSet、Integration & Upstream Changes）。
- **Findings**：
  - `amark_inline_delegate`（`begin` / `parse` / `end`）は葉ごとに `amark_text_input`（論理的な位置、区間の隙間は内容に含まない）を受け取る。段落の類の区間は行頭の空白の後から始まり、最後の行は末尾の空白の前で終わる。行の間の改行は区間の中にある。
  - `amark_parse_add_decoded` は、論理的な範囲を `ESCAPE`、`CHAR_REF`、`LINE_ENDING`、`TEXT` / `REPLACED` に分けて記録し、解読した値を返す。`amark_parse_add_text` は `TEXT` / `REPLACED` に分ける。
  - 走査の関数（ラベル、宛先、タイトル、HTML の開始タグと終了タグ）は論理的な位置で区間をまたぐ。コメント、処理命令、宣言、CDATA の走査はない（インラインに固有なので、この spec の `raw_html` が持つ）。
  - `amark_inline_delegate_default()` は `inline-parser` が自分のエンジンに変える。`amark_syntax_set_add_commonmark` にインラインの拡張を足すのも `inline-parser`。
  - エンジン用の役割の範囲 0xFF00–0xFFFF のうち、ブロックのエンジンは 0xFF01–0xFF05 を使う。
  - 宛先の走査の括弧の深さの上限は design に書かれていない。
  - GFM 相当のブロックの検証用の構文（`tests/parse/gfm_block_ext.{h,c}`）はライブラリとして公開されていない。脚注の定義の表は拡張の状態の `amark_label_map` で、インラインの委譲の中から引く設計。
- **Implications**：エンジンは `src/parse/` に置き、既定の委譲先にする。走査・解読・ラベルの表を再利用し、重複して実装しない。上流の変更として、宛先の括弧の深さ（32）の明記、役割の範囲の分割の注記、GFM のブロックの検証用の構文の INTERFACE ライブラリ化と脚注の表の参照関数、`amark_test_parse_check` の関数の形の確認を求める。

### `syntax-tree` と `html-renderer` との継ぎ目
- **Context**：要件 5、8、9、13。トークンのモデルとスロット、HTML の部品の再利用。
- **Sources Consulted**：`.kiro/specs/syntax-tree/design.md`（トークンの 9 種類、`MARKER` は長さ 1 以上、`TEXT` は検査なしで長さ 0 も可、役割は所有するノードの型を扱う構文が決める、スロットの範囲、`amark_node_transfer_tokens`、構造の編集の循環の検査は子を持つノードの挿入で挿入先の深さに比例）、`.kiro/specs/html-renderer/design.md`（組み込みの出力の表、`amark_test_html_gfm` の部品：`test.strikethrough`、`test.footnote_ref`、`test.task_item`、表、脚注、タグフィルタ 2 方式）。
- **Findings**：
  - インラインのノードは組み立ての間、葉の直下に並び、強調やリンクを作るときに新しいノードへ移す。移す先のノードは葉の直下にあるので、循環の検査は葉の深さ（ブロックの深さ）に比例するだけで、インラインの入れ子の深さに比例しない。
  - text のリテラルは改行を拒否する（LINE）。html_inline は空行を拒否し LF に揃える（SPAN）。code は空を拒否する。CommonMark のコードスパンの中身は空にならない。
  - 拡張オートリンクのように、自分の表記を持たないノード（リンク）は長さ 0 の `TEXT` トークンを位置の印として持てる。
- **Implications**：上流の `syntax-tree` には、スロットの範囲の表（並行して追加中）にこの spec のスロットの番号を載せること以外の変更は要らない。`html-renderer` の部品はそのまま使う。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 汎用エンジン＋登録（採用） | 特別な文字ごとの構文、区切り文字の種類、ブラケットの後処理、テキストの走査をスロットで登録し、エンジンは走査・まとめ・2 つのスタックだけを持つ | 組み込みと拡張が同じ立場。cmark の線形の仕組みをそのまま一般化できる | スロットの種類が 6 つに増える | `block-parser` の段階のコールバックと同じ形 |
| 構文ごとの完全なコールバック（PEG 風） | 各構文が位置から再帰的に解析する | 構文を自由に書ける | 区切り文字の規則が構文をまたぐため表現できない。再帰で深さに比例するスタック | 不採用 |
| cmark-gfm 方式（固定パーサ＋特別な文字のフック） | 中核の処理は固定し、拡張は特別な文字と後処理だけ | 実績 | 強調やリンクを拡張として置き換えられない。roadmap の不採用の案 | 不採用 |

## Design Decisions

### Decision: 区切り文字の種類を宣言的に登録する
- **Context**：要件 5。`*`、`_`、`~` の規則の違いは、flanking の規則、長さの一致の規則、作るノード型に集約できる。
- **Alternatives Considered**：
  1. 区切り文字ごとの任意のコールバック（開閉の判定、一致の判定、ノードの生成）
  2. 宣言的な記述子（flanking の方式、一致の規則の列挙、最大の長さ、使った数ごとのノード型、トークンの役割）
- **Selected Approach**：2。flanking は `ASTERISK`（左右の flanking だけ）と `UNDERSCORE`（語の中の規則つき）。一致の規則は `EMPHASIS`（3 の倍数の規則、両方 2 以上なら 2 つ使う）と `EXACT`（同じ長さだけ、全部使う）。`max_run_length` を超える並びはテキスト。使った数 1 と 2 のノード型の名前を持つ。
- **Rationale**：`openers_bottom` の正しさは一致の規則に依存する。任意のコールバックでは下限の最適化を一般に保てず、病的な入力で二次になる。列挙した規則ごとに下限の鍵を定められる。
- **Trade-offs**：CommonMark と GFM 以外の規則（例えば 3 つ使う区切り）は、規則の列挙を足す（公開の列挙に値を足すだけで、構造体は変えない）。
- **Follow-up**：EXACT の下限の鍵（閉じの長さ）が正しいことを、区切り文字のテストで確かめる。

### Decision: ブラケットの開きと閉じはエンジンが持ち、意味づけは後処理のスロットに任せる
- **Context**：要件 6。リンクと画像と脚注の参照が同じ閉じのブラケットを共有する。
- **Alternatives Considered**：
  1. リンクの拡張が `[` と `]` をすべて扱い、画像や脚注はそのフックに乗る
  2. 開きは構文が `amark_inline_push_bracket` で積み（閉じの文字も指定）、閉じはエンジンが検出してブラケットの後処理を優先度の順に試す
- **Selected Approach**：2。開きの種類の印（`LINK`、`IMAGE`、利用者のビット）で、後処理の対象と無効化の対象を決める。後処理はノードを作って返し、中身を子にするか（CHILDREN）捨てるか（LEAF）を選ぶ。作ったノードの型が LINK の性質を持てば、前の LINK の開きを無効にする。
- **Rationale**：リンクと画像を別々の構文の拡張にでき、どちらを取り除いても他方が動く。脚注の参照のような拡張が、リンクの前に割り込める。
- **Trade-offs**：閉じの文字は開きごとに指定する（`]` を前提にしない）。
- **Follow-up**：`bracket_after` を後処理に渡し、ショートカットの判定を cmark と一致させる。

### Decision: きっかけの文字を持たない構文は、組み立て後のテキストのノードの走査にする
- **Context**：要件 4。拡張オートリンクは、URL の途中に `_` や `*` を含みうるため、走査の途中のテキストの断片では判定できない。
- **Alternatives Considered**：
  1. 走査の途中で、どの構文にも消費されない範囲ごとに調べる
  2. 区切り文字とブラケットの処理の後、隣り合うテキストをまとめてから、テキストのノードを走査する
- **Selected Approach**：2。スロットは `scan`（次の一致の範囲を返す）と `build`（一致したテキストのノードを包むノードを作る）の 2 つの関数を持つ。エンジンがテキストのノードを一致の範囲で分割し（トークンも分ける）、分割は左から右へ 1 回だけ行う。リンクの性質を持つノードの中では `in_link` を真にして渡す。
- **Rationale**：分割をエンジンが行うことで、トークンの被覆を保ち、1 つの大きなテキストに多数の一致があっても線形に保てる。
- **Trade-offs**：一致の境界がエスケープ・文字参照・置き換えたバイトのトークンの途中に来る場合は、その一致を無視する（文書化）。cmark-gfm と異なる結果になるのは、URL の途中で強調が成立する入力だけ。
- **Follow-up**：代表的な入力で cmark-gfm と一致することを GFM の検証で確かめる。

### Decision: 改行と行末の空白は、直前の未確定のテキストを構文が引き取る形にする
- **Context**：要件 2.4、7.1、8.5。ハードブレーク（2 つ以上の空白）とソフトブレークの前の空白。
- **Alternatives Considered**：
  1. 空白をきっかけの文字にして、行末まで先読みする（空白の並びが長いと二次。作業領域で記憶すれば線形）
  2. 改行をきっかけの文字にし、エンジンがまだテキストにしていない直前の範囲（直前の構文の後から）を、構文が後ろから引き取る
- **Selected Approach**：2（`amark_inline_pending_start`、`amark_inline_claim_preceding`）。
- **Rationale**：空白ごとの呼び出しがなく、cmark の rtrim と同じ結果になる。他の構文（GFM の `:` から前の scheme を引き取るような構文）にも使える。
- **Trade-offs**：引き取れるのは、直前の構文の後のエンジンの素のテキストだけ（エスケープや文字参照の後ろからは引き取れない）。CommonMark の結果はこれで一致する。

### Decision: 葉ごとの作業領域
- **Context**：要件 11。バッククォート、生の HTML の閉じの有無、行末の空白などの記憶。
- **Selected Approach**：インライン構文・ブラケットの後処理・テキストの走査のスロットは `leaf_state_size` と任意の `leaf_end` を持つ。エンジンはパースごとに 1 回確保し、葉の中で最初に使うときに 0 で埋める。`leaf_end` は使った葉の終わり（失敗のときも）に呼ぶ。
- **Rationale**：大量の小さな葉でも、使わない構文の領域を毎回埋める費用がかからない。動的な索引（バッククォートの並び）を `leaf_end` で解放できる。

### Decision: エスケープに関わる文字は宣言と問い合わせだけを提供する
- **Context**：要件 9。`markdown-renderer` がエスケープの方式を決める。
- **Selected Approach**：`AMARK_SLOT_INLINE_SPECIAL`（文字の 256 ビットの集合と、構文を始める文字の並び）と、構文セットに対する問い合わせ（拡張ごとの一覧と全体の和）。きっかけの文字と区切り文字の文字は自動的に含める。
- **Rationale**：方式を決めずに、拡張の構文を含めた情報を渡せる。後で方式を改善しても API は変わらない。

### Generalization / Build vs Adopt / Simplification
- **Generalization**：特別な文字の処理、区切り文字、ブラケット、テキストの走査は「構文セットのスロットを優先度の順に試す」という 1 つの形にそろえた。葉ごとの作業領域は 3 種類のスロットで共通。
- **Build vs Adopt**：cmark・md4c・markdown-it のコードは取り込まない（MIT 以外の許諾、内部の表現の違い、ロスレスなトークンの記録がない）。手順は cmark の `inlines.c` に合わせる（結果を cmark と一致させるため）。spec.txt はテストデータとしてだけ採用する。
- **Simplification**：区切り文字の使った数は 1 と 2 だけ（CommonMark と GFM の範囲）。ノードの性質は「リンク」だけ。リンクの後ろの部分の解析は公開せず、`src/syntax/` の共有のファイルに置く（必要になれば後で公開できる）。インラインのオプションは持たない。

## Risks & Mitigations
- 準拠テストの失敗が `html-renderer` の出力の差に由来する — `html-renderer` の出力の表を直す（Revalidation Trigger）。spec の例の番号で原因を追える。
- 拡張オートリンクの結果が URL の中の強調で cmark-gfm と異なる — 検証の入力を代表的なものに限り、差を設計に明記する。
- `block-parser` の宛先の走査に括弧の深さの上限がない — 上流の変更として 32 を明記してもらう。実装の前に確認する。
- 区切り文字の種類の規則の列挙が将来足りない — 列挙に値を足す形で拡張でき、構造体の `struct_size` で互換を保つ。
- 大きな葉でのテキストのまとめの費用 — まとめは葉ごとに 1 回の線形の走査で、リテラルは 1 回だけ設定する。

## Revision Notes

### 2026-11-25：`markdown-renderer` からの依頼の反映と、`block-parser` が実装する項目の重複の解消
- **Context**：`markdown-renderer` の design の Integration & Upstream Changes が、この spec に 4 つの変更を依頼した（区切り文字の判定と照合の公開、生の HTML のバイト列の判定の公開、テキストの走査の規則の追加、例の生成の `CHECK=`）。あわせて、この spec が `block-parser` に依頼した 4 つの変更（宛先の括弧の入れ子の上限、エンジンの役割の範囲の分け方、`amark_test_parse_gfm`、関数の形の `amark_test_parse_check`）は `block-parser` の spec に反映され、`block-parser` のタスクで実装されることになった。承認済みの要件・設計・タスクに追加する形で行い、既存の受け入れ基準の意味は変えない。
- **flanking と照合の公開（5.7）**：`markdown-renderer` は、作り直した区切り文字が再パースで同じ対になるかを、パースと同じ規則で確かめる必要がある。規則を 2 か所に書くと食い違いが起きるので、エンジンの process emphasis を「並びの配列の上で照合の列を返す純粋な関数」`amark__inline_match_runs` に分け、公開の `amark_inline_match_delimiters` とエンジンの両方がそれを通る形にした。cmark の `openers_bottom` は `process_emphasis` の 1 回の呼び出しの中だけの状態で、`bottom` より上の区切り文字は呼び出しの時点でまだ照合に使われていないので、照合を先に求めてから順に木に適用しても、cmark の逐次の適用と同じ木になる。種類は文字で区別する（同じ文字の 2 つの種類はスロットの検査で拒否している）。ノード型の有無（`node_types[1]` など）は「使える数の印」に写して、純粋な関数がノード型を知らないようにした。検討した代替：エンジンの中の照合を残して公開の関数を別に書く案は、規則の重複になるので採らなかった。照合の結果をコールバックで木に適用する案は、公開の関数の契約が複雑になり、`markdown-renderer` にとって不要なので採らなかった。
- **生の HTML の判定の所有（7.7）**：`markdown-renderer` は `amark_scan_raw_html` を `inline-parser` に依頼し、同時に `block-parser` に HTML のタグのバイト列の走査（`amark_scan_html_open_tag_bytes` / `_closing_tag_bytes`）を依頼し、それは `block-parser` の spec に反映された（`src/parse/scan_html.c` の 1 つの実装。`block-parser` のタスク 1.1、3.4）。開始タグと終了タグの規則は HTML ブロックの種類 7 とインラインの生の HTML で共通なので `block-parser` が持ち、区間の並びの版とバイト列の版を `block-parser` の中で 1 つの実装にする。コメント・処理命令・宣言・CDATA はインラインの生の HTML だけの文法（HTML ブロックの開始と終わりの条件は別の文法）なので、この spec の `raw_html.c` が持つ。公開の `amark_scan_raw_html` もその振り分けも `raw_html.c` に置き、構文（区間の並びの上）と公開の関数（バイト列の上）は「前からバイトを読む読み手」の上の同じ走査を使う。こうして、タグの規則は `block-parser` に 1 つ、その他の 4 種類の規則はこの spec に 1 つとなり、どちらにも重複がない。検討した代替：(a) `amark_scan_raw_html` を `block-parser` の `scan.h` に置く案は、インラインだけの文法（コメントの `<!-->` など 0.31.2 の規則）を `block-parser` に持ち込み、`commonmark.raw_html` と規則が離れるので採らなかった。(b) 構文の中で区間の並びをいったん連続のバッファに写して公開の関数を呼ぶ案は、確保と複写が増え、作業領域の「閉じがない」の印との組み合わせが不自然になるので採らなかった。公開の関数は 1 回の判定なので「閉じがない」の印を持たないが、印は走査を省くだけで結果を変えないので、同値性は保たれる。
- **テキストの走査の採否の規則（4.5）**：エスケープや文字参照は、書き手が「この文字を構文として読ませない」ために使う表記である。きっかけの文字を持たない構文（拡張オートリンク）はエンジンがまとめた text の値を見るので、規則がないと `www\.example.com` のようなエスケープした表記もリンクになり、`markdown-renderer` がエスケープでテキストを保護できない。採否をエンジンの InlineText に置き、範囲に `ESCAPE` か `CHAR_REF` のトークンの文字を含む一致を採らず、範囲の後ろから続ける。拡張の側で採否を実装しない。`REPLACED` は書き手の意図の表記ではないので対象にしない。判定は text のトークンを 1 回たどるカーソルで行い、線形を保つ。cmark-gfm はエスケープの位置で text のノードが分かれるため、エスケープを含む URL の一部だけをリンクにすることがあり、この規則と結果が違いうる。そのため、そのような入力は `gfm_cases.txt`（cmark-gfm と同じ形の HTML の照合）に入れず、この spec のテストで確かめる。
- **例の生成の `CHECK=`（14.4）**：`markdown-renderer` は spec.txt と自分のコーパスを、同じ区切りの形式と生成の手順で、往復の検査に使う。生成の手順を複製しないよう、`gen_examples.cmake` に検査の関数の名前を受ける `CHECK=` を足し、関数の形を `amark_test_example_check_fn` として `inline_check.h` に置いた。生成するソースが関数の宣言を出すので、呼び出し側はヘッダを足さずにリンクするだけでよい。`EXPECT=` との同時の指定は意味が曖昧なので構成の失敗にした。
- **`gfm_set.c` の編集の所有**：`gfm_set.{h,c}` と `amark_test_gfm` はこの spec が作るが、Markdown の検証用部品を加える編集は `markdown-renderer` のタスク 4.2 が行う（`markdown-renderer` の brief の Extends）。この spec は部品の並びを 1 つの配列に置き、1 か所に足せる形にするだけにした。
- **`block-parser` が実装する項目の重複の解消**：宛先の括弧の入れ子の上限 `AMARK_SCAN_MAX_LINK_PAREN_DEPTH`（`block-parser` のタスク 1.1、3.3、10.2）、エンジンの役割の範囲の分け方（1.1）、`amark_test_parse_gfm`（9.1–9.3）、関数の形の `amark_test_parse_check`（1.4）は `block-parser` が実装する。この spec のタスク 1.2、1.3 は実装の作業から、公開 API とテストのライブラリを使って確かめる作業に変え（タスクの番号は保つ）、`_Depends:_` に `block-parser` のタスクを書いた。タスク 1.2 には、生の HTML が頼るタグのバイト列の走査の確認も加えた。design の Integration & Upstream Changes は、依頼 1–4 を「反映済み、`block-parser` が実装する」と書き換え、Boundary Commitments の Out of Boundary に挙げた。
- **Implications**：Revalidation Triggers に、公開した 3 つの関数、テキストの走査の採否の規則、`CHECK=` の形と `gfm_set` の部品の並び（→ `markdown-renderer`）、`block-parser` の定数・タグの走査・テストのライブラリの契約（→ この spec）を加えた。要件に 4.5、5.7、7.7、14.4 を加えた。

### 2026-11-25：cross-spec review r1 の反映
- **きっかけ**：全 spec の横断レビュー（cross-spec review r1）の指摘 #1、#6、#7、#10、#11、#12、#14 のうち、この spec に関わる部分。承認の状態は変えず、要件・設計・タスクを揃えた。
- **#1 ファジングがリンクするライブラリと Naos**：`-DAMARK_FUZZ=ON -DAMARK_BUILD_TESTS=OFF` で構成できるよう、`foundation` の規則（ファジングが使うテスト支援ライブラリは Naos に依存せず、`AMARK_FUZZ` か `AMARK_BUILD_TESTS` が ON のときに定義される。`LIBRARIES` のターゲットがなければ `amark_add_fuzzer` が構成を失敗させる）に従う。`amark_test_gfm`（`gfm_inline_ext.c`、`gfm_set.c`）は Naos に依存しないことを明記し、Naos を使う照合と検査（`amark_test_inline`、`gfm_compose_test.c`）はファジングにリンクしない側に置く。GFM 相当に固有の Naos を使う検査の補助が要るときは、別の INTERFACE ライブラリ `amark_test_gfm_check` に分ける。要件 11.6 を加え、タスク 7.3、8.2 の完了条件に入れた。
- **#6 拡張オートリンクの役割の番号**：`test.inline.autolink` の link はブラケットや括弧のトークンを持たず、長さ 0 の `TEXT` だけを持つが、その役割が公開されていなかった。`gfm_inline_ext.h` に `AMARK_TEST_GFM_ROLE_STRIKE_MARKER`（0x0E40）、`AMARK_TEST_GFM_ROLE_EXT_AUTOLINK`（0x0E50）、`AMARK_TEST_GFM_ROLE_FOOTNOTE_REF_MARKER`（0x0E60）、`AMARK_TEST_GFM_ROLE_FOOTNOTE_REF_LABEL`（0x0E61）を公開した（`block-parser` の 0x0E01–0x0E20 と同じテスト用の範囲で重ならない）。LinkCommon に、組み込みのリンクは必ず `LINK_OPEN`/`LINK_CLOSE` を持ち、拡張が定義するリンクは `LINK_OPEN`・`LINK_PAREN_*`・`LINK_REF_*` を持たずショートカットとも扱わないことを書いた。要件 13.6 を加えた。
- **#7 `amark.h` の順**：`foundation` が定める正規の順に従う書き方に変えた（`inline.h` は `block.h` と `commonmark.h` の間）。
- **#10 例の生成のスクリプト**：`tests/support/gen_examples.cmake` は `block-parser`（タスク 1.4）が先に spec.txt の形式の唯一の読み手として作り、この spec は `EXPECT=`・`FACTORY=`・`CHECK=` を足して拡張する（別の読み手を作らない。引数がないときの既定の動作を変えない）。所有の記述、タスク 1.4 の内容と `_Depends:`（`block-parser 1.4`）を直した。`tests/spec/spec_version.cmake` はこの spec が所有し、副作用のない変数の定義として `markdown-renderer` が include して再利用できる形にした。`bench/CMakeLists.txt` は `block-parser` の所有で、この spec は追記する側（タスク 8.3 の `_Depends:` に `block-parser 10.4`）。
- **#11 構築関数の順のテスト**：10 個のインラインの拡張どうしの相対の順だけを確かめ、絶対の位置やブロックの拡張との間の位置は確かめない（後の spec が拡張を足しても壊れない）。
- **#12 内部ヘッダ・名前・`user_data`**：`link_common_internal.h` を、steering が許す同じ層（`src/syntax/`）の内部ヘッダとして、Allowed Dependencies・Dependency direction・RawHtml・タスク 5.1 の書き方を揃えた（include するのは `link_common.c`、`link.c`、`image.c`）。生の HTML の判定の関数は、`inline.h` で宣言するのに `scan.h` の名前の空間 `amark_scan_*` を使っていたので、`amark_scan_raw_html` から **`amark_inline_scan_raw_html`** に改名した（引数と規則は変えない。`markdown-renderer` はこの名前で参照する）。上の 2026-11-25 の記録の `amark_scan_raw_html` は旧名。`amark_inline_special` と `amark_inline_node_traits` に v1 のスロットの慣例どおり末尾の `void *user_data` を加えた（エンジンは読まない）。
- **#14 Revalidation Triggers の群**：独立した継ぎ目ごとに、A（エンジンと公開 API）、B（準拠ゲート：`spec_version.cmake`、`gen_examples.cmake` の引数、`amark_test_inline`、`html-renderer` の出力の表、`AMARK_REQUIRE_SPEC_TESTS`）、C（GFM 相当の組み合わせのライブラリ：`amark_test_gfm`、部品の並び、検証用の役割の番号、Naos に依存しない取り決め、上流の検証用ライブラリ）に分けた。spec は分けない。

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — インライン構文と付録の手順
- [spec.txt 0.31.2](https://raw.githubusercontent.com/commonmark/commonmark-spec/0.31.2/spec.txt) — 準拠テストのデータ（CC-BY-SA 4.0）
- [cmark 0.31.1 src/inlines.c](https://github.com/commonmark/cmark/blob/0.31.1/src/inlines.c) — 手順と線形の仕組みの基準
- [cmark 0.31.1 test/pathological_tests.py](https://github.com/commonmark/cmark/blob/0.31.1/test/pathological_tests.py) — 病的な入力
- [GitHub Flavored Markdown Spec](https://github.github.com/gfm/) — 取り消し線、拡張オートリンク、表、タスクリスト、タグフィルタ、脚注の振る舞いの基準（テストデータは写さない）
- `.kiro/specs/block-parser/design.md`、`.kiro/specs/syntax-tree/design.md`、`.kiro/specs/html-renderer/design.md`、`.kiro/specs/foundation/design.md`
