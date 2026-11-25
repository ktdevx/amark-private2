# Research & Design Decisions

## Summary
- **Feature**：`syntax-tree`
- **Discovery Scope**：New Feature（greenfield。`foundation` の共通基盤の上に作る。下流の 4 つの spec が使う継ぎ目を定義する）
- **Key Findings**：
  - 元の表記の保持は「意味の AST ＋ ノードごとのソース範囲の参照」で足りる。ただし、CommonMark の行の接頭辞（`>`、リストのインデント）は内側のノードの範囲の途中に現れるため、トークンは「所有するノード」と「ソース上の順序」を別々に持つ必要がある。所有は意味（どのコンテナの接頭辞か）で決め、順序はオフセットで決める。
  - タブは「1 バイトが複数の構文要素に列単位で分かれる」唯一のケースである（CommonMark 0.31.2 の 2.2 節）。バイト範囲だけのトークンでは表せないため、部分的なタブの列の情報が要る。
  - 編集の追跡は、`markdown-renderer` が「元の表記を出せるか」を判定できれば足りる。ノード単位のビットの記録（内容、子の構成、位置、新規、子孫）で判定でき、より細かい単位（テキストの範囲）は後から別の問い合わせとして足せる。
  - cmark-gfm の拡張の仕組みは「固定のコア ＋ 後付けのフック」で、ノード型の登録は動的な ID と関数表による。Amark は同じく動的な ID と関数表を採るが、組み込みの型も同じ記述子の形式で定義し、スロットは種類の番号つきの一覧にしてレジストリが中身を知らない形にする。

## Research Log

### cmark のノードモデルと走査
- **Context**：利用者になじみのある API の形と、走査中の変更の規則を決めるため。
- **Sources Consulted**：cmark の `cmark.h`（`cmark_node_*`、`cmark_iter_*`、`cmark_node_free`、`cmark_node_unlink`）、cmark の `man 3 cmark`
- **Findings**：
  - 走査は `ENTER` / `EXIT` / `DONE` のイベントで、子を持てない型（葉）には `ENTER` だけを返す。`cmark_iter_reset` で位置を設定し直せる。現在のノードの削除は「次の位置に動いてから」行う規約。
  - `cmark_node_free` は木から外してから部分木を解放する。挿入の関数は包含の規則（`S_can_contain`）を検査して失敗を返す。
  - ノードの文字列は `cmark_chunk` で、ソースへの参照と所有の両方がありうる。元の表記（マーカー、空白）は残さない。
- **Implications**：走査の API と葉の扱いは cmark に合わせる（利用者の移行が容易）。解放の規則も同じにする。包含の規則は型の記述子の「分類」で一般化する。

### ロスレスな構文木の先行例
- **Context**：元の表記を保ったまま意味の単位で編集できる木の形を選ぶため。
- **Sources Consulted**：Roslyn の syntax trivia、rust-analyzer の rowan（green/red tree）、swift-syntax、recast（JavaScript の AST を変更部分だけ印刷し直す）、mdast/micromark の position、comrak の sourcepos
- **Findings**：
  - Roslyn・rowan・swift-syntax は完全な CST で、トークンに空白（trivia）を付ける。往復は完全だが、編集は具象構文を意識する必要がある（brief の Rejected と同じ評価）。
  - recast は元のソースと元の AST の複製を持ち、変更されたノードだけを印刷し直す。変更の検出は木の比較で行うため、比較のコストと「何が変わったか」の曖昧さがある。
  - mdast は位置（行・列・オフセット）だけを持ち、マーカーなどの表記は持たない。`definition` ノード（リンク参照定義）を木に残す。
- **Implications**：意味の AST に「ソース範囲を参照する構文トークンの列」を付け、変更は比較ではなく明示的な記録（ビット）で追う。リンク参照定義は mdast と同じくノードとして残す（HTML には何も出さない）。

