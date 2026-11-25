# Requirements Document

## Introduction
Amark は、Markdown テキストと構文木を相互変換する C99 ライブラリである。`syntax-tree` は、パーサ（`block-parser`、`inline-parser`）が作り、レンダラ（`html-renderer`、`markdown-renderer`）と利用者が使う構文木の土台を提供する。

利用者は、Markdown を意味の単位（段落、見出し、強調、リンクなど）で参照・編集したい。主な用途は、組み込み側で翻訳のためにテキストを置換することである。このとき、編集していない部分は元の表記のまま残り、編集した部分だけが変わる必要がある。一般的な AST は元の表記を捨てるため往復で同じ出力にならず、完全な CST は具象構文を意識しないと編集できない。そこで、木は意味を表す AST とし、各ノードに元の表記を構文トークンとして持たせ、編集を追跡する。

この spec は次のものを提供する。

- 意味の単位のノードからなる木と、文書単位のメモリの所有
- CommonMark の全ノード型（とリンク参照定義）を、拡張と同じ登録の仕組みで定義した組み込みのノード型
- 参照、走査（入る/出るイベント）、編集（挿入、削除、移動、置換、複製、取り込み、テキストと属性の変更）
- 元のソースを参照する構文トークンと、ロスレス性の検証
- 編集の追跡（元の表記を使える部分と作り直す部分の区別）
- ノード型の登録、構文拡張の記述子、構文セット（パーサとレンダラがスロットを足す土台）
- 木の比較と、テストで使う S 式のダンプ

現在、`foundation`（ビルド、テスト基盤、アロケータ、バッファ、エラーコード、UTF-8）の spec がある。構文木のコードはまだない。

## Boundary Context
- **In scope**：
  - 文書とノードの生成・解放・所有の規則、元のソースの保持、文書による構文セットの所有
  - 組み込みのノード型（document、block_quote、list、item、code_block、html_block、paragraph、heading、thematic_break、text、softbreak、linebreak、code、html_inline、emph、strong、link、image と、リンク参照定義）とその属性、包含の規則
  - 参照、走査、構造と内容の編集、複製、別の文書からの取り込み（部分木の単位と、ある親の子をまとめた単位）
  - 構文トークンの記録・参照・検証の仕組み
  - 編集の追跡と、その問い合わせ
  - ノード型の登録、構文拡張の記述子、構文セット、スロットの登録と列挙、Amark のスロットの種類の通し番号の割り当て
  - 木の意味の比較、S 式のダンプ、テスト用に S 式から木を組み立てる補助
- **Out of scope**：
  - ブロックとインラインのパースの規則、構文トークンを実際に記録する処理（`block-parser`、`inline-parser`）
  - HTML と Markdown の出力の規則、編集した部分を Markdown に作り直す処理、エスケープの方式（`html-renderer`、`markdown-renderer`）
  - 各スロット（ブロック構文、インライン構文、後処理、HTML 出力、Markdown 出力）の具体的なインタフェースと種類の番号（そのスロットを実行する spec が、この spec が割り当てた番号で定義する）
  - CommonMark の組み込み構文をまとめた構文セットの用意（組み込みの構文拡張を定義する spec が提供する）
- **Adjacent expectations**：
  - パーサは、この spec の構文トークンで元の表記をすべて記録し、無編集の文書がロスレス性の検証に通るようにする。木を組み立てる間は編集の追跡を無効にし、利用者に返す前に有効にする。
  - `markdown-renderer` は、この spec の編集の記録と構文トークンから、元の表記を出せる部分と作り直す部分を判断する。
  - 構文セットを内部で作る便宜の関数を提供する上位の spec（`block-parser` など）は、この spec の構文セットの所有の受け渡しで、作った構文セットの寿命を返す文書に結び付ける。断片を挿入する spec は、子をまとめた取り込みで、一時的な文書の結果を全か無かで挿入する。
  - 構文トークンのモデルやレジストリに不足が見つかったら、下流の spec はこの spec に戻って更新する。

## Requirements

### Requirement 1: 文書とメモリの所有
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want 構文木のすべてのメモリが文書単位で管理され、グローバルのアロケータで確保されること, so that ノードの寿命を個別に追わずに安全に使え、メモリ不足でも壊れない

