# Requirements Document

## Project Description (Input)
ブロック構造が確定した後、段落や見出しの中身を、強調、リンク、コードスパン、オートリンク、生の HTML などのインライン要素として解析するインライン構文エンジンと公開インライン構文 API を提供する。CommonMark の区切り文字のアルゴリズム（delimiter run と process emphasis）とリンク・ブラケットの処理を汎用化し、拡張（新しい区切り文字、きっかけの文字がないインライン構文、ブラケットの後処理）が同じ仕組みに乗れるようにする。CommonMark の全インライン構文を、公開 API だけを使う組み込み拡張として実装し、エスケープや文字参照の元の表記、区切り文字の実際の文字と長さ、リンクの書式、空白などをインラインの構文トークンとして記録する。CommonMark spec（固定した版）の全例を HTML 出力で 100% パスする準拠テストと、GFM 相当（表、取り消し線、タスクリスト、拡張オートリンク、タグフィルタ、脚注）をテストの中にだけ置く検証用拡張の組み合わせで表現できることの検証、インラインのファジングと病的な入力に対する計算量のテストを持つ。利用者は Amark を組み込む C/C++ の開発者と拡張の実装者で、現在インラインの解析は存在せず、`block-parser` の単純な委譲先（テキストとソフトブレークだけ）しかない。

## Introduction
Amark は、Markdown テキストと構文木を相互変換する C99 ライブラリである。`inline-parser` は、`block-parser` がブロック構造を確定させた後に、段落・見出し・拡張のブロック（表のセルなど）の中身を、強調、リンク、画像、コードスパン、オートリンク、生の HTML、改行、テキストといったインライン要素として解析する。

Amark では CommonMark の構文も利用者の拡張と同じ公開 API の上に「組み込み拡張」として乗せる。そのため、この spec は個別の構文の知識を持たない汎用のインライン構文エンジンと、拡張から使える公開のインライン構文 API（きっかけの文字を持つ構文、きっかけの文字を持たない構文、区切り文字の種類、ブラケットの後処理）を提供し、その上に CommonMark の全インライン構文を実装する。さらに、無編集の往復でバイト単位で同じ Markdown を得るため、エスケープ、文字参照、区切り文字、リンクの書式、空白などの元の表記をインラインの構文トークンとして記録する。

この spec は、プロジェクト全体の受け入れゲートでもある。CommonMark spec（固定した版）の全例を HTML 出力で照合する準拠テストと、GFM 相当の構文をテストの中にだけ置く検証用拡張の組み合わせで表現できることの検証を持つ。

現在、`foundation`、`syntax-tree`、`html-renderer`、`block-parser` の spec がある。インラインの解析はまだなく、`block-parser` が提供する単純な委譲先（中身をテキストとソフトブレークとして扱う）だけがある。

## Boundary Context
- **In scope**：
  - 汎用のインライン構文エンジン（文字ごとの構文の試行、テキストのまとめ、区切り文字のスタックとブラケットのスタックの処理）
  - 公開のインライン構文 API（きっかけの文字を持つ構文、きっかけの文字を持たない構文、区切り文字の種類とその規則、ブラケットの後処理、エスケープに関わる文字の宣言）
  - 既定のインラインの委譲先をこのエンジンにすること、インラインの文脈での断片のパース
  - CommonMark の全インライン構文の組み込み拡張と、それらを CommonMark の構文セットに加えること
  - インラインの構文トークンの記録と、その役割の番号の公開
  - CommonMark spec の準拠テスト一式（固定した版の spec の全例、HTML 出力で 100%）
  - テスト専用の GFM 相当の検証用拡張（インライン側の部品）と、ブロック側・HTML 側の検証用拡張と組み合わせた表現力の検証
  - インラインの限度、計算量、ファジング
- **Out of scope**：
  - ブロック構造の解析（`block-parser`）
  - HTML 出力の規則（`html-renderer`）。準拠テストで HTML 出力の不具合が見つかったら `html-renderer` を直す
  - Markdown への書き戻しと、エスケープの方式の決定（`markdown-renderer`）
  - GFM の配布と、GFM spec への公式な準拠の主張
