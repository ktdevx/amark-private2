# Requirements Document

## Project Description (Input)
利用者は、パースした（あるいは編集した）構文木を HTML として出力したい。CommonMark の準拠テストは HTML 出力で判定するため、準拠を検証するためにも、CommonMark の参照実装（cmark）と互換な HTML レンダラが必要である。さらに、拡張したノードや既存のノードの出力を、利用者が差し替えられなければならない。

2026-11-27 に、`syntax-tree` の新しいモデル（「行 × 帯」と「中身の上の CST」、値と style と綴りの区別、定義の索引）に合わせて作り直した。前の版（構文トークンのモデル）の要件は引き継がない。入力は `brief.md` である。

## Introduction
Amark は、Markdown テキストと構文木を相互変換する C99 ライブラリである。`html-renderer` は、`syntax-tree` の構文木（パースしたもの、手で組み立てたもの、編集したもの）を HTML に出力する。

利用者は、CMS、静的サイトジェネレータ、ドキュメントツールなどで、Markdown を HTML に変換したい。CommonMark の準拠は HTML の出力で判定されるため、出力は CommonMark の参照実装（cmark）と同じ形でなければならない。また、信頼できない入力を扱う場面が多いため、既定で生の HTML と危険な URL を出さない安全モードが必要である。

Amark では、CommonMark の構文も方言の構文も、同じ構文拡張の仕組みで組み立てる。そのため、HTML の出力もノード型ごとの「出力のスロット」として構文拡張から登録し、組み込みのノード型の出力も同じスロットで提供する。拡張は、既存の出力の差し替え、出力全体のフィルタ、文書の出力の前後への追加ができ、GFM 相当の構文（表、取り消し線、タスクリスト、タグフィルタ、脚注）の HTML を、公開のスロットだけで実現できなければならない。

構文木は、ノードの情報を意味の値（value）、表記の属性（style）、綴り（spelling）に分けて持つ。HTML の出力は意味の値だけで決まる。値を編集した木も、参照の形のリンクの定義を変えた木も、編集の後の値で出力する。ソース位置の属性は、構文木が返すソース上の範囲から作る。

現在、`foundation`（ビルド、テスト基盤、アロケータ、バッファ、エラーコード、UTF-8）と `syntax-tree`（ノード、値と style、配置と綴り、定義の索引、位置、走査、拡張のレジストリ、テスト用の S 式の木の組み立て）の実装がある。HTML レンダラのコードはまだない。

## Boundary Context
- **In scope**：
  - 部分木を HTML に出力する入口と、出力先（利用者のバッファ、利用者の書き出しの関数）
  - 出力のオプション（安全モードと生の HTML の許可、ソース位置の属性、ソフトブレークの扱い）
  - 構文木の値（参照の解決の結果、タイト、綴りを捨てた後の値を含む）とソース上の範囲を、出力に使う規則
  - ノード型ごとの HTML 出力のスロット、出力の前後のフック、出力のフィルタ、出力の文脈（タイトさ、テキストだけのモード、子の省略、入れ子の出力、拡張ごとの状態）
  - HTML のエスケープ、URL のエスケープ、URL の安全判定のユーティリティ
  - 組み込みのすべてのノード型（リンク参照定義を含む）の HTML 出力を、スロットとして持つ組み込みの構文拡張
  - 手で組み立てた木による出力のテストと、GFM 相当の HTML 出力がスロットだけで書けることを確かめるテスト専用の検証用拡張
- **Out of scope**：
  - パース（`block-parser`、`inline-parser`）と、CommonMark の構文の構文拡張・構文セットの用意（`block-parser`）
  - CommonMark spec の準拠テストのランナー（`inline-parser`）
  - GFM 相当の構文のパースの部分と、それらを組み合わせた検証（`block-parser`、`inline-parser`）
  - 値の導出、参照の解決、タイトの導出、ソース上の範囲の求め方（`syntax-tree`）
  - Markdown への出力（`markdown-renderer`）
  - HTML 以外の出力形式（利用者が独自のスロットで作れる）
