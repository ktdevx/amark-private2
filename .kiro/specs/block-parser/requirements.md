# Requirements Document

## Introduction
Amark は、Markdown テキストと構文木を相互変換する C99 ライブラリである。`block-parser` は、Markdown のテキストからブロック構造（引用、リスト、コードブロック、見出し、段落など）を確定させ、`syntax-tree` の構文木を組み立てるパーサの入口を提供する。

Amark では、CommonMark のブロック構文も、利用者の拡張と同じ公開 API の上に「組み込み拡張」として乗せる。そのため、この spec は個別の構文の知識を持たない汎用のブロック構文エンジンと、拡張から使える公開のブロック構文 API を提供し、その上に CommonMark の全ブロック構文とリンク参照定義を実装する。さらに、無編集の往復でバイト単位で同じ出力を得るために、パースの途中で元の表記（コンテナの接頭辞、マーカー、インデント、空行、改行コード、末尾の空白など）を構文トークンとして記録する。

段落や見出しの中身（インライン）の解析は、後続の `inline-parser` が提供するエンジンへ委譲する。この spec は委譲の取り決めと、インラインのエンジンがない状態でも完結する既定の扱いを定める。

現在、`foundation`（ビルド、テスト基盤、共通ランタイム）と `syntax-tree`（ノードモデル、構文トークン、編集の追跡、拡張レジストリ）の spec がある。パーサのコードはまだない。

## Boundary Context
- **In scope**：
  - パーサの公開 API：構文セットとオプションの指定、文字列全体のパース、逐次の入力（feed してから finish する）、Markdown の断片のパース
  - 入力の前処理（行末、タブ、NUL 文字、BOM、不正な UTF-8）を、元の表記を復元できる形で行うこと
  - 汎用のブロック構文エンジン（開いているコンテナとの照合、新しいブロックの開始、遅延継続、ブロックの閉鎖）
  - 公開のブロック構文 API（開始と継続の条件、段落の中断、閉じた後の処理、既存のブロックの解釈し直し、段落の先頭の消費）と、文書の後処理のフック
  - インラインへの委譲の取り決めと、既定の扱い
  - CommonMark の全ブロック構文とリンク参照定義の組み込み拡張と、それらをまとめた CommonMark の構文セット
  - リンク参照定義の表とラベルの正規化（インラインの解析と共有する）
  - ブロックの構文トークンの記録
  - ブロックのパースの限度、計算量、ファジング
- **Out of scope**：
  - インライン構文の解析と区切り文字の処理（`inline-parser`）
  - CommonMark spec の全例での HTML 出力の 100% の判定（`inline-parser` が行う）
  - HTML と Markdown への出力（`html-renderer`、`markdown-renderer`）
  - GFM 相当の構文そのものの提供（検証用の拡張は `inline-parser` がテストの中にだけ置く）
- **Adjacent expectations**：
  - `syntax-tree` のノード、構文トークン、編集の追跡、拡張のレジストリを使う。不足が見つかったら `syntax-tree` に戻って更新する。
  - `inline-parser` は、この spec の委譲の取り決めとリンク参照定義の表を使ってインラインを解析し、インラインの構文トークンを記録する。エンジンの役割の番号はこの spec が分けた範囲の中で定め、GFM 相当の検証ではこの spec の検証用の構文とパースの検査の補助をテストの中で再利用する。spec の例の形式のデータからテストを生成する共有の手順と、ベンチマークのビルドの構成はこの spec が作り、`inline-parser` はそれを拡張する（生成の手順への検査の種類の追加、ベンチマークの追加）。
  - `foundation` が所有する最上位のビルドの構成と配布物の除外の設定へのこの spec の追記（ベンチマークの有効化のオプション、テストデータの除外）は、`foundation` が定める下流の拡張点として行う。
  - `markdown-renderer` は、この spec が記録した構文トークンから元の表記を復元する。HTML ブロックとして再パースされるかの判定には、この spec が公開する HTML ブロックの開始と終わりの条件、HTML のタグの走査（1 行のバイト列の版）を使い、規則を複製しない。
  - `html-renderer` は組み込みのノード型の HTML 出力を 1 つの構文拡張 `amark.core.html` として提供し、`markdown-renderer` は同じ形の `amark.core.markdown` を提供する。この spec の CommonMark の構文セットを作る手段は、`amark.core.html` を `amark.core` の直後に、`amark.core.markdown` をその直後に加える。`amark.core.markdown` を加える 1 行の変更は `markdown-renderer` のタスクが行い、この spec はその 1 行で足せる形を用意する。

