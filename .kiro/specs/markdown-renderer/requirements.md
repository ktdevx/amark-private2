# Requirements Document

## Project Description (Input)
利用者は、Markdown をパースして木を編集し（例えば、翻訳のためにテキストを置換し）、Markdown に書き戻したい。そのとき、編集していない部分は元の表記のまま残り、編集した部分は編集後の木と同じ意味を持つ正しい Markdown として出力され、無編集なら元の入力とバイト単位で同じ出力にならなければならない。ナイーブに出力すると、テキスト中の記号が構文として解釈される、区切り文字の左右の条件（flanking）が崩れて強調が成立しなくなる、コンテナ（引用やリスト）の中で構造が崩れる、といった問題が起きる。

現状、Markdown レンダラはまだ存在しない。`syntax-tree` の構文トークンと編集の追跡、パーサ（`block-parser`、`inline-parser`）が記録する構文トークンの上に作る。

望む結果：`amark_render_markdown(node, options, ...)` 相当の API で木を Markdown に書き戻せる。往復の保証（無編集ならバイト単位で同じ）、意味の保存（再パースすると編集後の木と意味的に等しい。表現できない木はエラーなどで知らせる）、局所性（出力の変化は編集したノードとその周辺に限る）を必須とする。構文トークンを持たない新しいノードも正しく出力でき、拡張ノードも同じ保証に参加できる。エスケープの方式、区切りの選び直し、コンテナの接頭辞の付け直し、周りの表記との一貫性、拡張のスロットは design で決める。最初は保守的な方式でよいが、公開 API とスロットは後の改善（必要最小限のエスケープ、周りの表記への一致）を妨げない形にする。意味の保存は、出力を再パースして木を比較するプロパティテストで検証し、これを受け入れ基準とする。

## Introduction
Amark は、Markdown テキストと構文木を相互変換する C99 ライブラリである。`markdown-renderer` は、`syntax-tree` の構文木（パースしたもの、編集したもの、手で組み立てたもの）を Markdown に書き戻す。

主な利用者は、翻訳やリンクの書き換えなど、パースした文書の一部だけを編集して書き戻すツールの開発者である。こうしたツールでは、触っていない部分に差分が出ないこと、無編集ならバイト単位で同じになること、編集した部分が編集後の木と同じ意味になることの 3 つが同時に求められる。一般的な AST ライブラリは書き戻しで表記を正規化してしまうため、この 3 つを満たせない。

Amark では、パーサが元の表記をすべて構文トークンとしてノードに記録し、`syntax-tree` がノードごとの編集を記録する。このレンダラは、それらを使って「編集していない部分は元の表記を出し、編集した部分は作り直す」。作り直した部分は、テキストの記号のエスケープ、区切り文字の選び直し、ブロックの区切りの調整、コンテナの接頭辞の付け直しで、周りの元の表記と組み合わさっても意味が変わらないようにする。CommonMark の構文も方言の構文も同じ構文拡張の仕組みで組み立てるため、Markdown の出力もノード型ごとの「出力のスロット」として構文拡張から登録でき、拡張のノードも同じ保証に参加できなければならない。

現在、`foundation`、`syntax-tree`、`html-renderer`、`block-parser`、`inline-parser` の spec がある。Markdown レンダラのコードはまだない。

## Boundary Context
- **In scope**：
  - 部分木を Markdown に出力する入口と、出力先（利用者のバッファ、利用者の書き出しの関数）
  - 元の表記を出すか作り直すかの判定、元の表記の出力、作り直し（エスケープ、区切りの選び直し、ブロックの区切り、コンテナの接頭辞）
  - 新しいノードの表記を選ぶ出力のオプションと、表現できない木の報告
  - ノード型ごとの Markdown 出力のスロット、出力の前後のフック、拡張がエスケープやブロックの区切りの判断に参加する手段
  - 組み込みのすべてのノード型（リンク参照定義を含む）の Markdown 出力を、スロットとして持つ組み込みの構文拡張
  - テスト専用の GFM 相当の検証用拡張（表、取り消し線、タスクリスト、拡張オートリンク、脚注）の Markdown 出力
  - 往復、意味の保存、局所性のプロパティテスト（CommonMark spec の例、コーパス、ファジング）
