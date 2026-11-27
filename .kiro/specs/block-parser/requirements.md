# Requirements Document

## Project Description (Input)
Markdown のテキストから構文木を作るには、まずブロック構造（引用、リスト、コードブロック、見出し、段落など）を確定させる必要がある。Amark では、CommonMark のブロック構文も、利用者の拡張と同じ公開 API の上に乗せる。そのため、汎用のブロック構文エンジンと、拡張から使えるブロック構文 API が要る。パーサは、元の表記を `syntax-tree` の配置の層（行の表、帯、空行、葉自身の表記、中身）に過不足なく記録しなければならない。

2026-11-27 に、`syntax-tree` の新しいモデル（「行 × 帯」と「中身の上の CST」、値と style と綴りの区別、定義の索引、ロスレス性の検証）に合わせて作り直した。前の版（構文トークンと編集の追跡のモデル）の要件は引き継がない。入力は `brief.md` である。

## Introduction
Amark は、Markdown テキストと構文木を相互変換する C99 ライブラリである。`block-parser` は、Markdown のテキストからブロック構造を確定させ、`syntax-tree` の構文木を組み立てるパーサの入口を提供する。

この spec は、個別の構文の知識を持たない汎用のブロック構文エンジンと、拡張から使える公開のブロック構文 API を提供し、その上に CommonMark の全ブロック構文とリンク参照定義を組み込み拡張として実装する。パーサは、無編集の往復でバイト単位で同じ出力を得られるように、元の表記を行と列の単位で記録する。コンテナの左端の帯は帯として、葉の記号やフェンスやインデントは葉自身の表記として、残りは葉の中身として記録する。表記の区別（フェンスの文字、見出しの書き方など）は style として記録する。リストのタイトは記録せず、空行から導かせる。

段落や見出しの中身（インライン）の解析は、後続の `inline-parser` が提供するエンジンへ、葉の中身を単位に委譲する。この spec は委譲の取り決めと、インラインのエンジンがない状態でも中身を覆える既定の扱いを定める。

現在、`foundation`（ビルド、テスト基盤、共通基盤）、`syntax-tree`（ノード、値と style、配置と綴り、定義の索引、ロスレス性の検証、拡張のレジストリ）、`html-renderer`（組み込みのノード型の HTML 出力の構文拡張、GFM 相当の HTML の検証用の部品）の spec がある。パーサのコードはまだない。

## Boundary Context
- **In scope**：
  - パーサの公開 API：構文セットとオプションの指定、文字列全体のパース、逐次の入力、ブロックの断片とインラインの断片のパース
  - 入力の行と文字の扱い（改行の種類、タブ、NUL、不正な UTF-8、BOM）を、バイトを書き換えずに記録で表すこと
  - 汎用のブロック構文エンジン、公開のブロック構文 API、文書の後処理、パースの間の拡張の状態
  - インラインへの委譲の取り決めと既定の委譲先
  - CommonMark の全ブロック構文とリンク参照定義の組み込み拡張、その style の記録、CommonMark の構文セットの構築
  - 帯、空行、葉自身の表記、中身、コードブロックと HTML ブロックとリンク参照定義の中身の綴りの記録と、その規約の公開
  - 下流と共有する走査の関数、ブロックのパースの限度、計算量、ファジング、ベンチマーク
  - ブロックの GFM 相当の検証用の部品と、テストの補助、共有のテストの生成手順
- **Out of scope**：
  - インライン構文の解析と区切り文字の処理（`inline-parser`）
  - CommonMark spec の全例での HTML 出力の 100% の判定（`inline-parser`）
  - HTML と Markdown への出力（`html-renderer`、`markdown-renderer`）
  - 記録の形と検査、値の導出、定義の索引と参照の解決、ラベルの正規化、タイトの導出（`syntax-tree`）
  - GFM 相当の構文そのものの配布（検証用の部品はテストの中にだけ置く）