## Requirements

### Requirement 1: パーサの公開 API
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want 構文セットとオプションを指定して、文字列全体または逐次の入力から構文木を得られること, so that 用途（一括の変換、ストリームの処理）に合わせてパースできる

#### Acceptance Criteria
1. When 利用者が構文セット、オプション、アロケータを指定して文字列全体をパースする, the Amark Block Parser shall その構文セットに従って構文木を持つ文書を作って返し、すべての確保を指定したアロケータで行う
2. When 利用者が構文セットを用意せずに CommonMark でパースする手段を使う, the Amark Block Parser shall CommonMark の構文セットを内部で作って使い、返した文書が利用者の操作なしにその構文セットを保持し続けるようにする
3. When 利用者が入力を任意の位置で区切って順に与え、最後に終了を指示する, the Amark Block Parser shall 区切りの位置（改行コードや UTF-8 の文字の途中を含む）によらず、同じ入力を一括でパースしたときと同じ木と同じ構文トークンを持つ文書を返す
4. If 利用者が凍結していない構文セットを指定する, then the Amark Block Parser shall 状態の誤りを返し、パーサを作らない
5. If 利用者が終了の後に入力を与えるか、もう一度終了を指示する, then the Amark Block Parser shall 状態の誤りを返す
6. If パースの途中で確保が失敗する, then the Amark Block Parser shall メモリ不足を返し、リークせず、以後はパーサの解放だけを受け付ける
7. The Amark Block Parser shall 異なるパーサを別々のスレッドで同時に使えるようにし、1 つの凍結した構文セットを複数のパーサで共有できるようにする
8. Where 利用者がオプションで編集の追跡を無効のままにすることを選ぶ, the Amark Block Parser shall 返す文書の編集の追跡を無効のままにする
9. The Amark Block Parser shall 通常のパーサの生成では、利用者が指定した構文セットを読み取り専用で借りるだけで変更も解放もせず、構文セットの所有権を文書へ渡す生成は、変更可能な構文セットを受け取る別の明示の入口としてだけ提供する

### Requirement 2: 入力の前処理と文字の扱い
**Objective:** As a 信頼できない Markdown を扱う開発者, I want どんなバイト列を与えても CommonMark どおりに解釈され、元の表記が失われないこと, so that 不正な入力でも安全に処理でき、往復で同じバイト列を得られる

#### Acceptance Criteria
1. The Amark Block Parser shall LF、CR、CRLF のいずれも行の終わりとして扱い、意味の値（リテラルと属性）では LF に揃え、元の改行コードを構文トークンに残す
2. The Amark Block Parser shall ブロック構造の判定では、タブを 4 列ごとのタブストップとして扱い、タブを書き換えずに、1 つのタブが複数の構文要素に分かれる場合はその列の内訳を構文トークンに残す
3. When 入力に U+0000 が含まれる, the Amark Block Parser shall 意味の値では U+FFFD に置き換え、元のバイトを構文トークンに残す
4. When 入力が UTF-8 の BOM で始まる, the Amark Block Parser shall BOM を内容として扱わず、構文トークンとして残す
5. If 入力に不正な UTF-8 のバイト列が含まれる, then the Amark Block Parser shall クラッシュせず、意味の値では U+FFFD に置き換えて処理を続け、元のバイトを構文トークンに残す
6. When 入力の最後の行が改行で終わらない, the Amark Block Parser shall その行を通常の行として扱う
7. When 入力が空である, the Amark Block Parser shall 子を持たない文書を返す

### Requirement 3: 汎用のブロック構文エンジン
**Objective:** As a 拡張の実装者, I want ブロックの解析の手順が構文の知識を持たない汎用のエンジンとして動くこと, so that 組み込みの構文と同じ条件で、自分の構文を組み合わせられる

