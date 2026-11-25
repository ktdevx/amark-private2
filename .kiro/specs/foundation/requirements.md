# Requirements Document

## Introduction
Amark は、Markdown テキストと構文木を相互変換する C99 ライブラリである。`foundation` は、後続のすべての spec（`syntax-tree`、`html-renderer`、`block-parser`、`inline-parser`、`markdown-renderer`）が依存する土台を用意する。土台は 2 つの部分からなる。

- **インフラ**：Windows・Linux・macOS で静的/動的ライブラリとしてビルド・インストールでき、利用者が 4 つの方法（`find_package`、`add_subdirectory`、FetchContent、pkg-config）で取り込めるビルド構成。Naos によるテスト基盤、GitHub Actions による CI とリリース、ファジングの基盤、ライセンスとソースの規約。
- **共通ランタイム**：公開ヘッダの規約とバージョン API、エラーコードの規約、差し替え可能なアロケータ、伸長できるバイトバッファ、UTF-8 のデコードと検証、CommonMark が要求する文字の分類。

組み込みの構文も公開 API だけを使って実装する方針（steering）のため、共通ランタイムのうち拡張の実装者が必要とするもの（アロケータ、バッファ、UTF-8、文字の分類）は公開 API として提供する。

現在のリポジトリにはコード・ビルド構成・CI がない（greenfield）。

## Boundary Context
- **In scope**：
  - トップレベルのビルド構成、ビルドのオプション、install と export、CMake のパッケージ設定、pkg-config のファイル
  - コンパイラの警告設定、C99 の厳密な適用
  - Naos との統合、テスト用の補助（テスト用アロケータなど）、テストデータからテストを生成する汎用の仕組み
  - GitHub Actions のワークフロー（CI、サニタイザ、ファジングの短時間実行、リリース）
  - LICENSE、README の骨子、ソースのライセンスヘッダの規約とその検査
  - 公開ヘッダの配置、全体をまとめるヘッダ、バージョン API
  - エラーコード、アロケータ、バイトバッファ、UTF-8、文字の分類（Unicode の空白・句読点、ASCII の句読点）
  - ファジングの基盤と、共通ランタイムのファジングターゲット
- **Out of scope**：
  - ノード型、構文トークン、構文木、パーサ、レンダラの実装
  - CommonMark の spec.txt の取り込みと準拠テストのランナー（`inline-parser` が持つ）
  - Unicode の大文字小文字の畳み込み（case folding）や HTML の文字参照の表など、特定の構文だけが使う文字データ（必要とする spec が、この spec の用意した仕組みの上に追加する）
  - CLI ツール、C 以外の言語へのバインディング
- **Adjacent expectations**：
  - 後続の spec は、この spec が定めるエラー規約とメモリ管理の規約に従い、テストとファジングターゲットをこの spec の仕組みで追加する。
  - 文字の分類は、準拠対象とする CommonMark spec の版（0.31.2）の定義に従う。準拠対象の版を変えるときは、この spec の分類を見直す。
  - 後続の spec は、この spec が持つファイルに次の追加を行うことが予定されている（2026-11-25 に確認）。追加の作業と追加した部分の所有は追加する spec にあり、この spec は追加を受け入れる拡張点（生成の手順、属性のファイル、CI の構成、状態コードの末尾）を保つ。
    - `block-parser`：Unicode の生成手順への `CaseFolding.txt` の読み取りと case fold の関数、HTML の文字参照の表とその生成手順、最上位の構成へのベンチマークのオプションとそのサブディレクトリ、配布物からブロックの準拠データを除く属性（`export-ignore`）
    - `inline-parser`：配布物から spec.txt を除く属性（`export-ignore`）と、CI の構成で準拠テストのデータを必須にする指定
    - `markdown-renderer`：「表現できない」ことを表す状態コード（この spec の要件 10.9 として取り込み済み）
  - 後続の spec は、ファジングターゲットにリンクするテスト用の補助ライブラリを Naos に依存させず、この spec の規則（要件 7.7、7.8）に従って定義する。Naos に依存する検査の補助は、ファジングターゲットにリンクしない別のライブラリに分ける。
  - 後続の spec は、全体をまとめるヘッダに自分の公開ヘッダを、この spec が定める正規の順（要件 9.7）の自分の位置に追加する。

## Requirements

