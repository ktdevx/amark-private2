# Requirements Document

## Introduction
Amark は、Markdown テキストと構文木を相互変換する C99 ライブラリである。`syntax-tree` は、パーサ（`block-parser`、`inline-parser`）が作り、レンダラ（`html-renderer`、`markdown-renderer`）と利用者が使う構文木の土台を提供する。

主な用途は、Markdown と po の相互変換である。原文から、作者が書いた表記のままの msgid を取り出し、翻訳者が書いた msgstr を表記のまま差し込んで、翻訳した Markdown を作る。訳していない部分は原文とバイト単位で同じに残り、引用やリストの中の段落を訳してもコンテナの構造は崩れない必要がある。

Markdown のブロックは 2 次元である。コンテナ（引用、リスト項目）は各行の左端の帯で、中身はその右にある。インラインは、葉の中身から帯を除いて行をつないだ 1 次元の文字列の上にある。そこでこの spec は、元の表記を次の 2 層で持つ。

- **配置の層**：文書の行の表と、ブロックの行の範囲、コンテナの行ごとの帯と空行、葉自身の表記。座標は行と列である。
- **綴りの層**：葉の中身と、その上のインラインのトークン。座標は中身の上の位置である。

ノードの情報は、意味の値（value）、表記の属性（style）、綴り（spelling）に分ける。意味の値は綴りがあれば綴りから導く。値を編集すると、その値の綴りだけを捨て、style は残す。どの部分を元の表記で出せるかは、綴りと帯が残っているかで判断し、編集の記録は持たない。

この spec は、最初の設計（ソースの絶対位置の構文トークンと編集の追跡、2026-11-25 承認）を置き換える。モデルの入力資料は、リポジトリの直下の `INPUT.md` である。現在、`foundation` の実装と、最初の設計による構文木の実装（`src/tree/`）がある。

## Boundary Context
- **In scope**：
  - 文書とノードの生成・解放・所有の規則、ソースの保持、行の表、文書による構文セットの所有
  - 組み込みのノード型（document、block_quote、list、item、code_block、html_block、paragraph、heading、thematic_break、text、softbreak、linebreak、code、html_inline、emph、strong、link、image と、リンク参照定義）と、その意味の値、style、包含の規則
  - 配置（帯、空行、葉自身の表記、行の範囲）と綴り（中身、インラインのトークン）を記録・参照する仕組み、意味の値の導出
  - 参照、走査、位置の問い合わせ、構造と内容の編集、中身の置き換え、複製、別の文書からの取り込み
  - ラベルの正規化、リンク参照定義の索引と、参照の形のリンクの解決
  - ロスレス性の検証と、元の表記を出せるかの問い合わせ
  - ノード型の登録、構文拡張の記述子、構文セット、スロットの登録と列挙、Amark のスロットの種類の通し番号の割り当て
  - 木の意味の比較、S 式のダンプ、テスト用に S 式から木を組み立てる補助
- **Out of scope**：
  - ブロックとインラインのパースの規則と、配置と綴りを実際に記録する処理（`block-parser`、`inline-parser`）
  - HTML と Markdown の出力の規則、書き戻しで帯と綴りを合成する処理、エスケープの方式（`html-renderer`、`markdown-renderer`）
  - 各スロットの具体的なインタフェースと種類の番号（そのスロットを実行する spec が、この spec が割り当てた番号で定義する）
  - CommonMark の組み込み構文をまとめた構文セットの用意（組み込みの構文拡張を定義する spec が提供する）
  - msgid のための空白の正規化と、po の形式の読み書き
- **Adjacent expectations**：
  - パーサは、元のソースのすべてのバイトを、この spec の配置と綴りのどれか 1 つの持ち主に記録し、無編集の文書がロスレス性の検証に通るようにする。パーサが記録する綴りは、値の編集として扱わない。
  - `markdown-renderer` は、この spec の「元の表記を出せるか」の問い合わせと style から、元の表記を出す部分と合成する部分を判断する。
  - リンク参照定義の索引とラベルの正規化はこの spec が持つ。`block-parser` と `inline-parser` は、それを使って定義を登録し、参照を解決する（それぞれの spec の改訂で、自前の表と正規化をこの spec のものに置き換える）。
  - 構文セットを内部で作る便宜の関数を提供する上位の spec は、この spec の構文セットの所有の受け渡しで、作った構文セットの寿命を返す文書に結び付ける。断片を挿入する spec は、子をまとめた取り込みと中身の置き換えで、一時的な文書の結果を全か無かで挿入する。
  - モデルやレジストリに不足が見つかったら、下流の spec はこの spec に戻って更新する。