#### Acceptance Criteria
1. When 1 行を処理する, the Amark Block Parser shall 開いているコンテナを外側から順に、それぞれの構文の継続の条件と照合し、照合できなかった最初のコンテナより内側を照合できなかったものとして扱う
2. When 照合の後に行の残りがある, the Amark Block Parser shall 構文セットに登録されたブロック構文の開始の条件を優先度の順に試し、最初に成立した構文でブロックを開く
3. When 新しいブロックが開く, the Amark Block Parser shall 照合できなかったブロックと、新しいブロックを子として受け入れられない祖先を閉じてから、新しいブロックを受け入れられる最も内側のブロックの子とする
4. While 最も内側の開いているブロックが遅延継続を受け入れる段落の類のブロックである, when 照合できなかったコンテナがあり、新しいブロックが開かず、行が空行でない, the Amark Block Parser shall その行を遅延継続行としてそのブロックに加え、照合できなかったコンテナを閉じない
5. When 空行を処理する, the Amark Block Parser shall 各ブロックの構文が空行の扱いを決められるようにし、空行であったかどうかを、後で問い合わせられるように記録する
6. When 入力の終わりに達する, the Amark Block Parser shall 開いているすべてのブロックを内側から順に閉じる
7. The Amark Block Parser shall エンジン自体に個別の構文（段落、引用、リストなど）の知識を持たせず、組み込みの構文もすべて登録されたブロック構文として扱う
8. When 構文セットからある組み込みの構文が取り除かれる, the Amark Block Parser shall その構文が認識されないことを除いて、他の構文の解析を変えない

### Requirement 4: 公開のブロック構文 API
**Objective:** As a 拡張の実装者, I want 新しいブロック構文を公開 API だけで定義でき、組み込みの構文と同じ表現力を持つこと, so that GFM 相当の表や脚注を含む方言を拡張の組み合わせだけで実装できる

#### Acceptance Criteria
1. The Amark Block Parser shall 拡張がブロック構文の開始の条件、継続の条件、行の受け入れ、閉じた後の処理、優先度を定義し、構文拡張のスロットとして登録できるようにする
2. The Amark Block Parser shall 拡張が、開始の条件ごとに段落を中断できるかどうかを指定でき、さらに行の内容に応じて中断の可否を判断できるようにする
3. The Amark Block Parser shall 拡張が、1 つの行で複数のブロックを入れ子に開くこと（例：リストとリスト項目）と、開いているブロックごとにパースの間だけ使うデータを持つことをできるようにする
4. When 拡張が開いている段落の類のブロックを別のブロックとして解釈し直す, the Amark Block Parser shall 指定した行から後の行と、それに対応する構文トークンを新しいブロックへ移し、残りの行を元のブロックに残す
5. The Amark Block Parser shall 拡張が段落の先頭の行を消費して別のノード（例：リンク参照定義）を作る処理を登録でき、他の構文がこの処理を必要な時点で明示的に実行させられるようにする
6. The Amark Block Parser shall 拡張が行の位置、インデント、列、空行かどうかを問い合わせ、指定した列の数だけ読み進めて、読み進めた部分を構文トークンとして記録できるようにする
7. The Amark Block Parser shall 拡張がブロックの中身をインラインの入力として登録し、インラインの解析の前にその先頭を消費して別の構文トークンとして記録できるようにする
8. If 拡張のコールバックが失敗を返す, then the Amark Block Parser shall パースを止めてそのエラーコードを呼び出し側へ返す
9. The Amark Block Parser shall 組み込みの構文が使うフックをすべて公開 API として提供し、内部専用の近道を使わない

### Requirement 5: 文書の後処理とパースの間の拡張の状態
**Objective:** As a 拡張の実装者, I want パースが終わった後に木を書き換える処理と、パースの間に拡張が共有する状態を持てること, so that 脚注やタスクリストのように、文書全体を見て結果を書き換える構文を実装できる

#### Acceptance Criteria
1. The Amark Block Parser shall 拡張が文書の後処理を登録でき、インラインの解析の前と後のどちらで実行するかを選べるようにする
2. When 終了が指示される, the Amark Block Parser shall すべてのブロックを閉じた後、インラインの解析の前の後処理、インラインの解析、インラインの解析の後の後処理の順に、構文セットの順で実行する
3. The Amark Block Parser shall 拡張がパースの間だけ存在する自分の状態を持ち、ブロック構文、インラインの解析、後処理のどこからでも参照できるようにする
4. When パースが終わるか失敗する, the Amark Block Parser shall 拡張の状態とパースの間だけのデータを解放する
5. While 後処理を実行している, the Amark Block Parser shall 木の変更を編集として記録しない

### Requirement 6: インラインへの委譲
**Objective:** As a `inline-parser` の開発者, I want ブロックの解析が、段落や見出しの中身を明確な取り決めでインラインのエンジンへ渡すこと, so that ブロックとインラインを別々に実装でき、元の表記の対応を失わない

