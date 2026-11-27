# Requirements Document

## Project Description (Input)
Markdown をパースして木を編集し（翻訳のためのテキストの置き換え、msgstr の差し込み、リンクの書き換えなど）、Markdown に書き戻す Markdown レンダラを提供する。無編集なら入力とバイト単位で同じ出力（往復の保証）、編集した木は再パースすると編集後の木と意味的に等しい出力（意味の保存）、出力の変化は編集した部分とその区切りに限る（局所性）の 3 つを必須とする。表現できない木は、違う意味の出力を作らずに知らせる。新しいノードも正しく出力でき、拡張のノードもノード型ごとの Markdown 出力のスロットで同じ保証に参加できる。往復と意味の保存は、出力を再パースして木を比べるプロパティテストで確かめる。

2026-11-27 に、`syntax-tree` の新しいモデル（「行 × 帯」と「中身の上の CST」、値と style と綴りの区別、定義の索引、ロスレス性の検証、元の表記の判定）と、改訂した `html-renderer`、`block-parser`、`inline-parser` に合わせて作り直した。前の版（構文トークンと編集の追跡のモデル）の要件は引き継がない。入力は `brief.md` である。

## Introduction
Amark は、Markdown テキストと構文木を相互変換する C99 ライブラリである。`markdown-renderer` は、`syntax-tree` の構文木（パースしたもの、編集したもの、手で組み立てたもの）を Markdown に書き戻す。

主な用途は Markdown と po の相互変換である。原文の段落から msgid を取り出し、翻訳者が書いた msgstr をインラインの断片としてパースして段落の中身と置き換え、翻訳した Markdown を書き出す。このとき、訳していない部分（コードブロック、表、HTML、参照の定義、空行、ほかの段落）は原文とバイト単位で同じに残り、引用やリストの中の段落を訳してもコンテナの構造は崩れず、翻訳者が書いたインラインの表記はそのまま出なければならない。一般的な AST ライブラリは書き戻しで表記を正規化してしまうため、これを満たせない。

`syntax-tree` は、元の表記を「行 × 帯」（行の表、コンテナの行ごとの帯、空行、葉自身の表記）と「中身の上の CST」（葉の中身と、その上のインラインのトークン）の 2 層で持ち、どの部分に元の表記が残っているかを編集の記録なしに問い合わせられる。このレンダラは、行の単位で書き戻す。元の表記が残る部分は元のバイトを出し、残らない部分は style と値から合成する。合成した部分は、エスケープ、区切りの選び直し、ブロックの区切り、帯の合成で、周りの元の表記と組み合わさっても意味が変わらないようにする。

CommonMark の構文も方言の構文も同じ構文拡張の仕組みで組み立てるため、Markdown の出力もノード型ごとの「出力のスロット」として構文拡張から登録でき、拡張のノードも同じ保証に参加できなければならない。

現在、`foundation`、`syntax-tree`（実装済み）、`html-renderer`、`block-parser`、`inline-parser` の spec がある。Markdown レンダラのコードはまだない。

## Boundary Context
- **In scope**：
  - 部分木を Markdown に出力する入口と、出力先（利用者のバッファ、利用者の書き出しの関数）
  - 元の表記を出す部分と合成する部分の判断、行の組み立て（祖先の帯、空行、葉自身の表記、中身）、中身の作り直し（エスケープ、区切りの選び直し、行頭の保護）、ブロックの区切り
  - 新しいノードの表記を選ぶオプション、エスケープの方式、表現できない木の報告
  - ノード型ごとの Markdown 出力のスロット、出力の前後のフック、中身の行のフィルタ、出力の文脈
  - 組み込みの 19 のノード型の Markdown 出力を持つ組み込みの構文拡張と、CommonMark の構文セットへの追加
  - テスト専用の GFM 相当の検証用拡張（表、取り消し線、タスクリスト、脚注、拡張オートリンク）の Markdown 出力の部品
  - 往復、意味の保存、局所性のプロパティテストとファジング
- **Out of scope**：
  - フォーマッタとしての整形（編集していない部分を書き換えること）。ただし、意味を保つために編集の周りで要る最小の変更（区切りの空行、エスケープ）は含む
  - パース、断片のパースと差し込みの操作（`block-parser`、`inline-parser`、`syntax-tree`）
  - HTML の出力（`html-renderer`）
  - 元の表記のモデル、値の導出、参照の解決、元の表記の判定（`syntax-tree`）
  - 必要最小限のエスケープ（後の改善。この spec ではそれを妨げない形だけを保証する）、msgid の空白の正規化、po の形式