- **Adjacent expectations**：
  - `syntax-tree` は、行の表、記録の関数とその検査、値の導出、定義の索引、ラベルの正規化、ロスレス性の検証を提供する。この spec はそれらを自前で持たない。不足が見つかったら `syntax-tree` に戻って更新する。
  - `html-renderer` は、組み込みのノード型の HTML 出力の構文拡張と、GFM 相当の検証用のノード型を登録する HTML の部品を提供する。この spec の CommonMark の構文セットは、その構文拡張を組み込みのノード型の拡張の直後に含める。検証用の部品は、そのノード型を参照するだけで登録しない。
  - `inline-parser` は、この spec の委譲の取り決めで葉の中身を解析し、中身の全体をトークンで覆う。段落と見出しの中身の末尾の空白もインラインの解析が綴る。GFM 相当の検証とテストの生成では、この spec の検証用の部品と共有の手順を再利用する。
  - `markdown-renderer` は、この spec が公開する記録の規約と HTML ブロックの判定を使い、CommonMark の構文セットに組み込みの Markdown 出力の構文拡張を足す。

## Requirements

### Requirement 1: パーサの公開 API
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want 構文セットとオプションを指定して、文字列全体または逐次の入力から構文木を得られること, so that 用途（一括の変換、ストリームの処理）に合わせてパースできる

#### Acceptance Criteria
1. When 利用者が凍結した構文セットとオプションを指定して文字列全体をパースする, the Amark Block Parser shall その構文セットに従って構文木を持つ文書を作って返し、すべての確保を `foundation` のグローバルのアロケータで行う
2. When 利用者が構文セットを用意せずに CommonMark でパースする手段を使う, the Amark Block Parser shall CommonMark の構文セットを内部で作り、返した文書がその構文セットの所有権を持つようにする
3. When 利用者が入力を任意の位置で区切って順に与え、最後に終了を指示する, the Amark Block Parser shall 区切りの位置（改行の CR と LF の間や UTF-8 の文字の途中を含む）によらず、同じ入力を一括でパースしたときと同じ木、同じ記録、同じ行の表を持つ文書を返す
4. If 利用者が凍結していない構文セットか、未知のオプションの値を指定する, then the Amark Block Parser shall パーサを作らずに、凍結していない構文セットには状態の誤りを、未知のオプションには引数の誤りを返す
5. If 利用者が終了の後に入力を与えるか、もう一度終了を指示する, then the Amark Block Parser shall 状態の誤りを返す
6. If パースの途中で確保が失敗する, then the Amark Block Parser shall メモリ不足を返し、リークせず、以後はパーサの解放だけを受け付ける
7. The Amark Block Parser shall 利用者が指定した構文セットを読み取り専用で借りるだけで変更も解放もせず、所有権を文書へ渡すことは `syntax-tree` の所有の受け渡しに任せる
8. The Amark Block Parser shall 異なるパーサを別々のスレッドで同時に使えるようにし、1 つの凍結した構文セットを複数のパーサで共有できるようにする

### Requirement 2: 入力の行と文字の扱い
**Objective:** As a 信頼できない Markdown を扱う開発者, I want どんなバイト列を与えても CommonMark どおりに解釈され、元のバイトが書き換えられずに記録されること, so that 不正な入力でも安全に処理でき、往復で同じバイト列を得られる

#### Acceptance Criteria
1. The Amark Block Parser shall LF、CR、CRLF のいずれも行の終わりとして扱い、改行の種類を文書の行の表に任せ、意味の値ではどの改行も LF として扱う
2. The Amark Block Parser shall ブロック構造の判定でタブを 4 列ごとのタブストップとして扱い、入力のタブを書き換えず、1 つのタブを帯、葉自身の表記、中身のうち複数で分けるときは列の範囲で分ける
3. When 入力に U+0000 か不正な UTF-8 のバイト列が含まれる, the Amark Block Parser shall クラッシュせず、ブロック構造の判定では普通の文字として扱い、元のバイトを記録に残し、意味の値では U+FFFD に置き換わるように綴る
4. When 入力が UTF-8 の BOM で始まる, the Amark Block Parser shall BOM を内容として扱わず、文書のソースの印に任せる
5. When 入力の最後の行が改行で終わらない, the Amark Block Parser shall その行を通常の行として扱う
6. When 入力が空である, the Amark Block Parser shall 子を持たない文書を返す