#### Acceptance Criteria
1. When すべてのブロックが閉じた後にインラインの解析を行う, the Amark Block Parser shall インラインの中身を持つ各ノードについて、文書の順に、そのノードと中身の入力をインラインのエンジンへ渡す
2. The Amark Block Parser shall インラインの入力を、元のソース上の区間の並びとして渡し、コンテナの接頭辞や行頭の空白など、ブロックが記録した表記を区間に含めない
3. The Amark Block Parser shall インラインのエンジンが、パースの間の文脈（文書、オプション、リンク参照定義の表、拡張の状態）を参照できるようにする
4. Where 利用者がオプションでインラインのエンジンを指定する, the Amark Block Parser shall そのエンジンへ委譲する
5. The Amark Block Parser shall インラインの中身をテキストと改行のノードとして扱う単純な委譲先を提供し、インラインのエンジンがない状態でも、元の表記をすべて構文トークンで覆う文書を作れるようにする
6. If インラインのエンジンが失敗を返す, then the Amark Block Parser shall パースを止めてそのエラーコードを返す

### Requirement 7: CommonMark のブロック構文の組み込み拡張
**Objective:** As a Amark の利用者, I want CommonMark の全ブロック構文が組み込み拡張として提供され、構文ごとに無効化や置き換えができること, so that CommonMark どおりにパースでき、方言に合わせて構文を組み替えられる

#### Acceptance Criteria
1. The Amark Block Parser shall thematic break、ATX 見出し、setext 見出し、インデントされたコードブロック、フェンスで囲まれたコードブロック、HTML ブロックの 7 種類、リンク参照定義、段落、空行、引用、リスト項目、リストを、CommonMark 0.31.2 の規則どおりに解析する
2. The Amark Block Parser shall 空行（ノードを作らず、すべてのコンテナの照合に関わるため、エンジンが扱う）を除く各ブロック構文を、構文ごとに 1 つの名前を持つ構文拡張として、公開のブロック構文 API の上に実装する
3. The Amark Block Parser shall CommonMark のすべての組み込み拡張と、組み込みのノード型の出力の拡張（HTML。後に Markdown）を正しい順に加えた構文セットを作る手段と、利用者の構文セットにそれらを加える手段を提供する
4. The Amark Block Parser shall 各組み込み拡張を名前で取り除いたり、別の拡張に置き換えたりできるようにする
5. The Amark Block Parser shall リストのタイトかどうか、リストの種類と開始番号、見出しのレベル、コードブロックの情報文字列とリテラル、HTML ブロックのリテラルを、CommonMark の規則どおりにノードの属性とリテラルとして設定する
6. The Amark Block Parser shall 組み込み拡張の構造を、ノード型ごとの出力の拡張（`html-renderer` の HTML、後の `markdown-renderer` の Markdown）を構文セットに組み込め、特定の構文だけが独自の出力を必要とする場合はその構文の拡張にも構造を変えずに足せる形にする
7. The Amark Block Parser shall HTML ブロックの 7 種類の開始の条件（段落を中断する場面かどうかを含む）、種類 1–5 の終わりの条件、HTML の開始タグと終了タグの走査を、区間の並びのテキストを作らずに 1 行のバイト列の上で使える公開の関数として提供する
8. The Amark Block Parser shall HTML ブロックの組み込み拡張と区間の並びの上の HTML のタグの走査を、7 の関数と同じ規則の 1 つの実装の上に置き、同じ入力に対してバイト列の版と区間の並びの版が同じ判定と同じ終わりの位置を返すようにする

### Requirement 8: リンク参照定義とラベルの表
**Objective:** As a インラインの解析と拡張の実装者, I want リンク参照定義が CommonMark どおりに解析され、正規化したラベルで引ける表として共有されること, so that 参照リンクや脚注のような、ラベルで対応付ける構文を一貫して実装できる

#### Acceptance Criteria
1. When 段落の先頭の行がリンク参照定義の規則を満たす, the Amark Block Parser shall その行をリンク参照定義のノードとして段落の前に置き、ラベル、宛先、タイトルを属性に設定し、残りの行を段落として残す
2. When 段落のすべての行がリンク参照定義になる, the Amark Block Parser shall 空の段落を木に残さない
3. The Amark Block Parser shall ラベルを、Unicode の大文字小文字の畳み込みと連続する空白の 1 つの空白への置き換え、前後の空白の除去で正規化して比べる
4. When 同じ正規化したラベルを持つ定義が複数ある, the Amark Block Parser shall 最初の定義を表に登録し、後の定義もノードとして木に残す
5. The Amark Block Parser shall リンク参照定義の表を、インラインの解析と後処理から正規化したラベルで引けるようにする
6. The Amark Block Parser shall ラベルの正規化と、ラベルで引ける表の仕組みを公開し、拡張が自分の定義の表（例：脚注の定義）を同じ規則で作れるようにする
7. The Amark Block Parser shall 宛先とタイトルの中のバックスラッシュのエスケープと文字参照を CommonMark の規則どおりに解釈して属性に設定し、元の表記を構文トークンに残す