- **Adjacent expectations**：
  - `block-parser` の委譲の取り決め、区間の並びの入力、走査と解読の関数、ラベルの正規化とリンク参照定義の表、CommonMark の構文セットを作る手段を使う。不足が見つかったら `block-parser` の spec を直す。
  - `syntax-tree` のノード、構文トークン、拡張のレジストリを使う。不足が見つかったら `syntax-tree` の spec を直す。
  - `html-renderer` の組み込みの HTML 出力と、GFM 相当の HTML の検証用部品を使う。
  - `markdown-renderer` は、この spec が記録したインラインの構文トークンと、宣言されたエスケープに関わる文字を使って書き戻す。あわせて、区切り文字の開閉の判定と照合、生の HTML の判定を、この spec が公開する関数で行い、同じ規則を重複して実装しない。
  - `block-parser` は、リンクの宛先の括弧の入れ子の上限、エンジンの役割の番号の範囲の分け方、GFM 相当のブロックの検証用拡張のテスト用ライブラリ、文書を返すパースの検査の補助、HTML のタグのバイト列の走査を提供する（`block-parser` の spec が実装する）。この spec はそれらを使い、同じものを実装しない。
  - 例の区切りのデータ（spec.txt と同じ形式）からテストを生成する手順は、`block-parser` が先に唯一の読み手として作る。この spec はそれを拡張し（期待の種類、構文セットの生成関数、検査の関数の指定）、別の読み手を作らない。固定した spec の版の定義はこの spec が 1 か所に置き、他の spec はそれを再利用する。
  - `markdown-renderer` は、この spec の例の生成の手順に別の検査の関数を指定して同じデータを使い、GFM 相当の構文セットに Markdown の出力の検証用部品を加える（その変更は `markdown-renderer` のタスクが行う）。

## Requirements

### Requirement 1: インラインの解析の入口
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want CommonMark の構文セットでパースすると、段落や見出しの中身がインライン要素として解析された木が得られること, so that 構文木を意味の単位で参照・編集・出力できる

#### Acceptance Criteria
1. When 利用者が委譲先を指定せずにパースする, the Amark Inline Parser shall インラインの中身を持つ各ノードの中身を、構文セットのインライン構文に従って解析し、その結果をそのノードの子として加える
2. The Amark Inline Parser shall 既定の委譲先としてこのエンジンを提供し、`block-parser` の単純な委譲先は従来どおり明示して選べるようにする
3. The Amark Inline Parser shall 中身が複数の区間（複数の行、コンテナの接頭辞で区切られた範囲）からなるとき、区間をつないだテキストを中身として解析し、区間の間の表記（コンテナの接頭辞、行頭の空白）を自分のノードに含めない
4. When 利用者が入力を任意の位置で区切って逐次に与える, the Amark Inline Parser shall 一括のパースと同じ木と同じ構文トークンを作る
5. If インライン構文の処理が失敗を返すか、確保が失敗する, then the Amark Inline Parser shall パースを止めてそのエラーコードを返し、リークしない
6. The Amark Inline Parser shall `foundation` のアロケータの設定のほかにグローバルな可変状態を持たず、1 つの凍結した構文セットを、別々のスレッドの複数のパースで共有できるようにする

### Requirement 2: 汎用のインライン構文エンジン
**Objective:** As a 拡張の実装者, I want インラインのエンジンが個別の構文の知識を持たず、構文セットに登録された構文だけで動くこと, so that 組み込みの構文と同じ立場で構文を追加・削除・置換できる

#### Acceptance Criteria
1. The Amark Inline Parser shall 中身を先頭から順に調べ、各位置で、その位置の文字に反応する構文を、指定された優先度の順（同じ優先度なら構文セットの順）に試し、最初に成立した構文に、その構文が消費した範囲を任せる
2. The Amark Inline Parser shall どの構文にも消費されなかった範囲をテキストとして扱い、隣り合うテキストを 1 つのテキストのノードにまとめ、空のテキストのノードを作らない
3. When 構文セットから組み込みの構文の拡張を取り除く, the Amark Inline Parser shall その構文が反応していた文字をテキストとして扱い、他の構文の振る舞いを変えない
4. If どの構文も改行を扱わない, then the Amark Inline Parser shall その改行をソフトブレークとして扱い、構文トークンの被覆を保つ
5. If 構文の処理が成立しないと答えたのに中身を消費したかノードを作った, then the Amark Inline Parser shall 状態の誤りとしてパースを止める
6. The Amark Inline Parser shall エンジンが個別の構文の名前や文字を前提にせず、構文の性質をすべて公開 API の登録から得るようにする