### Requirement 1: クロスプラットフォームのビルド
**Objective:** As a Amark の開発者, I want 3 つの OS と 5 つのコンパイラ環境で同じ手順でビルドとテストができること, so that 移植性の問題を早く見つけられる

#### Acceptance Criteria
1. When 開発者がリポジトリの最上位で既定の設定のまま構成・ビルド・テストを順に実行する, the Amark Build shall Linux（GCC、Clang）、macOS（Clang）、Windows（MSVC、MinGW-w64）のそれぞれで追加の手順なしに成功する
2. The Amark Build shall ライブラリとテストを C99 の規格に従ってコンパイルし、コンパイラ固有の言語拡張を無効にする
3. If 開発者が CMake 3.21 より古い版で構成する, the Amark Build shall 構成の段階で、必要な版を示して失敗する
4. While Amark が最上位のプロジェクトとしてビルドされている, the Amark Build shall GCC と Clang では `-Wall -Wextra -Wpedantic -Werror`、MSVC では `/W4 /WX` に相当する警告設定で Amark 自身のソースをコンパイルする
5. While Amark が他のプロジェクトに組み込まれてビルドされている, the Amark Build shall 警告をエラーとして扱う設定を既定で無効にし、利用者のターゲットのコンパイルオプションを変更しない
6. Where 開発者が警告をエラーとして扱う設定を明示的に指定する, the Amark Build shall 最上位か組み込みかにかかわらず、その指定に従う
7. The Amark Build shall 実行時に C 標準ライブラリ以外のライブラリを必要としない

### Requirement 2: 静的ライブラリと動的ライブラリ、シンボルの公開範囲
**Objective:** As a Amark の利用者, I want 静的版と動的版のどちらでも同じ名前で Amark をリンクでき、公開 API だけが見えること, so that 配布形態を選べ、ABI を安定して保てる

#### Acceptance Criteria
1. When 開発者が静的ライブラリを選んで構成する, the Amark Build shall Amark を静的ライブラリとしてビルドする
2. When 開発者が動的ライブラリを選んで構成する, the Amark Build shall Amark を動的ライブラリ（Windows では DLL）としてビルドする
3. The Amark Build shall どちらの構成でも、ライブラリのターゲット `amark` とエイリアス `Amark::amark` を提供する
4. Where Amark が動的ライブラリとしてビルドされる, the Amark Build shall 公開 API として宣言された `amark_` 接頭辞の関数だけをエクスポートし、内部の関数と変数をエクスポートしない
5. Where Amark が Windows で動的ライブラリとして使われる, the Amark Build shall 利用者が `Amark::amark` をリンクするだけで、手作業のマクロ定義なしに DLL の関数を呼べるようにする
6. Where Amark が動的ライブラリとしてビルドされる, the Amark Build shall 1.0 未満ではメジャー版とマイナー版から、1.0 以降ではメジャー版から ABI の互換性番号を決めて動的ライブラリに付ける
7. When 開発者が静的版と動的版を同じインストール先に順にインストールする, the Amark Build shall 両方のファイルを衝突させずに並べて置く

### Requirement 3: インストールとパッケージの利用
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want 自分のビルド方法に合った方法で Amark を取り込めること, so that 追加の設定なしに Amark を使い始められる

#### Acceptance Criteria
1. When 開発者がビルドした Amark をインストールする, the Amark Build shall 公開ヘッダ、ライブラリ、CMake のパッケージ設定、pkg-config のファイル、ライセンスをインストール先に置く
2. When 利用者のプロジェクトがインストール済みの Amark を `find_package(Amark)` で探す, the Amark Package shall `Amark::amark` を提供し、リンクするだけでヘッダとライブラリを使えるようにする
3. When 利用者のプロジェクトが Amark を `add_subdirectory` または FetchContent で取り込む, the Amark Build shall `Amark::amark` を提供し、Amark のテスト・ファジング・インストール規則を既定で無効にし、Naos を取得しない
4. When 利用者が pkg-config でインストール済みの Amark を問い合わせる, the Amark Package shall インストールされた種類（静的/動的）のリンクに必要なコンパイルとリンクのフラグを返す
5. When 利用者が `find_package` で版を指定する, the Amark Package shall 1.0 未満では同じメジャー版とマイナー版の版だけを、1.0 以降では同じメジャー版の版だけを互換と判定する
6. Where 静的版と動的版の両方がインストールされている, the Amark Package shall 利用者がどちらを使うかを選べるようにし、指定がないときは文書化された規則に従って一方を選ぶ
7. If 利用者が要求した種類（静的/動的）がインストールされていない, the Amark Package shall パッケージが見つからないものとして扱い、理由を示す