- **Adjacent expectations**：
  - `syntax-tree` は、出力に使う値（参照の解決、空行から導くタイト）とソース上の範囲を、公開の問い合わせで返す。この spec は綴りを解析しない。
  - CommonMark の構文セットを用意する spec（`block-parser`）は、組み込みの HTML 出力の構文拡張を、CommonMark の各構文の構文拡張より前に含める。これにより、パースした木は追加の登録なしで出力できる。
  - `inline-parser` は、生の HTML を許可するオプションでこの spec の出力を使い、CommonMark spec の全例を照合する。そこで見つかった出力の不具合は、この spec に戻って直す。
  - `block-parser`、`inline-parser`、`markdown-renderer` は、この spec の検証用拡張のノード型（表、取り消し線、タスクリスト、脚注）を、パースと Markdown の出力の検証で再利用する。
  - `markdown-renderer` は、出力のスロット、前後のフック、出力の文脈、出力先の形を、この spec と揃えて定義する。

## Requirements

### Requirement 1: 出力の入口と出力先
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want 任意の構文木や部分木を、バッファか自分の書き出しの関数へ HTML として出力できること, so that 文書全体でも一部分でも、用途に合った出力先へ変換できる

#### Acceptance Criteria
1. When 利用者がノード（文書の根、木の途中のノード、木から切り離したノードのどれでもよい）を指定して HTML の出力を求める, the Amark HTML Renderer shall そのノードを根とする部分木の HTML を出力する
2. When 利用者が出力先としてバッファを指定する, the Amark HTML Renderer shall 出力をバッファの既存の内容の後ろに追加する
3. When 利用者が出力先として書き出しの関数を指定する, the Amark HTML Renderer shall 出力を先頭から順に渡し、渡したバイト列をつなげたものを、バッファに出力した場合と同じにする
4. If 利用者の書き出しの関数が失敗を返す, then the Amark HTML Renderer shall 直ちに出力を止め、その失敗を利用者に返す
5. If 出力の途中で、確保、構文木の値か位置の問い合わせ、拡張の出力の処理、フック、フィルタのどれかが失敗する, then the Amark HTML Renderer shall 出力を止めて失敗を返し、出力先がバッファならその内容を呼び出し前の状態に戻し、リークしない
6. The Amark HTML Renderer shall 出力の間に構文木の値、構造、配置、綴りを変更しない
7. The Amark HTML Renderer shall 同じ木、同じ構文セット、同じオプションに対して、常に同じバイト列を出力する

### Requirement 2: 組み込みのノード型の CommonMark 互換の出力
**Objective:** As a CommonMark の文書を HTML に変換する利用者, I want 組み込みのノード型が CommonMark の参照実装と同じ HTML になること, so that 既存の処理系と置き換えても出力が変わらず、CommonMark spec の期待値と照合できる

#### Acceptance Criteria
1. The Amark HTML Renderer shall 組み込みのすべてのノード型を、準拠対象の CommonMark 0.31.2 に対応する参照実装（cmark）と同じ HTML（要素と属性の名前・順・書き方、改行の位置、エスケープ）で出力する
2. When 段落が、タイトなリストの項目の直接の子である, the Amark HTML Renderer shall 段落の開始と終了のタグを出さず、中身だけを出力する
3. When 順序付きリストの開始番号が 1 以外である, the Amark HTML Renderer shall 開始番号を属性として出力する
4. When コードブロックが空でない情報文字列を持つ, the Amark HTML Renderer shall 情報文字列の最初の語を言語のクラスとして出力する
5. When 画像を出力する, the Amark HTML Renderer shall 画像の子孫のテキストの内容だけを、タグを含めずに代替テキストの属性として出力する
6. When リンクか画像のタイトルが空でない, the Amark HTML Renderer shall タイトルの属性を出力し、タイトルが空ならその属性を出さない
7. When リンク参照定義のノードを出力する, the Amark HTML Renderer shall 何も出力しない
8. When 文書のノードを出力する, the Amark HTML Renderer shall 文書のノード自身のタグを出さず、子の出力だけを出す
9. The Amark HTML Renderer shall 既定では、ソフトブレークを改行として出力する