### Requirement 3: 汎用のブロック構文エンジン
**Objective:** As a 拡張の実装者, I want ブロックの解析の手順が構文の知識を持たない汎用のエンジンとして動くこと, so that 組み込みの構文と同じ条件で、自分の構文を組み合わせられる

#### Acceptance Criteria
1. When 1 行を処理する, the Amark Block Parser shall 開いているブロックを外側から順に、それぞれの構文の継続の条件と照合し、照合できなかった最初のブロックより内側を照合できなかったものとして扱う
2. When 照合の後に行の残りがあり、最も内側の開いているブロックが新しいブロックの開始を許す, the Amark Block Parser shall 構文セットに登録されたブロック構文の開始の条件を優先度の順に試し、成立した構文でブロックを開き、行の残りで続けて開始を試す
3. When 新しいブロックが開く, the Amark Block Parser shall 照合できなかったブロックと、新しいブロックを子として受け入れられない祖先を閉じてから、新しいブロックを受け入れられる最も内側のブロックの子とする
4. While 最も内側の開いているブロックが遅延継続を受け入れる段落の類のブロックである, when 照合できなかったコンテナがあり、新しいブロックが開かず、行が空行でない, the Amark Block Parser shall その行を遅延継続行としてそのブロックに加え、照合できなかったコンテナを閉じない
5. When 空行を処理する, the Amark Block Parser shall ノードを作らず、各構文の継続の条件に空行かどうかを渡し、その空行が中身になるか、どのブロックの空行になるかを後の行で確定させる
6. When 入力の終わりに達する, the Amark Block Parser shall 開いているすべてのブロックを内側から順に閉じる
7. The Amark Block Parser shall エンジン自体に個別の構文（段落、引用、リストなど）の知識を持たせず、組み込みの構文もすべて登録されたブロック構文として扱う
8. When 構文セットからある組み込みの構文が取り除かれる, the Amark Block Parser shall その構文が認識されないことを除いて、他の構文の解析を変えない

### Requirement 4: 公開のブロック構文 API
**Objective:** As a 拡張の実装者, I want 新しいブロック構文を公開 API だけで定義でき、組み込みの構文と同じ表現力を持つこと, so that GFM 相当の表や脚注を含む方言を拡張の組み合わせだけで実装できる

#### Acceptance Criteria
1. The Amark Block Parser shall 拡張がブロック構文の開始の条件、優先度、開いたブロックの継続の条件、行の受け入れ、閉じる処理を定義し、構文拡張のスロットとして登録できるようにする
2. The Amark Block Parser shall 拡張が開始の条件ごとに段落を中断しうるかを指定でき、さらに行の内容に応じて中断するかを判断できるようにする
3. The Amark Block Parser shall 拡張が 1 つの行で複数のブロックを入れ子に開くこと（例：リストとリスト項目）と、開いているブロックごとにパースの間だけ使うデータを持つことをできるようにする
4. The Amark Block Parser shall 拡張が現在の行について、行の番号、列、インデント、空行かどうか、行のバイトを問い合わせ、列の単位（タブの途中を含む）で読み進められるようにする
5. The Amark Block Parser shall 拡張が読み進めた列を、行と列の範囲で、コンテナの帯、葉自身の表記（種類、綴る項目、役割を指定する）、葉の中身の部分として記録できるようにし、ソースのオフセットを計算させない
6. When 拡張が開いている段落の類のブロックの末尾の行を別のブロックとして解釈し直す, the Amark Block Parser shall その行を元のブロックから取り除いて新しいブロックが記録し直せるようにし、元のブロックに行が残らなければ木から除く
7. The Amark Block Parser shall 拡張が開いている段落の類のブロックを、そのすべての行を引き継ぐ別の型のブロックに置き換えられるようにする（例：setext 見出し）
8. The Amark Block Parser shall 拡張が段落の先頭の行を消費して別の葉（例：リンク参照定義）を作る処理を登録でき、段落が閉じるときと、他の構文が明示的に求めたときに実行されるようにする
9. If 拡張のコールバックが失敗を返す, then the Amark Block Parser shall パースを止めてそのエラーコードを呼び出し側へ返す
10. The Amark Block Parser shall 組み込みの構文が使うフックをすべて公開 API として提供し、内部専用の近道を使わない

