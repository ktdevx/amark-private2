# Research & Design Decisions

## Summary
- **Feature**：`block-parser`
- **Discovery Scope**：New Feature（greenfield。`foundation` と `syntax-tree` の上に作る。`inline-parser`、`html-renderer`、`markdown-renderer` と利用者の拡張が使う継ぎ目を定義する）
- **Key Findings**：
  - CommonMark のブロックの解析（cmark の `blocks.c` の手順）は、「コンテナの継続の照合」「開始の条件を順に試す」「遅延継続」「葉への行の追加」「閉鎖」の 5 つの段階に分けられ、個別の構文の知識は各段階のコールバックと少数の性質（コンテナかどうか、段落の類か、行をそのまま受け入れるか、空行を数えるか）に閉じ込められる。エンジンはこの 5 段階と性質だけを知ればよい。
  - 構文間で共有される処理（段落の先頭からのリンク参照定義の取り出し、段落の解釈し直し、ラベルの正規化、リンクの宛先とタイトルと HTML のタグの走査、エスケープと文字参照の解読）は、組み込みの構文どうしの近道になりやすい。これらを公開 API にすることで、setext 見出し、GFM 相当の表、`inline-parser` のリンクが同じ部品を使える。
  - 葉のブロックの構文トークンを「閉じるときに、保存した行の記録から記録する」方式にすると、解釈し直し（setext 見出し、表）とリンク参照定義の取り出しでトークンを移す必要がなくなる。コンテナの接頭辞だけを行ごとに即座に記録すればよい。
  - 遅延継続行の印（長さ 0 のトークン）を照合できなかった各コンテナに付けるモデル（`syntax-tree` の決定）は、深さ D の入れ子で 1 行あたり O(D) のトークンを作る。入れ子の深さの上限を固定すれば全体は O(n·D) で線形になる。上限は必須の設計要素である。

## Research Log

### CommonMark 0.31.2 のブロック構造の手順
- **Context**：要件 3、7。汎用のエンジンが持つべき段階と、構文ごとに分けられる部分を確定するため。
- **Sources Consulted**：CommonMark Spec 0.31.2 の付録「A parsing strategy」（Phase 1: block structure）、2.1–2.3 節、4 章、5 章。cmark の `blocks.c`（`S_process_line`、`check_open_blocks`、`open_new_blocks`、`add_text_to_container`、`finalize`、`S_can_contain`、`resolve_reference_link_definitions`）。
- **Findings**：
  - 1 行の処理は次の順：(1) 開いているブロックを外側から照合する（`check_open_blocks`）。(2) 最後に照合したブロックが行をそのまま受け入れる葉（コードブロック、HTML ブロック）でなければ、開始の条件を試す（`open_new_blocks`）。コンテナが開いたら同じ行の残りでさらに試す。(3) 残りを最も内側のブロックに加える。最も内側が段落で、照合できなかったコンテナがあり、新しいブロックが開かず、空行でなければ遅延継続とする。
  - cmark の開始の条件の順：引用、ATX 見出し、フェンス、HTML ブロック、setext 見出し、thematic break、リスト項目、インデントされたコード。setext は thematic break より前（段落の下の `---` は見出し）。インデントされたコードは「段落の遅延継続になりうる」とき（最も内側が段落）には開かない。
  - 段落の中断：最後に照合したブロックが段落のとき（`interrupting`）、開始の条件のうち中断できるものだけが成立する。順序付きリストは 1 から始まるときだけ、空の項目は中断できない、HTML ブロックの種類 7 は中断できない、インデントされたコードは中断できない。
  - 新しいブロックを開くときは、親が子を受け入れられるまで閉じながら上へたどる（`S_can_contain`）。リスト項目の開始は、親が同じ種類のリストでなければ新しいリストを開いてから項目を開く。
  - 空行の扱い：`last_line_blank` は、引用、見出し、thematic break、フェンスのコードブロック、最初の行の空の項目では立てない。タイトかどうかは、リストを閉じるときに、項目の間の空行と項目の中の子の間の空行から決める。
  - リンク参照定義は、段落が閉じるとき（と setext の下線が来たとき）に段落の先頭から取り出す（0.30 以降、cmark は `resolve_reference_link_definitions` で setext の判定の前に行う）。すべての行が定義になった段落は消える。`[foo]: /url\n===` の `===` は段落になる。
  - タブは 4 列のタブストップとして、ブロック構造の判定の文脈でだけ扱う（`S_advance_offset` の `partially_consumed_tab`）。
- **Implications**：エンジンは 5 段階と、構文の性質の印（コンテナ、段落の類、行をそのまま受け入れる、空行を数えない、受け皿）だけを知る。開始の順は数値の優先度で表す。「段落の類」の性質（遅延継続、中断の対象、先頭の消費、解釈し直し）をまとめて 1 つの印にする。

### ロスレスな記録と `syntax-tree` の構文トークン
- **Context**：要件 2、9。`markdown-renderer` がバイト単位で同じ出力を得るために、どのトークンをどのノードに持たせるか。
- **Sources Consulted**：`.kiro/specs/syntax-tree/design.md`（TokenStore、TokenCheck、EditTracking、9 種類のトークン、`CONTAINER_PREFIX`、`PARTIAL_TAB`、役割の番号）、`.kiro/specs/markdown-renderer/brief.md`（タブ、CRLF、末尾の空白、遅延継続、NUL、BOM の往復）。
- **Findings**：
  - トークンは所有するノードとソース上の順序を別々に持つ。コンテナの接頭辞はコンテナが `CONTAINER_PREFIX` を付けて持つ。遅延継続は長さ 0 の `LAZY_CONTINUATION` をコンテナが持つ。役割の番号は所有するノードの型を扱う構文が決める。
  - `amark_document_check_tokens` は、抜け（GAP）、重なり（OVERLAP）、部分的なタブの列の誤り（TAB_COLUMNS）を検出する。1 行目の列は BOM の後から数える。
  - 部分木を動かしたとき、その部分木のトークンが「自分の表記だけ」になることが `markdown-renderer` の前提である（`RELOCATED` のノードは自分のトークンを新しい接頭辞で出す）。