#### Acceptance Criteria
1. When 利用者が構文セットを指定して文書を作る, the Amark Syntax Tree shall 文書ノードだけを持つ木を作り、その文書のすべての確保を `foundation` のグローバルのアロケータで行う
2. When 利用者が構文セットを指定せずに文書を作る, the Amark Syntax Tree shall 組み込みのノード型だけを持つ構文セットを使う
3. When 利用者が文書を解放する, the Amark Syntax Tree shall その文書に属するすべてのノード（木から切り離したノードを含む）、構文トークン、ソースを解放する
4. When 利用者が元のソースのバイト列を文書に登録する, the Amark Syntax Tree shall バイト列をそのまま複製して文書の寿命の間保持し、識別子を返す
5. When 利用者が登録済みのソースに後からバイト列を追加する, the Amark Syntax Tree shall 既存の内容と、それを参照する構文トークンを変えずに末尾へ追加する
6. When 利用者がノードを木から切り離す, the Amark Syntax Tree shall そのノードと子孫を解放せずに文書に残し、同じ文書の木へ再び挿入できるようにする
7. When 利用者がノードを解放する, the Amark Syntax Tree shall そのノードが木にあれば切り離してから、ノードと子孫を解放する
8. If 確保が失敗する, then the Amark Syntax Tree shall メモリ不足を返し、文書とノードを呼び出し前の状態のまま保ち、リークしない
9. The Amark Syntax Tree shall 異なる文書を別々のスレッドで同時に使えるようにし、凍結した構文セットを複数の文書とスレッドで共有できるようにする
10. When 利用者が、文書が使っている、利用者が作った構文セットの所有権を文書に渡す, the Amark Syntax Tree shall その構文セットを文書の寿命の間保持し、文書の解放のときに、すべてのノードを解放した後でその構文セットを解放する
11. If 所有権を渡す構文セットが、その文書が使っているものでないか、ライブラリが提供する静的な構文セットであるか、文書がすでに構文セットを所有している, then the Amark Syntax Tree shall 不正な引数を返し、文書を変えず、構文セットの所有権を呼び出し側に残す

### Requirement 2: 組み込みのノード型
**Objective:** As a 構文木を使う開発者, I want CommonMark のすべての要素が型と属性を持つノードとして表されること, so that 文書の構造と意味を型に沿って扱える

#### Acceptance Criteria
1. The Amark Syntax Tree shall document、block_quote、list、item、code_block、html_block、paragraph、heading、thematic_break、text、softbreak、linebreak、code、html_inline、emph、strong、link、image とリンク参照定義を、固定の識別子と名前を持つ組み込みのノード型として提供する
2. The Amark Syntax Tree shall 見出しのレベル、リストの種類・開始番号・タイトかどうか、コードブロックの情報文字列、リンクと画像の宛先とタイトル、リンク参照定義のラベル・宛先・タイトルを、ノードの属性として取得・設定できるようにする
3. The Amark Syntax Tree shall text、code、html_inline、code_block、html_block のリテラル（文字列の内容）を取得・設定できるようにする
4. The Amark Syntax Tree shall 各ノード型について、ブロックかインラインか、子を持てるか、どの種類の子を持てるかを、CommonMark の構造に従って定め、問い合わせられるようにする
5. The Amark Syntax Tree shall 組み込みのノード型を、拡張のノード型と同じ記述の形式と登録の仕組みで定義する
6. When 利用者が属性を持たない型のノードの属性を設定する, the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない

### Requirement 3: 参照と走査
**Objective:** As a 文書処理ツールの開発者, I want ノードの関係とソース上の位置を参照し、部分木を文書順に走査できること, so that 見出しの抽出や Lint などを実装できる

#### Acceptance Criteria
1. The Amark Syntax Tree shall 各ノードについて、型、属する文書、親、最初と最後の子、前後の兄弟を返し、該当するノードがなければ「なし」を返す
2. When 利用者が部分木の走査を始める, the Amark Syntax Tree shall 指定した根から文書順に、子を持てるノードには入るイベントと出るイベントを、子を持てないノードには入るイベントだけを返し、指定した根の外へ出ない
3. While 走査の途中である, when 利用者が現在のノードの内容や子を変更するか、現在のノードとその祖先以外を編集する, the Amark Syntax Tree shall 走査を続け、変更後の木の未訪問の部分を文書順に訪問する
4. When 利用者が走査の位置を、根の部分木の中のノードとイベントに設定し直す, the Amark Syntax Tree shall その位置から走査を続ける
5. When 利用者がノードのソース上の範囲を問い合わせる, the Amark Syntax Tree shall そのノードと子孫の構文トークンが覆う範囲を、バイトの位置と、1 から数える行と列で返す
6. If 問い合わせたノードとその子孫が構文トークンを持たない, then the Amark Syntax Tree shall 位置がないことを返す
7. The Amark Syntax Tree shall 利用者が各ノードに任意のポインタを関連付け、取り出せるようにする