### Requirement 4: テスト基盤
**Objective:** As a Amark の開発者, I want Naos でテストを書き、CTest で個別に実行でき、メモリの扱いまで検査できる共通の仕組み, so that すべての spec が同じ方法で品質を確かめられる

#### Acceptance Criteria
1. The Amark Test Infrastructure shall Naos をタグ v0.4.0 に固定して取得し、テストをビルドするときだけ取得する
2. When 開発者がテストをビルドする, the Amark Test Infrastructure shall 各テストケースを個別の CTest のテストとして登録する
3. The Amark Test Infrastructure shall 公開 API だけを使うテストと内部の関数を使うテストの両方を、静的・動的のどちらの構成でもビルドして実行できるようにする
4. Where Amark が Windows で動的ライブラリとしてビルドされる, the Amark Test Infrastructure shall 手作業なしにテストの実行ファイルが Amark の DLL を見つけられるようにする
5. The Amark Test Infrastructure shall 指定した回数目の確保を失敗させられ、確保と解放の対応（リーク）を検査できるテスト用のアロケータを提供する
6. The Amark Test Infrastructure shall 対象の処理を、確保の各位置で順に失敗させながら繰り返し、各回でメモリ不足のエラーが返ることとリークがないことを確かめる補助を提供する
7. The Amark Test Infrastructure shall テストデータのファイルからテストケースを生成してテストの実行ファイルに組み込む汎用の仕組みを提供し、NUL や不正な UTF-8 を含む任意のバイト列を変えずに保つ
8. When 開発者がテストデータのファイルを変更して再ビルドする, the Amark Test Infrastructure shall 再構成なしにテストケースを生成し直し、CTest への登録を更新する
9. The Amark Test Infrastructure shall 各公開ヘッダが単独で C99 のコンパイラと C++ のコンパイラでコンパイルできることをテストで確かめる
10. Where Amark が動的ライブラリとしてビルドされ、エクスポートされたシンボルを一覧できるツールが使える, the Amark Test Infrastructure shall エクスポートされたシンボルが `amark_` 接頭辞のものだけであることをテストで確かめる
11. The Amark Test Infrastructure shall 4 つの取り込み方法（`find_package`、`add_subdirectory`、FetchContent、pkg-config）のそれぞれで、利用者のプロジェクトが構成・ビルド・実行できることを確かめるテストを提供する
12. If テストの実行に必要なツール（pkg-config など）がない, the Amark Test Infrastructure shall そのテストを失敗ではなくスキップとして報告する

### Requirement 5: 継続的インテグレーション
**Objective:** As a Amark の保守者, I want すべての変更が対象の全環境とサニタイザで自動的に検査されること, so that 移植性やメモリ安全性の問題がマージ前に見つかる

#### Acceptance Criteria
1. When 変更が既定のブランチに push されるか、プルリクエストが作成・更新される, the Amark CI shall Linux（GCC、Clang）、macOS（Clang）、Windows（MSVC、MinGW-w64）のそれぞれで、静的版と動的版の両方をビルドし、すべてのテストを実行する
2. When 変更が既定のブランチに push されるか、プルリクエストが作成・更新される, the Amark CI shall AddressSanitizer と UndefinedBehaviorSanitizer を有効にしたビルドですべてのテストを実行し、検出があれば失敗とする
3. When 変更が既定のブランチに push されるか、プルリクエストが作成・更新される, the Amark CI shall すべてのファジングターゲットを短い時間の上限つきで実行し、クラッシュや検出があれば失敗とする
4. If いずれかのジョブで構成・ビルド（警告を含む）・テストが失敗する, the Amark CI shall ワークフロー全体を失敗として報告し、失敗したジョブとテストの出力を確認できるようにする
5. The Amark CI shall パッケージの取り込みのテスト（4 つの取り込み方法）を、対象の各環境で実行する

### Requirement 6: リリース
**Objective:** As a Amark の保守者, I want 版のタグを付けるだけで配布用の成果物ができること, so that 手作業の誤りなくリリースできる