- **Implications**：
  - 空行の空白と改行は、その空行を含む最も内側のブロックが持つ（要件 9.6）。照合で消費した接頭辞はコンテナが持つ。葉の表記（インデント、マーカー、改行）は葉が持つ。段落の行の間の改行はインラインの入力に含め、インラインのノード（softbreak など）が持つ。
  - エンジンが記録するトークン（空行、遅延継続、BOM、受け皿がないときの残り）は、どの構文のノードにも付きうるため、役割の番号の範囲 0xFF00–0xFFFF をエンジン用に予約する。構文は 1–0xFEFF を使う。これは `syntax-tree` の「役割は構文が決める」の規約の範囲内の取り決めである。

### インラインの解析との継ぎ目
- **Context**：要件 6、8。`inline-parser` が、ブロックが確定した中身を、元の位置の対応を失わずに解析するため。
- **Sources Consulted**：cmark の `inlines.c`（`cmark_parse_inlines`、`cmark_parse_reference_inline`、`subject`）、`.kiro/specs/inline-parser/brief.md`、CommonMark Spec 4.7（リンク参照定義）、6.3（リンク）、6.6（生の HTML）、2.4–2.5（エスケープと文字参照）。
- **Findings**：
  - cmark はインラインの入力を「行を連結した 1 つの文字列」として持ち、位置の対応を失う。Amark では、ソース上の区間の並び（コンテナの接頭辞や行頭の空白を除いた論理的なテキスト）として渡せば、インラインの各トークンがソースのオフセットを持てる。
  - リンクの宛先・タイトル・ラベルの文法は、リンク参照定義（ブロック）とインラインのリンクで同じ。HTML の開始タグと終了タグの文法は、HTML ブロックの種類 7 と生の HTML で同じ。これらはどちらも複数行にまたがりうる（リンク参照定義のタイトル、インラインのリンク）。
  - リンク参照定義の表は、すべてのブロックが閉じた後にしか完成しない（後ろの定義も参照できる）。したがってインラインの解析は終了の指示の後に行う。
- **Implications**：
  - 区間の並びの抽象（`amark_text_input_t`）を公開し、走査の関数（ラベル、宛先、タイトル、HTML のタグ）と、エスケープと文字参照を解読しながらトークンを記録する関数を、この上に作って公開する。ブロックが最初に必要とするので、この spec が持つ。
  - 委譲は関数表（`amark_inline_delegate_t`）で行い、既定の委譲先を差し替え可能にする。この spec は単純な委譲先（テキストと softbreak）を提供し、インラインのエンジンがない段階でもロスレス性の検証まで完結させる。`inline-parser` は既定の委譲先を自分のエンジンに変える。

### ラベルの正規化と Unicode の case fold
- **Context**：要件 8.3。foundation の design は「case fold は必要とする spec が生成手順の上に追加する」としている。
- **Sources Consulted**：CommonMark Spec 4.7（「Unicode case fold」、例：`[ẞ]` と `[SS]` が一致する）、Unicode 17.0.0 の `CaseFolding.txt`（状態 C、F、S、T）、cmark の `cmark_utf8proc_case_fold`、`.kiro/specs/foundation/design.md`（UnicodeTableGen の拡張の規則）、`.kiro/specs/foundation/research.md`。
- **Findings**：
  - CommonMark の例は完全な case fold（C と F。ẞ → ss）を要求する。単純な case fold（C と S）では一致しない。T（トルコ語）は使わない。
  - F の対応は最大 3 つの符号位置になる。
- **Implications**：`block-parser` が foundation の生成スクリプトに `CaseFolding.txt` の読み取りを足し、`src/runtime/case_fold_tables.inc` と `amark_unicode_case_fold()`（`unicode.h`）を追加する。`inline-parser` は同じ関数を使う（追加しない）。

### 文字参照の表
- **Context**：要件 8.7。リンク参照定義の宛先とタイトル、フェンスの情報文字列は文字参照を解読する。foundation の design は HTML の文字参照の表も「必要とする spec が追加する」としている。
- **Sources Consulted**：CommonMark Spec 2.5（HTML5 の名前付き文字参照、数値参照の桁数、0 とサロゲートと範囲外は U+FFFD）、WHATWG HTML の `entities.json`。
- **Findings**：CommonMark はセミコロンで終わる名前だけを認める（セミコロンなしの古い名前は認めない）。1 つの名前は最大 2 つの符号位置になる。数値参照は 10 進 1–7 桁、16 進 1–6 桁。
- **Implications**：ブロックが最初に必要とするので、この spec が `tools/gen_entity_table.py`、`src/runtime/entity_tables.inc`、`src/runtime/entity.c`、公開ヘッダ `include/amark/entity.h` を追加する。表は名前の昇順で、二分探索する。`inline-parser` は同じ関数を使う。

### 計算量の危険のある箇所
- **Context**：要件 12。
- **Sources Consulted**：cmark の過去の性能の問題（深い入れ子のリスト、リンク参照定義の再走査、`S_ends_with_blank_line` の再帰）、commonmark.js の pathological tests、markdown-it の `maxNesting`。
- **Findings**：
  - 遅延継続行と、入れ子の深いリストの空行は、1 バイト以下の入力で O(D) の照合を起こす。上限 D を固定すれば O(n·D)。
  - 段落の先頭の消費を、行が増えるたびに先頭から走査し直すと O(n²) になる（表の拡張は行ごとに判定する）。消費の処理に再開の状態を持たせれば、各行の走査は定数回で済む。
  - ラベルの表をハッシュ表にすると、固定のハッシュ関数では衝突を作る入力で O(n²) になる。平衡木なら最悪でも O(L log n)。
  - 末尾が空行かどうかの判定を部分木の再帰で行うと、深い木で再帰が深くなり、繰り返すと二次になる。エンジンが行ごとに印を保てば定数時間。