### Requirement 9: ブロックの構文トークンの記録
**Objective:** As a `markdown-renderer` の開発者, I want 無編集の文書の全バイトが、どのノードの表記かが分かる構文トークンで過不足なく覆われること, so that 無編集の往復でバイト単位で同じ出力を作り、編集した部分だけを作り直せる

#### Acceptance Criteria
1. When 構文トークンを持つ委譲先でパースした無編集の文書を検証する, the Amark Block Parser shall 入力のすべてのバイトが、抜けも重なりもなく構文トークンで覆われた文書を作る
2. The Amark Block Parser shall 引用の `>` と空白、リスト項目のマーカーと空白、続く行のインデントを、そのコンテナが持つ接頭辞の構文トークンとして記録する
3. When 遅延継続行を処理する, the Amark Block Parser shall 接頭辞が省かれた各コンテナに、遅延継続の印の構文トークンを記録する
4. The Amark Block Parser shall マーカー、フェンス、下線、インデント、行末の空白、空行、改行コード、置き換えたバイト、BOM、部分的なタブを、それぞれ構文トークンとして記録する
5. The Amark Block Parser shall 組み込みの各構文が使う構文トークンの役割の番号を公開し、利用者とレンダラが表記（例：ATX か setext か、フェンスの文字）を区別できるようにする
6. The Amark Block Parser shall 空行の構文トークンを、その空行を含む最も内側のブロックに持たせ、部分木を移動したときに自分の表記だけが一緒に移るようにする
7. The Amark Block Parser shall パーサのエンジンが記録する構文トークンの役割の番号の範囲を、ブロックのエンジンの範囲とインラインのエンジンの範囲に重ならないように分けて文書化し、組み込みの構文の役割の番号とも重ならないようにする

### Requirement 10: Markdown の断片のパース
**Objective:** As a 構文木を編集する開発者, I want Markdown の断片をブロックまたはインラインの文脈でパースし、既存の文書の指定した位置へ差し込めること, so that テキストの置換や新しい内容の挿入を、元の表記つきの正しいノードとして行える

#### Acceptance Criteria
1. When 利用者が断片をブロックの文脈でパースして挿入先の親と位置を指定する, the Amark Block Parser shall 断片をブロックとしてパースし、得たノードをすべて、構文トークンつきでその位置に挿入する
2. When 利用者が断片をインラインの文脈でパースして挿入先を指定する, the Amark Block Parser shall 断片の全体を 1 つのインラインの中身として委譲先で解析し、得たインラインのノードを挿入する
3. Where 利用者が挿入先の文書のリンク参照定義を使うことを選ぶ, the Amark Block Parser shall 断片の中の参照を、挿入先の文書のリンク参照定義でも解決できるようにする
4. If 得たノードのどれかを挿入先に置けない, then the Amark Block Parser shall 何も挿入せず、引数の誤りを返す
5. While 挿入先の文書で編集の追跡が有効である, the Amark Block Parser shall 挿入したノードを、構文木の規則どおりに編集として記録する

### Requirement 11: 完成した文書と編集の追跡
**Objective:** As a 構文木を編集する開発者, I want パーサが返す文書が、編集の追跡を有効にした無編集の状態であること, so that 返された木を編集すれば、その部分だけが編集として記録される

#### Acceptance Criteria
1. When パースが終わって文書を返す, the Amark Block Parser shall 組み立てと後処理による変更を編集として記録せず、オプションで無効にした場合を除いて、文書の編集の追跡を有効にしてから返す
2. The Amark Block Parser shall 返す文書が、構文木のすべての不変条件（包含の規則、リテラルと属性の値の規則、構文トークンの範囲）を満たすようにする
3. The Amark Block Parser shall 返した文書をパーサから独立させ、パーサを解放した後も文書を使えるようにする

### Requirement 12: 限度、計算量、堅牢性
**Objective:** As a 信頼できない入力を扱う開発者, I want 病的な入力でもパースの時間とメモリが入力の大きさにほぼ比例し、異常終了しないこと, so that サービスの中で安全にパースできる