## Requirements

### Requirement 1: 文書とメモリの所有
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want 構文木のすべてのメモリが文書単位で管理され、グローバルのアロケータで確保されること, so that ノードの寿命を個別に追わずに安全に使え、メモリ不足でも壊れない

#### Acceptance Criteria
1. When 利用者が構文セットを指定して文書を作る, the Amark Syntax Tree shall 文書ノードだけを持つ木を作り、その文書のすべての確保を `foundation` のグローバルのアロケータで行う
2. When 利用者が構文セットを指定せずに文書を作る, the Amark Syntax Tree shall 組み込みのノード型だけを持つ構文セットを使う
3. When 利用者が文書を解放する, the Amark Syntax Tree shall その文書に属するすべてのノード（木から切り離したノードを含む）、ソース、行の表、配置と綴りの記録を解放する
4. When 利用者が元のソースのバイト列を文書に登録する, the Amark Syntax Tree shall バイト列をそのまま複製して文書の寿命の間保持し、識別子を返す
5. When 利用者がノードを木から切り離す, the Amark Syntax Tree shall そのノードと子孫を、配置と綴りとともに解放せずに文書に残し、同じ文書の木へ再び挿入できるようにする
6. When 利用者がノードを解放する, the Amark Syntax Tree shall そのノードが木にあれば切り離してから、ノードと子孫を解放する
7. If 確保が失敗する, then the Amark Syntax Tree shall メモリ不足を返し、文書とノードを呼び出し前の状態のまま保ち、リークしない
8. When 利用者が、文書が使っている、利用者が作った構文セットの所有権を文書に渡す, the Amark Syntax Tree shall その構文セットを文書の寿命の間保持し、文書の解放のときに、すべてのノードを解放した後でその構文セットを解放する
9. If 所有権を渡す構文セットが、その文書が使っているものでないか、ライブラリが提供する静的な構文セットであるか、文書がすでに構文セットを所有している, then the Amark Syntax Tree shall 不正な引数を返し、文書を変えず、構文セットの所有権を呼び出し側に残す

### Requirement 2: 行の表と位置
**Objective:** As a パーサとレンダラの開発者, I want 文書の行が、ソース、範囲、改行の種類、列の幅を持つ第一級の単位として扱えること, so that ブロックの配置と位置を行と列で記録・参照でき、行の数え直しをせずに済む

#### Acceptance Criteria
1. When 利用者がソースを登録する, the Amark Syntax Tree shall ソースを行に分け、ソースの登録順・行の順に通し番号を付けて文書の行の表の末尾に加える
2. The Amark Syntax Tree shall 各行について、属するソース、改行を含まないバイトの範囲、改行の種類（LF、CRLF、CR、なし）、列の幅を返す
3. The Amark Syntax Tree shall 列を、タブを 4 列のタブストップとして数え、行の中の列とバイトの位置を相互に変換できるようにし、タブの途中の列はそのタブのバイトに対応付ける
4. When 登録したソースの先頭が UTF-8 の BOM である, the Amark Syntax Tree shall BOM を文書が持つものとして記録し、最初の行のバイトの範囲と列に含めない
5. When 利用者が登録済みのソースの末尾にバイト列を追加する, the Amark Syntax Tree shall 既存の内容と確定した行を変えずに、新しく確定した行を行の表の末尾に加える
6. While ソースの最後の行が改行で終わっていないか、CR で終わり次のバイトがまだない, the Amark Syntax Tree shall その行を未確定とし、続くバイト列の追加か、利用者による入力の終わりの通知で確定する
7. The Amark Syntax Tree shall 同じバイト列をどの位置で分けて追加しても、一度に登録して入力の終わりを通知したときと同じ行の表を作る
8. The Amark Syntax Tree shall 文書の既定の改行として、行の表で最初に現れる改行の種類を返し、改行が 1 つもなければ LF を返す
9. When 利用者がブロックのソース上の範囲を問い合わせる, the Amark Syntax Tree shall そのブロックの行の範囲と列の範囲から、バイトの位置と、1 から数える行と列で範囲を返す
10. When 利用者が、配置済みの中身の上にあるインラインのノードのソース上の範囲を問い合わせる, the Amark Syntax Tree shall 中身の上の位置を元の行と列に写して、バイトの位置と、1 から数える行と列で範囲を返す
11. If 問い合わせたノードが元の行に対応しない（行の範囲も綴りも持たない新しいノード、綴りをすべて捨てたインラインのノードで綴りを持つ子孫もないもの、自由な中身の上のノード）, then the Amark Syntax Tree shall 位置がないことを返す