### CommonMark の表記の要素（0.31.2）
- **Context**：構文トークンのモデルが表現しなければならない要素を確定するため。
- **Sources Consulted**：CommonMark Spec 0.31.2（2.1 Characters and lines、2.2 Tabs、2.3 Insecure characters、2.4 Backslash escapes、2.5 Entity and numeric character references、4.7 Link reference definitions、5.1 Block quotes の laziness、6.1 Code spans、6.3 Links の title と label）、cmark の BOM の扱い
- **Findings**：
  - 行末は LF、CR、CRLF のどれでもよい。行の内容としては区別しないが、往復には元のバイトが要る。
  - タブは展開せず、ブロック構造の判定の文脈でだけ 4 列のタブストップとして振る舞う。例えば `>\t\tfoo` では、引用の任意の空白が最初のタブの 1 列を使い、残りの列が内容のインデントになる。
  - U+0000 は U+FFFD に置き換える。BOM は仕様にないが、cmark は先頭の UTF-8 の BOM を読み飛ばす。
  - 遅延継続行では、段落の継続の行で引用の `>` を省ける。
  - リンクのラベルは 999 文字以下で、空白以外の文字を含み、エスケープされていない `[` `]` を含まない。タイトルは複数行にできるが空行を含めない。宛先は改行を含めない。コードスパンの内容は改行を空白に変えるため、意味の値として改行を持たず、空にもならない。
  - コードブロックの内容は行の並びで、各行は改行で終わる（cmark のリテラルは末尾に LF を持つ）。
- **Implications**：構文トークンの種類に改行、置き換えたバイト、BOM、遅延継続行の印、エスケープ、文字参照を持たせる。部分的なタブを列の単位で表す。リテラルと属性の設定の検査を、これらの規則のうち「どの文脈でも表現できない値」に限って行う。複数行のリテラルは LF に揃え、末尾を LF にする。

### cmark-gfm の拡張の登録
- **Context**：ノード型の登録と構文拡張の記述子の形を決めるため。
- **Sources Consulted**：cmark-gfm の `cmark-gfm-extension_api.h`（`cmark_syntax_extension_add_node`、`cmark_syntax_extension_set_*_func`、`cmark_node_set_type`、`cmark_syntax_extension_set_can_contain_func`、`opaque_alloc_func` / `opaque_free_func`）
- **Findings**：
  - 拡張のノード型は実行時に番号を割り当てる。型ごとのデータは `opaque` のポインタと解放の関数で持つ。
  - 包含の規則を拡張が上書きする関数がある（表の行は表にだけ入る）。
  - パース・HTML 出力・後処理の関数は、拡張の構造体の固定のフィールドとして足されている（後付けのフック）。そのため、コアの構造体が新しいフックのたびに変わる。
- **Implications**：番号の動的な割り当て、型ごとのデータの関数表、包含の上書きは採る。フックは固定のフィールドにせず、「種類の番号 ＋ 種類ごとの構造体」のスロットの一覧にする。レジストリはスロットの中身を知らず、下流の spec がスロットの種類を足してもレジストリの API は変わらない。

### foundation の公開 API との整合
- **Context**：上流の契約に正確に合わせるため。
- **Sources Consulted**：`.kiro/specs/foundation/design.md`
- **Findings**：
  - エラーコード（`AMARK_ERROR_NONE`、`OUT_OF_MEMORY`、`INVALID_ARGUMENT`、`OVERFLOW`、`LIMIT_EXCEEDED`、`INVALID_STATE`）、出力引数は最後で `out_` で始める、失敗時は強い保証、解放の関数は NULL を受け付ける、公開の構造体は先頭に `struct_size`。
  - 確保はグローバルのアロケータ（`amark_malloc` など）で行い、オブジェクトはアロケータを持たない。`amark_buffer_t` は内部で埋め込める（`buffer_internal.h` の `amark_buffer_init` / `amark_buffer_deinit`）。
  - `amark_utf8_validate` で UTF-8 を検査できる。
  - テストは `amark_add_test(... [INTERNAL] [LIBRARIES ...])`、確保失敗の総当たりは `AMARK_CHECK_OOM_SWEEP`、比較は `AMARK_CHECK_BYTES_EQ` / `AMARK_CHECK_BUFFER_EQ`。ファジングは `amark_add_fuzzer`。
  - `amark_test_support` は Amark の関数を呼ばない約束（`amark` と `amark_internal` の両方と組み合わせるため）。