### Requirement 3: きっかけの文字を持つインライン構文
**Objective:** As a 拡張の実装者, I want 特定の文字で始まるインライン構文を、公開 API だけで定義して登録できること, so that コードスパンやオートリンクのような構文を組み込みと同じ方法で書ける

#### Acceptance Criteria
1. The Amark Inline Parser shall 拡張が、反応する文字の集合、優先度、その位置で構文が成立するかを判定して成立すれば範囲を消費する処理を持つインライン構文を定義して登録できるようにする
2. The Amark Inline Parser shall 構文の処理が、中身のテキスト、現在の位置、前後の文字（Unicode の空白と句読点の判定を含む）、中身の終わりまでの範囲を参照できるようにする
3. The Amark Inline Parser shall 構文の処理が、組み込みと拡張のどのインラインのノード型のノードでも作り、構文トークンを記録し、リテラルと属性を設定できるようにする
4. The Amark Inline Parser shall 構文の処理が、リンク参照定義の表、パースの間の拡張の状態、パースのオプションを参照できるようにする
5. The Amark Inline Parser shall 構文の処理が、区切り文字の並びやブラケットの開きを、後の処理に回すために登録できるようにする

### Requirement 4: きっかけの文字を持たないインライン構文
**Objective:** As a 拡張の実装者, I want 拡張オートリンクのように特定の開始の文字を持たない構文を、公開 API だけで定義できること, so that テキストの中の語やパターンからノードを作れる

#### Acceptance Criteria
1. Where 拡張がきっかけの文字を持たないインライン構文を定義する, the Amark Inline Parser shall 他の構文に消費されずテキストになった範囲を、その構文に調べさせ、範囲の一部を構文のノードに置き換えられるようにする
2. The Amark Inline Parser shall きっかけの文字を持たない構文が、調べる範囲の直前の文字（範囲の外を含む）と、その範囲がリンクや画像の中にあるかを参照できるようにする
3. When きっかけの文字を持たない構文がテキストの一部をノードに置き換える, the Amark Inline Parser shall 残りのテキストと新しいノードの構文トークンが、元のバイトを抜けも重なりもなく覆うようにする
4. The Amark Inline Parser shall きっかけの文字を持たない構文を、強調やリンクの組み立てが終わった後のテキストに対して適用し、強調の中の語にも適用できるようにする
5. If きっかけの文字を持たない構文が見つけた範囲に、バックスラッシュのエスケープか文字参照で表された文字が含まれる, then the Amark Inline Parser shall その範囲をノードに置き換えずテキストのまま残し、その範囲の後ろから調べ続ける

### Requirement 5: 区切り文字の種類と規則
**Objective:** As a 拡張の実装者, I want `*` と `_` に加えて、新しい区切り文字（取り消し線の `~` など）を、開く・閉じるの判定、長さの一致の規則、作るノードとともに登録できること, so that 強調と同じ仕組みで新しいインライン要素を定義できる

#### Acceptance Criteria
1. The Amark Inline Parser shall 拡張が、区切り文字の文字、開く・閉じるの判定の規則、開きと閉じの長さが一致するための規則、一致した長さごとに作るノード型を持つ区切り文字の種類を登録できるようにする
2. The Amark Inline Parser shall すべての区切り文字の種類を、CommonMark の区切り文字の処理（左右の flanking、`_` の語の中の規則、長さの和が 3 の倍数になる場合の規則を含む）と同じ手順で、1 つの区切り文字のスタックの上で処理する
3. The Amark Inline Parser shall 異なる種類の区切り文字どうしを一致させない
4. When 区切り文字の開きと閉じが一致する, the Amark Inline Parser shall 間のノードを子とするノードを作り、使った区切り文字の実際の文字と長さをそのノードの構文トークンとして記録する
5. When 区切り文字が一致しないか一部だけが使われる, the Amark Inline Parser shall 残った区切り文字をテキストとして扱い、その文字を構文トークンで覆う
6. The Amark Inline Parser shall 組み込みの強調と強い強調も、この区切り文字の種類の登録だけで実装する
7. The Amark Inline Parser shall 区切り文字の並びの開く・閉じるの判定（flanking の規則）と、区切り文字の並びの列の照合（長さの一致の規則と下限の扱いを含む）を、パースの文脈なしで使える公開の関数として提供し、エンジン自身も同じ関数で判定と照合を行う

### Requirement 6: ブラケットとその後処理
**Objective:** As a 拡張の実装者, I want `[` と `![` で始まり `]` で閉じる範囲の解釈（リンク、画像、脚注の参照など）を、ブラケットの後処理として登録できること, so that リンクと同じ仕組みで新しいブラケットの構文を定義できる