#### Acceptance Criteria
1. When `v<メジャー>.<マイナー>.<パッチ>` の形式のタグが push される, the Amark Release shall Linux、macOS、Windows のそれぞれについて、静的版と動的版の両方を含むインストール済みの一式をアーカイブにまとめる
2. When `v<メジャー>.<マイナー>.<パッチ>` の形式のタグが push される, the Amark Release shall ソースのアーカイブと、すべての成果物のチェックサムを作り、成果物と一緒にそのタグの GitHub リリースに添付する
3. If タグの版がソースに定義された Amark の版と一致しない, the Amark Release shall 成果物を作らずに失敗し、2 つの版を示す
4. If リリースのためのビルドかテストが失敗する, the Amark Release shall リリースを公開しない
5. The Amark Release shall 成果物に LICENSE を含め、テストにだけ使うデータ（CommonMark の spec.txt など）を含めない

### Requirement 7: ファジングの基盤
**Objective:** As a Amark の開発者, I want ファジングターゲットを簡単に追加でき、どの環境でも少なくともコーパスの再生はできること, so that 後続のパーサやレンダラを不正な入力に対して検査できる

#### Acceptance Criteria
1. The Amark Build shall ファジングを既定で無効にする
2. Where 開発者がファジングを有効にし、コンパイラが libFuzzer に対応している, the Amark Build shall 登録された各ファジングターゲットを libFuzzer の実行ファイルとしてビルドする
3. Where 開発者がファジングを有効にし、コンパイラが libFuzzer に対応していない, the Amark Build shall 同じファジングターゲットを、コーパスの各ファイルを入力として 1 回ずつ実行する通常の実行ファイルとしてビルドする
4. While テストがビルドされている, the Amark Build shall 各ファジングターゲットのコーパスの再生を CTest のテストとして登録し、ファジングの有効・無効にかかわらず実行できるようにする
5. The Amark Build shall 後続の spec が、ファジングの入口となるソースとコーパスのディレクトリを指定するだけで新しいファジングターゲットを追加できるようにする
6. The Amark Build shall 共通ランタイムの UTF-8 の処理とバイトバッファを対象とするファジングターゲットを提供する
7. Where 開発者がファジングかテストのどちらかを有効にする, the Amark Build shall ファジングターゲットがリンクするテスト用の補助ライブラリを Naos に依存しない形で定義し、テストを無効にしてファジングだけを有効にした構成でも Naos を取得せずに構成とビルドを成功させる
8. If ファジングターゲットの追加でリンク対象に指定されたライブラリが定義されていないか Naos に直接依存している, the Amark Build shall 構成の段階で、ファジングターゲットとライブラリの名前を示して失敗する

### Requirement 8: ライセンスとソースの規約
**Objective:** As a Amark の保守者, I want ライセンスと著作権の表示がリポジトリ全体で一貫していること, so that MIT ライセンスのライブラリとして安心して配布・利用できる

#### Acceptance Criteria
1. The Amark Repository shall 最上位に MIT ライセンスの LICENSE を置き、著作権者を「The Amark Authors」と表示する
2. The Amark Repository shall 概要、対応環境、ビルドの方法、取り込みの方法、ライセンスの節を持つ README を置く
3. The Amark Repository shall ライブラリ、テスト、ファジングのすべての C のソースとヘッダの先頭に、SPDX 形式（MIT）のライセンス識別子と著作権の表示を置く
4. If ライセンスヘッダのない C のソースかヘッダがある, the Amark Test Infrastructure shall テストを失敗させ、該当するファイルを示す
5. The Amark Repository shall テストデータのファイルが、どの OS でチェックアウトしても改行コードを含めてバイト単位で同じ内容になるようにする

### Requirement 9: 公開ヘッダとバージョン API
**Objective:** As a Amark の利用者, I want 1 つのヘッダで全 API を使え、コンパイル時と実行時の両方で版を確かめられること, so that 取り込みが簡単で、ヘッダと異なる版のライブラリが読み込まれたことを検出できる