- **Implications**：入れ子の深さの既定の上限を 128 にする（オプションで 1–65535）。段落の先頭の消費は再開の状態を持つ。ラベルの表は AVL 木。末尾の空行の印はエンジンが保つ。計算量は INTERNAL のカウンタ（照合の回数、トークンの数、消費の走査の行数）で、入力の大きさを倍にしたときの比で確かめる。

### 出力のスロットを足す下流の spec との関係
- **Context**：`html-renderer` は並行して spec を書いており、組み込みのノードの HTML 出力のスロットを定義する。`markdown-renderer` は後で Markdown 出力のスロットを足す。
- **Sources Consulted**：`.kiro/steering/roadmap.md`（Boundary Strategy：1 つの構文拡張が、ノード型、パース、HTML 出力、Markdown 出力をまとめる）、`.kiro/steering/structure.md`（構文ごとに 1 ファイル）、`.kiro/specs/syntax-tree/design.md`（`amark_extension_t` のスロットの一覧、`amark.core` が組み込みの型を持つ）。
- **Findings**：組み込みのノード型は `amark.core` が持つため、構文ごとの拡張はスロットだけを持つ。見出し（ATX と setext）とコードブロック（インデントとフェンス）は、2 つの構文が同じノード型を作る。document 型は構文ではなくエンジンが作る。
- **Implications（`html-renderer` の design の確定後に更新）**：`html-renderer` は、組み込みの 19 のノード型の HTML 出力を 1 つの構文拡張 `amark.core.html`（`amark_html_core_extension()`）に置き、`commonmark.*` の構文の拡張は組み込みのノード型の HTML のスロットを持たないと決めた（出力は構文ではなくノード型に従う）。これにより、見出しやコードブロックのように 2 つの構文が同じ型を作る場合の重複の問題は起きない。この spec の構築関数は `amark.core.html` を `amark.core` の直後に加え、`markdown-renderer` の `amark.core.markdown` も同じ位置に加えられる形にする。構文ごとのファイルの静的なスロットの一覧は残し、特定の構文だけが独自の出力を必要とする場合にそこへ足せるようにする。GFM 相当の検証用の構文は、`html-renderer` の `amark_test_html_gfm` のノード型の記述子を使い、パースのスロットだけを持つ。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 段階のコールバックを持つ汎用エンジン（採用） | エンジンは 5 段階と性質の印だけを知り、構文は開始・継続・行の受け入れ・閉鎖のコールバックを持つ | CommonMark と GFM 相当の構文を同じ API で表せる。組み込みの構文を差し替えられる | 段落の類の扱い（遅延、中断、消費、解釈し直し）を性質としてまとめる必要がある | cmark の手順を一般化したもの |
| 固定のコア ＋ フック（cmark-gfm 方式） | CommonMark の構文はコアに固定し、拡張はフックで割り込む | 実績がある | 表や脚注の割り込みで歪みが出る。roadmap で不採用 | — |
| 文法（PEG など）による宣言的な定義 | 構文を文法として登録し、エンジンが解釈する | 宣言的 | コンテナと遅延継続は文脈依存で、文法では素直に表せない。計算量の保証が難しい | 不採用 |
| 構文ごとに全行を走査する多段のパス | 構文ごとに入力全体を走査する | 単純 | 構文の数に比例して遅い。逐次の入力に向かない | 不採用 |

## Design Decisions

### Decision: 葉の構文トークンは閉じるときに行の記録から記録する
- **Context**：要件 4.4、4.5、9。setext 見出しと表は段落を後から別のブロックに変え、リンク参照定義は段落の先頭の行を別のノードにする。
- **Alternatives Considered**：
  1. 行が来るたびに段落にトークンを記録し、解釈し直すときにトークンを移す（`amark_node_transfer_tokens`）
  2. 行の記録（ソース、開始、内容の開始、終わり、改行の長さ、部分的なタブの残り）だけを保存し、閉じるときに記録する
- **Selected Approach**：2。コンテナの接頭辞と遅延継続の印は、行を処理するときに即座にコンテナへ記録する。葉は `amark_block_append_line` で行の記録を保存し、閉じるときに自分のトークンを記録してインラインの入力を登録する。
- **Rationale**：解釈し直しと先頭の消費が「行の記録の移動」だけで済む。トークンの移動や分割が不要になる。
- **Trade-offs**：開いている葉の行の記録をメモリに持つ（入力の大きさに比例。ソースはどのみち保持する）。
- **Follow-up**：行の記録から記録したトークンが、行ごとに即座に記録した場合と同じになることを、ロスレス性の検証で確かめる。

### Decision: 空行はエンジンが扱い、構文拡張にしない
- **Context**：要件 3.5、7.1、7.2、9.6。brief は空行を組み込み拡張の一覧に含めている。
- **Alternatives Considered**：
  1. 空行を構文拡張にする（取り除けるようにする）
  2. 空行はエンジンが判定し、空白と改行のトークンを最も内側のブロックに記録する
- **Selected Approach**：2。
- **Rationale**：空行はノードを作らず、すべてのコンテナの継続の判定とタイトの判定に関わる。取り除けるようにすると、コンテナの照合とロスレス性が壊れる。CommonMark 4.9 節も空行を「ブロックの間で無視されるもの」として扱っている。
- **Trade-offs**：brief の一覧の 1 項目がエンジンの責務になる。要件 7.2 に明記した。

### Decision: 開始の順は数値の優先度で決める
- **Context**：要件 3.2、4.1、7.4。setext は thematic break より前、インデントされたコードは最後など、開始の順に意味がある。
- **Alternatives Considered**：
  1. 構文セットの順（拡張の並び）で試す
  2. ブロック構文ごとの数値の優先度で試し、同じ値は構文セットの順