### Requirement 5: 文書の後処理とパースの間の拡張の状態
**Objective:** As a 拡張の実装者, I want パースが終わった後に木を書き換える処理と、パースの間に拡張が共有する状態を持てること, so that 脚注やタスクリストのように、文書全体を見て結果を書き換える構文を実装できる

#### Acceptance Criteria
1. The Amark Block Parser shall 拡張が文書の後処理を登録でき、インラインの解析の前と後のどちらで実行するかを選べるようにする
2. When 終了が指示される, the Amark Block Parser shall すべてのブロックを閉じた後、インラインの解析の前の後処理、インラインの解析、インラインの解析の後の後処理の順に、それぞれ構文セットの順で実行する
3. The Amark Block Parser shall インラインの解析の前の後処理が、葉の中身の先頭を表記に変える操作と、ブロックを同じ記録を持つ別の型のブロックに置き換える操作を使えるようにする
4. The Amark Block Parser shall 拡張がパースの間だけ存在する自分の状態を持ち、ブロック構文、インラインの解析、後処理のどこからでも参照できるようにする
5. When パースが終わるか失敗する, the Amark Block Parser shall 拡張の状態とパースの間だけのデータを解放する

### Requirement 6: インラインへの委譲
**Objective:** As a `inline-parser` の開発者, I want ブロックの解析が、段落や見出しの中身を明確な取り決めでインラインのエンジンへ渡すこと, so that ブロックとインラインを別々に実装でき、中身の上の位置で綴りを記録できる

#### Acceptance Criteria
1. When すべてのブロックが閉じ、インラインの前の後処理が終わる, the Amark Block Parser shall インラインを受け入れる中身を持つ各葉について、文書の順に、その葉をインラインのエンジンへ渡す
2. The Amark Block Parser shall インラインのエンジンに渡す葉の中身を、帯と葉自身の表記を除いた残りを行の順に仮想の改行でつないだ配置済みの中身とし、段落では各行の先頭の空白を葉自身の表記にし、最後の行の末尾の空白を中身に残す
3. The Amark Block Parser shall インラインのエンジンが、パースの間の文脈（文書、オプション、定義の検索、拡張の状態）を参照できるようにする
4. Where 利用者がオプションでインラインのエンジンを指定する, the Amark Block Parser shall そのエンジンへ委譲し、指定しないときは既定の委譲先（ライブラリにインラインのエンジンがない間は 5 の単純な委譲先）へ委譲する
5. The Amark Block Parser shall 中身の各行をテキストのノード、行の間をソフトブレークのノードとし、中身の全体をトークンで覆う単純な委譲先を公開し、インラインのエンジンがない構成でもロスレス性の検証に通る文書を作れるようにする
6. If インラインのエンジンが失敗を返す, then the Amark Block Parser shall パースを止めてそのエラーコードを返す

### Requirement 7: CommonMark のブロック構文の組み込み拡張
**Objective:** As a Amark の利用者, I want CommonMark の全ブロック構文が組み込み拡張として提供され、構文ごとに無効化や置き換えができること, so that CommonMark どおりにパースでき、方言に合わせて構文を組み替えられる

