# Research & Design Decisions

## Summary
- **Feature**: `foundation`
- **Discovery Scope**: New Feature（greenfield。コード・ビルド構成・CI がまだない）
- **Key Findings**:
  - Naos v0.4.0 は `Naos::naos` と `naos_add_executable()` / `naos_discover_tests()` を提供し、テストの一覧をビルド直後に実行ファイルを起動して取得する。そのため、Windows で動的版の Amark を使うテストでは、ビルドの時点で DLL が実行ファイルから見つかる必要がある。Naos 自身は、他のプロジェクトに組み込まれると常に静的ライブラリになり、セルフテストとインストール規則は無効になる。
  - CommonMark 0.31.2 では、Unicode punctuation character が「P または S の一般カテゴリ」に広がった（0.30 までは P と ASCII 句読点）。Unicode whitespace character は「Zs、タブ、LF、FF、CR」。ASCII punctuation character は U+0021–2F、3A–40、5B–60、7B–7E。
  - 組み込みの構文も公開 API だけで実装する（steering）。そのため、拡張の実装者が必要とする共通ランタイム（アロケータ、バッファ、UTF-8、文字の分類）は内部ヘッダではなく公開 API として出す必要がある。

## Research Log

### Naos v0.4.0 の統合
- **Context**：テスト基盤の土台。v0.x は破壊的変更がありうる。
- **Sources Consulted**：https://github.com/vabelorg/naos のタグ v0.4.0（README、docs/reference/cmake.md、docs/guides/shared-library.md、include/naos/suite.h、CMakeLists.txt）
- **Findings**：
  - `FetchContent_Declare(Naos GIT_REPOSITORY https://github.com/vabelorg/naos.git GIT_TAG v0.4.0)` と `FetchContent_MakeAvailable(Naos)` で、`Naos::naos` と CMake 関数が使える。CMake 3.21 以上が必要。
  - `naos_add_executable(<target> SOURCES ... LIBRARIES ... [LABELS ...] [PROPERTIES ...])` は実行ファイルを作り、`<target>.<suite>.<test>` の名前で各テストを CTest に登録する。スキップは終了コード 77。
  - テストは `NAOS_TEST` / `NAOS_SUITE` / `NAOS_MAIN`。別ファイルのスイートは `NAOS_DECLARE_SUITE` で宣言して `NAOS_ADD_SUITE` で加える。アサーションは `NAOS_CHECK_*`（続行）と `NAOS_REQUIRE_*`（中断、longjmp）。アサーションを 1 つも実行しないテストは失敗になる。
  - テストの一覧はビルド直後に実行ファイルを `--naos-list` で起動して得る。DLL が見つからないとビルドが失敗する。`naos_add_executable()` では一覧の取得が常に最初の POST_BUILD になるため、DLL のコピーを後から足しても間に合わない。
  - Naos は組み込まれると常に静的ライブラリ。`NAOS_SELFTEST` と `NAOS_INSTALL` は最上位のときだけ既定で ON。
- **Implications**：テストの実行ファイルと Amark の DLL を同じ出力ディレクトリに置く（`RUNTIME_OUTPUT_DIRECTORY` を揃える）。DLL のコピーに頼らない。Naos の取得はテストをビルドするときだけにする。

### CommonMark の文字の定義
- **Context**：要件 14。準拠対象の版の定義を正確に写す必要がある。
- **Sources Consulted**：https://spec.commonmark.org/0.31.2/spec.txt（「Characters and lines」の節）
- **Findings**：上の Key Findings のとおり。大文字小文字の畳み込み（Unicode case fold）はリンクラベルの正規化でだけ使う。
- **Implications**：P と S のカテゴリの表、Zs の表を Unicode のデータから生成する。case fold は `block-parser` が同じ生成手順で追加する（この spec では作らない）。

### 静的版と動的版の作り分け
- **Context**：brief が design に委ねた判断。要件 2.7、3.6。
- **Findings**：
  - CMake の慣習は「1 つのターゲットが `BUILD_SHARED_LIBS` に従う」。両方を 1 回の構成で作る方式は、ターゲット名が 2 つになり `Amark::amark` の意味が曖昧になる。
  - 両方を同じ接頭辞にインストールする場合、エクスポートファイルを種類ごとに分け（`AmarkTargets-static.cmake` / `AmarkTargets-shared.cmake`）、`AmarkConfig.cmake` が `COMPONENTS` や変数で選ぶ方式が広く使われている（Alex Reinking, "Building a Dual Shared and Static Library with CMake"）。
  - MSVC では静的ライブラリ `amark.lib` と DLL のインポートライブラリ `amark.lib` の名前が衝突する。MinGW では `libamark.a` と `libamark.dll.a` で衝突しない。
- **Implications**：1 回の構成では `BUILD_SHARED_LIBS` に従う 1 つの `amark` を作る。両方が必要なら 2 回構成して同じ接頭辞にインストールする。MSVC 形式（静的ライブラリとインポートライブラリの接尾辞が同じ）のときだけ、静的版の出力名を `amark_static` にする。