### Requirement 3: 組み込みのノード型、意味の値、style
**Objective:** As a 構文木を使う開発者, I want CommonMark のすべての要素が、意味の値と表記の属性を持つノードとして表されること, so that 文書の構造と意味を型に沿って扱い、表記の違い（`*` か `_` かなど）を綴りを解析せずに知ることができる

#### Acceptance Criteria
1. The Amark Syntax Tree shall document、block_quote、list、item、code_block、html_block、paragraph、heading、thematic_break、text、softbreak、linebreak、code、html_inline、emph、strong、link、image とリンク参照定義を、固定の識別子と名前を持つ組み込みのノード型として提供する
2. The Amark Syntax Tree shall 見出しのレベル、リストの種類・開始番号・タイトかどうか、コードブロックの情報文字列、リンクと画像の宛先とタイトル、リンク参照定義のラベル・宛先・タイトル、text・code・html_inline・code_block・html_block のリテラルを、意味の値として取得・設定できるようにする
3. When 利用者が、綴りを持つ意味の値を取得する, the Amark Syntax Tree shall 綴りから CommonMark の規則で導いた値（エスケープと文字参照を解読し、NUL と不正な UTF-8 を置き換え、複数行のリテラルの改行を LF に揃えた値）を返す
4. When 利用者が、綴りを持たない意味の値を取得する, the Amark Syntax Tree shall 設定された値か、型の既定の値を返す
5. The Amark Syntax Tree shall 次の表記の属性（style）を、名前の付いた属性として取得・設定できるようにする：見出しの書き方（ATX / setext）と閉じの `#` の有無と setext の下線の文字、コードブロックの書き方（インデント / フェンス）とフェンスの文字と長さ、区切り線の文字と数と間の空白の有無、リストの記号（`-` `*` `+`）と番号の区切り（`.` `)`）、強調の区切り文字（`*` `_`）、コードスパンのバッククォートの数と内側の空白の有無、ハード改行の書き方（行末の空白 / バックスラッシュ）、リンクと画像の形（インライン / full / collapsed / shortcut / オートリンク）と宛先の山括弧の有無とタイトルの引用符、リンク参照定義の宛先の山括弧の有無とタイトルの引用符
6. The Amark Syntax Tree shall style が未設定の状態を持てるようにし、新しく作ったノードの style を未設定にし、取得で未設定であることを返す
7. When 利用者がリストのタイトかどうかを取得し、利用者が上書きの値を設定していない, the Amark Syntax Tree shall リストの部分木の空行の配置から CommonMark の規則で導いた値を返す
8. The Amark Syntax Tree shall 各ノード型について、ブロックかインラインか、子を持てるか、どの種類の子を持てるかを、CommonMark の構造に従って定め、問い合わせられるようにする
9. The Amark Syntax Tree shall 組み込みのノード型を、拡張のノード型と同じ記述の形式と登録の仕組みで定義する
10. If 利用者が、その型にない意味の値か style を取得または設定する, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない

### Requirement 4: 参照と走査
**Objective:** As a 文書処理ツールの開発者, I want ノードの関係を参照し、部分木を文書順に走査できること, so that 見出しの抽出や Lint などを実装できる

#### Acceptance Criteria
1. The Amark Syntax Tree shall 各ノードについて、型、属する文書、親、最初と最後の子、前後の兄弟を返し、該当するノードがなければ「なし」を返す
2. When 利用者が部分木の走査を始める, the Amark Syntax Tree shall 指定した根から文書順に、子を持てるノードには入るイベントと出るイベントを、子を持てないノードには入るイベントだけを返し、指定した根の外へ出ない
3. While 走査の途中である, when 利用者が現在のノードの内容や子を変更するか、現在のノードとその祖先以外を編集する, the Amark Syntax Tree shall 走査を続け、変更後の木の未訪問の部分を文書順に訪問する
4. When 利用者が走査の位置を、根の部分木の中のノードとイベントに設定し直す, the Amark Syntax Tree shall その位置から走査を続ける
5. The Amark Syntax Tree shall 利用者が各ノードに任意のポインタを関連付け、取り出せるようにする