#### Acceptance Criteria
1. The Amark Block Parser shall thematic break、ATX 見出し、setext 見出し、インデントされたコードブロック、フェンスで囲まれたコードブロック、HTML ブロックの 7 種類、リンク参照定義、段落、引用、リスト項目、リストを、CommonMark 0.31.2 の規則どおりに解析する
2. The Amark Block Parser shall 空行と遅延継続（エンジンが扱う）を除く各ブロック構文を、構文ごとに名前を持つ構文拡張として、公開のブロック構文 API の上に実装する
3. The Amark Block Parser shall 組み込みのノード型の拡張、組み込みのノード型の HTML 出力の拡張、CommonMark のブロック構文の拡張をこの順に加えた構文セットを作る手段と、利用者の構文セットにそれらを同じ規則で加える手段を提供する
4. The Amark Block Parser shall 各組み込み拡張を名前で取り除いたり、別の拡張に置き換えたりできるようにする
5. The Amark Block Parser shall リストの種類と開始番号、見出しのレベル、コードブロックの情報文字列とリテラル、HTML ブロックのリテラルが、CommonMark の規則どおりの値として構文木から取得できるように記録する
6. The Amark Block Parser shall 各構文の表記の区別を style として記録する（リストの記号と区切り、見出しの書き方と閉じの記号の有無、コードブロックの形とフェンスの文字と長さ、thematic break の文字と数と間の空白、リンク参照定義の宛先の山括弧とタイトルの引用符）
7. The Amark Block Parser shall リストのタイトを記録せず、構文木が空行から導く値が CommonMark の規則と一致するように空行を記録する
8. The Amark Block Parser shall HTML ブロックの 7 種類の開始の条件（段落を中断する場面かどうかを含む）、種類 1–5 の終わりの条件、HTML の開始タグと終了タグの走査を、1 行または 1 つの連続したバイト列の上で使える公開の関数として提供し、HTML ブロックの組み込み拡張も同じ関数で判定する

### Requirement 8: リンク参照定義
**Objective:** As a インラインの解析と拡張の実装者, I want リンク参照定義が CommonMark どおりに解析され、構文木の定義の索引で引けること, so that 参照リンクを一貫して解決でき、定義の編集にも追従できる

#### Acceptance Criteria
1. When 段落が閉じるとき、またはほかの構文が求めたときに、段落の先頭の行がリンク参照定義の規則を満たす, the Amark Block Parser shall その行を行の単位で切り出してリンク参照定義の葉として段落の前に置き、残りの行を段落として残す
2. When 段落のすべての行がリンク参照定義になる, the Amark Block Parser shall 空の段落を木に残さない
3. The Amark Block Parser shall リンク参照定義のラベル、宛先、タイトルを中身の綴りとして記録し、構文木から取得した値が CommonMark の規則（ラベルは元の表記のまま、宛先とタイトルはバックスラッシュのエスケープと文字参照を解読した値）と一致するようにする
4. When 同じ正規化したラベルを持つ定義が複数ある, the Amark Block Parser shall どの定義もノードとして木に残し、どれを使うかを構文木の定義の索引（文書の順で最初の定義）に任せる
5. The Amark Block Parser shall 自前の定義の表とラベルの正規化を持たず、インラインの解析と拡張の定義の検索を構文木の定義の索引とラベルの正規化に任せる
6. The Amark Block Parser shall ラベルの空白を CommonMark の規則（空白、タブ、LF、CR だけ）で扱い、空白だけのラベルと 999 文字を超えるラベルを定義として扱わない
7. The Amark Block Parser shall リンクのラベル、宛先、タイトルの走査と、値を綴る単位（テキスト、エスケープ、文字参照、置き換え、改行）への分割を、1 つの連続したバイト列の上で使える公開の関数として提供し、インラインの解析が同じ規則で使えるようにする

### Requirement 9: 配置と綴りの記録
**Objective:** As a `markdown-renderer` の開発者, I want 無編集の文書の全バイトが、帯、空行、葉自身の表記、中身のちょうど 1 つの持ち主に記録されていること, so that 無編集の往復でバイト単位で同じ出力を作り、編集した部分だけを作り直せる