- **Adjacent expectations**：
  - `syntax-tree` は、行の表、帯、空行、葉自身の表記、中身、中身のトークン、値と style、定義の索引、意味の比較、元の表記の判定（ノードの綴りの状態、帯の検索、中身と部分木を元の表記で出せるか、新しいノードか）を提供する。このレンダラはそれを使い、同じ判定を作らない。
  - `block-parser` と `inline-parser` は、元の表記を過不足なく記録し、記録の規約と役割の番号、区切り文字の開閉と照合の関数、生の HTML と HTML ブロックの判定、エスケープに関わる文字の宣言を公開する。CommonMark の構文セットの構築関数は、組み込みの Markdown 出力の構文拡張を含める。
  - `html-renderer` とは、出力のスロット、前後のフック、出力の文脈、出力先と入口の形を揃える。ケースの生成の手順を共有する。
  - 上流の spec は、テストの生成の手順、CommonMark spec のデータと版の定義、GFM 相当の構文セットとその検証用のノード型を共有できる形で提供する。このレンダラのテストはそれを再利用し、複製しない。
  - 上流の API に不足が見つかったら、その spec に戻って補い、この spec の中で回避の仕組みを作らない。

## Requirements

### Requirement 1: 出力の入口と出力先
**Objective:** As a 編集ツールの開発者, I want 文書の任意の部分木を、バッファか自分の書き出しの関数へ Markdown として出力できること, so that 文書の全体や一部を必要な場所に書き戻せる

#### Acceptance Criteria
1. When 利用者が文書のノード、木の途中のノード、木から切り離した部分木の根のどれかを指定して Markdown の出力を求める, the Amark Markdown Renderer shall そのノードの部分木を Markdown として出力する
2. When 出力先として利用者のバッファが指定される, the Amark Markdown Renderer shall 出力をバッファの既存の内容の後ろに追加する
3. When 出力先として利用者の書き出しの関数が指定される, the Amark Markdown Renderer shall 出力を 1 回以上の呼び出しに分けて渡し、その連結をバッファへの出力と同じバイト列にする
4. If 書き出しの関数が失敗を返す, then the Amark Markdown Renderer shall 出力を止め、その失敗を利用者に返す
5. If 出力の途中でメモリ不足、出力の処理の失敗、表現できない木のどれかが起きる, then the Amark Markdown Renderer shall 失敗を返し、出力先のバッファを呼び出し前の長さに戻し、確保したものをすべて解放する
6. The Amark Markdown Renderer shall 出力の前後で、木の値、style、配置、綴りを変えない
7. The Amark Markdown Renderer shall 同じ木、構文セット、オプションに対して、常に同じバイト列を出力する
8. When 出力する部分木の根が文書のノードでない, the Amark Markdown Renderer shall 根の祖先のコンテナの帯を出力に含めず、部分木の各行を根の始まりの列から出力する

### Requirement 2: 無編集の往復
**Objective:** As a 編集ツールの開発者, I want 無編集の文書を書き戻すと入力とバイト単位で同じになること, so that 触っていない文書に差分が出ない

#### Acceptance Criteria
1. When パースした直後の無編集の文書の根を出力する, the Amark Markdown Renderer shall 入力とバイト単位で同じバイト列を出力する
2. The Amark Markdown Renderer shall 2.1 を、帯と中身で分け合うタブを含むタブ、LF・CRLF・CR の改行とその混在、最後の行の改行の有無、行末の空白、遅延継続の行、空白だけの行、NUL 文字と不正な UTF-8、先頭の BOM を含む入力でも満たす
3. The Amark Markdown Renderer shall 2.1 を、構文木の「元の表記のまま出せる」の判定（行ごとの分割と中身の被覆に基づくもの）だけに基づいて満たし、Markdown の出力の処理を持たない拡張のノード型を含む文書でも満たす
4. When 元の表記のまま出せると判定された部分木を、元の祖先の下の元の行の位置のまま出力する, the Amark Markdown Renderer shall その部分木の各行を、祖先の帯とともに元の行のバイトのまま出力する
5. When 文書の根を出力し、文書の最初の行のソースが BOM を持つ, the Amark Markdown Renderer shall 出力の先頭に BOM を出す