### Requirement 5: 構造の編集、複製、取り込み
**Objective:** As a Markdown を加工するツールの開発者, I want ノードの挿入、削除、移動、置換、複製、取り込みを、木の構造の規則と元の表記を守ったまま行えること, so that 不正な木を作る心配なく文書を変形でき、動かした部分の表記を失わない

#### Acceptance Criteria
1. When 利用者が新しいノードを型を指定して作る, the Amark Syntax Tree shall その文書に属し、どこにも挿入されておらず、行の範囲も綴りも持たないノードを、型の既定の値と未設定の style で作る
2. When 利用者がノードを、あるノードの前、後、最初の子、最後の子として挿入する, the Amark Syntax Tree shall その位置に挿入する
3. When 挿入するノードがすでに木のどこかにある, the Amark Syntax Tree shall 元の位置から取り除いて新しい位置に移し、そのノードと子孫の配置と綴りを保つ
4. When 利用者があるノードを別のノードで置き換える, the Amark Syntax Tree shall 置き換えたノードを同じ位置に置き、元のノードを切り離された状態にする
5. If 挿入・移動・置換が、包含の規則に反するか、ノードを自身の子孫の下に置くか、文書ノードを動かすか、別の文書のノードを使う, then the Amark Syntax Tree shall 不正な引数を返し、木を変えない
6. When 利用者がノードを複製する, the Amark Syntax Tree shall 型、意味の値、style、配置、綴り、子孫をすべて複製した、切り離されたノードを同じ文書に作る
7. When 利用者が別の文書のノードを取り込む, the Amark Syntax Tree shall 部分木を複製し、その配置と綴りが参照する行のバイト列を取り込み先の文書に保持して、元の表記を失わない
8. When 利用者が、あるノードの子をすべて、挿入先の親と位置を指定して取り込む, the Amark Syntax Tree shall すべての子の部分木を元の順のまま複製して 1 回の操作でその位置に挿入し、子の部分木の配置と綴りが参照する行のバイト列を取り込み先の文書に保持する
9. If 取り込むノードか子の部分木のどれかが取り込み先の構文セットにない型を持つか、子のどれかが挿入先の親の包含の規則に反するか、挿入の位置が挿入先の親の子でない, then the Amark Syntax Tree shall 不正な引数を返し、何も挿入せず、取り込み先の文書を変えない
10. The Amark Syntax Tree shall 入れ子の深さが 10 万段の木でも、走査、複製、取り込み、解放、比較、ダンプをスタックあふれなしに行う

### Requirement 6: 値と style の編集の意味論
**Objective:** As a 翻訳ツールの開発者, I want 値や表記の変更が、Markdown で表現できる値だけを受け付け、変更した部分の元の表記だけを失うこと, so that 編集が黙って違う意味の出力を招かず、触っていない部分の表記が残る