#### Acceptance Criteria
1. The Amark Inline Parser shall ブラケットの開きと閉じを 1 つのブラケットのスタックで扱い、閉じのブラケットが現れたとき、登録されたブラケットの後処理を順に試して、最初に成立したものにノードを作らせる
2. The Amark Inline Parser shall ブラケットの後処理が、ブラケットの中のテキスト、閉じのブラケットの後のテキスト、開きの種類（`[` か `![`）を参照し、閉じのブラケットの後の範囲を消費できるようにする
3. When ブラケットの後処理が成立する, the Amark Inline Parser shall ブラケットの中の区切り文字を先に処理し、ブラケットの中のノードを新しいノードの子にする（後処理が子を持たないノードを作る場合はその中身を構文トークンとして記録できるようにする）
4. When リンクを作るブラケットの後処理が成立する, the Amark Inline Parser shall CommonMark の規則どおり、それより前のリンクの開きを無効にし、リンクの中にリンクができないようにし、拡張のブラケットの構文も同じ規則に参加できるようにする
5. If どのブラケットの後処理も成立しない, then the Amark Inline Parser shall 開きと閉じのブラケットをテキストとして扱う
6. The Amark Inline Parser shall 参照の形式のリンクのラベルを、`block-parser` と同じラベルの正規化とリンク参照定義の表で解決し、拡張が自分の表を同じ正規化で引けるようにする

### Requirement 7: CommonMark のインライン構文の組み込み拡張
**Objective:** As a Amark の利用者, I want CommonMark のすべてのインライン構文が、構文ごとの組み込み拡張として提供され、CommonMark の構文セットに含まれること, so that 追加の設定なしに CommonMark を解析でき、構文ごとに無効化や置き換えもできる

#### Acceptance Criteria
1. The Amark Inline Parser shall CommonMark spec（固定した版）のすべてのインライン構文（バックスラッシュのエスケープ、実体参照と数値文字参照、コードスパン、強調と強い強調、リンク（インライン、完全参照、省略参照、ショートカット参照）、画像、オートリンク（URI とメールアドレス）、生の HTML、ハードブレーク、ソフトブレーク、テキスト）を組み込み拡張として実装する
2. The Amark Inline Parser shall 各インライン構文を、名前で取り除いたり置き換えたりできる構文ごとの拡張として提供する
3. When 利用者が CommonMark の構文セットを作る手段を使う, the Amark Inline Parser shall ブロックの構文の拡張に加えて、すべてのインライン構文の拡張をその構文セットに含める
4. The Amark Inline Parser shall リンクと画像の宛先とタイトル、テキスト、コードスパン、生の HTML のノードの値を、CommonMark の規則どおり（エスケープと文字参照の解読、コードスパンの改行と前後の空白の扱いを含む）に設定する
5. The Amark Inline Parser shall 組み込みのインライン構文を、利用者の拡張と同じ公開 API だけで実装する
6. The Amark Inline Parser shall 構文の間の優先の規則（コードスパン、オートリンク、生の HTML が強調やリンクより強く結びつくこと）を CommonMark spec のとおりに満たす
7. The Amark Inline Parser shall バイト列が 1 つの生の HTML（開始タグ、終了タグ、コメント、処理命令、宣言、CDATA）であるかを、パースの文脈なしで判定する公開の関数を提供し、組み込みの生の HTML の構文と同じ規則で判定する。開始タグと終了タグの規則は `block-parser` の走査を使い、重複して実装しない

### Requirement 8: インラインの構文トークンの記録
**Objective:** As a `markdown-renderer` の開発者, I want インラインの元の表記が、どのノードの何の表記かが分かる構文トークンで、バイト単位で過不足なく記録されること, so that 無編集の往復で同じバイト列を出し、編集した部分だけを作り直せる