### Requirement 3: 構文木の値に従う出力
**Objective:** As a 構文木を編集して HTML にする利用者, I want 出力が、元の表記ではなく、構文木が返す意味の値に従うこと, so that 値の編集、定義の変更、綴りを捨てる操作の後も、木の今の意味のとおりの HTML が得られる

#### Acceptance Criteria
1. The Amark HTML Renderer shall テキスト、コード、生の HTML の内容、情報文字列、見出しのレベル、リストの種類と開始番号、リンクと画像の宛先とタイトルを、構文木がそのノードについて返す意味の値で出力する
2. When 利用者が値を編集するか、ノードの綴りを捨てる, the Amark HTML Renderer shall 次の出力で、編集の後の値を出力する
3. When 参照の形（full、collapsed、shortcut）のリンクか画像のラベルで、文書の木の中のリンク参照定義が引ける, the Amark HTML Renderer shall その定義の宛先とタイトルを、そのリンクか画像の宛先とタイトルとして出力する
4. When 利用者が、参照の形のリンクが引くリンク参照定義の宛先かタイトルを変えるか、その定義を木から取り除く, the Amark HTML Renderer shall 次の出力で、そのリンクの出力に変更を反映する
5. If 参照の形のリンクか画像のラベルで定義が引けない, then the Amark HTML Renderer shall 宛先を空、タイトルをなしとして、リンクか画像の要素を出力し、子は通常どおり出力する
6. The Amark HTML Renderer shall リストがタイトかどうかを、構文木がそのリストについて返す値（空行から導いた値、または利用者が上書きした値）で決める
7. When 意味の値が、利用者が設定の操作では設定できない値（改行を含むテキスト、空のコードなど）である, the Amark HTML Renderer shall 失敗せずに、ほかの値と同じ規則でエスケープして出力する

### Requirement 4: 安全モード
**Objective:** As a 信頼できない Markdown を HTML にする利用者, I want 既定で生の HTML と危険な URL が出力されないこと, so that 追加の対策なしにスクリプトの埋め込みなどを防げる

#### Acceptance Criteria
1. While 生の HTML を許可するオプションが指定されていない, the Amark HTML Renderer shall HTML ブロックとインラインの HTML の内容を出力せず、代わりに省略したことを示す HTML のコメント（`<!-- raw HTML omitted -->`）を出力する
2. While 生の HTML を許可するオプションが指定されていない, when リンクの宛先か画像の URL が危険なスキーム（`javascript:`、`vbscript:`、`file:`、および画像の種類（png、gif、jpeg、webp）以外の `data:`）で始まる, the Amark HTML Renderer shall その宛先を空として出力する
3. Where 生の HTML を許可するオプションが指定されている, the Amark HTML Renderer shall 生の HTML をそのまま出力し、危険なスキームの URL も通常の URL と同じくエスケープして出力する
4. The Amark HTML Renderer shall 危険なスキームの判定で、大文字と小文字を区別しない
5. While 生の HTML を許可するオプションが指定されていない, when 拡張の出力の処理が内容を生の HTML として出力する, the Amark HTML Renderer shall 組み込みのノード型と同じ規則で、その内容を省略したことを示すコメントに置き換える
6. The Amark HTML Renderer shall どのオプションでも、テキストと属性の値をエスケープして出力し、生の HTML として出力した内容以外の利用者のテキストが、HTML のタグや属性として解釈される出力をしない

### Requirement 5: 出力のオプション
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want ソース位置の属性や改行の扱いを選べること, so that エディタのプレビューとの対応付けや、表示の用途に合わせた出力ができる