#### Acceptance Criteria
1. The Amark Runtime shall すべての公開ヘッダを `amark/` 以下に置き、`amark/amark.h` を include するだけで全公開 API を使えるようにする
2. The Amark Runtime shall 公開ヘッダを C++ のソースから include しても、関数を C の名前でリンクできるようにする
3. The Amark Runtime shall 公開 API のすべての関数と型に `amark_`、マクロに `AMARK_` の接頭辞を付ける
4. The Amark Runtime shall コンパイル時に参照できる版（メジャー、マイナー、パッチ、版の文字列、大小を比較できる版の数値）を提供する
5. When 利用者が実行時に版を問い合わせる, the Amark Runtime shall 実際にリンクされたライブラリの版の数値と版の文字列を返す
6. The Amark Build shall 版を 1 か所で定義し、ヘッダの版、実行時の版、CMake のパッケージの版、pkg-config の版、動的ライブラリの互換性番号をすべてその値から決める
7. The Amark Runtime shall 全体をまとめるヘッダが公開ヘッダを include する順を 1 つの正規の順として定め、すべての spec の公開ヘッダをその順に並べる

### Requirement 10: エラーの規約
**Objective:** As a 後続の spec の開発者と Amark の利用者, I want 失敗の伝え方と失敗後の状態がライブラリ全体で同じであること, so that エラー処理を一貫した方法で書ける

#### Acceptance Criteria
1. The Amark Runtime shall 失敗しうる公開関数が状態コードを返し、成功のときは成功を表すコードを、結果は出力引数で返すという規約を定める
2. The Amark Runtime shall メモリ不足、不正な引数、サイズの桁あふれ、上限の超過、不正な状態を、互いに区別できる状態コードとして定義する
3. When 利用者が状態コードの説明を問い合わせる, the Amark Runtime shall 解放の不要な静的な文字列を返し、未知のコードに対しても NULL でない文字列を返す
4. If 公開関数が失敗する, the Amark Runtime shall 操作の対象のオブジェクトを呼び出し前の有効な状態のまま保ち、オブジェクトを返す出力引数には NULL を設定する
5. If 必須のポインタ引数に NULL が渡される, the Amark Runtime shall 不正な引数を表すコードを返し、異常終了しない
6. When 解放の関数に NULL が渡される, the Amark Runtime shall 何もせずに戻る
7. The Amark Runtime shall 既存の状態コードの値を将来の版で変えず、新しいコードは追加だけで増やす
8. The Amark Runtime shall グローバルな可変状態を持たず、別々のオブジェクトを別々のスレッドから同時に使えるようにする
9. The Amark Runtime shall 対象（構文木など）を要求された出力の形式で意味を変えずに表せないことを表す状態コードを、2 の状態コードと区別できるコードとして、既存のコードの値を変えずに末尾に追加して定義する

### Requirement 11: 差し替え可能なアロケータ
**Objective:** As a Amark を組み込むアプリケーションの開発者, I want Amark が使うメモリをすべて自分のアロケータで管理できること, so that メモリの上限や配置をアプリケーション側で制御できる

#### Acceptance Criteria
1. The Amark Runtime shall 確保・再確保・解放の関数と利用者のデータからなるアロケータを、メモリを確保するオブジェクトの作成時に渡せるようにする
2. When アロケータが指定されない, the Amark Runtime shall C 標準ライブラリの確保関数に基づく既定のアロケータを使う
3. The Amark Runtime shall あるオブジェクトのために確保するメモリを、すべてそのオブジェクトの作成時に指定されたアロケータで確保し、同じアロケータで解放する
4. If アロケータがメモリの確保に失敗する, the Amark Runtime shall メモリ不足を表すコードを返し、その操作のためにすでに確保したメモリをリークしない
5. If 渡されたアロケータに必要な関数がないか、アロケータの構造体のサイズが足りない, the Amark Runtime shall 不正な引数を表すコードを返し、そのアロケータを使わない
6. The Amark Runtime shall アロケータの構造体に構造体のサイズを表すフィールドを持たせ、将来の版でフィールドを追加しても、古い版に合わせてビルドされたプログラムが動き続けるようにする
7. If 要素数と要素のサイズを指定した確保で、サイズの計算が桁あふれする, the Amark Runtime shall アロケータを呼ばずに失敗を返す
8. The Amark Runtime shall 拡張の実装者が、オブジェクトと同じアロケータでメモリを確保・解放できる公開の関数を提供する

### Requirement 12: 伸長できるバイトバッファ
**Objective:** As a 後続の spec の開発者と拡張の実装者, I want 任意のバイト列を効率よく組み立てられるバッファ, so that レンダラの出力やテキストの組み立てを安全に書ける