- **Implications**：
  - 構文木のテスト用の補助（S 式から木を組み立てる）は Amark の関数を呼ぶので、`amark_test_support` には入れない。INTERFACE ライブラリ `amark_test_tree` の INTERFACE のソースとして、各テストの実行ファイルにそのテストの設定でコンパイルさせる（`amark` と `amark_internal` のどちらとも組める）。foundation の変更は不要。
  - 「表現できない値」も `AMARK_ERROR_INVALID_ARGUMENT`、凍結済みの構文セットの変更は `AMARK_ERROR_INVALID_STATE` で表せる。新しいエラーコードは不要。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 意味の AST ＋ ノードごとの構文トークン（採用） | ノードは意味の単位。各ノードがソース範囲を参照するトークンの列を持つ | 編集 API が素直。無編集の部分はソースのバイト列で再現できる | トークンの所有の規則（行の接頭辞）を明確にする必要がある | roadmap の決定どおり |
| 完全な CST | すべてのバイトをトークンとして木に置く | 往復が構造的に保証される | 編集が具象構文を意識する。実装量が倍 | roadmap で不採用 |
| 位置だけの AST ＋ ソースの再走査 | ノードは範囲だけを持ち、表記はレンダラがソースを読み直して判断する | 木が小さい | レンダラがパーサの知識を重複して持つ。部分的なタブを表せない | 不採用 |
| ノードごとにソースの断片を複製 | 表記の文字列をノードに持つ | ソースが不要 | メモリが倍。位置の問い合わせができない | 不採用 |

## Design Decisions

### Decision: 文書オブジェクトを根のノードと分ける
- **Context**：メモリの所有、ソース、構文セット、編集の追跡の切り替えを 1 か所に置く必要がある（1.1–1.5、7.8）。
- **Alternatives Considered**：
  1. cmark と同じく、文書ノードが根であり所有者でもある
  2. 不透明型の `amark_document_t` が所有者で、根は型 document のノード
- **Selected Approach**：2。`amark_document_root()` が根のノードを返す。
- **Rationale**：ソースの一覧や構文セットをノードの型のデータに混ぜずに済む。切り離したノードの所有者が明確になる。
- **Trade-offs**：オブジェクトが 1 つ増える。
- **Follow-up**：`html-renderer` と `markdown-renderer` の入口はノードを受け取る形にすれば、部分木の出力にも使える。

### Decision: ノードの所有と削除の規則
- **Context**：brief は削除時の所有権の規約を design に委ねた（1.3、1.6、1.7）。
- **Alternatives Considered**：
  1. 切り離しと同時に解放する
  2. 切り離しは所有を変えず、解放は明示する。文書の解放で残りをすべて解放する
- **Selected Approach**：2。ノードは常に作った文書に属する。`amark_node_unlink` は切り離すだけ、`amark_node_destroy` は（必要なら切り離してから）部分木を解放する。文書はすべてのノードを内部の環でつなぎ、解放時にまとめて解放する。
- **Rationale**：移動や一時的な取り外しが安全になる。解放し忘れても文書の寿命で回収される。
- **Trade-offs**：長い編集のセッションで、切り離したまま解放しないノードは文書の解放まで残る（文書化する）。
- **Follow-up**：ノードの確保は 1 回の確保（ヘッダ ＋ 型のデータ）。必要になればスラブの確保に変えても API は変わらない。

### Decision: 構文トークンはソースの範囲の参照で、所有はノード、順序はオフセット
- **Context**：6.1–6.6。コンテナの接頭辞は内側のノードの範囲の途中に現れる。
- **Alternatives Considered**：
  1. トークンを子の間の位置（スロット）で持つ
  2. トークンをソース上のオフセットで並べ、所有は意味で決める
- **Selected Approach**：2。各ノードのトークンは（ソース、オフセット、部分的なタブの列）の順に保つ。接頭辞は `AMARK_TOKEN_FLAG_CONTAINER_PREFIX` を付けてコンテナが持つ。遅延継続行は長さ 0 の `LAZY_CONTINUATION` をコンテナが持つ。
- **Rationale**：行の接頭辞のように子をまたぐ表記も、所有と順序を分ければ一貫して表せる。部分木を移動しても、その部分木が持つトークンは自分の表記だけになる（祖先の接頭辞を含まない）。
- **Trade-offs**：部分木の表記の列を得るには、自分のトークンと子の列のマージが要る（`markdown-renderer` と検証が行う）。
- **Follow-up**：パーサの spec は、どのトークンをどのノードに持たせるかと、役割の番号を自分の構文の中で決める。