#### Acceptance Criteria
1. The Amark Block Parser shall コンテナの入れ子の深さに上限を設け、上限をオプションで変えられるようにし、既定の上限の値を文書化する
2. When コンテナの入れ子が上限に達する, the Amark Block Parser shall それより深いコンテナを開かず、その部分を内容として扱って解析を続ける
3. The Amark Block Parser shall 入れ子の深さの上限を固定したとき、深い入れ子、大量のリンク参照定義、長い行、大量の空行や遅延継続行などの病的な入力に対して、パースの時間が入力の大きさに対して二次以上にならないようにする
4. The Amark Block Parser shall 入れ子の深い入力（上限まで入れ子にしたコンテナ、多数の行）でも、呼び出しのスタックを使い果たさずにパースを終える
5. The Amark Block Parser shall 任意のバイト列と任意の区切りの逐次の入力に対して、クラッシュせず、リークせず、構文木の不変条件と構文トークンの被覆を満たす文書を返すことを、ファジングと計算量のテストで確かめられるようにする
6. The Amark Block Parser shall リンクの宛先の走査で、エスケープされていない括弧の入れ子の深さに 32 の上限を設けてその値を公開し、上限を超える入れ子を含む並びを宛先として扱わないことで、リンク参照定義とインラインのリンクの走査の状態を入力によらず有界にする

### Requirement 13: 拡張 API の表現力の確認
**Objective:** As a 方言を実装する拡張の実装者, I want GFM 相当の表、脚注の定義、タスクリストに必要なフックがブロックの公開 API にあること, so that これらを拡張だけで実装できる

#### Acceptance Criteria
1. The Amark Block Parser shall 段落の最後の行と次の行を見て段落を表として解釈し直し、続く行を行単位で表の行として受け入れる構文を、公開 API だけで実装できるようにする
2. The Amark Block Parser shall 脚注の定義のように、独自のノード型のコンテナブロックを開き、正規化したラベルで引ける自分の定義の表をパースの間に作り、インラインの解析から参照する構文を、公開 API だけで実装できるようにする
3. The Amark Block Parser shall タスクリストのように、リスト項目の中身の先頭を後処理で読み取り、その表記を構文トークンとして記録し直す構文を、公開 API だけで実装できるようにする
4. The Amark Block Parser shall これらの 3 つのフックを、この spec のテストの中にだけ置く小さな検証用の構文で確かめる
5. The Amark Block Parser shall 3 つの検証用の構文を、他の spec のテスト（`inline-parser` の GFM 相当の検証）がリンクして再利用できるテスト専用のライブラリとして提供し、各構文の拡張を得る手段と、パースの文脈から脚注の定義の表を引く手段を公開し、配布するライブラリとインストールする成果物に含めない
6. When 表の検証用の構文がセルの中のエスケープされた `|`（`\|`）を処理する, the Amark Block Parser shall その `|` でセルを区切らず、`|` の前の `\` をセルのインラインの区間から除いてセルの構文トークンとして記録し、`|` をセルの中身として残す

### Requirement 14: 検証
**Objective:** As a Amark の開発者, I want ブロックの解析の正しさが、木のダンプと CommonMark spec のブロックの章の例で確かめられること, so that インラインの解析が揃う前に、ブロックの解析の誤りを見つけられる

#### Acceptance Criteria
1. The Amark Block Parser shall 入力と期待する木のダンプの組をデータとして書き、パースした結果と照合するテストを持つ
2. The Amark Block Parser shall CommonMark 0.31.2 の spec のうちタブ、不正な文字、ブロック構造の章の例について、期待するブロックの構造と照合するテストを持つ
3. The Amark Block Parser shall すべてのパースのテストで、構文トークンが入力を過不足なく覆うことと、逐次の入力の結果が一括のパースと等しいことを確かめる
4. The Amark Block Parser shall CommonMark spec の例をテストデータとしてだけ扱い、配布物に含めない
5. The Amark Block Parser shall パースの検査の補助を、被覆、不変条件、逐次の入力の一致を確かめた後に一括でパースした文書を呼び出し側へ返す形で提供し、他の spec のテストが同じ検査の上に独自の照合（HTML の出力など）を重ねられるようにする
6. The Amark Block Parser shall CommonMark spec の例の形式のデータからテストを生成する手順を、プロジェクトで唯一の共有の生成手順として提供し、データの形式とテストの名前の規則を変えずに、後続の spec が引数の追加だけで検査の種類を拡張できるようにする