### エクスポートマクロ
- **Findings**：CMake の `GenerateExportHeader` はビルド時にヘッダを生成するため、ヘッダがソースツリーだけで完結しない（pkg-config や他のビルドシステムから使いにくい）。Naos は手書きのヘッダと `NAOS_SHARED` の定義で対応している。
- **Implications**：手書きの `include/amark/export.h` を使う。動的版では `AMARK_SHARED` を PUBLIC に定義し、Amark 自身のコンパイル時だけ `AMARK_BUILDING` を定義する。可視性の属性（`__attribute__((visibility("default")))`、`__declspec`）は C99 の範囲外なので、この 1 ファイルの条件付きの定義に閉じ込める。

### UTF-8 の不正なバイト列の扱い
- **Sources Consulted**：Unicode Standard の Chapter 3「U+FFFD Substitution of Maximal Subparts」、WHATWG Encoding Standard の UTF-8 decoder
- **Findings**：不正な並びは「最大部分（maximal subpart）」ごとに 1 つの U+FFFD に置き換えるのが推奨。WHATWG と同じ結果になる。先頭バイトは必ず新しい区切りの始まりになり、継続バイトは直前の先頭バイトに属するか、単独で 1 つの U+FFFD になる。
- **Implications**：後ろ向きの復号は「直前の 3 バイト以内の最も近い先頭バイトから前向きに復号して、ちょうど位置に届くか」で決まり、前向きの区切りと必ず一致する。この性質をファジングで確かめる。

### テストデータからのテスト生成
- **Context**：要件 4.7、4.8。`inline-parser` が spec.txt から、`markdown-renderer` が往復のデータから、テストを生成する見込み。
- **Findings**：
  - ビルド時に Python などの追加のツールを要求すると、Windows の MSVC 環境や MSYS2 で準備が増える。CMake のスクリプトモード（`cmake -P`）はどの環境でも使える。
  - CMake の文字列は NUL を持てず、`;` はリストの区切りになる。`file(READ ... HEX)` で 16 進の文字列として読めば任意のバイト列を失わずに扱える。
  - MSVC には文字列リテラルの長さの上限（1 つのリテラルで約 16KB、連結後で 64KB）がある。`unsigned char` の配列の初期化子なら上限がない。
- **Implications**：生成スクリプトは CMake のスクリプトとし、データは 16 進で読み、C の配列の初期化子として書き出す補助を共通に用意する。

### Unicode データの版
- **Findings**：Unicode 17.0.0 が 2025 年 9 月に公開済み。CommonMark はカテゴリの定義だけを参照し、版を固定していない。
- **Implications**：Unicode 17.0.0 に固定し、ヘッダのマクロとテストで明示する。表は保守者が生成スクリプトで作り直し、生成物をリポジトリに入れる（ビルドに Python や UCD のダウンロードを要求しない）。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 1 つの `amark` が `BUILD_SHARED_LIBS` に従う（採用） | 構成ごとに 1 種類。両方は 2 回の構成で作り、同じ接頭辞に並べる | CMake の慣習どおり。`Amark::amark` の意味が常に 1 つ。add_subdirectory の利用者の期待どおり | 両方の配布には 2 回のビルドが要る（リリースのワークフローで吸収） | 後から「1 回で両方」を足しても、利用者側の契約は変わらない |
| 静的・動的を同時に作る（`amark_static` と `amark_shared`） | 1 回の構成で 2 つのターゲット | ビルドが 1 回 | `Amark::amark` がどちらを指すかの規則が増える。オブジェクトの PIC の扱いが複雑 | 却下 |
| OBJECT ライブラリ ＋ 内部テスト用の静的ライブラリ（採用） | ソースを 1 回だけコンパイルし、`amark` と `amark_internal` の両方に使う | 動的版でも内部関数を単体テストできる | 動的版の構成では `amark_internal` のオブジェクトに dllexport が付く（実害なし） | 要件 4.3 |

## Design Decisions

### Decision: 版の定義を `include/amark/version.h` に 1 か所で置く
- **Context**：要件 9.6。
- **Alternatives Considered**：
  1. `project(VERSION)` を正として `configure_file` でヘッダを生成する
  2. 手書きのヘッダを正として、CMake がヘッダを読む
- **Selected Approach**：2。`cmake/AmarkVersion.cmake` が `AMARK_VERSION_MAJOR/MINOR/PATCH/STRING` を読み、矛盾があれば構成を失敗させる。
- **Rationale**：ヘッダが生成物でなくなり、ソースツリーだけで完結する。Naos と同じ方式。
- **Trade-offs**：ヘッダの書式を固定する必要がある（正規表現で読むため）。

### Decision: アロケータは 3 つのコールバックと構造体のサイズを持つ公開構造体
- **Context**：要件 11。
- **Alternatives Considered**：
  1. Lua 方式の 1 関数（`realloc` だけ）
  2. 確保・再確保・解放の 3 関数（サイズ情報なし）
  3. 3 関数に旧サイズも渡す（サイズつきの解放）