- **Selected Approach**：2。組み込みの優先度は 100 刻み（引用 100、ATX 200、フェンス 300、HTML 400、setext 500、thematic break 600、リスト項目 700、インデントされたコード 800）。
- **Rationale**：利用者が拡張を構文セットのどこに足しても、意図した開始の位置に入れられる。構文セットの順は、後処理と段落の先頭の消費とレンダラの解決に使う。
- **Trade-offs**：優先度の値が公開の契約になる。

### Decision: 段落の先頭の消費は再開できるスロットにする
- **Context**：要件 4.5、8.1、12.3、13.1。リンク参照定義の取り出しを、setext 見出しと表が明示的に行わせる必要がある。
- **Alternatives Considered**：
  1. 段落の閉鎖の中でリンク参照定義の構文を直接呼ぶ（近道）
  2. 段落の先頭の消費のスロット（`AMARK_SLOT_PARAGRAPH_CONSUMER`）を作り、`amark_block_settle_paragraph` で実行する。消費の処理は段落ごとの再開の状態を持つ
- **Selected Approach**：2。結果は「n 行を消費した」「まだ決まらない（状態を保存）」「消費しない」の 3 つ。最終の指示（`final`）では「まだ決まらない」を返さない。
- **Rationale**：内部の近道を作らずに、GFM 相当の脚注の定義などの別の消費の処理も同じ仕組みに乗れる。再開の状態により、表の拡張が行ごとに消費を行わせても線形に収まる。

### Decision: 区間の並びのテキストと走査の関数を公開する
- **Context**：要件 6.2、8.7、4.9。
- **Alternatives Considered**：
  1. 段落の中身を連結した文字列として渡し、位置の対応表を別に持つ
  2. ソース上の区間の並び（`amark_text_input_t`）として渡し、論理的な位置からソースのオフセットを引ける形にする
- **Selected Approach**：2。走査（ラベル、宛先、タイトル、HTML のタグ）と、解読しながらトークンを記録する関数を、`amark_text_input_t` の上に公開する。
- **Rationale**：連結した文字列を作らずに済み、トークンはそのままソースのオフセットを持てる。リンク参照定義とインラインのリンクが同じ実装を使う。

### Decision: 断片のパースはこの spec が持ち、別の文書でパースして取り込む
- **Context**：要件 10。roadmap と markdown-renderer の brief は、断片をパースして木に差し込む API の担当を design で決めるとしている。
- **Alternatives Considered**：
  1. `markdown-renderer` が持つ
  2. `syntax-tree` が持つ（パーサに依存できないため不可）
  3. この spec が持ち、挿入先と同じ構文セットの一時的な文書でパースし、取り込んで挿入する
- **Selected Approach**：3。インラインの文脈は、一時的な段落の中身として委譲先で解析する。挿入は `syntax-tree` の取り込みで行う。
- **Rationale**：パースの規則と委譲を知っているのはパーサである。`syntax-tree` の「取り込み時にソースを複製する」モデルにより、元の表記つきで差し込める。
- **Trade-offs**：複数の兄弟のノードを 1 回の操作で、同じソースの複製を共有して、全か無かで取り込む関数が `syntax-tree` にない。**上流の変更が必要**：`amark_node_import_children`（下記の Upstream Changes）。

### Decision: 既定の構文セットは CommonMark の便宜の関数で作り、文書に所有させる
- **Context**：要件 1.2、11.3。文書は構文セットを参照するだけで所有しない（`syntax-tree`）。構文セットは文書より長く生きなければならない。
- **Alternatives Considered**：
  1. `amark_parser_new` が NULL を受けたら内部で CommonMark の構文セットを作る（parse のレイヤーが syntax のレイヤーを参照してしまう）
  2. 静的な `const` の CommonMark の構文セット（凍結した構造を静的に初期化する必要があり、tree のレイヤーが syntax のレイヤーを知ることになる）
  3. `src/syntax/commonmark.c` の便宜の関数（`amark_parse_commonmark`、`amark_parser_new_commonmark`）が構文セットを作り、所有権をパーサに渡し、パーサが文書に渡す
- **Selected Approach**：3。所有権を受け取るパーサの生成の入口が構文セットの所有権を受け取り、文書へ渡す（2026-11-25 の改訂で、渡す時点を終了時からパーサの生成の最後に移した。横断レビュー r1 で、印のフラグ `AMARK_PARSE_TAKE_SYNTAX_SET` をやめ、非 `const` の構文セットを受け取る別の入口 `amark_parser_new_take_syntax_set` にした。Revision Notes を参照）。
- **Rationale**：レイヤーの方向（syntax → parse → tree）を守れる。グローバルな可変状態を増やさない。
- **Trade-offs**：便宜の関数は呼び出しごとに構文セットを作って凍結する（拡張は十数個で、費用は小さい）。多数の文書をパースする利用者は、構文セットを 1 つ作って共有する。**上流の変更が必要**：`amark_document_take_syntax_set`。

### Decision: ラベルの表は AVL 木
- **Context**：要件 8.4–8.6、12.3。
- **Selected Approach**：正規化したラベルのバイト列（長さ、内容）の順の AVL 木。最初の登録だけを残す。
- **Rationale**：決定的で、衝突を狙った入力でも O(L log n)。グローバルな乱数の種を持たない規約（foundation）と両立する。

### Decision: 入れ子の深さの上限を超えたコンテナは開かない
- **Context**：要件 12.1、12.2。
- **Alternatives Considered**：1. `AMARK_ERROR_LIMIT_EXCEEDED` で失敗する　2. 上限より深いコンテナを開かず、内容として扱う
- **Selected Approach**：2。エンジンは、深さが上限に達したら、コンテナの印を持つブロック構文の開始を試さない。
- **Rationale**：信頼できない入力でも結果の文書が得られる（劣化は上限より深い部分だけ）。CommonMark の例の入れ子の深さは 10 未満で、既定の 128 は準拠に影響しない。