#### Acceptance Criteria
1. Where ソース位置の属性の出力が指定されている, the Amark HTML Renderer shall ブロックの要素（引用、リスト、リスト項目、見出し、コードブロック、区切り線、段落）の開始タグに、cmark と同じ書式（`data-sourcepos="開始の行:開始の列-終わりの行:終わりの列"`）の属性を出力する
2. Where ソース位置の属性の出力が指定されている, the Amark HTML Renderer shall 属性の値を、構文木がそのノードについて返すソース上の範囲の、最初のバイトの行と列と、最後のバイトの行と列（どちらも 1 から数え、行はそのソースの中の番号、列は行の先頭からのバイトの位置）にする
3. If ソース位置の属性の出力が指定されていて、構文木がそのノードの位置がないと返す（行の範囲を持たない新しいブロックなど）, then the Amark HTML Renderer shall そのノードの属性を出さない
4. Where ソフトブレークをハードブレークとして出す指定がある, the Amark HTML Renderer shall ソフトブレークを改行の要素として出力する
5. Where ソフトブレークを空白として出す指定がある, the Amark HTML Renderer shall ソフトブレークを 1 つの空白として出力する
6. The Amark HTML Renderer shall ソフトブレークの扱いを、改行・ハードブレーク・空白のうち 1 つだけを選ぶ指定として受け付ける
7. If 利用者が未知のオプションを指定する, then the Amark HTML Renderer shall 不正な引数を返し、何も出力しない
8. When 利用者がオプションを省略する, the Amark HTML Renderer shall 安全モード、ソース位置の属性なし、ソフトブレークは改行、の既定で出力する

### Requirement 6: エスケープと URL のユーティリティ
**Objective:** As a 拡張の実装者と利用者, I want 組み込みの出力と同じ規則のエスケープと URL の安全判定を使えること, so that 自分の出力や前処理でも、組み込みの出力と一貫した安全な HTML を作れる

#### Acceptance Criteria
1. The Amark HTML Renderer shall テキストを、参照実装と同じ規則（`&`、`<`、`>`、`"` を文字参照にする）で HTML のエスケープをするユーティリティを提供する
2. The Amark HTML Renderer shall URL を、参照実装と同じ規則（URL として安全な ASCII の文字と既存の百分率符号化はそのまま、`&` と `'` は文字参照、それ以外のバイトは百分率符号化）でエスケープするユーティリティを提供する
3. The Amark HTML Renderer shall URL が安全モードで出力できない危険なスキームで始まるかを判定するユーティリティを、4.2 と 4.4 と同じ規則で提供する
4. The Amark HTML Renderer shall これらのユーティリティを、HTML の出力の処理の外でも、文書や構文セットなしに使えるようにする

### Requirement 7: ノード型ごとの HTML 出力のスロット
**Objective:** As a 拡張の実装者, I want ノード型ごとの HTML の出力を構文拡張から登録し、既存の出力を差し替えたり包んだりできること, so that 新しいノード型の出力を足し、組み込みの出力も自分の方針に変えられる

#### Acceptance Criteria
1. The Amark HTML Renderer shall 構文拡張が、ノード型を名前で指定して、そのノード型の HTML の出力の処理（ノードに入るときと出るときの処理）をスロットとして登録できるようにする
2. When 同じノード型に複数の構文拡張が出力の処理を登録している, the Amark HTML Renderer shall 構文セットで後にある構文拡張の処理を使う
3. When 出力の処理が、差し替えた前の処理（構文セットでより前にある構文拡張の、同じノード型の処理）の実行を求める, the Amark HTML Renderer shall その処理を実行し、前の処理がなければ出力の処理がない場合の振る舞いをする
4. When 出力の処理を持たないノード型のノードを出力する, the Amark HTML Renderer shall そのノード自身は何も出力せず、子を順に出力する
5. When 出力の処理が、ノードに入るときに子の出力を省くことを求める, the Amark HTML Renderer shall 子を出力せずに、そのノードを出るときの処理へ進む
6. If 登録された出力のスロットが不正である（必須の処理がない、スロットの大きさが足りない、指定したノード型が構文セットにない、1 つの構文拡張が前後のフックのスロットを 2 つ以上持つ）, then the Amark HTML Renderer shall 不正な引数を返し、何も出力しない
7. The Amark HTML Renderer shall 組み込みのノード型と拡張のノード型を、同じスロットの仕組みで扱う
8. The Amark HTML Renderer shall HTML の出力のスロットを、構文拡張の他のスロット（パース、Markdown の出力など）とは独立に登録できるようにし、出力のスロットだけを持つ構文拡張も許す