### Decision: トークンの種類は汎用の分類、構文ごとの意味は役割の番号
- **Context**：パーサと Markdown 出力の同じ構文のコードの間の語彙を、レジストリに固定しないため。
- **Selected Approach**：種類（テキスト、マーカー、空白、改行、エスケープ、文字参照、置換、BOM、遅延継続）はこの spec が定める。役割（16 ビット）は、所有するノードの型を扱う構文拡張が決める。組み込みの構文の役割の値も下流の spec が決める。
- **Rationale**：汎用のエンジン（接頭辞の付け直し、検証）は種類と印だけで動ける。構文ごとの詳細は構文のファイルの中に閉じる。
- **Trade-offs**：利用者が表記（ATX か setext かなど）を知るには、構文が公開する役割の値を使う。

### Decision: ソースは複数、追加のみ、複製して保持
- **Context**：1.4、1.5、4.7。`block-parser` は逐次の入力（feed）を受ける。`markdown-renderer` は断片の差し込みを検討する。
- **Selected Approach**：文書は複数のソースを持つ。ソースは複製で登録し、末尾への追加だけを許す。取り込み（import）は参照されるソースを取り込み先に複製する。行の先頭の索引をソースごとに追加のたびに更新する。
- **Rationale**：パーサはチャンクを文書のソースへ直接追加でき、二重の保持が要らない。別の文書でパースした断片も元の表記つきで差し込める。
- **Trade-offs**：取り込みのたびにソースを複製する（参照されるソースの大きさに比例）。借用（ゼロコピー）は今回は持たない。

### Decision: 編集の追跡はノード単位のビットと、追跡の切り替え
- **Context**：7.1–7.10。brief とステアリングは粒度を design に委ね、後から改善できることを求める。
- **Alternatives Considered**：
  1. ノード単位のビット
  2. 変更の履歴（ログ）
  3. 元の木の複製との比較（recast 方式）
- **Selected Approach**：1。`CONTENT`、`CHILDREN`、`DESCENDANTS`、`RELOCATED`、`CREATED`。追跡は文書ごとに切り替え、新しい文書は無効で始まる（パーサが組み立て後に有効にする）。値が同じ設定は記録しない。利用者には「未知のビットは `CONTENT` とみなす」ことを求め、ビットや別の問い合わせ（テキストの範囲の変更など）を後から足せるようにする。
- **Rationale**：判定に必要な情報（自分の表記が使えるか、子の並びが変わったか、文脈が変わったか）を定数時間で得られる。ログや比較より単純で、メモリも小さい。
- **Trade-offs**：記録を消す操作は持たない（消すとトークンと意味の食い違いを隠せるため）。追跡を無効にして編集した利用者の責任は文書化する。

### Decision: ノード型の識別子は組み込みが固定、拡張が凍結時に割り当て
- **Context**：2.1、8.6、8.10。C の利用者は `switch` で型を判定したい。
- **Selected Approach**：1–255 を組み込みに予約し、現在は 1–19 を使う。拡張の型は構文セットの凍結時に、拡張の順、記述子の順で 256 から割り当てる。拡張の型の名前には `.` を必須にし、`.` のない名前を組み込みに予約する。
- **Rationale**：組み込みの型は定数で比較できる。同じ構成の構文セットは同じ識別子になる。名前の衝突を構造的に避けられる。
- **Trade-offs**：拡張の型の識別子は構文セットごとに違いうる。取り込みと比較は記述子のポインタで対応付ける。

### Decision: スロットは種類の番号つきの見出しを先頭に持つ構造体への参照の一覧
- **Context**：8.2、8.8、8.9。下流の spec がスロットを足しても、レジストリの API を変えないため。
- **Selected Approach**：`amark_slot_header_t { struct_size; kind; }` を先頭のメンバーに持つ構造体を、構文拡張が `const amark_slot_header_t *const *` の一覧で持つ。レジストリは凍結時に種類ごとの索引を作り、順に列挙する。番号の範囲は 1–0xFFFF を Amark、0x10000 以上を利用者とする。
- **Rationale**：利用者が独自のレンダラ（HTML と Markdown 以外）のスロットを定義できる。配列の要素の大きさが将来変わる問題が起きない（ポインタの配列）。
- **Trade-offs**：型の安全はスロットの種類ごとの規約に頼る（種類の番号と `struct_size` の検査はエンジンの責任）。