## Synthesis Outcomes
- **Generalization**：
  - setext 見出し、表、リンク参照定義、脚注の定義は、どれも「段落の類のブロックの行を、別のノードに渡す」操作である。行の記録、解釈し直し、先頭の消費の 3 つの操作に一般化した。
  - リンク参照定義の表と、脚注の定義の表は、同じ「正規化したラベルで引ける表」に一般化した（`amark_label_map_t`）。
  - ブロックの中身のインラインの入力、リンク参照定義の走査、断片のインラインの文脈は、どれも「区間の並びのテキスト」に一般化した（`amark_text_input_t`）。
- **Build vs. Adopt**：
  - 外部の依存は持たない（steering）。cmark のアルゴリズムは採り入れるが、コードは使わない（ノードのモデルとトークンの記録が根本的に異なり、ライセンスの表記も分けたい）。
  - Unicode の case fold と HTML の文字参照は、公式のデータ（UCD、WHATWG の `entities.json`）から生成する。
- **Simplification**：
  - 空行を構文拡張にしない（上記）。
  - リストとリスト項目は 1 つの拡張（`commonmark.list_item`）にまとめる。リストはリスト項目の並びとして定義され（5.3 節）、リストのブロック構文は開始の条件を持たない。
  - エンジンの内部の状態を、ノードの `user_data` に置かない（利用者のものなので）。パースの間のデータは、ノードのポインタを鍵とする内部の表に置く。

## Upstream Changes
- **`syntax-tree`**：
  1. `amark_error_t amark_document_take_syntax_set(amark_document_t *document, amark_syntax_set_t *set)`：文書がすでに使っている、ヒープに作られた凍結済みの構文セットの所有権を受け取り、`amark_document_free` でノードの後に解放する。静的な core の構文セット、別の構文セット、2 回目の呼び出しは `AMARK_ERROR_INVALID_ARGUMENT`。
  2. `amark_error_t amark_node_import_children(amark_node_t *parent, amark_node_t *before, const amark_node_t *source_parent)`：別の文書（または同じ文書）の `source_parent` の子をすべて、1 回の操作で取り込み、`parent` の `before` の前（NULL なら末尾）に順に挿入する。ソースの複製は呼び出し全体で 1 回だけ行う。包含の規則の検査を先に行い、確保の失敗を含めて全か無か（強い保証）。挿入は通常の挿入と同じく編集を記録する。
  3. `extension.h` にこの spec のスロットの種類の名前と番号（0x0100–0x0103）を追記する（`syntax-tree` の design が予定している追記）。
- **`foundation`**（foundation の design が予定している「必要とする spec が追加する」作業）：
  - `tools/gen_unicode_tables.py` に `CaseFolding.txt` の読み取りを足し、`src/runtime/case_fold_tables.inc` を出力する。`include/amark/unicode.h` に `amark_unicode_case_fold()` を足す。
  - `tools/gen_entity_table.py`、`src/runtime/entity_tables.inc`、`src/runtime/entity.c`、`include/amark/entity.h` を追加する。
  - `.gitattributes` に、CommonMark spec の例を写したテストデータの `export-ignore` を足す。
- **スロットの種類の番号の範囲**：`block-parser` 0x0100–0x01FF、`inline-parser` 0x0200–0x02FF、`html-renderer` 0x0300–0x03FF（`html-renderer` の design と一致）、`markdown-renderer` 0x0400–0x04FF。`syntax-tree` の `extension.h` の注記に範囲の表を加えることを推奨する。
- 上記の `syntax-tree` への変更は、2026-11-25 に `syntax-tree` の spec に反映された（Revision Notes を参照）。

## Risks & Mitigations
- 公開のブロック構文 API が大きくなる — 組み込みの構文が使う操作だけに絞り、GFM 相当の 3 つの検証用の構文で不足と過剰を確かめる。
- 行の記録から記録したトークンと、即座に記録したコンテナの接頭辞の順序が崩れる — トークンはノードごとにソース上の順で整列される（`syntax-tree`）ので順序の問題はない。被覆はすべてのテストとファジングで検証する。
- 構築関数が `html-renderer` の公開関数に依存する — roadmap の順で `html-renderer` が先に実装されるので許される。テストは構築関数の拡張の順を確かめる。
- 後処理がインラインの入力を持つノードを解放すると、内部の表に古いポインタが残る — 後処理の契約として、`amark_parse_discard_inline_input` / `amark_parse_move_inline_input` を呼ぶことを要求し、テストで確かめる。
- CommonMark spec の例の期待値を手で書く量が多い — 期待値はブロックの構造だけのダンプ（インラインは単純な委譲先のテキスト）にし、生成の補助で形式を検査する。

## Revision Notes