#### Acceptance Criteria
1. When 利用者がリテラルや文字列の値を設定する, the Amark Syntax Tree shall 値を複製して保持する
2. If 設定する文字列が NUL 文字か不正な UTF-8 を含む, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
3. If 改行を含められない値（text と code のリテラル、リンクの宛先、コードブロックの情報文字列）に改行を含む文字列を設定する, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
4. If 空行を含められない値（リンクのタイトル、リンク参照定義のラベル、html_inline のリテラル）に空行を含む文字列を設定する, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
5. When 利用者が複数行のリテラル（code_block、html_block）を設定する, the Amark Syntax Tree shall CRLF と CR を LF に揃え、空でなければ末尾が LF で終わる形で保持する
6. If 値が定められた範囲の外にある（見出しのレベルが 1–6 の外、リストの開始番号が 0–999999999 の外、未知のリストの種類、空の code のリテラル、CommonMark の規則で作れないリンク参照定義のラベル）, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
7. When 利用者が意味の値を現在と異なる値に設定する, the Amark Syntax Tree shall その値を表す綴りだけを捨て、そのノードのほかの綴り、style、配置と、ほかのノードの綴りを残す
8. When 利用者が style を現在と異なる値に設定する, the Amark Syntax Tree shall その style を表す綴りだけを捨て、意味の値とほかの綴りを残す
9. When 利用者が意味の値か style に、現在と同じ値を設定する, the Amark Syntax Tree shall 綴りを捨てない
10. When 利用者が、参照の形（full、collapsed、shortcut）のリンクか画像に宛先かタイトルを設定する, the Amark Syntax Tree shall そのノードをインラインの形に変え（形の style をインラインにし、ラベルの綴りを捨て）、設定した値と、その時点で解決していたもう一方の値を持たせ、同じラベルを使うほかのノードとリンク参照定義を変えない
11. When 利用者がリストのタイトかどうかを設定する, the Amark Syntax Tree shall その値を上書きの値として保持して以後の取得で返し、利用者が上書きを取り消したら空行から導く値に戻す
12. When 利用者がインラインを持てるノードにテキストを一括で設定する, the Amark Syntax Tree shall 既存の子を取り除き、改行（LF、CRLF、CR）で区切ったテキストのノードと、その間のソフト改行のノードを子として置く
13. When 利用者か拡張の実装者がノードの綴りを捨てるよう求める, the Amark Syntax Tree shall そのノード自身のすべての綴りを捨て、捨てる前の意味の値と style を保つ
14. The Amark Syntax Tree shall パーサや拡張の実装者が配置と綴りを記録する操作を値の編集として扱わず、記録した綴りを捨てない

### Requirement 7: 配置の記録
**Objective:** As a パーサと拡張の実装者, I want ブロックの行の範囲、コンテナの帯と空行、葉自身の表記を、行と列で記録できること, so that 部分的なタブや遅延継続を特例なしに表し、ソースのオフセットを計算せずに元の表記を残せる