### Requirement 4: 構造の編集
**Objective:** As a Markdown を加工するツールの開発者, I want ノードの挿入、削除、移動、置換、複製を木の構造の規則を守ったまま行えること, so that 不正な木を作る心配なく文書を変形できる

#### Acceptance Criteria
1. When 利用者が新しいノードを型を指定して作る, the Amark Syntax Tree shall その文書に属し、どこにも挿入されていないノードを、型の既定の属性で作る
2. When 利用者がノードを、あるノードの前、後、最初の子、最後の子として挿入する, the Amark Syntax Tree shall その位置に挿入する
3. When 挿入するノードがすでに木のどこかにある, the Amark Syntax Tree shall 元の位置から取り除いて新しい位置に移す
4. When 利用者があるノードを別のノードで置き換える, the Amark Syntax Tree shall 置き換えたノードを同じ位置に置き、元のノードを切り離された状態にする
5. If 挿入・移動・置換が、包含の規則に反するか、ノードを自身の子孫の下に置くか、文書ノードを動かすか、別の文書のノードを使う, then the Amark Syntax Tree shall 不正な引数を返し、木を変えない
6. When 利用者がノードを複製する, the Amark Syntax Tree shall 型、属性、リテラル、構文トークン、子孫をすべて複製した、切り離されたノードを同じ文書に作る
7. When 利用者が別の文書のノードを取り込む, the Amark Syntax Tree shall 部分木を複製し、その構文トークンが参照する元のソースも取り込み先の文書に保持して、元の表記を失わない
8. If 取り込み先の文書の構文セットが、取り込むノードの型を持たない, then the Amark Syntax Tree shall 不正な引数を返し、取り込み先の文書を変えない
9. The Amark Syntax Tree shall 入れ子の深さが 10 万段の木でも、走査、複製、取り込み、解放、比較、ダンプをスタックあふれなしに行う
10. When 利用者が、あるノードの子をすべて、挿入先の親と位置を指定して取り込む, the Amark Syntax Tree shall すべての子の部分木を元の順のまま複製して 1 回の操作でその位置に挿入し、子の部分木の構文トークンが参照する元のソースを、ソースごとに 1 回だけ取り込み先の文書に保持する
11. If 子をまとめて取り込むとき、子の部分木のどれかが取り込み先の構文セットにない型を持つか、子のどれかが挿入先の親の包含の規則に反するか、挿入の位置が挿入先の親の子でない, then the Amark Syntax Tree shall 不正な引数を返し、何も挿入せず、取り込み先の文書を変えない

### Requirement 5: 内容と属性の編集の意味論
**Objective:** As a 翻訳ツールの開発者, I want テキストや属性の変更が、Markdown で表現できる値だけを受け付けること, so that 編集が黙って違う意味の出力を招かない

#### Acceptance Criteria
1. When 利用者がリテラルや文字列の属性を設定する, the Amark Syntax Tree shall 値を複製して保持する
2. If 設定する文字列が NUL 文字か不正な UTF-8 を含む, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
3. If 改行を含められないリテラル（text、code）や属性（リンクの宛先、コードブロックの情報文字列）に改行を含む文字列を設定する, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
4. If 空行を含められない値（リンクのタイトル、リンク参照定義のラベル、html_inline のリテラル）に空行を含む文字列を設定する, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
5. When 利用者が複数行のリテラル（code_block、html_block）を設定する, the Amark Syntax Tree shall CRLF と CR を LF に揃え、空でなければ末尾が LF で終わる形で保持する
6. If 属性の値が定められた範囲の外にある（見出しのレベルが 1–6 の外、リストの開始番号が 0–999999999 の外、未知のリストの種類、空の code のリテラル、CommonMark の規則で作れないリンク参照定義のラベル）, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
7. When 利用者がインラインを持てるノードにテキストを一括で設定する, the Amark Syntax Tree shall 既存の子を取り除き、改行（LF、CRLF、CR）で区切ったテキストのノードと、その間のソフト改行のノードを子として置く
8. The Amark Syntax Tree shall 属性やリテラルを変更しても、そのノードの構文トークンを消さずに、元の表記の参考として残す

### Requirement 6: 構文トークン
**Objective:** As a パーサと Markdown レンダラの開発者, I want 各ノードが元の表記を、ソースへの範囲の参照として持てること, so that 無編集の部分を元のバイト列のまま出力できる