### Requirement 3: 意味の保存
**Objective:** As a 編集ツールの開発者, I want 編集した木を書き戻すと、再パースで編集後の木と同じ意味になること, so that 編集が黙って違う意味の Markdown を作らない

#### Acceptance Criteria
1. When 編集した木を出力し、出力を同じ構文セットで再パースする, the Amark Markdown Renderer shall 再パースした木が、編集後の木と意味的に等しく（ノードの型、意味の値、子の並び。隣り合うテキストの分け方は問わない）なる出力を作る
2. When 綴りを持たないテキストの値に、そのまま書くと構文として読まれる文字（構文セットが宣言する特別な文字、行頭のブロックの構文、行の端の空白）が含まれる, the Amark Markdown Renderer shall その文字をバックスラッシュのエスケープか文字参照で表し、値を保つ
3. When 綴りを捨てた後に残った値が、設定の関数では拒まれる値（改行を含むテキスト、構造のトークンだけで綴った空のコードなど）である, the Amark Markdown Renderer shall 値を保つ表し方（改行の文字参照など）で出力し、表せなければ表現できないと報告する
4. When 強調、強い強調など、区切り文字の並びで表すノードを合成する, the Amark Markdown Renderer shall 区切り文字（style の文字か、それが使えなければ同じノードを作る別の文字）と前後の文字の表し方を選び、再パースで意図した開きと閉じだけが対になるようにする
5. When コードスパン、リンクの宛先とタイトル、コードブロックのフェンスと情報文字列を合成する, the Amark Markdown Renderer shall 値に合わせて区切り（バッククォートの数、フェンスの文字と長さ、宛先の山括弧、タイトルの引用符）を選び、値を保つ
6. When ブロックの挿入、移動、削除で、元の文書では隣り合っていなかったブロックが隣り合う, the Amark Markdown Renderer shall ブロックがつながったり別の構文として読まれたりしないように、空行か表記の切り替えで区切る
7. When 参照の形のリンクと画像を出力する, the Amark Markdown Renderer shall 定義の索引で解決した宛先とタイトルが、再パースでも同じに解決される形で出力する
8. When リンク参照定義を追加または編集した文書を出力する, the Amark Markdown Renderer shall リンクにならなかったブラケットのテキストが、その定義によってリンクとして読まれないようにする
9. When リストのタイトかどうかの値が、出力の空行から導かれる値と異なる（利用者の上書き、すべての綴りを捨てる操作で写した上書き）, the Amark Markdown Renderer shall 項目の間と項目の子の間の空行を足すか取り除いて、再パースでその値になるようにする
10. When 元の文書で遅延継続のおかげで段落の中身だった行を、帯を合成する文脈へ移して出力する, the Amark Markdown Renderer shall その行がブロックの構文（setext の下線など）として読まれないようにエスケープする

### Requirement 4: 表現できない木の報告
**Objective:** As a 編集ツールの開発者, I want Markdown で表せない木を、違う意味の出力ではなくエラーで知ること, so that 編集を直すか別の表し方を選べる

#### Acceptance Criteria
1. If 木が構文セットの Markdown で表せない（中身が空の段落、空のコード、リンクの中のリンク、段落の末尾のハードブレークとソフトブレーク、定義で解決できない参照の形のリンク、HTML として成り立たない HTML ブロックと生の HTML、区切りの条件を満たせない区切り文字の並び、空行なしには区切れないタイトな文脈のブロック、ルーズにできないリスト）, then the Amark Markdown Renderer shall 違う意味の出力を作らず、表現できないことを表すエラーを返す
2. When 表現できないことを報告する, the Amark Markdown Renderer shall 利用者が報告先を指定していれば、原因のノードと理由の分類を報告先に書く
3. If 元の表記のまま出せないノードの型に Markdown の出力の処理がなく、そのノードの綴りも元の表記として使えない, then the Amark Markdown Renderer shall 表現できないことを報告する
4. The Amark Markdown Renderer shall 拡張が、自分のノード型について、拡張に割り当てた範囲の理由の分類で表現できないことを報告できるようにする

### Requirement 5: 局所性
**Objective:** As a 翻訳ツールの開発者, I want 出力の変化が編集した部分とその周りの区切りに限られること, so that 差分のレビューと版の管理がしやすい