### 2026-11-25：`inline-parser` からの上流の変更の依頼と、`syntax-tree` の更新の反映
- **Context**：`inline-parser` の設計で、この spec の API とテストの補助に足りない点が見つかり、`inline-parser` の design の Integration & Upstream Changes に 4 つの変更の依頼が書かれた。`inline-parser` は回避策をとらず（`inline-parser` の 13.5）、この spec を更新する約束（Boundary Context の Adjacent expectations）に従って反映する。あわせて、この spec が依頼した `syntax-tree` の変更（`amark_document_take_syntax_set`、`amark_node_import_children`、スロットの種類の範囲の表）が `syntax-tree` の spec に反映されたので、この spec の記述をそれに揃えた。承認済みの要件・設計・タスクに追加する形で行い、既存の受け入れ基準の意味は変えない。
- **宛先の括弧の入れ子の上限（12.6。依頼 1）**：CommonMark 0.31.2 は、宛先の括弧の入れ子に実装が上限を設けることを許し、少なくとも 3 段を求める。上限がないと、走査の深さの数が入力とともに増え、リンク参照定義（この spec）とインラインのリンク（`inline-parser`）で振る舞いの根拠が文書化されない。cmark と同じ 32 を `scan.h` の `AMARK_SCAN_MAX_LINK_PAREN_DEPTH` として公開し、超えたら宛先としない。走査は同じ関数なので両方で同じ値になる。`inline-parser` の 11.3（許された制限の文書化）もこの値を参照する。検討した代替：上限をオプションにする案は、spec の例を満たす値が 1 つで足り、ブロックとインラインで値が食い違う危険を生むので採らなかった。
- **エンジンの役割の範囲の分け方（9.7。依頼 2）**：この spec は 0xFF00–0xFFFF を「エンジン」の範囲としていたが、インラインのエンジン（`inline-parser` の `UNMATCHED`、`FALLBACK_BREAK`）も同じ種類の役割を必要とする。値が衝突すると `markdown-renderer` が表記を区別できないので、ブロックのエンジン 0xFF00–0xFF0F、インラインのエンジン 0xFF10–0xFF1F に分け、0xFF20–0xFFFF を予約した。あわせて、組み込みの構文の役割（ブロック 0x0001–0x00FF、インライン 0x0100–0x01FF）も同じ注記に書き、1 か所で全体の割り当てが分かるようにした。既存のブロックのエンジンの役割の値（0xFF01–0xFF05）は変えない。
- **検証用の構文の公開（13.5、13.6。依頼 3）**：`inline-parser` の GFM 相当の検証（`inline-parser` の 13.1–13.3）は、この spec の表・脚注の定義・タスクリストの検証用の構文をそのまま使う。同じものを `inline-parser` のテストに複製すると、ブロックのフックの検証と HTML の検証が別々の実装を見ることになるので、`tests/parse/gfm_block_ext.{h,c}` を INTERFACE ライブラリ `amark_test_parse_gfm`（`amark_test_tree`、`amark_test_html_gfm` と同じ方式）として公開した。脚注の参照の解決のため、拡張の状態の表を返す `amark_test_gfm_footnote_definitions(ctx)` を足した。表のセルの中の `\|` は、GFM では行をセルに分ける前に `|` に戻され、コードスパンの中でも `|` になる。インラインのエンジンはエスケープを文字としてしか知らないので、ブロック側でセルの区間から `\` を除いて `MARKER` のトークンとして記録する形にした（区間の隙間をトークンが覆うので被覆は保たれる）。検討した代替：インラインの検証用拡張で `\|` を特別に扱う案は、コードスパンの中で効かないため採らなかった。
- **文書を返す検査の補助（14.5。依頼 4）**：`inline-parser` の例の照合（`amark_test_check_example`）は、被覆・不変条件・逐次の一致の検査の上に HTML の比較を重ねる。マクロだけの形では文書を受け取れないので、`amark_test_parse_check` を検査の後に一括の文書を返す関数とし、期待のダンプとの照合は `AMARK_CHECK_PARSE` のマクロに残した。構文セットを指定しないときに補助が作るテスト用の構文セットは、`AMARK_PARSE_TAKE_SYNTAX_SET` で返す文書に所有させ、寿命の問題を作らない。
- **`syntax-tree` の更新との整合**：
  - `amark_document_take_syntax_set` は、静的な構文セット・文書が使っていない構文セット・2 回目の受け渡しを `AMARK_ERROR_INVALID_ARGUMENT` で拒否し、確保をしない。これまでの設計は終了の最後に受け渡していたが、静的な構文セットが渡された誤りがパースの全体の後まで分からず、失敗の後の所有権の扱いも 2 通りになる。受け渡しをパーサの生成の最後（他の確保と拡張の状態の初期化の後）に移し、「生成が失敗したら所有権は呼び出し側に残り、成功したら文書のもの」を 1 か所で決めた。パーサは所有の印を持たなくなり、パーサの解放は文書の解放だけで構文セットも解放する。外から見える契約（便宜の関数の文書が構文セットを保持する、生成の失敗で所有権が残る）は変わらない。
  - `amark_node_import_children` は根を取り込み元の親にでき、型の対応付け・包含の検査・確保をつなぎ込みの前に終え、記録（親に `CHILDREN`、各複製に `RELOCATED`）を定めている。断片のパースはこれをそのまま使い、独自の検査や巻き戻しを持たないことを design に明記した。子のない断片は何もせず成功する。
  - スロットの種類の範囲の表は `syntax-tree` の `extension.h` に定数（`AMARK_SLOT_KIND_BLOCK_PARSER_FIRST` / `_LAST`）として置かれたので、この spec のスロットの種類はその節の下に追記する。
  - `syntax-tree` のタスク（3.4、5.5）がこれらの操作を実装するので、この spec のタスク 1.2、1.3 は実装の作業から、パーサと断片のパースが頼る契約を公開 API で確かめる作業に変えた（タスクの番号と依存は保つ）。
- **Implications**：Revalidation Triggers に、宛先の括弧の入れ子の上限、エンジンの役割の範囲の分け方、`amark_test_parse_gfm` と `amark_test_parse_check` の契約を加えた。依頼 1–4 はこの spec のタスク（1.1、1.4、3.3、6.2、7.10、9.1–9.3、10.2）で実装する。この spec は `inline-parser` より先に実装されるので、`inline-parser` のタスク 1.2、1.3 は実装済みのものを確かめる作業になる。

### 2026-11-25：`markdown-renderer` からの上流の変更の依頼の反映と、`inline-parser` との所有の明確化
- **Context**：`markdown-renderer` の設計で、HTML ブロックとして再パースされるかの判定（HTML ブロックのリテラルの検査、段落の行頭の保護）に、パーサと同じ HTML ブロックの開始と終わりの条件とタグの走査が要ると分かり、`markdown-renderer` の design の Integration & Upstream Changes に `block-parser` への 2 つの依頼が書かれた。`markdown-renderer` は回避策をとらない方針なので、この spec を更新する。承認済みの要件・設計・タスクに追加する形で行い、既存の受け入れ基準の意味は変えない。
- **バイト列の HTML ブロックの条件とタグの走査（7.7、7.8。依頼 1）**：これまでタグの走査は `amark_text_input_t` の上の版だけで、HTML ブロックの 7 種類の条件は `commonmark.html_block` の中に閉じていた。レンダラは文書のパースの文脈も区間の並びも持たず、リテラルの行を見るだけなので、`amark_scan_html_block_start(line, length, interrupting, out_kind)`、`amark_scan_html_block_end(kind, line, length)`、`amark_scan_html_open_tag_bytes`、`amark_scan_html_closing_tag_bytes` を `scan.h` に公開した。規則を 2 か所に書くと、パーサとレンダラの判定が食い違ったときに往復が壊れる（レンダラが「HTML ブロックにならない」と判断した出力が再パースで HTML ブロックになる）。そこで `scan_html.c` の内部の走査を、連続したバイト列と区間の並びのどちらからも読める読み取りの上の 1 組の関数にし、公開の関数はすべてその薄い包みとした。`commonmark.html_block` も行の `<` からのバイト列を同じ関数に渡すだけにした。同じ内容に対する両方の版の一致をテストで確かめる（7.8）。
  - 開始の条件の関数は、字下げを除いた行を受け取る。タブを含む字下げの列の計算はコンテナの接頭辞の文脈に依存するので、呼び出し側（パーサはブロックの文脈の列、レンダラはリテラルの先頭の空白）に任せた。
  - 終わりの条件の関数は種類 1–5 だけを判定し、種類 6・7 は false とした。6・7 は空行の前で終わり、空行はブロックに含まれないので、「この行で終わる（行を含む）」の 1–5 と意味が違う。同じ関数で空行を true にすると、行を含むかどうかを呼び出し側が種類で分け直すことになる。
  - 検討した代替：（a）レンダラの側で規則を複製する案は、上の食い違いの危険があり、spec の境界の回避策になるので採らなかった。（b）レンダラに 1 区間の `amark_text_input_t` を作らせる案は、`amark_text_input_new` が文書を要求し確保を伴うため、行ごとの判定に向かないので採らなかった。
- **出力の core 拡張の一覧への `amark.core.markdown` の追加（依頼 2）**：設計はもともと、構築関数の「出力の core 拡張の一覧」を 1 行で足せる配列にする予定だった。`markdown-renderer` のタスク 3.7 が、その配列への 1 行と、拡張の順（`amark.core`、`amark.core.html`、`amark.core.markdown`、構文の拡張）を確かめるテストの更新を行う。`commonmark.c` の所有はこの spec のままで、その 1 行の編集だけを `markdown-renderer` が行うことを Boundary Commitments と CommonMarkSet に明記した。`amark_markdown_core_extension()` はこの spec の実装の時点ではまだないので、この spec のタスク 8.1 は `amark.core.html` だけの配列で実装し、タスクは足さない。Revalidation Triggers に `amark_markdown_core_extension()` の名前と登録の規則の変更を加えた。
- **`inline-parser` との所有の明確化**：宛先の括弧の入れ子の上限、エンジンの役割の範囲の分け方、`amark_test_parse_gfm`、`amark_test_parse_check` は、`inline-parser` の依頼でこの spec が実装する（前の改訂）。`inline-parser` のタスク 1.2、1.3 がこれらを実装の作業として重ねて書いているが、所有者と唯一の実装者はこの spec であり、`inline-parser` の側の記述は `inline-parser` の更新で直す。この spec は Boundary Commitments と Integration & Upstream Changes でそれを明記しただけで、タスクは変えない。HTML のタグの走査のバイト列の版は、`inline-parser` が公開する `amark_scan_raw_html` も使える（重複して実装しない）。
- **Implications**：要件 7.7、7.8 を追加し、design の Scanners（契約、責務、テスト）、Builtin syntaxes の html_block、CommonMarkSet、Boundary Commitments、Revalidation Triggers、Integration & Upstream Changes を更新した。タスクは 1.1（宣言）、3.4（1 つの実装とバイト列の版、HTML ブロックの条件）、7.9（条件の関数を使う）、8.1（`amark.core.markdown` の 1 行は `markdown-renderer` のタスク 3.7 が足すことの注記）を更新した。タスクの番号と依存は変えない。

### 2026-11-25：横断レビュー r1（cross-spec review r1）の反映
- **Context**：全 spec の横断レビューで、この spec に関わる指摘（#5、#7、#8、#10、#14）が出た。approvals は維持し、境界・契約の明確化として反映した。
- **#5 共同で編集するファイルと `foundation` の拡張点**：`bench/CMakeLists.txt` はこの spec が作るが、`inline-parser` が `inline_pathological` を追記する。Boundary Commitments に「共同で編集するファイル」を置き、登録関数 `amark_add_bench(<name> SOURCES ... [INTERNAL])` を定義して、他の spec は 1 行の追記だけにした。最上位の `CMakeLists.txt`（`AMARK_BUILD_BENCH`、`add_subdirectory(bench)`）と `.gitattributes`（`spec_blocks.txt export-ignore`）への追記は、`foundation` の Downstream Extension Points の表の行として載る拡張点で行う（行の追加は `foundation` の spec の作業。この spec はその前提を Integration に書いた）。
- **#7 `amark.h` の include の順**：「`extension.h` の後に」という書き方をやめ、`foundation` が定める正準の順（foundation → tree → render、html → entity、scan、parser、block、inline、commonmark → markdown）のそれぞれの位置に足す、と書き直した（design の Modified Files、タスク 1.1）。
- **#8 `const` を外すキャストの除去**：`amark_parser_new(const amark_syntax_set_t *)` が印のフラグで `amark_document_take_syntax_set(amark_syntax_set_t *)` を呼ぶと、`const` を外すキャストが要る。検討した代替：（a）`amark_parser_new` の引数を非 `const` にする（借りるだけの利用者と静的な構文セット `amark_syntax_set_core()` が使いにくくなる）、（b）`syntax-tree` の受け取りを `const` にする（所有権の移動を `const` で表すのは誤解を招く。`syntax-tree` は非 `const` のまま）、（c）非 `const` の構文セットを受け取る別の入口を設ける。（c）を選び、`amark_parser_new_take_syntax_set(amark_syntax_set_t *, ...)` を追加し、`AMARK_PARSE_TAKE_SYNTAX_SET`（0x02）を削った（0x02 は未知のビットとして拒否）。`amark_parser_new` は `const` で借りるだけになった。所有権の受け渡しは、非 `const` の構文セットを持つ経路（CommonMark の便宜の関数、構文セットを指定しないときの `amark_test_parse_check`）だけで起こる。要件 1.9 を追加した。
- **#10 spec.txt の例の形式のパーサの一本化**：この spec の `tests/syntax/gen_block_cases.cmake` と `inline-parser` の `gen_examples.cmake` が同じ形式を別々に読んでいた。この spec が先に実装されるので、`tests/support/gen_examples.cmake` をこの spec が作り、`inline-parser` が `EXPECT=html`、`FACTORY=`、`CHECK=` を追加で足す形にした。拡張が追加だけで済むよう、データの形式・テストの名前の規則（`ex<4 桁の番号>_<節>`。`inline-parser` の design と同じ）・基本の ARGS（`EXPECT=dump`、`DUMP_FLAGS=`）・基本の経路の出力を固定し、`FACTORY` / `CHECK` / `EXPECT=html` を予約した（未実装の間は構成の失敗）。spec の一部を写す `spec_blocks.txt` のために、開始の行の語 `n=<N>` で番号を明示できるようにした（spec.txt には現れないので通し番号と矛盾しない）。見出しの行は、foundation の生成の規則でスイートの名前がデータのファイル名から決まるので、テストの名前の節の部分にした。要件 14.6 を追加した。
- **#14 Revalidation Triggers の分割**：独立に変わりうる継ぎ目ごとに、ブロックのエンジンと公開 API、`scan.h` の走査、実行時の文字データ（case fold、文字参照）、CommonMark の構文セットの構築関数、GFM 相当の検証用ライブラリとテストの補助（`gen_examples.cmake` を含む）、ベンチマークの基盤の群に分けた。spec の分割はしない。
- **Implications**：requirements に 1.9、14.6 と Boundary Context の 2 項目を追加した。design の Boundary Commitments、Revalidation Triggers、File Structure Plan、Modified Files、Requirements Traceability、ParserAPI、Fragment、CommonMarkSet、ParseCheck、SyntaxTests、Bench、Integration & Upstream Changes を更新した。タスクは 1.1、1.4、6.2、8.1、8.4、10.4 の記述と `_Boundary:_` / `_Requirements:_` を更新した（依存の順は変わらない）。

### 2026-11-25：横断レビュー r2（cross-spec review r2）の反映
- **ファジング用の補助ライブラリの規則（`foundation` 7.7、7.8）**：`amark_test_parse_gfm` は `inline-parser` の `amark_test_gfm` を通して `inline_parse`（`inline-parser`）と `md_roundtrip`・`md_edit`（`markdown-renderer`）のファジングターゲットに間接にリンクされる。そこで、`amark_test_parse_gfm` は Naos に依存せず、`AMARK_FUZZ` か `AMARK_BUILD_TESTS` が ON なら必ず定義されることを明記した。`tests/CMakeLists.txt` の `add_subdirectory(parse)` は条件なしにし、Naos を使う `amark_test_parse`（`parse_check.c`）、`add_subdirectory(syntax)`、`tests/parse/` の Naos のテストは `if(AMARK_BUILD_TESTS)` の中に置く。design の Modified Files、GfmHookTests、Revalidation Triggers（`amark_test_parse_gfm` の依存 → `inline-parser`・`markdown-renderer` のファジング）を更新し、タスク 1.4、9.3 に反映した（9.3 の完了条件にファジングだけの構成の確認を加えた）。
- **名前の修正**：ScanAPI の目的の記述の `amark_scan_raw_html` を `inline-parser` の現在の名前 `amark_inline_scan_raw_html` に直した。
- **ベンチマークの構成の置き場所**：`AMARK_BUILD_BENCH` と `add_subdirectory(bench)` は最上位の `CMakeLists.txt`（`foundation` の拡張点）に置き、`bench/CMakeLists.txt` は `amark_add_bench` の定義と登録だけを持つ、と Boundary Commitments の「共同で編集するファイル」を書き直した。
- approvals は維持した。

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — ブロック構造、タブ、不正な文字、リンク参照定義、付録の parsing strategy
- [cmark](https://github.com/commonmark/cmark) — `blocks.c`、`inlines.c`、`utf8.c`（case fold）、`houdini_html_u.c`（文字参照）
- [commonmark.js](https://github.com/commonmark/commonmark.js) — `blocks.js` の構文ごとの `continue` / `finalize` / `canContain` / `acceptsLines` の表。pathological tests
- [cmark-gfm](https://github.com/github/cmark-gfm) — 表（段落の解釈し直し）、脚注、タスクリストの拡張の実装
- [Unicode 17.0.0 CaseFolding.txt](https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt) — 完全な case fold
- [WHATWG entities.json](https://html.spec.whatwg.org/entities.json) — 名前付き文字参照
- `.kiro/specs/foundation/design.md`、`.kiro/specs/syntax-tree/design.md` — 上流の契約