#### Acceptance Criteria
1. When 中身の全体を綴る委譲先でパースした無編集の文書をロスレス性の検証にかける, the Amark Block Parser shall 行ごとの分割と中身の被覆のどちらにも通る文書を作る
2. The Amark Block Parser shall 引用の記号と後の空白、リスト項目の記号と後の空白、続く行のインデントを、そのコンテナの帯として記録し、項目の最初の行の帯に最初の行の印を付ける
3. When 遅延継続行を処理する, the Amark Block Parser shall 帯を省いた各コンテナに、遅延継続の帯を、外側の祖先の帯が終わる列（なければ列 0）で記録する
4. The Amark Block Parser shall 空行を、その行を含み、その行を行の範囲に含む子を持たない一番内側のブロック（文書を含む）の空行として記録し、ブロックの行の範囲に末尾の空行を含めない
5. While 空行がリスト項目の中にある, the Amark Block Parser shall 項目の継続の判定を cmark の順で行い（先に字下げが項目の幅に足りるかを調べ、足りればその幅だけ読み進め、足りなければ空行で子があるときだけ空白をすべて読み進める）、その行の項目の帯を長さ 0 の帯とし、項目が読み進めた空白を空行の持ち主かコードブロックの記録に渡す
6. The Amark Block Parser shall 見出しの記号と閉じの記号、フェンスの行、setext の下線、thematic break、インデントされたコードの字下げ、段落の各行の先頭の空白、見出しとフェンスと thematic break の行の行末の空白を、葉自身の表記として記録し、フェンスの情報文字列はその値を綴る表記として記録する
7. The Amark Block Parser shall コードブロックと HTML ブロックの中身の全体を中身の綴りで覆い、NUL と不正な UTF-8 を置き換えの綴りに、タブの途中から始まる中身の空白をタブの残りの綴りにする
8. The Amark Block Parser shall 組み込みの各構文の記録の規約（どの列を帯、空行、葉自身の表記、中身にするか、表記の種類と役割の番号）を公開し、利用者とレンダラが表記を区別できるようにする

### Requirement 10: 断片のパース
**Objective:** As a 構文木を編集する開発者（msgstr の差し込みなど）, I want Markdown の断片をブロックまたはインラインの文脈でパースし、既存の文書の指定した位置へ差し込めること, so that テキストの置換や新しい内容の挿入を、元の表記つきの正しいノードとして行える

#### Acceptance Criteria
1. When 利用者が断片をブロックの文脈でパースして挿入先の親と位置を指定する, the Amark Block Parser shall 断片を自分の行の表を持つ部分木としてパースし、得たブロックと、それが参照する行を挿入先の文書へ取り込んで、その位置に挿入する
2. When 利用者が断片をインラインの文脈でパースして挿入先の葉を指定する, the Amark Block Parser shall 断片の全体を自由な中身としてインラインのエンジンで解析し、挿入先の葉の中身とインラインの子をそれで置き換え、葉の行の範囲、帯、葉自身の表記、style を残す
3. Where 利用者が挿入先の文書の定義を使うことを選ぶ, the Amark Block Parser shall 断片の中の参照を、断片の定義に加えて挿入先の文書の定義でも解決できるものとして解析する
4. If 得たノードのどれかを挿入先に置けないか、途中で失敗する, then the Amark Block Parser shall 挿入先の文書を変えずに失敗を返す

### Requirement 11: 返す文書
**Objective:** As a 構文木を参照・編集する開発者, I want パーサが返す文書が、構文木の不変条件を満たし、パーサから独立した無編集の状態であること, so that 返された木をそのまま参照・編集・出力できる

#### Acceptance Criteria
1. When パースが終わって文書を返す, the Amark Block Parser shall 組み立てと後処理を記録として行い、値の編集として扱わない（綴りを捨てた印のない文書を返す）
2. The Amark Block Parser shall 返す文書が、構文木のすべての不変条件（包含の規則、記録の順序と範囲、値の規則）を満たすようにする
3. The Amark Block Parser shall 返した文書をパーサから独立させ、パーサを解放した後も文書を使えるようにする
4. The Amark Block Parser shall 返す文書の値（参照の解決、空行から導くタイトを含む）を、構文木の問い合わせで CommonMark の規則どおりに取得できるようにする

### Requirement 12: 限度、計算量、堅牢性
**Objective:** As a 信頼できない入力を扱う開発者, I want 病的な入力でもパースの時間とメモリが入力の大きさにほぼ比例し、異常終了しないこと, so that サービスの中で安全にパースできる