#### Acceptance Criteria
1. The Amark Syntax Tree shall 各ブロックに、覆う行の範囲を記録できるようにする
2. The Amark Syntax Tree shall コンテナ（block_quote、item、拡張のコンテナ）に、覆う行ごとの帯を記録できるようにし、帯は状態（帯がある / 遅延継続で省いた）、列の範囲、項目の最初の行の印、役割の番号を持つ
3. The Amark Syntax Tree shall 遅延継続で省いた帯を、列の範囲が空の帯として表す
4. The Amark Syntax Tree shall 空行を、それを含む一番内側のコンテナ（文書を含む）に記録できるようにし、空行は行と、その中の空白の列の範囲を持つ
5. The Amark Syntax Tree shall 葉に、自身の表記（インデント、マーカー、フェンスの行、情報文字列の綴り、閉じの `#`、行末の空白など）を、行、列の範囲、種類、役割の番号を持つトークンとして記録できるようにする
6. The Amark Syntax Tree shall 1 つのタブを、列の単位で分けた複数の範囲（例えば、帯が使う列と、葉自身の表記や中身が使う残りの列）で表せるようにする
7. If 記録が、行の表にない行、行の列の幅の外、タブ以外の文字の途中の境界を参照するか、遅延継続の帯が空でない列の範囲を持つ, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
8. Where 拡張のノード型が 1 つの行を複数の葉で分け合う（GFM の表のセルなど）, the Amark Syntax Tree shall 兄弟のブロックが、同じ行の重ならない列の範囲を分け合えるようにする
9. Where 拡張のノード型が葉の中身の行の途中のバイトを中身から除く（GFM の表のセルの `\|` の `\` など）, the Amark Syntax Tree shall 中身の 1 つの行を複数の列の範囲で表し、間の範囲を葉自身の表記として記録できるようにする
10. When パーサか拡張の実装者が、ブロックの行の範囲の一部を新しいブロックに移す（段落の先頭の行をリンク参照定義や表として解釈し直すなど）, the Amark Syntax Tree shall 移した行の範囲、葉自身の表記、中身を、元のバイトとの対応を保ったまま新しいブロックに移す

### Requirement 8: 中身と綴りの記録
**Objective:** As a パーサの開発者と翻訳ツールの開発者, I want 葉の中身とその上のインラインのトークンを、帯や行から独立した位置で記録・参照・置き換えできること, so that インラインの部分木をコンテナから独立に扱い、msgstr を表記のまま差し込める

#### Acceptance Criteria
1. The Amark Syntax Tree shall インラインを持つ葉とコードブロックと HTML ブロックに中身を持たせ、中身が配置済み（各部分が元の行の列の範囲に対応し、行の間を仮想の改行でつなぐ）か自由（元の行に対応しないバイト列を持つ）かを返す
2. When 利用者が葉の中身を取り出す, the Amark Syntax Tree shall 帯と葉自身の表記を除いた中身の文字列を、行の間を改行でつないで返し、中身がタブの途中から始まる行では、そのタブの残りの列を同じ数の空白で表す
3. The Amark Syntax Tree shall 中身の上に、範囲、種類、役割の番号を持つインラインのトークンを記録できるようにし、種類として、テキスト、区切り文字、バックスラッシュのエスケープ、文字参照、置き換えたバイト（NUL や不正な UTF-8）、改行（仮想の改行）、空白、マーカー（リンクの括弧、山括弧、引用符など）、タブの残りの列を区別する
4. If 記録するトークンが中身の外を指すか、UTF-8 の文字の途中に境界を置くか、種類と中身のバイトが合わない（仮想の改行でない位置の改行、タブの残りの列でない位置のタブの残り、空でない範囲を持たないトークンなど）, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
5. When パーサがインラインのトークンを別のノードへ移すか、1 つのテキストのトークンを中身の上の位置で 2 つに分ける, the Amark Syntax Tree shall 中身の上の順序を保ったまま反映する
6. When 利用者が葉の中身を、別の葉（同じ文書か別の文書）の中身とインラインの子で置き換える, the Amark Syntax Tree shall 置き換え先の葉の行の範囲、葉自身の表記、style を残し、中身を自由な中身にし、置き換えたインラインの子とその綴りを保ち、失敗すれば何も変えない
7. When 利用者がコードブロックか HTML ブロックのリテラルを設定する, the Amark Syntax Tree shall 中身を、設定したリテラルを持つ自由な中身にし、葉自身の表記（フェンスの行など）を残す

### Requirement 9: ラベルの正規化と定義の索引
**Objective:** As a パーサとレンダラと翻訳ツールの開発者, I want 参照の形のリンクが定義をラベルで参照し、定義の変更に追従すること, so that 参照リンクを URL に展開せずに保ち、定義を編集すれば参照するすべてのリンクに反映される

#### Acceptance Criteria
1. The Amark Syntax Tree shall CommonMark の規則によるラベルの正規化（Unicode の大文字小文字の畳み込み、空白の連続のまとめ、前後の空白の除去）を提供する
2. The Amark Syntax Tree shall 木の中のリンク参照定義を、正規化したラベルで引けるようにし、同じラベルの定義が複数あれば文書の順で最初のものを返す
3. When リンク参照定義の挿入、削除、移動、ラベルの変更、またはリンク参照定義を含む部分木の挿入と切り離しが起きる, the Amark Syntax Tree shall 以後のラベルによる問い合わせにその変更を反映し、木から切り離した定義を対象にしない
4. When 利用者が参照の形のリンクか画像の宛先かタイトルを取得する, the Amark Syntax Tree shall そのノードのラベルで引いた定義の宛先かタイトルを返す
5. If 参照の形のリンクか画像のラベルで引ける定義がない, then the Amark Syntax Tree shall 宛先とタイトルが解決できないことを返し、ラベルは返す
6. The Amark Syntax Tree shall 参照の形のリンクと画像のラベルを取得・設定できるようにし、ラベルを変えたら、以後の宛先とタイトルの取得で新しいラベルの定義を引く

### Requirement 10: ロスレス性の検証
**Objective:** As a パーサの開発者とテストの開発者, I want 元のソースのすべてのバイトが、配置と綴りのちょうど 1 つの持ち主に属しているかを検証できること, so that 無編集の文書がバイト単位で書き戻せることを局所的な検査で保証できる

#### Acceptance Criteria
1. When 利用者がソースを指定してロスレス性を検証する, the Amark Syntax Tree shall そのソースの各行について、木の中の帯、空行の空白、葉自身の表記、中身の範囲が、行の列の全体を重なりも抜けもなく分割しているかを判定する
2. When 利用者がロスレス性を検証する, the Amark Syntax Tree shall 中身を持つ各葉について、トークンが中身の全体を重なりも抜けもなく覆っているかを判定する
3. The Amark Syntax Tree shall 検証で、範囲の境界をタブ以外の文字の途中に置いた記録を不整合とし、タブの途中の境界は列の単位で認める
4. If 検証で不整合が見つかる, then the Amark Syntax Tree shall ロスレスでないことを表す `foundation` のエラーを返し、最初の不整合の位置（行と列、または葉と中身の上の位置）と、抜け・重なりの別を報告する
5. The Amark Syntax Tree shall 文書の BOM と行の表の改行を、それぞれ文書と行の表が持つバイトとして検証に含める

### Requirement 11: 元の表記を出せるかの問い合わせ
**Objective:** As a Markdown レンダラと拡張の実装者, I want どの部分に元の表記が残っているかを、編集の記録なしに問い合わせられること, so that 元の表記を出す部分と合成する部分を判断できる

#### Acceptance Criteria
1. When 利用者がノードに綴りが残っているかを問い合わせる, the Amark Syntax Tree shall そのノード自身の綴りがすべて残っているか、一部が捨てられたか、綴りを持たないかを返し、捨てられた値と style を特定できるようにする
2. When 利用者が、あるコンテナがある行に帯を持つかを問い合わせる, the Amark Syntax Tree shall 帯の有無と、あればその状態と列の範囲を返す
3. When 利用者が葉の中身を元の行のまま出せるかを問い合わせる, the Amark Syntax Tree shall 中身が配置済みで、その上のすべてのインラインのノードの綴りが残っているときだけ、出せると返す
4. When 利用者が部分木を元の表記のまま出せるかを問い合わせる, the Amark Syntax Tree shall 部分木のすべてのノードの綴りが残り、すべての葉の中身を元の行のまま出せ、部分木のすべての行に部分木の中のコンテナの帯があるときだけ、出せると返す
5. When 利用者がブロックの行に、その祖先のコンテナの帯があるかを問い合わせる, the Amark Syntax Tree shall 祖先ごとに帯の有無を返す（移動したブロックの行には、移動先の祖先の帯がない）
6. The Amark Syntax Tree shall 行の範囲も綴りも持たないノードを、新しいノードとして判定できるようにする

### Requirement 12: ノード型の登録、構文拡張の記述子、構文セット
**Objective:** As a 拡張の実装者とパーサ・レンダラの開発者, I want ノード型と各スロットを構文拡張の単位で登録し、順序付きに組み合わせられること, so that CommonMark と方言を同じ仕組みで組み立て、組み込み構文を無効化・置換できる

#### Acceptance Criteria
1. The Amark Extension Registry shall ノード型を、名前、ブロックかインラインか、包含の規則、型ごとのデータの大きさ、型ごとの関数表（初期化、解放、複製、比較、ダンプ）、style の項目の宣言を持つ記述子で定義できるようにする
2. The Amark Extension Registry shall style の項目を、名前、種類（文字、整数、列挙、真偽）、既定の値で宣言できるようにし、組み込みの型と拡張の型の style を同じ取得・設定の仕組みで扱う
3. The Amark Extension Registry shall 構文拡張を、名前、ノード型の記述子の一覧、スロットの一覧を持つ記述子で定義できるようにし、各スロットは種類の番号と、その種類を定義する spec や利用者が決める中身を持つ
4. When 利用者が構文セットを作る, the Amark Extension Registry shall 組み込みのノード型を登録する拡張を先頭に含め、利用者がそれを取り除けないようにする
5. When 利用者が構文セットに構文拡張を追加・名前を指定して挿入・削除・置換する, the Amark Extension Registry shall 順序を保って反映し、置換では同じ位置に置く
6. If 追加する構文拡張の名前が重複するか記述子が不正である, then the Amark Extension Registry shall 不正な引数を返し、構文セットを変えない
7. When 利用者が構文セットを凍結する, the Amark Extension Registry shall ノード型の名前の重複を検出してエラーにし、拡張のノード型に識別子を割り当て、同じ構成の構文セットには同じ識別子を割り当てる
8. If 凍結した構文セットを変更しようとするか、凍結していない構文セットで文書を作ろうとする, then the Amark Extension Registry shall 不正な状態を返す
9. When パーサやレンダラがスロットの種類を指定して問い合わせる, the Amark Extension Registry shall その種類のスロットを、構文セットの順に、それを持つ構文拡張とともに返す
10. The Amark Extension Registry shall スロットの種類の番号に、Amark の spec が使う範囲と、利用者が独自のスロットに使う範囲を分けて定め、Amark の spec が使う種類を重ならない通し番号の 1 つの一覧として公開ヘッダで定める
11. The Amark Extension Registry shall ノード型の名前や記述子から識別子を、識別子から記述子を、名前から構文拡張を引けるようにする
12. When 利用者が構文セットを複製する, the Amark Extension Registry shall 同じ構文拡張の並びを持つ、凍結していない構文セットを作る
13. The Amark Extension Registry shall 拡張のノード型のデータについて、ノードの生成・解放・複製・比較・ダンプのときに、その型の関数表を呼ぶ

### Requirement 13: 比較とダンプ
**Objective:** As a 後続の spec と利用者のテストの開発者, I want 木の意味の比較と、決定的なテキストのダンプができること, so that パースや編集の結果を期待値と照合し、往復の意味の保存を検証できる

#### Acceptance Criteria
1. When 利用者が 2 つの部分木を比較する, the Amark Syntax Tree shall 型、意味の値、子の並びが一致するときだけ等しいと判定し、style、配置、綴り、ソース上の位置、関連付けたポインタを無視し、参照の形のリンクと画像は解決した宛先とタイトルで比べる
2. Where style を含める指定をして比較する, the Amark Syntax Tree shall style（リンクと画像の形と、参照の形のラベルを含む）も一致するときだけ等しいと判定する
3. Where テキストの正規化を指定して比較する, the Amark Syntax Tree shall 隣り合う text のノードを連結したものとして比べ、空の text のノードを無視する
4. When 利用者が部分木をダンプする, the Amark Syntax Tree shall 型の名前、意味の値、子を、文書化された S 式の形式で、同じ木に対して常に同じテキストとして出力する
5. The Amark Syntax Tree shall ダンプの文字列の中の引用符、バックスラッシュ、制御文字、不正な UTF-8 のバイトを、文書化された方法でエスケープする
6. Where style、行の表、帯、空行、葉自身の表記、中身、インラインのトークン、ソース上の位置を含める指定がある, the Amark Syntax Tree shall 指定したものもダンプに含める
7. The Amark Syntax Tree shall 拡張のノード型のデータと style を、組み込みの型と同じ書式でダンプに含められるようにする
8. Where テスト用の補助を使う, the Amark Tree Test Support shall ダンプと同じ形式の S 式（配置と綴りを含む形を含む）から木を組み立てる

### Requirement 14: API の規約、並行性、性能
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want 構文木の API が共通の規約に従い、大きな文書でも効率よく動くこと, so that ABI を保ったまま長く使え、大きな文書の編集と書き戻しにも使える

#### Acceptance Criteria
1. The Amark Syntax Tree shall 文書、ノード、構文セット、走査の状態を不透明型として公開し、公開する構造体には大きさを表すフィールドを持たせる
2. The Amark Syntax Tree shall `foundation` のエラーコード、出力引数、NULL の扱い、アロケータの規約に従う
3. The Amark Syntax Tree shall 異なる文書を別々のスレッドで同時に使えるようにし、凍結した構文セットを複数の文書とスレッドで共有できるようにする
4. The Amark Syntax Tree shall 1 つの文書（読み取りだけの操作を含む）を同時に使うスレッドを 1 つに限ることを、API の規約として文書化する
5. The Amark Syntax Tree shall ノードの挿入、切り離し、兄弟や親への移動を、文書のノードの数によらず、挿入先の深さに比例する時間以内で行う
6. The Amark Syntax Tree shall 行の表への追加と、行の順・中身の上の順に記録する帯、空行、葉自身の表記、中身、インラインのトークンの記録を、記録の数に比例する時間で行う
7. The Amark Syntax Tree shall ロスレス性の検証を、検証する行の数とトークンの数に比例する時間で行う
8. The Amark Syntax Tree shall ラベルによるリンク参照定義の問い合わせを、文書のノードの数によらない時間（ラベルの長さと、同じラベルの定義の数に比例する時間以内）で行う
9. When レンダラが文書を出力するために、すべてのノードについて「元の表記のまま出せるか」を問い合わせる, the Amark Syntax Tree shall 問い合わせの合計を、文書のノードの数と記録の数に比例する時間で行う