### Decision: 組み込みの型にリンク参照定義を加える
- **Context**：往復で元の表記を残すには、リンク参照定義が木のどこかにある必要がある。product の用途（リンクの書き換え）では定義の宛先を編集したい。
- **Selected Approach**：ブロックの葉の型 `link_definition`（ラベル、宛先、タイトル）を組み込みに加える。HTML には何も出さない（`html-renderer` の責任）。
- **Rationale**：mdast と同じ扱い。表記をトークンとして持つ場所ができ、比較にも含められる。
- **Trade-offs**：brief の 18 の型の一覧から 1 つ増える（報告で明示する）。

### Decision: リテラルと属性の検査は「どの文脈でも表現できない値」に限る
- **Context**：5.2–5.6。brief は、表現できない編集を黙って受け入れないことを求める。エスケープや区切りの選び直しは `markdown-renderer` が決める。
- **Selected Approach**：設定時には、NUL、不正な UTF-8、改行を持てない値の改行、空行を持てない値の空行、範囲外の数値、空の code、作れないラベルを拒否する。文脈に依存する表現の可否（リンクの中のリンクなど）は `markdown-renderer` が出力時に判定する。複数行のリテラルは LF に揃え末尾を LF にする（意味を変えない正規化）。
- **Rationale**：木の不変条件として、パースで作れる値の集合に合わせる。エスケープの方式を縛らない。

### Decision: テキストの一括設定は改行をソフト改行に変える
- **Context**：5.7。翻訳では段落のテキストを丸ごと置き換えたい。
- **Selected Approach**：インラインを持てるノードに、改行で区切った text と softbreak の列を設定する関数を用意する。text のリテラル自体は改行を拒否する。
- **Rationale**：意味が明確で、表現できない値を作らない。

### Decision: 比較は意味だけ、テキストの正規化は任意
- **Context**：9.1、9.2。`markdown-renderer` の性質テストは、出力の再パースの木と編集後の木を比べる。パーサとの text の分け方の違いを吸収したい。
- **Selected Approach**：型（記述子のポインタ）、属性（型の比較関数）、リテラル、子の並びを比べる。`AMARK_EQUAL_NORMALIZE_TEXT` で隣り合う text を連結とみなし、空の text を無視する。確保をしない反復で実装する。

### Decision: ダンプは S 式、テスト用に同じ形式から木を組み立てる
- **Context**：9.3–9.7。後続の spec は「手で組み立てた木」でレンダラをテストする。
- **Selected Approach**：属性とリテラル、任意でトークン・編集・位置を含む決定的な S 式。拡張の属性はダンプの書き手の関数で書く。テスト用の補助はダンプと同じ形式（トークンを含む）を読む。

## Design Synthesis

### Generalization
- 包含の規則：組み込みの規則（document はブロック、list は項目、段落はインライン…）を、型の「分類（provides）」と「受け入れる分類（accepts）」のビットに一般化し、表や表の行のような拡張には上書きの関数を許す。
- リテラル：text、code、html_inline、code_block、html_block の規則を、リテラルの方式（なし、1 行、空行なしの複数行、行の並び）と任意の検査関数に一般化した。拡張の型も同じ方式を選べる。
- スロット：パース・後処理・HTML・Markdown と利用者独自の出力を、種類の番号つきの一覧に一般化した。
- ソース：逐次の入力と断片の取り込みを、「複数の追加のみのソース」に一般化した。

### Build vs. Adopt
- 既存の C のライブラリ（cmark、cmark-gfm、md4c）は元の表記を持たず、ノードモデルも固定である。ロスレスなトークンと拡張の記述子を後付けするより、作る方が要件に合う。API の形（走査のイベント、ノードの関数名）は cmark にならう。
- S 式のダンプは cmark の `--to xml` や mdast の JSON の代わりに、依存なしで比較しやすい独自の小さな形式にする。

### Simplification
- トークンの合成（ソースにない文字列のトークン）は持たない。新しいノードはトークンを持たず、作り直しは `markdown-renderer` が行う。
- ソースの借用（ゼロコピー）、記録の消去、拡張の依存関係の宣言、部分木のトークンの列の公開の反復子は持たない（必要が具体化したら追加する）。
- 文書単位のアリーナは「文書がすべてのノードを所有する」規則で満たし、確保の方式（個別の確保）は内部の実装の選択とする。