- **Out of scope**：
  - フォーマッタとしての整形（編集していない部分を書き換えること）。ただし、意味を保つために編集の周辺で必要な最小の変更は含む
  - パース、断片のパースと挿入（`block-parser`、`inline-parser`）
  - HTML の出力（`html-renderer`）
  - 構文トークンのモデルと編集の追跡の定義（`syntax-tree`）
  - 必要最小限のエスケープ、周りの表記への自動的な一致（後の改善。この spec ではそれを妨げない形だけを保証する）
- **Adjacent expectations**：
  - `syntax-tree` は、無編集の文書の全バイトを覆う構文トークン、ノード単位の編集の記録、意味の比較（隣り合うテキストの分け方を無視する比較を含む）を提供する。
  - `block-parser` と `inline-parser` は、元の表記の全バイトを、どのノードの何の表記かが分かる構文トークンとして記録し、役割の番号と、テキストに現れると構文として解釈されうる文字の宣言を公開する。CommonMark の構文セットを作る手段は、組み込みの Markdown 出力の構文拡張を含める。
  - このレンダラが HTML のリテラルの妥当性など、パーサの持つ規則を必要とするときは、上流の spec がその規則を公開する。不足が見つかったら上流の spec に戻って補い、この spec の中で同じ規則を作り直さない。
  - `html-renderer` とは、出力のスロット、前後のフック、出力の文脈、出力先の形を揃える。
  - 上流の spec は、検証用拡張（GFM 相当）の構文トークンの役割の番号（拡張オートリンクを含む）を公開し、テストの生成の手順（例の形式とケースの形式）と CommonMark spec の版の定義を共有できる形で提供する。このレンダラのテストはそれらを再利用し、同じ手順や定義を複製しない。
  - ファジングのターゲットが使うテストの補助は、テストの枠組み（Naos）に依存しない（`foundation` の規則）。

## Requirements

### Requirement 1: 出力の入口と出力先
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want 任意の構文木や部分木を、バッファか自分の書き出しの関数へ Markdown として出力できること, so that 文書全体でも一部分でも、用途に合った出力先へ書き戻せる

#### Acceptance Criteria
1. When 利用者がノード（文書の根、木の途中のノード、木から切り離したノードのどれでもよい）を指定して Markdown の出力を求める, the Amark Markdown Renderer shall そのノードを根とする部分木の Markdown を出力する
2. When 利用者が出力先としてバッファを指定する, the Amark Markdown Renderer shall 出力をバッファの既存の内容の後ろに追加する
3. When 利用者が出力先として書き出しの関数を指定する, the Amark Markdown Renderer shall 出力を先頭から順に渡し、渡したバイト列をつなげたものを、バッファに出力した場合と同じにする
4. If 利用者の書き出しの関数が失敗を返す, then the Amark Markdown Renderer shall 直ちに出力を止め、その失敗を利用者に返す
5. If 出力の途中で確保、拡張の出力の処理、フックのどれかが失敗する, then the Amark Markdown Renderer shall 出力を止めて失敗を返し、出力先がバッファならその内容を呼び出し前の状態に戻し、リークしない
6. The Amark Markdown Renderer shall 出力の間に構文木（ノード、属性、リテラル、構文トークン、編集の記録）を変更しない
7. The Amark Markdown Renderer shall 同じ木、同じ構文セット、同じオプションに対して、常に同じバイト列を出力する

### Requirement 2: 無編集の往復
**Objective:** As a 文書を書き戻すツールの開発者, I want パースしただけの文書を書き戻すと、入力とバイト単位で同じになること, so that 書き戻しだけで差分が生まれず、利用者の文書を壊さない