#### Acceptance Criteria
1. The Amark Syntax Tree shall 各ノードに、登録済みのソースの範囲を参照する構文トークンの列を持たせ、ソース上の順に並べて返す
2. The Amark Syntax Tree shall 構文トークンの種類として、内容のテキスト、マーカー、空白、改行、バックスラッシュのエスケープ、文字参照、別の文字に置き換えたバイト（NUL や不正な UTF-8）、BOM、遅延継続行の印を区別し、さらにノード型ごとに意味を決める役割の番号を持てるようにする
3. The Amark Syntax Tree shall 改行の構文トークンについて、LF、CRLF、CR のどれであるかを元のバイト列のまま区別できるようにする
4. The Amark Syntax Tree shall コンテナ（引用やリスト項目）が各行の先頭に置く接頭辞（`>`、リストのマーカーやインデント）を、そのコンテナが持ち、行の接頭辞であることがわかる構文トークンとして表せるようにする
5. The Amark Syntax Tree shall 遅延継続行で接頭辞が省かれたことを、長さ 0 の構文トークンとして表せるようにする
6. The Amark Syntax Tree shall 1 つのタブ文字を、列の単位で分けた複数の構文トークン（例えば、コンテナの接頭辞が使う列と、内容が使う残りの列）で表せるようにする
7. When 利用者が構文トークンを追加、削除、または別のノードへ移す, the Amark Syntax Tree shall ソース上の順序を保ったまま反映する
8. If 追加する構文トークンが、未登録のソースや範囲の外を参照するか、種類と元のバイト列が合わない（改行でないバイトの改行、タブでないバイトの部分的なタブ、長さが 0 でない遅延継続行の印など）, then the Amark Syntax Tree shall 不正な引数を返し、ノードを変えない
9. When 利用者が文書のロスレス性を検証する, the Amark Syntax Tree shall 木の中のノードの構文トークンが、指定したソースのすべてのバイトを重なりも抜けもなく覆っているか（部分的なタブは列の合計が一致するか）を判定し、不整合があれば最初の位置を返す

### Requirement 7: 編集の追跡
**Objective:** As a Markdown レンダラの開発者, I want どのノードがどのように編集されたかを問い合わせられること, so that 元の表記を出せる部分と作り直す部分を判断できる

#### Acceptance Criteria
1. While 文書の編集の追跡が有効である, when ノードのリテラルや属性が変わる, the Amark Syntax Tree shall そのノードに「内容の変更」を記録する
2. While 文書の編集の追跡が有効である, when ノードの子の挿入・削除・移動・置換が起きる, the Amark Syntax Tree shall その親に「子の構成の変更」を記録する
3. While 文書の編集の追跡が有効である, when 移動・複製・取り込みしたノードが木に挿入される, the Amark Syntax Tree shall そのノードに「位置の変更」を記録する
4. While 文書の編集の追跡が有効である, when 利用者がノードを新しく作る, the Amark Syntax Tree shall そのノードに「新規」を記録する
5. When ノードに編集が記録される, the Amark Syntax Tree shall そのノードのすべての祖先に「子孫の変更」を記録する
6. When 利用者が、現在と同じ値をリテラルや属性に設定する, the Amark Syntax Tree shall 編集を記録しない
7. While 文書の編集の追跡が無効である, the Amark Syntax Tree shall 編集を記録しない
8. When 利用者が文書を新しく作る, the Amark Syntax Tree shall 編集の追跡を無効の状態で始め、利用者やパーサが有効と無効を切り替えられるようにする
9. When 拡張の実装者が、拡張のノード型のデータを変更したことを通知する, the Amark Syntax Tree shall そのノードに「内容の変更」を記録する
10. The Amark Syntax Tree shall 編集の記録の種類を後から追加できる形で提供し、利用者が未知の種類を「内容の変更」として扱えば判断が安全側になるようにする

### Requirement 8: ノード型の登録、構文拡張の記述子、構文セット
**Objective:** As a 拡張の実装者とパーサ・レンダラの開発者, I want ノード型と各スロットを構文拡張の単位で登録し、順序付きに組み合わせられること, so that CommonMark と方言を同じ仕組みで組み立て、組み込み構文を無効化・置換できる