#### Acceptance Criteria
1. When 木の一部を編集して出力する, the Amark Markdown Renderer shall 元の表記のまま出せると判定され、祖先の帯も元のまま出せる部分木の行を、元の行とバイト単位で同じに出力する
2. When 葉の中身の一部を編集する, the Amark Markdown Renderer shall 出力の変化を、その葉の行と、その葉の前後の区切りの空行に限る
3. When ブロックの値か style を編集する, the Amark Markdown Renderer shall そのブロックの表記のうち捨てられていない綴り（葉自身の表記、帯）と、子の元の表記を、意味を保てる限り残す
4. When 中身を作り直す, the Amark Markdown Renderer shall エスケープと区切りの選び直しをその葉の中だけで行い、ほかの葉の表記を変えない
5. When style が設定されたノードを合成する, the Amark Markdown Renderer shall その style の表記（強調の文字、フェンスの文字と長さ、見出しの形、リストの記号、リンクの形と引用符など）で出力し、意味を保つために別の表記が要るときだけ別の表記にする

### Requirement 6: msgstr の差し込み
**Objective:** As a 翻訳ツールの開発者, I want 段落の中身を翻訳者の msgstr で置き換えて書き戻すと、翻訳者の表記と原文のコンテナの表記で出ること, so that 翻訳した Markdown が原文の構造と翻訳者の意図を保つ

#### Acceptance Criteria
1. When 段落（引用やリストの中を含む）の中身を、インラインの断片のパースで作った自由な中身で置き換えて出力する, the Amark Markdown Renderer shall 翻訳者が書いたインラインのバイトを、意味が保てる限りそのまま出力する
2. When 置き換えた中身の i 行目を出力し、その段落が元の i 行目を持つ, the Amark Markdown Renderer shall 元の i 行目の祖先の帯（遅延継続の行では帯を省いたまま）と、その行の葉自身の表記を出力する
3. When 置き換えた中身の行の数が元の行の数より多い, the Amark Markdown Renderer shall 元の行を超えた行に、祖先の帯を style から合成して出力する
4. When 置き換えた中身の行の数が元の行の数より少ない, the Amark Markdown Renderer shall 使わない元の行を出力しない
5. If 翻訳者のバイトが、行頭でブロックの構文として読まれるか、文書の定義と組み合わさって意味が変わる, then the Amark Markdown Renderer shall その部分だけをエスケープして、断片の木と同じ意味を保つ
6. While 文書の一部の段落の中身を置き換えている, the Amark Markdown Renderer shall 置き換えていない部分（コードブロック、HTML、リンク参照定義、空行、ほかの段落、コンテナの帯）を元とバイト単位で同じに出力する

### Requirement 7: 新しいノードとオプション
**Objective:** As a 文書処理ツールの開発者, I want 新しく作ったノードを、選んだ表記の正しい Markdown として出力できること, so that 木を組み立てて文書を作れる

#### Acceptance Criteria
1. When 綴りも行も持たない新しいノード（拡張のノードを含む）を出力する, the Amark Markdown Renderer shall style が設定されていればそれに従い、なければオプションの新しいノードの表記に従って、正しい Markdown を出力する
2. The Amark Markdown Renderer shall 新しいノードの表記として、リストの記号と番号の区切り、強調と強い強調の文字、コードブロックの形とフェンスの文字と長さ、見出しの形、区切り線の文字と数と空白、ハードブレークの形、タイトルの引用符を、オプションで選べるようにする
3. When オプションを指定しない, the Amark Markdown Renderer shall 既定の表記（リストの記号 `-`、番号の区切り `.`、強調 `*`、強い強調 `**`、フェンスのコードブロック `` ``` ``、ATX の見出し、区切り線 `***`、`\` のハードブレーク、タイトルの引用符 `"`）を使う
4. When 新しい行の改行を出力する, the Amark Markdown Renderer shall 文書の既定の改行（文書で最初に現れた改行の種類、なければ LF）を出力する
5. If オプションの大きさのフィールドが小さい、未知の印がある、選択肢の値が範囲の外にある, then the Amark Markdown Renderer shall 不正な引数を返し、何も出力しない
6. The Amark Markdown Renderer shall エスケープの方式をオプションの選択肢として持ち、最初の版では保守的な方式（構文セットが宣言する文字を常にエスケープし、位置で保護する）を提供し、後の版で方式を足せる形にする

### Requirement 8: 帯と行
**Objective:** As a 翻訳ツールの開発者, I want 引用やリストの中の出力の行に、元の帯か正しく合成した帯が付くこと, so that 編集しても移動してもコンテナの構造が崩れない