#### Acceptance Criteria
1. When 構文トークンが全バイトを覆う無編集の文書の根を出力する, the Amark Markdown Renderer shall 元の入力とバイト単位で同じ出力をする
2. The Amark Markdown Renderer shall 要件 2.1 を、タブ（コンテナの接頭辞と内容に分かれた部分的なタブを含む）、LF・CR・CRLF の混在、行末の空白、空白だけの空行を含む入力でも満たす
3. The Amark Markdown Renderer shall 要件 2.1 を、遅延継続行（コンテナの接頭辞が省かれた行）を含む入力でも満たす
4. The Amark Markdown Renderer shall 要件 2.1 を、NUL 文字、不正な UTF-8、先頭の BOM を含む入力でも満たす
5. The Amark Markdown Renderer shall 要件 2.1 を、エスケープ、文字参照、リンク参照定義（同じラベルの重複を含む）、閉じていないフェンスなど、CommonMark のすべての表記と、構文トークンを記録する拡張の表記でも満たす
6. When 無編集の部分木（文書の根でないノード）を出力する, the Amark Markdown Renderer shall その部分木のノードが持つ構文トークンのバイトを、ソース上の順につないだものを出力する

### Requirement 3: 意味の保存
**Objective:** As a 翻訳などで木を編集するツールの開発者, I want 編集した木を書き戻した Markdown が、編集後の木と同じ意味になること, so that 書き戻しの結果を再び読み込んでも、意図しない構造や書式が生まれない

#### Acceptance Criteria
1. When 編集した木を出力する, the Amark Markdown Renderer shall 出力を文書の構文セットで再パースした木が、編集後の木と意味的に等しく（ノード型、属性、リテラル、子の並びが同じ。隣り合うテキストのノードの分け方の違いだけを許す）なる Markdown を出力する
2. The Amark Markdown Renderer shall 要件 3.1 を、リテラルと属性の変更、テキストの一括設定、ノードの挿入・削除・移動・置換、別の文書からの取り込み、断片のパースによる挿入、のどの編集の組み合わせでも満たす
3. When 出力するテキストが、そのまま出すと構文（強調、リンク、コード、HTML、オートリンク、行頭のブロックの構文、拡張の構文など）として解釈されうる文字や並びを含む, the Amark Markdown Renderer shall それがテキストとして再パースされるように表す
4. When テキストの行頭や行末の空白、段落や見出しの先頭と末尾の空白が、そのまま出すと再パースで取り除かれる, the Amark Markdown Renderer shall その空白がテキストとして残るように表す
5. When 強調などの区切り文字が、隣の文字の変化によって元の区切りとして成立しなくなる, the Amark Markdown Renderer shall 区切りの文字の切り替えや、隣の文字の表し方の変更によって、編集後の木と同じ構造になるように表す
6. When 編集によってブロックが新しく隣り合う, the Amark Markdown Renderer shall 隣り合うブロックが結合したり、一方が他方の続きとして解釈されたりしないように、空行の挿入、マーカーの切り替え、表記の切り替えを行う
7. When リンク参照定義が編集・追加・削除・移動される, the Amark Markdown Renderer shall それを参照する参照リンクと、新しく参照リンクになりうるテキストが、編集後の木と同じ意味に再パースされるように表す
8. While 文書の構文セットから一部の構文が取り除かれている, the Amark Markdown Renderer shall その構文セットで再パースしたときに意味が保たれる表記だけを使う

### Requirement 4: 表現できない木の報告
**Objective:** As a 木を編集するツールの開発者, I want どの Markdown でも表せない木を書き戻そうとしたとき、それが分かること, so that 黙って違う意味の文書を保存してしまうことを防げる

#### Acceptance Criteria
1. If 編集後の木が、文書の構文セットで再パースして同じ木になるどの Markdown でも表せない, then the Amark Markdown Renderer shall 出力を失敗させ、表現できないことを示す専用の状態コードを返す
2. When 表現できないことによって出力が失敗し、利用者が報告を求めている, the Amark Markdown Renderer shall 原因のノードと、理由の種類（空の中身、入れ子の文脈、隣り合うノード、区切りの条件、リテラルの形、属性の組み合わせ、出力の処理がない型、拡張の定めた理由）を知らせる
3. The Amark Markdown Renderer shall 少なくとも次の木を表現できないものとして報告する：空の段落、空の強調、リンクの中のリンク、隣り合うコードスパン、3 以上のレベルの見出しの中の改行、段落の末尾のハードブレーク、タイトなリストの項目の中で連続する段落、構文として成立しない HTML のリテラル
4. If 編集したノード（または元の表記を使えないノード）の型に Markdown の出力の処理がない, then the Amark Markdown Renderer shall 表現できないとして報告する
5. The Amark Markdown Renderer shall 文書の構文セットでパースして作った木を、構文トークンを取り除いた場合やすべてのノードを編集済みとした場合も含めて、表現できないとして報告しない

