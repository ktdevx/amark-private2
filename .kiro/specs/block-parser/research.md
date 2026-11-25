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
  - 区間の並びの抽象（`amark_text_input`）を公開し、走査の関数（ラベル、宛先、タイトル、HTML のタグ）と、エスケープと文字参照を解読しながらトークンを記録する関数を、この上に作って公開する。ブロックが最初に必要とするので、この spec が持つ。
  - 委譲は関数表（`amark_inline_delegate`）で行い、既定の委譲先を差し替え可能にする。この spec は単純な委譲先（テキストと softbreak）を提供し、インラインのエンジンがない段階でもロスレス性の検証まで完結させる。`inline-parser` は既定の委譲先を自分のエンジンに変える。

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
- **Sources Consulted**：`.kiro/steering/roadmap.md`（Boundary Strategy：1 つの構文拡張が、ノード型、パース、HTML 出力、Markdown 出力をまとめる）、`.kiro/steering/structure.md`（構文ごとに 1 ファイル）、`.kiro/specs/syntax-tree/design.md`（`amark_extension` のスロットの一覧、`amark.core` が組み込みの型を持つ）。
- **Findings**：組み込みのノード型は `amark.core` が持つため、構文ごとの拡張はスロットだけを持つ。見出し（ATX と setext）とコードブロック（インデントとフェンス）は、2 つの構文が同じノード型を作る。document 型は構文ではなくエンジンが作る。
- **Implications**：構文ごとのファイルが自分の拡張とスロットの静的な一覧を持ち、下流の spec は同じファイルの一覧に自分のスロットを足す。どのノード型をどの拡張が作るかの対応表を公開する。同じノード型を複数の拡張が作る場合の出力のスロットの選び方は、レンダラの spec が決める（この spec は「同じスロットの実体を複数の拡張から参照してよい」ことだけを保証する）。

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
  2. ソース上の区間の並び（`amark_text_input`）として渡し、論理的な位置からソースのオフセットを引ける形にする
- **Selected Approach**：2。走査（ラベル、宛先、タイトル、HTML のタグ）と、解読しながらトークンを記録する関数を、`amark_text_input` の上に公開する。
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
- **Selected Approach**：3。パーサは `AMARK_PARSE_TAKE_SYNTAX_SET` の印で構文セットの所有権を受け取り、終了時に文書へ渡す。
- **Rationale**：レイヤーの方向（syntax → parse → tree）を守れる。グローバルな可変状態がない。
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
  - リンク参照定義の表と、脚注の定義の表は、同じ「正規化したラベルで引ける表」に一般化した（`amark_label_map`）。
  - ブロックの中身のインラインの入力、リンク参照定義の走査、断片のインラインの文脈は、どれも「区間の並びのテキスト」に一般化した（`amark_text_input`）。
- **Build vs. Adopt**：
  - 外部の依存は持たない（steering）。cmark のアルゴリズムは採り入れるが、コードは使わない（ノードのモデルとトークンの記録が根本的に異なり、ライセンスの表記も分けたい）。
  - Unicode の case fold と HTML の文字参照は、公式のデータ（UCD、WHATWG の `entities.json`）から生成する。
- **Simplification**：
  - 空行を構文拡張にしない（上記）。
  - リストとリスト項目は 1 つの拡張（`commonmark.list_item`）にまとめる。リストはリスト項目の並びとして定義され（5.3 節）、リストのブロック構文は開始の条件を持たない。
  - エンジンの内部の状態を、ノードの `user_data` に置かない（利用者のものなので）。パースの間のデータは、ノードのポインタを鍵とする内部の表に置く。

## Upstream Changes
- **`syntax-tree`**：
  1. `amark_status amark_document_take_syntax_set(amark_document *document, amark_syntax_set *set)`：文書がすでに使っている、ヒープに作られた凍結済みの構文セットの所有権を受け取り、`amark_document_free` でノードの後に解放する。静的な core の構文セット、別の構文セット、2 回目の呼び出しは `AMARK_ERROR_INVALID_ARGUMENT`。
  2. `amark_status amark_node_import_children(amark_node *parent, amark_node *before, const amark_node *source_parent)`：別の文書（または同じ文書）の `source_parent` の子をすべて、1 回の操作で取り込み、`parent` の `before` の前（NULL なら末尾）に順に挿入する。ソースの複製は呼び出し全体で 1 回だけ行う。包含の規則の検査を先に行い、確保の失敗を含めて全か無か（強い保証）。挿入は通常の挿入と同じく編集を記録する。
  3. `extension.h` にこの spec のスロットの種類の名前と番号（0x0100–0x0103）を追記する（`syntax-tree` の design が予定している追記）。
- **`foundation`**（foundation の design が予定している「必要とする spec が追加する」作業）：
  - `tools/gen_unicode_tables.py` に `CaseFolding.txt` の読み取りを足し、`src/runtime/case_fold_tables.inc` を出力する。`include/amark/unicode.h` に `amark_unicode_case_fold()` を足す。
  - `tools/gen_entity_table.py`、`src/runtime/entity_tables.inc`、`src/runtime/entity.c`、`include/amark/entity.h` を追加する。
  - `.gitattributes` に、CommonMark spec の例を写したテストデータの `export-ignore` を足す。
- **スロットの種類の番号の範囲**：`html-renderer` と並行して決めるため、範囲の取り決め（`block-parser` 0x0100–0x01FF、`inline-parser` 0x0200–0x02FF、`html-renderer` 0x0300–0x03FF、`markdown-renderer` 0x0400–0x04FF）を提案する。

## Risks & Mitigations
- 公開のブロック構文 API が大きくなる — 組み込みの構文が使う操作だけに絞り、GFM 相当の 3 つの検証用の構文で不足と過剰を確かめる。
- 行の記録から記録したトークンと、即座に記録したコンテナの接頭辞の順序が崩れる — トークンはノードごとにソース上の順で整列される（`syntax-tree`）ので順序の問題はない。被覆はすべてのテストとファジングで検証する。
- `html-renderer` が別のスロットの番号の範囲や、構文ごとでない拡張の構造を選ぶ — Upstream Changes の取り決めを報告し、`html-renderer` の design と突き合わせる。構文ごとのファイルの静的なスロットの一覧は、どの選び方とも両立する。
- 後処理がインラインの入力を持つノードを解放すると、内部の表に古いポインタが残る — 後処理の契約として、`amark_parse_discard_inline_input` / `amark_parse_move_inline_input` を呼ぶことを要求し、テストで確かめる。
- CommonMark spec の例の期待値を手で書く量が多い — 期待値はブロックの構造だけのダンプ（インラインは単純な委譲先のテキスト）にし、生成の補助で形式を検査する。

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — ブロック構造、タブ、不正な文字、リンク参照定義、付録の parsing strategy
- [cmark](https://github.com/commonmark/cmark) — `blocks.c`、`inlines.c`、`utf8.c`（case fold）、`houdini_html_u.c`（文字参照）
- [commonmark.js](https://github.com/commonmark/commonmark.js) — `blocks.js` の構文ごとの `continue` / `finalize` / `canContain` / `acceptsLines` の表。pathological tests
- [cmark-gfm](https://github.com/github/cmark-gfm) — 表（段落の解釈し直し）、脚注、タスクリストの拡張の実装
- [Unicode 17.0.0 CaseFolding.txt](https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt) — 完全な case fold
- [WHATWG entities.json](https://html.spec.whatwg.org/entities.json) — 名前付き文字参照
- `.kiro/specs/foundation/design.md`、`.kiro/specs/syntax-tree/design.md` — 上流の契約