- **Selected Approach**：2。メンバ名は `allocate` / `reallocate` / `deallocate`（`malloc` や `free` を関数形式のマクロで置き換えるデバッグ用ツールとの衝突を避ける）。`struct_size` を先頭に置く。
- **Rationale**：標準ライブラリと同じ形で、利用者が既存のアロケータをそのままつなげる。バッファの所有権の移転（detach）でサイズを持ち回る必要がない。
- **Trade-offs**：サイズを必要とするアリーナ型のアロケータは自前でサイズを記録する必要がある。将来、`struct_size` で判定できる任意のフィールドとしてサイズつきの解放を足す余地を残す。

### Decision: バッファは公開の不透明型、内部向けには構造体を公開する内部ヘッダを持つ
- **Context**：要件 12。レンダラの出力スロットを拡張が使うため、公開が必要。内部では構造体に埋め込みたい。
- **Selected Approach**：`amark_buffer` は公開では不透明型（作成・解放の関数経由）。`src/runtime/buffer_internal.h` が構造体の定義と、埋め込み用の初期化・破棄の関数（`amark__buffer_init` / `amark__buffer_destroy`）を持つ。
- **Rationale**：ABI を不透明型で守りつつ、内部の性能を落とさない。

### Decision: テスト生成は CMake スクリプトで行い、データは 16 進で扱う
- **Context**：要件 4.7、4.8。
- **Alternatives Considered**：Python のスクリプト、ビルド時に作る C の生成ツール、CMake スクリプト
- **Selected Approach**：CMake スクリプト。`add_custom_command` でデータごとに C のソースを生成し、データか生成スクリプトが変わればビルド時に作り直す。
- **Rationale**：追加のツールが要らない。クロスコンパイルでもホストのツールを作る必要がない。
- **Trade-offs**：CMake の文字列処理は遅い。spec.txt 程度（約 200KB）を数秒で処理できることを生成の補助の目標とする。

### Decision: ファジングターゲットは libFuzzer がなくても通常の実行ファイルとしてビルドする
- **Context**：要件 7。AppleClang、GCC、MSVC には libFuzzer がない。
- **Selected Approach**：入口の関数 `LLVMFuzzerTestOneInput` を共通にし、libFuzzer がない環境ではファイルを読んで入口を呼ぶだけの小さな `main` をリンクする。コーパスのファイルの一覧は構成時に CMake が集め、引数で渡す（C99 にはディレクトリの列挙がないため）。libFuzzer の実行ファイルもファイルを引数に渡すと 1 回ずつ実行するので、CTest への登録は共通にできる。

### Decision: 文字の分類は範囲の表の二分探索と ASCII の早道
- **Context**：要件 14.6。
- **Alternatives Considered**：2 段の表引き（完全な定数時間）、範囲の表の二分探索
- **Selected Approach**：範囲の表の二分探索。表の大きさは固定なので、1 回の判定の手間に上限がある（約 11 回の比較）。ASCII は表を引かずに判定する。
- **Rationale**：表が小さく（P と S で数百の範囲）、生成も単純。性能が問題になれば、公開 API を変えずに 2 段の表に置き換えられる。

## Risks & Mitigations
- Naos の v0.x の破壊的変更 — タグで固定し、更新は専用の変更で行う。テストの補助を `cmake/AmarkTesting.cmake` に集め、影響を 1 か所に閉じ込める。
- Windows の動的版で、テストの一覧の取得時に DLL が見つからない — テストの実行ファイルと DLL の出力ディレクトリを揃える。CI の Windows の動的版のジョブで検出する。
- CMake スクリプトでのテスト生成が大きなデータで遅い — 補助を 16 進のトークン列の処理に限定し、行の分割を正規表現の置換 1 回で行う。性能の目標を補助のテストで確かめる。
- 可視性の属性がコンパイラ固有 — `export.h` だけに閉じ込め、属性のないコンパイラでは空に展開する。
- MSVC に C99 のモードがない — MSVC では C11 のモード（`/std:c11`）でコンパイルし、C99 の範囲の厳密さは GCC と Clang の `-std=c99 -Wpedantic` で担保する。

## References
- [Naos v0.4.0](https://github.com/vabelorg/naos/tree/v0.4.0) — テストフレームワーク、CMake 関数、DLL の扱い
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — 文字の分類の定義
- [Unicode 17.0.0 UCD](https://www.unicode.org/Public/17.0.0/ucd/) — `UnicodeData.txt`（一般カテゴリ）
- [Unicode Standard Chapter 3, U+FFFD Substitution of Maximal Subparts](https://www.unicode.org/versions/latest/core-spec/chapter-3/) — 不正な UTF-8 の置き換え
- [Building a Dual Shared and Static Library with CMake](https://alexreinking.com/blog/building-a-dual-shared-and-static-library-with-cmake.html) — 静的版・動的版を並べるパッケージ設定
- [libFuzzer](https://llvm.org/docs/LibFuzzer.html) — ファジングの入口と実行の方法