### Requirement 5: 局所性
**Objective:** As a 翻訳などで木の一部だけを編集する開発者, I want 書き戻しの差分が、編集した部分とその周辺だけに収まること, so that 版管理の差分やレビューが編集の内容だけになる

#### Acceptance Criteria
1. While 木の一部だけが編集されている, the Amark Markdown Renderer shall 編集の影響を受けないブロックを、元の表記とバイト単位で同じに出力する
2. The Amark Markdown Renderer shall 出力の変化を次の範囲に限る：(a) 編集したノード自身、(b) インラインの編集では、それを含む葉のブロック（段落、見出し、表のセルなど）の中身、(c) 編集したブロックの前後の区切りと、結合を避けるために必要な隣のブロックの最小の変更（マーカーの切り替え、閉じのフェンスの追加、行頭の字下げの除去、コードブロックの表記の切り替え）、(d) 作り直した行と移動した行のコンテナの接頭辞、(e) 意味が編集に依存するノード（編集したリンク参照定義を参照するリンクと、新しい定義で参照になりうるテキストを含む葉）
3. When 編集した葉の中の、編集していないノードを出力する, the Amark Markdown Renderer shall その表記を保ち、変えるのはテキストのエスケープの追加と区切り文字の調整だけにする
4. When 構文トークンを持つノードを作り直す, the Amark Markdown Renderer shall 元の表記の選択（マーカーの文字、フェンスの文字と長さ、見出しの書き方、リンクの書式、引用符の文字、改行コード、字下げ）を、意味を保てる限り引き継ぐ

### Requirement 6: 新しいノードの出力
**Objective:** As a 木を組み立てたり新しいノードを挿入したりする開発者, I want 構文トークンを持たないノードも正しい Markdown として出力できること, so that パースを経ずに作った文書や、編集で足した内容を書き戻せる

#### Acceptance Criteria
1. When 構文トークンを持たないノードを出力する, the Amark Markdown Renderer shall 組み込みの 19 のノード型のすべてを、要件 3.1 を満たす Markdown として出力する
2. When 新しいノードを出力する, the Amark Markdown Renderer shall オプションで指定した表記を使い、指定がなければ既定の表記を使う
3. When 新しい項目を既存のリストに加える, the Amark Markdown Renderer shall 同じリストの他の項目と同じマーカーの種類を使う
4. When 新しい行を出力する, the Amark Markdown Renderer shall オプションの改行コードを使い、既定では文書の元の入力の改行コード（なければ LF）に合わせる
5. When 新しい強調や取り消し線を、同じ文字の区切りを持つ強調の中や隣に出力する, the Amark Markdown Renderer shall 区切りが互いに結合しないように文字を選ぶ

### Requirement 7: 出力のオプション
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want 新しいノードの表記を選べること, so that 自分のプロジェクトの書き方の規約に合った Markdown を出せる