## Risks & Mitigations
- トークンのメモリが大きい（1 トークン約 32 バイト）— 内部の構造を詰め、計算量の要件（10.5）と合わせて大きな文書で計測する。必要なら後でまとめて確保する。
- 追跡を無効にしたまま編集すると、元の表記が古いまま出力される — 文書化し、ロスレス性の検証と不変条件の検査をテストとファジングで使う。
- 包含の規則が拡張の構文（脚注の定義、表）に足りない — `can_contain` の上書きで対応する。足りなければこの spec を更新する（Revalidation Trigger）。
- 取り込みがソースを毎回複製する — 文書化する。頻繁な差し込みの需要が出たら、ソースの共有を設計し直す。
- 下流の spec がトークンのモデルの不足を見つける — 種類と印は追加できる番号で定義し、未知の種類を扱う規則を文書化する。

## Revision Notes

### 2026-11-25：下流の spec からの上流の変更（`block-parser`、`html-renderer` の依頼）
- **Context**：`block-parser` と `html-renderer` の設計で、この spec の API と規約に足りない点が見つかった。下流の spec は回避策をとらず、この spec を更新する約束（Boundary Context の Adjacent expectations、design の Risks）に従って反映する。承認済みの要件・設計・タスクに追加する形で行い、既存の受け入れ基準の意味は変えない。
- **`amark_document_take_syntax_set`（1.10、1.11）**：`block-parser` の便宜の関数（`amark_parse_commonmark` など）は構文セットを内部で作り、返した文書がそれを保持し続ける必要がある（`block-parser` の 1.2、11.3）。パーサの層が構文の層（`src/syntax/`）や利用者に解放の責任を残さないよう、文書が構文セットの所有権を受け取れるようにした。既存の規則（文書は凍結済みの構文セットだけを使う、構文セットは使うすべての文書より長く生きる、`amark_syntax_set_core()` は静的で解放しても何もしない）と矛盾しないよう、受け付けるのは「その文書が使っている、静的でない構文セットを 1 回だけ」とし、解放はノードの `deinit` が記述子を参照できるようにすべてのノードの後に行う。失敗時に所有権が呼び出し側に残ることで、呼び出し側は失敗の後も通常どおり解放できる。検討した代替：構文セットに参照の数を持たせる案は、凍結した構文セットを読み取り専用で共有する規則（1.9）とスレッドの安全性に原子的な操作を持ち込むため採らなかった。
- **`amark_node_import_children`（4.10、4.11）**：`block-parser` の断片のパースは、一時的な文書の根（または一時的な段落）の子をすべて既存の文書へ全か無かで挿入する必要がある（`block-parser` の 10.1、10.2、10.4）。既存の `amark_node_import` を子ごとに呼ぶと、(1) 途中で失敗したときに挿入済みの子を戻す処理が呼び出し側に要り、(2) 子ごとに同じソースが複製される。検査（型の対応付け、包含）と確保（ソース、複製）を先にすべて行い、確保なしのつなぎ込みで終える操作として定義した。包含の判定とつなぎ込みは StructureEdit の内部関数を共有し、規則を 1 か所に保つ。
- **スロットの種類の番号の範囲の表（8.13）**：`html-renderer` は `0x0301`、`0x0302`、`block-parser` は `0x0100` 台を使う。各 spec が独立に番号を選ぶと衝突しうるため、Amark の範囲（`0x0001`–`0xFFFF`）を spec ごとに 256 ずつ割り当てる表を `extension.h` に置いた。この spec 自身はスロットの種類を定義しないが、レジストリが将来使う可能性のために `0x0001`–`0x00FF` を予約する。レジストリは表を強制しない（利用者の範囲 `0x10000` 以上と同じく、`kind` が 0 でなければ受け付ける）。表は spec の間の約束であり、範囲の重なりはテストで確かめる。
- **Implications**：Revalidation Triggers に、所有の受け渡しと子をまとめた取り込みの規則の変更、範囲の表の変更を加えた。下流の spec は、この時点の `block-parser` と `html-renderer` の design の記述（関数の形、エラーの規則、番号の範囲）と一致する。