### Requirement 8: 出力の文脈
**Objective:** As a 拡張の実装者, I want 出力の処理の中で、書き出しの手段と、タイトさやテキストだけのモードなどの文脈を使えること, so that 組み込みの出力と同じ品質の出力を、内部の仕組みに頼らずに書ける

#### Acceptance Criteria
1. The Amark HTML Renderer shall 出力の処理に、マークアップの書き出し、HTML のエスケープをしたテキストの書き出し、安全判定とエスケープをした URL の書き出し、安全モードに従う生の HTML の書き出しの手段を提供する
2. The Amark HTML Renderer shall 出力の処理に、直前の出力が改行で終わっていないときだけ改行を出す手段と、5.1–5.3 の規則でソース位置の属性を書き出す手段と、現在のオプションを問い合わせる手段を提供する
3. When コンテナのノードの出力の処理が、子をタイトとして出力することを設定する, the Amark HTML Renderer shall そのノードの直接の子の出力の処理がタイトかどうかを問い合わせられるようにし、設定のないノードの子はタイトでないとする
4. When 出力の処理が、ノードに入るときにテキストだけのモードを求める, the Amark HTML Renderer shall そのノードの子孫を、テキストだけのモードの処理で出力し、そのノードを出るときの処理は通常のモードで呼ぶ
5. The Amark HTML Renderer shall 構文拡張が、ノード型ごとのテキストだけのモードの処理を登録できるようにし、その処理を持たないノード型のノードは、テキストだけのモードでは何も出力せずに子を出力する
6. While テキストだけのモードである, the Amark HTML Renderer shall 組み込みの text、code、html_inline をエスケープした値として、ソフトブレークとハードブレークを 1 つの空白として出力する
7. When 出力の処理が、同じ文書の任意のノード（木から切り離したノードを含む）の出力を求める, the Amark HTML Renderer shall 同じ出力先、オプション、フィルタ、拡張ごとの状態を使って、そのノードを根とする部分木を出力する
8. If 出力の処理の中からの部分木の出力の入れ子が文書化された上限を超える, then the Amark HTML Renderer shall 上限を超えたことを返す
9. If 出力の処理が、処理中でないノードについて前の処理の実行を求めるか、別の文書のノードの出力を求める, then the Amark HTML Renderer shall 不正な引数を返す

### Requirement 9: 出力の前後のフックとフィルタ
**Objective:** As a 拡張の実装者, I want 出力の開始と終了に処理を足し、出力されるバイト列を加工できること, so that 脚注のように末尾に出力を足す構文や、タグフィルタのように出力を差し替える構文を、拡張だけで実装できる

#### Acceptance Criteria
1. The Amark HTML Renderer shall 構文拡張が、出力の開始の前と、部分木の出力の終了の後に呼ばれる処理を登録できるようにする
2. When 終了の後の処理が出力する, the Amark HTML Renderer shall その出力を部分木の出力の後ろに追加する
3. The Amark HTML Renderer shall 開始の前の処理を構文セットの順に、終了の後の処理を構文セットの逆の順に呼ぶ
4. The Amark HTML Renderer shall 構文拡張ごとに、1 回の出力の間だけ有効な状態を持てるようにし、出力の開始の前に 0 で初期化し、出力の成否にかかわらず後始末の処理を呼んでから解放する
5. The Amark HTML Renderer shall その構文拡張のノード型の出力の処理と前後の処理から、その構文拡張の状態を参照できるようにする
6. The Amark HTML Renderer shall 構文拡張が、出力されるバイト列を、マークアップ、エスケープしたテキスト、生の HTML の分類とともに受け取り、そのまま渡す、変えて渡す、省くことのできるフィルタを登録できるようにする
7. When 複数の構文拡張がフィルタを登録している, the Amark HTML Renderer shall 出力を構文セットの順にそれぞれのフィルタに通してから出力先へ渡す
8. The Amark HTML Renderer shall 前後の処理とフィルタを、文書の根の出力だけでなく、利用者が求めたすべての部分木の出力で使う

### Requirement 10: 組み込みの HTML 出力の提供と登録
**Objective:** As a 利用者と構文セットを用意する spec の開発者, I want 組み込みのノード型の HTML 出力が、利用者の拡張と同じ形の 1 つの構文拡張として提供されること, so that どの構文セットにも同じ方法で組み込みの出力を含められ、差し替えも同じ方法でできる