#### Acceptance Criteria
1. When 出力する行が元の行で、祖先のコンテナがその行に帯を持つ, the Amark Markdown Renderer shall その帯の元のバイトを出力する
2. When 出力する行が新しい行であるか、祖先のコンテナがその行に帯を持たない（移動したブロック、帯を捨てたコンテナ、新しいコンテナ）, the Amark Markdown Renderer shall その祖先の帯を style から合成して出力する
3. When 元の行で祖先のコンテナの帯が遅延継続（帯を省いた行）である, the Amark Markdown Renderer shall その行が段落の継続の行として出力される限り、帯を省いたまま出力する
4. When 出力の列が元の列とずれる, the Amark Markdown Renderer shall 帯と葉自身の表記のタブを元の幅の空白に展開し、中身のタブは値として残し、列のずれていない連続した元の範囲は元のバイトのままで出力する
5. When 空行の帯を合成する, the Amark Markdown Renderer shall 引用では、行が空なら `>` を、空白を含むなら `> ` を出力し、リストの項目では、行が空なら何も出さず、空白だけの行なら字下げを出力する
6. When リストの項目の最初の行の帯を合成し、その行の中身が空白で始まるか、記号と合わせて別の構文（区切り線など）になる, the Amark Markdown Renderer shall 項目を記号だけの行で始め、中身を次の行から字下げして出力する
7. When コンテナの最初の出力の行が、そのコンテナの元の最初の行（項目の記号の行）でない, the Amark Markdown Renderer shall 最初の出力の行の帯を合成し、元の最初の行の帯は続く行の帯として合成し直す
8. When CR だけの改行の直後に、LF で終わる空の行が続く, the Amark Markdown Renderer shall 前の行の改行を CRLF にして、2 つの行が 1 つの改行として読まれないようにする
9. When 改行のない元の行が出力の最後の行でない, the Amark Markdown Renderer shall その行の後に文書の既定の改行を出力する

### Requirement 9: Markdown の出力のスロット
**Objective:** As a 拡張の実装者, I want 自分のノード型の Markdown の出力を、組み込みの型と同じ仕組みで定義できること, so that 拡張のノードも往復、意味の保存、局所性の保証に参加できる

#### Acceptance Criteria
1. The Amark Markdown Renderer shall 拡張がノード型の名前を指定して、そのノード型の Markdown の出力の処理（必須の出力の処理、任意の帯の合成の処理、任意の隣接の判断の処理）を登録できるようにする
2. When 同じノード型に複数の出力の処理が登録されている, the Amark Markdown Renderer shall 構文セットの順で最後のものを使い、その処理が前の処理を呼び出して包めるようにする
3. The Amark Markdown Renderer shall 組み込みのノード型と拡張のノード型の出力の処理を、同じ規則で選ぶ
4. If スロットの大きさが小さい、必須の関数がない、ノード型の名前が構文セットにない、1 つの拡張が前後のフックのスロットを 2 つ以上持つ, then the Amark Markdown Renderer shall 不正な引数を返し、何も出力しない
5. The Amark Markdown Renderer shall 出力の前後のフック（始まりは構文セットの順、終わりと後始末は逆の順）、1 回の出力の間の拡張ごとの状態、中身の行のフィルタ（構文セットの順）を提供する
6. The Amark Markdown Renderer shall 出力の処理に、行の開始と終わり、元の列の範囲の出力、表記の書き出し、エスケープしたテキスト、文字参照、区切り文字の並び、中身の準備と行ごとの出力、ノードの綴りの再利用、子の省略、元の表記の判断の結果、表現できないことの報告を、出力の文脈の関数として提供する
7. The Amark Markdown Renderer shall 拡張が、エスケープする文字を宣言し、中身の行の行頭の保護（自分のブロックの構文として読まれうる行のエスケープ）に参加できるようにする
8. The Amark Markdown Renderer shall スロットの規約（ノード型ごとの処理と前後のフックの 2 種類、名前によるノード型の指定、構文セットの順の連鎖と前の処理の呼び出し、拡張ごとの状態、出力の入口と出力先の形）を、HTML のレンダラと揃える

### Requirement 10: 組み込みの Markdown 出力
**Objective:** As a 利用者, I want CommonMark の構文セットで、組み込みのすべてのノード型を Markdown に出力できること, so that 追加の設定なしに書き戻せる