#### Acceptance Criteria
1. When CommonMark の構文セットでパースした無編集の文書を検証する, the Amark Inline Parser shall インラインの中身のすべてのバイトが、抜けも重なりもなく構文トークンで覆われた文書を作る
2. The Amark Inline Parser shall バックスラッシュのエスケープと文字参照を、元の表記（エスケープした文字、参照の名前や数値の書き方）が分かる構文トークンとして記録し、エスケープや文字参照として成立しない `\` と `&` をテキストとして記録する
3. The Amark Inline Parser shall 強調などの区切り文字の実際の文字と長さ、コードスパンのバッククォートの長さと内側の空白、オートリンクと生の HTML の表記を、構文トークンとして記録する
4. The Amark Inline Parser shall リンクと画像について、書式（インライン、完全参照、省略参照、ショートカット参照）、宛先の書き方（山括弧の有無）、タイトルの区切りの文字、参照のラベルの表記、間の空白と改行を、構文トークンから区別できるように記録する
5. The Amark Inline Parser shall ハードブレーク（行末の空白か `\`）とソフトブレークの表記、ソフトブレークの前の行末の空白、NUL や不正な UTF-8 を置き換えたバイトを構文トークンとして記録する
6. The Amark Inline Parser shall 組み込みの各インライン構文が使う構文トークンの役割の番号を公開し、利用者とレンダラが表記を区別できるようにする
7. The Amark Inline Parser shall 拡張のインライン構文も、組み込みと同じ方法で構文トークンを記録できるようにする

### Requirement 9: エスケープに関わる文字の宣言
**Objective:** As a `markdown-renderer` の開発者, I want 構文セットのインライン構文が、テキストに現れると構文として解釈されうる文字を宣言し、それを問い合わせられること, so that 編集したテキストを書き戻すときに、拡張の構文を含めてエスケープを判断できる

#### Acceptance Criteria
1. The Amark Inline Parser shall インライン構文と区切り文字の種類が、テキストに現れると構文の開始や区切りとして解釈されうる文字を宣言できるようにし、反応する文字はその宣言に自動的に含める
2. The Amark Inline Parser shall 凍結した構文セットについて、宣言された文字の全体と、どの拡張が宣言したかを問い合わせる手段を提供する
3. The Amark Inline Parser shall エスケープの方式（どの文脈でどの文字をエスケープするか）を決めず、宣言の情報だけを提供する

### Requirement 10: インラインの文脈での断片のパース
**Objective:** As a 木を編集するツールの開発者, I want Markdown の断片をインラインとしてパースし、既存のノードに挿入するとき、完全なインラインの解析が使われること, so that 翻訳したテキストの強調やリンクを正しく木に反映できる

#### Acceptance Criteria
1. When 利用者が CommonMark の構文セットの文書にインラインの文脈で断片をパースして挿入する, the Amark Inline Parser shall 断片をこのエンジンで解析し、得たインラインのノードを挿入する
2. Where 利用者が挿入先の文書のリンク参照定義を使うことを選ぶ, the Amark Inline Parser shall 断片の参照の形式のリンクを、挿入先の文書の定義で解決する

### Requirement 11: 限度、計算量、堅牢性
**Objective:** As a 信頼できない入力を扱う開発者, I want 病的なインラインの入力でも、解析の時間とメモリが入力の大きさにほぼ比例し、異常終了しないこと, so that サービスの中で安全にパースできる

#### Acceptance Criteria
1. The Amark Inline Parser shall 大量の区切り文字、開きだけや閉じだけの区切り文字とブラケット、深い入れ子の強調とブラケット、大量のバッククォート、閉じない生の HTML やオートリンク、大量の参照の形式のリンクなどの病的な入力に対して、解析の時間が入力の大きさに対して二次以上にならないようにする
2. The Amark Inline Parser shall 深い入れ子の入力でも、呼び出しのスタックを使い果たさずに解析を終える
3. The Amark Inline Parser shall CommonMark spec が実装に許す制限（リンクの宛先の括弧の入れ子の深さなど）を設ける場合、その値を文書化し、spec の例をすべて満たす値にする
4. If 中身に NUL や不正な UTF-8 が含まれる, then the Amark Inline Parser shall クラッシュせず、意味の値では U+FFFD に置き換えて解析を続ける
5. The Amark Inline Parser shall 任意のバイト列と任意の区切りの逐次の入力に対して、クラッシュせず、リークせず、構文木の不変条件と構文トークンの被覆を満たす文書を返すことを、ファジングと計算量のテストで確かめられるようにする
6. Where 開発者がテストを無効にしてファジングだけを有効にする, the Amark Inline Parser shall インラインのファジングのターゲットとそれがリンクする検証用のライブラリを、テストのフレームワーク（Naos）に依存せずに構成してビルドできるようにする

### Requirement 12: CommonMark spec の準拠テスト
**Objective:** As a Amark の開発者, I want 固定した版の CommonMark spec の全例が、パースと HTML 出力で期待どおりになることを確かめるテストがあること, so that Amark が CommonMark に 100% 準拠していることを継続的に保証できる

#### Acceptance Criteria
1. The Amark Inline Parser shall 固定した版の CommonMark spec の全例を、例ごとに名前（章と例の番号）の付いたテストとして実行し、既定の CommonMark の構文セットでパースして生の HTML を許可した HTML 出力が期待する HTML と一致することを確かめる
2. The Amark Inline Parser shall 準拠テストのすべての例で、構文トークンの被覆、構文木の不変条件、逐次の入力と一括のパースの結果の一致もあわせて確かめる
3. The Amark Inline Parser shall 使う spec の版を 1 か所に固定し、spec のデータが固定した版と一致しなければ、テストの構成を失敗させる
4. The Amark Inline Parser shall spec のデータをテストデータとしてだけ扱い、そのライセンスと出典を明記し、ライブラリの成果物とソースのリリースのアーカイブに含めない
5. If spec のデータがない（ソースのリリースのアーカイブから構成した場合など）, then the Amark Inline Parser shall 構成とビルドとそれ以外のテストを成功させ、準拠テストを実行しなかったことを、合格と区別できる形で示す
6. Where 開発者が準拠テストを必須にすることを選ぶ（CI など）, the Amark Inline Parser shall spec のデータがないときに構成を失敗させる

### Requirement 13: 拡張 API の表現力の検証（GFM 相当）
**Objective:** As a 方言を実装する拡張の実装者, I want GFM 相当の構文が、公開 API だけで書いた小さな拡張の組み合わせで期待どおりに動くことが、テストで確かめられていること, so that 方言を拡張だけで完全に表現できると信頼できる

#### Acceptance Criteria
1. The Amark Inline Parser shall テストの中にだけ置くインライン側の検証用拡張として、取り消し線（新しい区切り文字）、拡張オートリンク（きっかけの文字を持たない構文）、脚注の参照（ブラケットの後処理と、ブロックの検証用拡張が作る定義の表の参照）を、公開 API だけで実装する
2. The Amark Inline Parser shall これらを、`block-parser` の検証用拡張（表、脚注の定義、タスクリスト）と `html-renderer` の検証用部品（各構文の HTML、タグフィルタ）と CommonMark の構文セットに組み合わせ、表、取り消し線、タスクリスト、拡張オートリンク、タグフィルタ、脚注のそれぞれと、それらを混ぜた文書が、cmark-gfm と同じ形の HTML になることを、自前の代表的な入力で確かめる
3. The Amark Inline Parser shall 検証用拡張を組み合わせた文書でも、構文トークンの被覆と、逐次の入力と一括のパースの結果の一致を確かめる
4. The Amark Inline Parser shall 検証用拡張を配布するライブラリとインストールする成果物に含めない
5. The Amark Inline Parser shall 検証用拡張を書くために上流の公開 API に不足があれば、回避策を作らず、その API を持つ spec の変更として扱う
6. The Amark Inline Parser shall インラインの検証用拡張が記録する構文トークンの役割の番号を検証用拡張のヘッダで公開し、拡張が定義したリンク（ブラケットや括弧のトークンを持たないリンク）を、組み込みのリンクの書式と区別できるようにする

### Requirement 14: 検証
**Objective:** As a Amark の開発者, I want インラインの各構文とエンジンの振る舞いが、木のダンプと構文トークンで確かめられること, so that 準拠テストだけでは見えないトークンや API の誤りを見つけられる

#### Acceptance Criteria
1. The Amark Inline Parser shall インラインの構文ごとに、入力と期待する木のダンプ（構文トークンを含む）の組をデータとして書き、パースした結果と照合するテストを持つ
2. The Amark Inline Parser shall エンジンの API（構文の優先度、区切り文字の種類の規則、ブラケットの後処理、きっかけの文字を持たない構文、文字の宣言、失敗の伝播）を、テストの中だけの最小の構文で確かめるテストを持つ
3. The Amark Inline Parser shall インラインの解析を含むパースで確保が失敗したとき、メモリ不足が返りリークしないことを、確保の失敗の総当たりで確かめる
4. Where 例の区切りのデータから生成するテストに検査の関数が指定される, the Amark Inline Parser shall 既定の照合の代わりにその関数で各例を検査するテストを生成し、他の spec が同じデータと同じ生成の手順を使えるようにする