#### Acceptance Criteria
1. The Amark Runtime shall NUL を含む任意のバイト列を末尾に追加でき、必要に応じて容量を伸ばすバッファを提供する
2. The Amark Runtime shall バッファの内容の直後に常に NUL を置き、内容の先頭と長さを参照できるようにする
3. The Amark Runtime shall バッファに、1 バイトの追加、同じバイトの繰り返しの追加、NUL 終端の文字列の追加、符号位置の UTF-8 での追加、容量の予約、短い長さへの切り詰め、内容を空にする操作、内容の所有権を呼び出し側に移して取り出す操作を提供する
4. If 容量の伸長に失敗する, the Amark Runtime shall メモリ不足を表すコードを返し、バッファの内容と長さを呼び出し前のまま保つ
5. If 追加や予約の後の長さが表現できる大きさを超える, the Amark Runtime shall 桁あふれを表すコードを返し、バッファを変更しない
6. If 現在の長さより長い長さへの切り詰めが指定される, the Amark Runtime shall 不正な引数を表すコードを返し、バッファを変更しない
7. When 利用者が合計 n バイトを少しずつ追加する, the Amark Runtime shall 容量の再確保の回数を n の対数に比例する程度に抑える
8. When 利用者がバッファの内容の所有権を取り出す, the Amark Runtime shall NUL 終端の内容と長さを返し、取り出した内容をバッファのアロケータで解放できるようにし、バッファを空の状態に戻す

### Requirement 13: UTF-8 のデコード・検証・エンコード
**Objective:** As a 後続の spec の開発者と拡張の実装者, I want 不正なバイト列があっても安全に UTF-8 を扱える関数, so that 入力をクラッシュせずに処理し続けられる

#### Acceptance Criteria
1. When 利用者が長さを指定したバイト列の先頭の文字を復号する, the Amark Runtime shall 符号位置と、消費したバイト数を返す
2. If 先頭のバイト列が不正な UTF-8（冗長な表現、サロゲート、U+10FFFF を超える値、途中で切れた並び、不正な先頭バイト）である, the Amark Runtime shall U+FFFD と、Unicode の推奨する「最大部分の置き換え」に従った 1 以上のバイト数を返す
3. When 利用者が長さを指定したバイト列の、指定した位置の直前にある文字を復号する, the Amark Runtime shall その文字の符号位置とバイト数を、先頭から順に復号したときの区切りと矛盾しない形で返す
4. When 利用者が長さを指定したバイト列を検証する, the Amark Runtime shall バイト列全体が正しい UTF-8 かどうかを返し、正しくないときは最初の不正なバイトの位置を返す
5. When 利用者が符号位置を符号化する, the Amark Runtime shall 1 から 4 バイトの UTF-8 を返し、サロゲートや U+10FFFF を超える値に対しては U+FFFD を符号化する
6. The Amark Runtime shall 長さ 0 を含む任意の入力に対して、指定された長さの外を読まず、入力のバイト列を書き換えない
7. The Amark Runtime shall 入力に NUL 終端を要求せず、途中に NUL を含むバイト列も長さどおりに扱う

### Requirement 14: 文字の分類
**Objective:** As a 後続の spec の開発者と拡張の実装者, I want CommonMark の定義どおりの文字の分類, so that 強調の区切りやエスケープなどを仕様どおりに判定できる

#### Acceptance Criteria
1. The Amark Runtime shall 符号位置が CommonMark の Unicode whitespace character（Unicode の Zs 一般カテゴリ、タブ、LF、FF、CR）かどうかを判定する
2. The Amark Runtime shall 符号位置が CommonMark の Unicode punctuation character（Unicode の P または S の一般カテゴリ）かどうかを判定する
3. The Amark Runtime shall 符号位置が CommonMark の ASCII punctuation character かどうかを判定する
4. If 判定の対象が Unicode の範囲外の値かサロゲートである, the Amark Runtime shall どの分類にも当たらないと判定する
5. The Amark Runtime shall 分類に使う Unicode のデータの版を固定し、その版を文書とテストで明示する
6. The Amark Runtime shall 1 回の判定を、符号位置の値によらず一定の上限の時間内で終える
7. The Amark Runtime shall Unicode のデータから分類の表を生成し直す手順を保守者向けに提供し、後続の spec が別の文字の性質の表を同じ手順で追加できるようにする