#### Acceptance Criteria
1. The Amark Block Parser shall ブロックの入れ子の深さに上限を設け、上限をオプションで変えられるようにし、既定の上限の値を文書化する
2. When ブロックの入れ子が上限に達する, the Amark Block Parser shall それより深いコンテナを開かず、その部分を段落の内容として扱って解析を続ける
3. The Amark Block Parser shall 入れ子の深さの上限を固定したとき、深い入れ子、大量のリンク参照定義、長い行、大量の空行や遅延継続行などの病的な入力に対して、パースの時間と記録の数が入力の大きさに対して二次以上にならないようにする
4. The Amark Block Parser shall 入れ子の深い入力でも、呼び出しのスタックを使い果たさずにパースを終える
5. The Amark Block Parser shall リンクの宛先の走査で、エスケープされていない括弧の入れ子の深さに 32 の上限を設けてその値を公開し、上限を超える入れ子を含む並びを宛先として扱わない
6. The Amark Block Parser shall 任意のバイト列と任意の区切りの逐次の入力に対して、クラッシュせず、リークせず、構文木の不変条件とロスレス性の検証を満たす文書を返すことを、ファジング、計算量のテスト、ベンチマークで確かめられるようにする

### Requirement 13: 拡張 API の表現力の確認
**Objective:** As a 方言を実装する拡張の実装者, I want GFM 相当の表、脚注の定義、タスクリストに必要なフックがブロックの公開 API にあること, so that これらを拡張だけで実装できる

#### Acceptance Criteria
1. The Amark Block Parser shall 段落の最後の行と次の行を見て段落を表として解釈し直し、続く行を行単位で表の行として受け入れ、1 つの行を複数のセルで分け合って記録する構文を、公開 API だけで実装できるようにする
2. The Amark Block Parser shall 脚注の定義のように、独自のノード型のコンテナを開き、正規化したラベルで引ける自分の定義の表をパースの間に作り、インラインの解析から参照する構文を、公開 API だけで実装できるようにする
3. The Amark Block Parser shall タスクリストのように、インラインの解析の前に、リスト項目を検証用の型の項目に置き換え、最初の段落の中身の先頭を表記に変え、値と style を記録する構文を、公開 API だけで実装できるようにする
4. When 表の検証用の構文がセルの中のエスケープされた `|`（`\|`）を処理する, the Amark Block Parser shall その `|` でセルを区切らず、`\` をセル自身の表記として 2 つの中身の部分の間に記録し、`|` をセルの中身に残す
5. The Amark Block Parser shall これらの 3 つの検証用の構文を、この spec のテストの中にだけ置き、`html-renderer` の検証用のノード型を参照するだけで登録せず、他の spec のテストとファジングがリンクして再利用できる、配布物に含めないテスト専用のライブラリとして提供する

### Requirement 14: 検証
**Objective:** As a Amark の開発者, I want ブロックの解析の正しさが、木のダンプと CommonMark spec のブロックの章の例で確かめられること, so that インラインの解析が揃う前に、ブロックの解析と記録の誤りを見つけられる

#### Acceptance Criteria
1. The Amark Block Parser shall 入力と期待する木のダンプ（値、style、配置、綴りを含む）の組をデータとして書き、パースした結果と照合するテストを持つ
2. The Amark Block Parser shall CommonMark 0.31.2 の spec のうちタブ、ブロックと行の構造、コンテナブロック、葉ブロックの章の例について、期待するブロックの構造と照合するテストを持つ
3. The Amark Block Parser shall すべてのパースのテストで、ロスレス性の検証、構文木の不変条件の検査、逐次の入力の結果が一括のパースと等しいことを確かめる
4. The Amark Block Parser shall CommonMark spec の例をテストデータとしてだけ扱い、配布物とソースのアーカイブに含めない
5. The Amark Block Parser shall パースの検査の補助を、検証と一致の確認の後に一括でパースした文書を呼び出し側へ返す形で提供し、他の spec のテストが同じ検査の上に独自の照合（HTML の出力など）を重ねられるようにする
6. The Amark Block Parser shall CommonMark spec の例の形式のデータからテストを生成する手順を共有の手順として提供し、後続の spec が検査の関数と含めるヘッダの指定だけで再利用できるようにする