### 2026-11-25：spec 横断のレビュー r1（cross-spec review r1）
- **Context**：全 spec を横断したレビューで、この spec に関わる指摘が 3 つあった（#7、#8、#9）。承認済みの要件・設計・タスクの意味は変えず、記述をそろえる修正だけを行う。
- **#9 スロットの種類の範囲の表が古い**：表の右の列が「各 spec の design が定める」のままだった。承認済みの下流の design に合わせ、具体的な種類を記した：`block-parser` 0x0100–0x0103（`BLOCK_SYNTAX`、`PARAGRAPH_CONSUMER`、`POSTPROCESS`、`PARSE_STATE`）、`inline-parser` 0x0201–0x0206（`INLINE_SYNTAX`、`INLINE_DELIMITER`、`INLINE_BRACKET`、`INLINE_TEXT_SCANNER`、`INLINE_NODE_TRAITS`、`INLINE_SPECIAL`）、`html-renderer` 0x0301–0x0302（`HTML_NODE`、`HTML_HOOKS`）、`markdown-renderer` 0x0401–0x0402（`MARKDOWN_NODE`、`MARKDOWN_HOOKS`）。`extension.h` の範囲の節の注記にも同じ一覧を置き、タスク 1.1 に反映した。定義の正本は各 spec の design のままとし、種類を足す・変える spec が表と注記を更新することを Revalidation Triggers に加えた。要件 8.13 の文言は変えない（表を公開ヘッダに置く、という要件のまま）。
- **#7 `amark.h` の include の順**：`amark.h` は `foundation` が所有し、正規の順（`foundation` のヘッダ → 木のヘッダ → `render.h`、`html.h` → `entity.h`、`scan.h`、`parser.h`、`block.h`、`inline.h`、`commonmark.h` → `markdown.h`）を定める。この spec の「`unicode.h` の後に追加する」という書き方をやめ、`foundation` の正規の順の「木のヘッダ」の位置に 6 つのヘッダを置く、という参照に改めた（File Structure Plan、公開ヘッダの構成、タスク 1.1）。正規の順の変更を Revalidation Triggers に加えた。
- **#8 `amark_document_take_syntax_set` の `const`**：`block-parser` の `amark_parser_create(const amark_syntax_set_t *)` から非 `const` の `take` を呼ぶと `const` を外すキャストが要る、という指摘。所有権の受け渡し（解放の責任の移動）は `const` のポインタでは表せないため、この関数の引数は非 `const` のままとする。設計に「`const` のポインタしか持たない呼び出し側は所有権を渡してはならない（`const` を外すキャストをしない）」と明記した。`const` でないポインタを持つ経路だけで受け渡すよう直すのは `block-parser` の側で行う。
- **Implications**：API・要件の受け入れ基準・タスクの範囲は変わらない。承認は維持する。

### 2026-11-25：spec 横断のレビュー r2（cross-spec review r2）
- **`amark_test_tree` の定義の条件**：`tree_builder.h` は Naos のマクロ `AMARK_CHECK_TREE_DUMP` を宣言するので `amark_test_tree` は Naos に依存する。`foundation` の規則 7.7 では `tests/` は `AMARK_FUZZ` だけが ON の構成でも入るため、`amark_test_tree` の定義と `add_subdirectory(tree)` を `if(AMARK_BUILD_TESTS)` の中に置くと明記した（design の Modified Files、TreeBuilder、タスクの該当箇所）。ファジングターゲットはこのライブラリをリンクしない。承認は維持する。

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — タブ、NUL、行末、遅延継続、リンクのラベル・タイトル・宛先、コードスパン
- [cmark](https://github.com/commonmark/cmark) — ノード API、走査のイベント、解放の規則
- [cmark-gfm extension API](https://github.com/github/cmark-gfm/blob/master/src/cmark-gfm-extension_api.h) — 拡張のノード型の登録、包含の上書き、型ごとのデータ
- [Roslyn syntax trivia](https://learn.microsoft.com/dotnet/csharp/roslyn-sdk/work-with-syntax) — ロスレスな CST とトリビア
- [rowan](https://github.com/rust-analyzer/rowan) — green/red tree
- [recast](https://github.com/benjamn/recast) — 変更部分だけを印刷し直す AST の書き戻し
- [mdast](https://github.com/syntax-tree/mdast) — `definition` ノードと位置
- `.kiro/specs/foundation/design.md` — エラーコード、アロケータ、バッファ、UTF-8、テストとファジングの仕組み