#### Acceptance Criteria
1. The Amark Markdown Renderer shall 新しいノードの表記として、箇条書きのマーカーの文字、順序付きリストの区切りの文字、強調と強い強調の文字、コードのフェンスの文字、区切り線の文字、見出しの書き方（ATX か setext）、新しい行の改行コード（文書に合わせる、LF、CRLF）を選べるようにする
2. When 利用者がオプションを省略する, the Amark Markdown Renderer shall 既定の表記（`-`、`.`、`*`、`**`、`` ` ``、`*`、ATX、文書に合わせる）で出力する
3. If オプションの大きさが足りない、未知のフラグがある、選べない値が指定されている, then the Amark Markdown Renderer shall 不正な引数を返し、何も出力しない
4. When 指定された表記がその位置で意味を保てない, the Amark Markdown Renderer shall 意味を保てる別の表記を使い、オプションを表記の希望として扱う
5. The Amark Markdown Renderer shall オプションと入口の形を、後からエスケープの方式（必要最小限のエスケープなど）や表記の選び方（周りの表記への一致など）の選択肢を足しても、既存の呼び出しが同じ意味で動く形にする

### Requirement 8: コンテナの接頭辞
**Objective:** As a 引用やリストの中を編集する開発者, I want 作り直した行や移動した行にも正しい接頭辞が付くこと, so that コンテナの中の編集で構造が崩れない

#### Acceptance Criteria
1. When 引用やリスト項目などのコンテナの中の行を新しく出力する, the Amark Markdown Renderer shall その行に、外側から順にすべての祖先のコンテナの接頭辞（項目の最初の行のマーカー、続く行の字下げ、引用の `>`、空行での表し方）を付ける
2. While 行が元の入力の行で、そのコンテナが同じ行に元の接頭辞を持つ, the Amark Markdown Renderer shall 元の接頭辞（遅延継続による省略を含む）を出力する
3. When ノードを別のコンテナへ移動する, the Amark Markdown Renderer shall 移動先の祖先の接頭辞を付け、移動元だけの祖先の接頭辞を出さない
4. If 出力の列が元の列と変わったために、タブの幅や、1 つのタブを接頭辞と内容で分けた表し方が元と同じに保てない, then the Amark Markdown Renderer shall 元の幅の空白で表して、内容の列の位置を保つ
5. When リストの項目のマーカーの幅が変わる, the Amark Markdown Renderer shall その項目の続く行の字下げを新しい幅に合わせる
6. When 項目の順序付きリストの最初の項目が変わる, the Amark Markdown Renderer shall 最初の項目の番号をリストの開始番号に合わせる

### Requirement 9: Markdown の出力のスロット
**Objective:** As a 拡張の実装者, I want 自分のノード型の Markdown の出力と、エスケープやブロックの区切りの判断への参加を、構文拡張から登録できること, so that 拡張のノードも往復・意味の保存・局所性の保証に参加できる

#### Acceptance Criteria
1. The Amark Markdown Renderer shall 構文拡張が、ノード型を名前で指定して、そのノード型の Markdown の出力の処理（ノードに入るときと出るときの処理）をスロットとして登録できるようにし、`html-renderer` と同じ形（後の構文拡張の処理を使う、前の処理を呼べる、子の出力を省ける）で扱う
2. The Amark Markdown Renderer shall 組み込みのノード型の Markdown の出力も同じスロットで提供する組み込みの構文拡張を持ち、組み込みのノード型と拡張のノード型を同じ仕組みで扱う
3. The Amark Markdown Renderer shall 出力の前後のフックと、1 回の出力の間の拡張ごとの状態を提供する
4. The Amark Markdown Renderer shall 出力の処理に、ノードの編集の状態の問い合わせ、元の表記（構文トークン）の出力、エスケープつきのテキストの書き出し、区切り文字の書き出し、ブロックの区切りの規則の指定の手段を提供する
5. The Amark Markdown Renderer shall コンテナの拡張が、行ごとの接頭辞（最初の行、続く行、空行）を定義できるようにする
6. The Amark Markdown Renderer shall 拡張が、エスケープすべき文字の追加、行の中の構文として解釈されうる範囲の検出と保護の要求、出力の前に元の表記を使えないノードの指定（文書全体の依存の表現）、を行えるようにする
7. The Amark Markdown Renderer shall エスケープの判断と区切り文字の検査をレンダラの側にまとめ、出力の処理がエスケープの方式に依存しないようにする
8. If 登録された出力のスロットが不正である（必須の処理がない、スロットの大きさが足りない、指定したノード型が構文セットにない、1 つの構文拡張がフックのスロットを 2 つ以上持つ）, then the Amark Markdown Renderer shall 不正な引数を返し、何も出力しない
9. Where 編集していない拡張のノードが Markdown の出力の処理を持たない, the Amark Markdown Renderer shall そのノードを元の表記のまま出力する
10. The Amark Markdown Renderer shall CommonMark の構文セットを作る手段で作った構文セットだけで、パースした文書を追加の登録なしに出力できるようにする

### Requirement 10: GFM 相当の検証用拡張の Markdown 出力
**Objective:** As a Amark の保守者, I want GFM 相当の構文の Markdown の書き戻しが、テストにだけ置く拡張で公開 API だけを使って書けることを確かめたい, so that 拡張の Markdown 出力の表現力を、配布物に GFM を含めずに保証できる

#### Acceptance Criteria
1. The Amark Markdown Renderer shall 表、取り消し線、タスクリスト、拡張オートリンク、脚注（定義と参照）の Markdown の出力を、テストにだけ置く検証用拡張として、公開 API だけで書けるようにする
2. When 検証用拡張を含む構文セットでパースした無編集の文書を出力する, the Amark Markdown Renderer shall 元の入力とバイト単位で同じ出力をする
3. When 検証用拡張のノードを編集したり新しく作ったりして出力する, the Amark Markdown Renderer shall 要件 3.1 を満たす
4. When 編集したテキストが拡張オートリンクとして解釈されうる並びを含む, the Amark Markdown Renderer shall その並びがリンクにならないように出力する
5. When 表のセルのテキストが列の区切りの文字を含む, the Amark Markdown Renderer shall それを区切りとして解釈されないように出力する

### Requirement 11: 往復と意味の保存のプロパティテスト
**Objective:** As a Amark の保守者, I want 往復・意味の保存・局所性を、出力を再パースして木を比べるテストで継続的に検証したい, so that 書き戻しの方式を変えても保証が崩れないことを確かめられる

#### Acceptance Criteria
1. The Amark Markdown Renderer shall CommonMark spec の全例と、往復のためのコーパス（タブ、CRLF、行末の空白、遅延継続、NUL、BOM、不正な UTF-8、深い入れ子、GFM 相当の構文を含む）で、無編集の往復がバイト単位で同じことを検証するテストを持つ
2. The Amark Markdown Renderer shall 同じ入力について、構文トークンを取り除いた木を出力して再パースした木が元の木と意味的に等しく、表現できないと報告されないことを検証するテストを持つ
3. The Amark Markdown Renderer shall 同じ入力について、決まった編集の手順（特別な文字や空白を含むテキストへの置換、テキストの一括設定、ノードの削除・移動・挿入、属性の変更、インラインのノードの包み込みと展開）を適用し、出力の再パースが編集後の木と意味的に等しいか、表現できないと報告されることを検証するテストを持つ
4. The Amark Markdown Renderer shall テキストの置換とテキストの一括設定の編集では、表現できないと報告されず、編集の影響を受けないブロックが元の表記のまま出力されることを検証するテストを持つ
5. The Amark Markdown Renderer shall 往復と編集の両方のファジングのターゲットを持ち、往復の一致と、編集後の意味の保存または表現できないことの報告を検査する
6. If CommonMark spec のデータがない, then the Amark Markdown Renderer shall そのテストを合格ではなくスキップとして報告する

### Requirement 12: 計算量と堅牢性
**Objective:** As a 信頼できない入力を扱う利用者, I want 出力が入力の大きさにほぼ比例する時間で終わり、深い木でも失敗しないこと, so that 大きな文書や悪意のある文書でもサービスが止まらない

#### Acceptance Criteria
1. The Amark Markdown Renderer shall 出力の時間を、木のノードの数、構文トークンの数、出力の大きさにほぼ比例させる
2. When 深さ 10 万の木を出力する, the Amark Markdown Renderer shall 呼び出しのスタックを深さに比例して使わずに出力するか、表現できないことを報告する
3. If 確保のどれかが失敗する, then the Amark Markdown Renderer shall メモリ不足を返し、リークせず、出力先を要件 1.5 のとおりに戻す
4. The Amark Markdown Renderer shall グローバルな可変状態を持たず、異なる文書を別々のスレッドで同時に出力できるようにする

### Requirement 13: API の規約
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want Markdown レンダラの API が他の Amark の API と同じ規約に従うこと, so that 学ぶことが少なく、ABI が安定する

#### Acceptance Criteria
1. The Amark Markdown Renderer shall 公開の型・関数・定数を `amark_` / `AMARK_` の接頭辞で、`include/amark/` の公開ヘッダに置き、公開ヘッダは他の公開ヘッダだけに依存する
2. The Amark Markdown Renderer shall 公開の構造体に大きさのフィールドを持たせ、知らない末尾のフィールドを読まない
3. The Amark Markdown Renderer shall すべての確保を文書のアロケータで行う
4. The Amark Markdown Renderer shall エスケープのユーティリティを、出力の処理の外でも、文書や構文セットなしに使えるようにする