#### Acceptance Criteria
1. The Amark HTML Renderer shall 組み込みのすべてのノード型の HTML 出力を、利用者の拡張と同じスロットの形式と公開の問い合わせだけを使う 1 つの組み込みの構文拡張として提供する
2. The Amark HTML Renderer shall その組み込みの構文拡張を、利用者が自分で作る構文セットに加えられるようにする
3. When 構文セットで組み込みの HTML 出力の構文拡張より後に、同じノード型の出力の処理を持つ構文拡張がある, the Amark HTML Renderer shall 後の構文拡張の処理を使い、その処理から組み込みの処理を呼べるようにする
4. If 構文セットが組み込みの HTML 出力の構文拡張を含まない, then the Amark HTML Renderer shall 組み込みのノード型を、出力の処理を持たないノード型として扱う

### Requirement 11: 方言の HTML 出力の表現力
**Objective:** As a 独自の方言を実装する利用者, I want GFM 相当の構文の HTML 出力を、公開のスロットだけで実装できること, so that 方言を拡張の組み合わせだけで完全に表現できると確かめられる

#### Acceptance Criteria
1. The Amark HTML Renderer shall 表（見出しの行、本体の行、列の揃え）を、拡張のノード型と公開のスロットだけで、cmark-gfm と同じ形の HTML として出力できるようにする
2. The Amark HTML Renderer shall 取り消し線を、拡張のノード型と公開のスロットだけで、cmark-gfm と同じ形の HTML として出力できるようにする
3. The Amark HTML Renderer shall タスクリストの項目のチェックボックスを、リストのタイトさの規則を保ったまま、公開のスロットだけで cmark-gfm と同じ形の HTML として出力できるようにする
4. The Amark HTML Renderer shall 脚注（参照の出現順の番号付け、参照された定義を文書の末尾にまとめた出力、参照への戻りのリンク）を、公開のスロットと前後のフックだけで、木を変えずに cmark-gfm と同じ形の HTML として出力できるようにする
5. The Amark HTML Renderer shall タグフィルタ（生の HTML の中の特定のタグ名の開始の `<` を文字参照に置き換える）を、出力の処理の差し替えと出力のフィルタのそれぞれだけで実装できるようにする
6. The Amark HTML Renderer shall 1 から 5 のそれぞれを、テストにだけ置く検証用の拡張で、内部の API を使わずに実現できることを手で組み立てた木で確かめる

### Requirement 12: 規約、堅牢性、性能
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want HTML の出力が共通の規約に従い、大きな木や深い木でも安全に動くこと, so that ABI を保ったまま長く使え、信頼できない入力から作った木も出力できる

#### Acceptance Criteria
1. The Amark HTML Renderer shall `foundation` のエラーコード、出力引数、NULL の扱い、アロケータの規約に従い、出力の文脈を不透明型とし、公開する構造体には大きさを表すフィールドを持たせる
2. The Amark HTML Renderer shall 出力に使う確保を、すべて `foundation` のグローバルのアロケータで行う
3. The Amark HTML Renderer shall 深さ 10 万の木でも、呼び出しのスタックを深さに比例して使わずに出力する
4. While ソース位置の属性の出力が指定されていない, the Amark HTML Renderer shall 出力の時間を、部分木のノードの数、出力の大きさ、構文セットのスロットの数、構文木の値の問い合わせの時間の和に比例する範囲に収める
5. Where ソース位置の属性の出力が指定されている, the Amark HTML Renderer shall 4 の時間に、属性を出す各ブロックの位置の問い合わせの時間（構文木の規定では、そのブロックの部分木の大きさに比例し、木全体では最大でノードの数と深さの積に比例する）を加えた範囲に収める
6. The Amark HTML Renderer shall 異なる文書を別々のスレッドで同時に出力できるようにし、凍結した構文セットを共有できるようにする
7. The Amark HTML Renderer shall NUL を含まない正しい UTF-8 の値だけを持つ木から、正しい UTF-8 の出力を作る