#### Acceptance Criteria
1. The Amark Extension Registry shall ノード型を、名前、ブロックかインラインか、包含の規則、型ごとのデータの大きさ、型ごとの関数表（初期化、解放、複製、比較、ダンプ）を持つ記述子で定義できるようにする
2. The Amark Extension Registry shall 構文拡張を、名前、ノード型の記述子の一覧、スロットの一覧を持つ記述子で定義できるようにし、各スロットは種類の番号と、その種類を定義する spec や利用者が決める中身を持つ
3. When 利用者が構文セットを作る, the Amark Extension Registry shall 組み込みのノード型を登録する拡張を先頭に含め、利用者がそれを取り除けないようにする
4. When 利用者が構文セットに構文拡張を追加・名前を指定して挿入・削除・置換する, the Amark Extension Registry shall 順序を保って反映し、置換では同じ位置に置く
5. If 追加する構文拡張の名前が重複するか記述子が不正である, then the Amark Extension Registry shall 不正な引数を返し、構文セットを変えない
6. When 利用者が構文セットを凍結する, the Amark Extension Registry shall ノード型の名前の重複を検出してエラーにし、拡張のノード型に識別子を割り当て、同じ構成の構文セットには同じ識別子を割り当てる
7. If 凍結した構文セットを変更しようとするか、凍結していない構文セットで文書を作ろうとする, then the Amark Extension Registry shall 不正な状態を返す
8. When パーサやレンダラがスロットの種類を指定して問い合わせる, the Amark Extension Registry shall その種類のスロットを、構文セットの順に、それを持つ構文拡張とともに返す
9. The Amark Extension Registry shall スロットの種類の番号に、Amark の spec が使う範囲と、利用者が独自のスロットに使う範囲を分けて定める
10. The Amark Extension Registry shall ノード型の名前や記述子から識別子を、識別子から記述子を引けるようにする
11. When 利用者が構文セットを複製する, the Amark Extension Registry shall 同じ構文拡張の並びを持つ、凍結していない構文セットを作る
12. The Amark Extension Registry shall 拡張のノード型のデータについて、ノードの生成・解放・複製・比較・ダンプのときに、その型の関数表を呼ぶ
13. The Amark Extension Registry shall Amark の spec が使うスロットの種類を、重ならない通し番号の 1 つの一覧として公開ヘッダで定め、各 spec が一覧に割り当てられた番号で種類を定義できるようにする

### Requirement 9: 比較とダンプ
**Objective:** As a 後続の spec と利用者のテストの開発者, I want 木の意味の比較と、決定的なテキストのダンプができること, so that パースや編集の結果を期待値と照合し、往復の意味の保存を検証できる

#### Acceptance Criteria
1. When 利用者が 2 つの部分木を比較する, the Amark Syntax Tree shall 型、属性、リテラル、子の並びが一致するときだけ等しいと判定し、構文トークン、ソース上の位置、編集の記録、関連付けたポインタを無視する
2. Where テキストの正規化を指定して比較する, the Amark Syntax Tree shall 隣り合う text のノードを連結したものとして比べ、空の text のノードを無視する
3. When 利用者が部分木をダンプする, the Amark Syntax Tree shall 型の名前、属性、リテラル、子を、文書化された S 式の形式で、同じ木に対して常に同じテキストとして出力する
4. The Amark Syntax Tree shall ダンプの文字列の中の引用符、バックスラッシュ、制御文字、不正な UTF-8 のバイトを、文書化された方法でエスケープする
5. Where 構文トークン、編集の記録、ソース上の位置を含める指定がある, the Amark Syntax Tree shall それらもダンプに含める
6. The Amark Syntax Tree shall 拡張のノード型の属性を、組み込みの型と同じ書式でダンプに含められるようにする
7. Where テスト用の補助を使う, the Amark Tree Test Support shall ダンプと同じ形式の S 式から木を組み立てる

### Requirement 10: API の規約と性能
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want 構文木の API が共通の規約に従い、大きな文書でも効率よく動くこと, so that ABI を保ったまま長く使え、大きな文書の編集にも使える

#### Acceptance Criteria
1. The Amark Syntax Tree shall 文書、ノード、構文セット、走査の状態を不透明型として公開し、公開する構造体には大きさを表すフィールドを持たせる
2. The Amark Syntax Tree shall `foundation` のエラーコード、出力引数、NULL の扱い、アロケータの規約に従う
3. The Amark Syntax Tree shall ノードの挿入、切り離し、兄弟や親への移動を、文書のノードの数によらず、挿入先の深さに比例する時間以内で行う
4. The Amark Syntax Tree shall 編集の記録の祖先への伝播を、連続する編集の全体で、編集の回数に比例する時間で行う
5. The Amark Syntax Tree shall ソース上の順に追加される構文トークンの記録を、トークンの数に比例する時間で行う