#### Acceptance Criteria
1. The Amark Markdown Renderer shall 組み込みの 19 のノード型の Markdown の出力の処理を、1 つの組み込みの構文拡張のスロットとして持ち、利用者が取得して自分の構文セットに加えられるようにする
2. When CommonMark の構文セットを作る, the Amark Markdown Renderer shall その構文セットに、組み込みの Markdown 出力の構文拡張を、組み込みの HTML 出力の構文拡張の後に含める
3. When 構文セットが組み込みの Markdown 出力の構文拡張を含まない, the Amark Markdown Renderer shall 組み込みのノード型を出力の処理を持たない型として扱い、元の表記のまま出せる部分を出力する
4. When 利用者が、組み込みの出力の処理と同じノード型の処理を持つ構文拡張を後に加える, the Amark Markdown Renderer shall 後の処理を使う

### Requirement 11: GFM 相当の検証用拡張の Markdown 出力
**Objective:** As a Amark の開発者, I want GFM 相当の構文の Markdown の出力を、公開のスロットだけで書けることを確かめたい, so that 方言を拡張の組み合わせで完全に表現できることを保証できる

#### Acceptance Criteria
1. The Amark Markdown Renderer shall テスト専用の検証用拡張の各ノード型（表、表の行、表のセル、取り消し線、タスクリストの項目、脚注の参照と定義、拡張オートリンク）の Markdown の出力を、公開のスロットと出力の文脈だけで書いた、テストの中の部品として提供する
2. When GFM 相当の構文セットでパースした無編集の文書を出力する, the Amark Markdown Renderer shall 入力とバイト単位で同じ出力にする
3. When GFM 相当の構文セットでパースした文書を編集して出力する, the Amark Markdown Renderer shall 再パースした木が編集後の木と意味的に等しくなる出力（表のセルの `|`、取り消し線の `~`、タスクの記号、脚注のラベル、拡張オートリンクとして検出される文字列の扱いを含む）を作る
4. The Amark Markdown Renderer shall 検証用の部品を配布するライブラリに含めず、テストの枠組みに依存させずにファジングから使えるようにする

### Requirement 12: 往復と意味の保存のプロパティテスト
**Objective:** As a Amark の開発者, I want 往復、意味の保存、局所性を、大量の入力と編集で自動的に確かめたい, so that 書き戻しの保証を受け入れ基準として維持できる

#### Acceptance Criteria
1. The Amark Markdown Renderer shall CommonMark spec の全例について、パースした無編集の文書の出力が入力とバイト単位で同じであることを、テストで確かめる
2. The Amark Markdown Renderer shall CommonMark spec の全例と追加のコーパスについて、決まった編集の操作（テキストの置き換え、綴りを捨てる操作、ノードの挿入・削除・移動、style の変更、中身の置き換え）の後の出力を再パースし、編集後の木と意味的に等しいことを、表現できないと報告した場合を除いて確かめる
3. The Amark Markdown Renderer shall 12.2 の各編集で、編集したブロックとその前後の区切りの外の行が、元の出力の行と同じであることを確かめる
4. The Amark Markdown Renderer shall 任意の入力と編集の操作の列から出力して、異常終了とメモリの誤りがなく、成功した出力が再パースで意味を保つことを、ファジングで確かめる
5. The Amark Markdown Renderer shall これらの検査で、テストの生成の手順、ケースの形式、CommonMark spec のデータと版の定義を上流の spec と共有し、複製しない

### Requirement 13: 規約、計算量、堅牢性
**Objective:** As a ライブラリの利用者, I want Markdown の出力が、ほかの API と同じ規約で、大きな文書や深い木でも安全に動くこと, so that 安心して組み込める

#### Acceptance Criteria
1. The Amark Markdown Renderer shall 公開の型と関数を、プロジェクトの API の規約（接頭辞、構造体の大きさのフィールド、エラーコードと出力引数、不透明型）に従って提供する
2. The Amark Markdown Renderer shall メモリをすべてグローバルのアロケータから確保し、アロケータの設定のほかにグローバルな可変状態を持たない
3. The Amark Markdown Renderer shall 深さ 10 万の木を、呼び出しのスタックを深さに比例して使わずに出力する
4. The Amark Markdown Renderer shall 出力の時間を、出力の行の数と入れ子の深さの積、ノードの数、記録の数、出力の大きさの和に比例する範囲に収める
5. The Amark Markdown Renderer shall NUL 文字や不正な UTF-8 を含む中身と値を、異常終了せずに扱う
