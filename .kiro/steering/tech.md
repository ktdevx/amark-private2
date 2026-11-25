# Technology Stack

## Architecture

**構文拡張を中心にした、構文定義駆動のアーキテクチャ**とする。

- **構文拡張（`amark_extension_t`）** が機能の単位である。1 つの構文拡張は、次のスロットのうち必要なものを持つ（すべてを持つ必要はない）。
  - ノード型の登録
  - ブロック構文の定義
  - インライン構文の定義
  - 文書の後処理
  - HTML 出力の処理
  - Markdown 出力の処理
- CommonMark の各構文も構文拡張として実装する（組み込み拡張）。**内部専用の近道は作らない**。組み込みの構文が必要とするフックは、公開 API として提供する。
- 出力は**ノード型**に結び付ける。組み込みのノード型の出力は、コア出力拡張（`amark.core.html`、`amark.core.markdown`）が持ち、パースの規則を持つ `commonmark.*` 拡張は出力を持たない。新しいノード型を足す拡張は、パースと出力を 1 つの拡張にまとめて持てる。出力の処理は後から登録したものが優先され、前の処理を呼び出して包むこともできる。
- **構文セット**は、構文拡張を順序付きで組み合わせたものである。パーサとレンダラは、構文セットに従って動く。組み込みの構文を無効化したり置き換えたりもできる。
- **パースは 2 段階で行う**（CommonMark と同じ）。まずブロック構造を行単位で確定させ、次に段落や見出しの中身をインラインとして解析する。最後に文書の後処理を行う。
- **構文木は、ロスレスな構文情報を持つ AST** とする。
  - ノードは意味の単位である。
  - 元の表記（マーカー、空白、改行、コンテナの接頭辞、エスケープ、ソース範囲）を、構文トークンとしてノードに付ける。
  - 編集を追跡する（粒度は design で決める）。
- **Markdown への書き戻しは局所的に行う**。編集していない部分は元の表記を出し、編集した部分は、編集後の木と同じ意味の正しい Markdown として出力する。エスケープや区切りの選び直しの方式は design で決める。ただし、後から方式を改善できる余地を残す。

## Core Technologies

- **Language**：C99（ISO/IEC 9899:1999）。コンパイラ固有の拡張は使わない。
- **Build**：CMake 3.21 以上
- **Test**：[Naos](https://github.com/vabelorg/naos)（FetchContent でタグ v0.4.0 に固定）と CTest
- **CI/CD**：GitHub Actions
- **Platforms**：
  - Linux（GCC、Clang）
  - macOS（Clang）
  - Windows（MSVC、MinGW-w64）

## Key Libraries

- 実行時の外部依存は持たない。C 標準ライブラリだけを使う。
- テスト時だけ、Naos を使う。
- 準拠テストのデータとして CommonMark の spec.txt を使う。バージョンは固定する。ライセンスが CC-BY-SA 4.0 なので、テストデータとしてだけ扱い、配布物には含めない。

## Development Standards

### API 設計
- 公開のシンボルと型には `amark_` を、マクロには `AMARK_` を接頭辞として付ける。typedef した型の名前は末尾に `_t` を付ける（タグには付けない）。
- 公開型は**不透明型**を基本とし、ABI を安定させる。構造体を公開する場合は、サイズや版を表すフィールドを持たせて、将来の拡張に備える。
- 公開関数には `AMARK_API`（エクスポートマクロ）を付ける。シンボルの可視性は既定で hidden にする。
- メモリの確保は、すべて `amark_malloc` などを通して、差し替え可能なグローバルのアロケータ（`amark_allocator_t`、`amark_allocator_set`）を経由させる。オブジェクトごとのアロケータは持たず、関数の引数でも渡さない。直接 `malloc` を呼ぶのは既定のアロケータだけにする。
- グローバルな可変状態は、このアロケータの設定だけとする。
- 失敗しうる関数は、エラーコードを返す。結果は出力引数で返す。エラーの規約は `foundation` で定める。
- 入力は UTF-8 とする。不正なバイト列があってもクラッシュせず、処理を続ける。

### Code Quality
- 警告をエラーとして扱う。GCC と Clang では `-Wall -Wextra -Wpedantic -Werror`、MSVC では `/W4 /WX` を使う。
- CI でサニタイザ（AddressSanitizer、UndefinedBehaviorSanitizer）を使ったジョブを実行する。
- パーサには libFuzzer のファジングターゲットを用意する。
- 計算量について：病的な入力（深い入れ子、大量の区切り文字）に対しても、二次以上の計算量にならないようにする。入れ子の深さには上限を設ける。

### Testing
- テストは Naos で書き、`naos_add_executable` で CTest に登録する。
- 正しさの基準は、次の 3 つとする。
  1. CommonMark spec の全例で、HTML 出力が 100% 一致すること
  2. 無編集の往復で、入力とバイト単位で同じ出力になること
  3. 編集した木を書き戻して再パースすると、編集後の木と意味的に等しいこと
- 拡張 API の表現力は、テストの中にだけ置く GFM 相当の検証用拡張で確かめる。
- 木の比較には、テキスト形式のダンプ（S 式などの形式）を使う。

## Development Environment

### Required Tools
- CMake 3.21 以上
- C99 コンパイラ（GCC、Clang、MSVC、MinGW-w64）
- Git（Naos の FetchContent で使う）

### Common Commands
```bash
# Configure: cmake -B build -DCMAKE_BUILD_TYPE=Debug
# Build:     cmake --build build
# Test:      ctest --test-dir build --output-on-failure
# Shared:    cmake -B build -DBUILD_SHARED_LIBS=ON
```

## Key Technical Decisions

- **構文定義駆動のエンジンを採用し、固定パーサ＋フック方式は採らなかった**：方言を拡張の組み合わせだけで完全に表現するため。後付けのフックで方言を足すと歪みが出る（cmark-gfm の表や脚注がその例）。
- **ロスレスな構文情報付き AST を採用し、正規化 AST や完全な CST は採らなかった**：主な用途（翻訳のための置換）には、「意味単位で編集できること」と「触っていない部分の表記を保つこと」の両方が必要なため。
- **GFM は同梱しない**：配布物の責務を、CommonMark と拡張の基盤に絞る。GFM が拡張だけで実装できることは、テストで保証する。
- **Naos はタグで固定する**：v0.x 系は、マイナーバージョンでも破壊的変更がありうるため。

---
_依存関係を列挙するのではなく、判断の基準になる標準と決定を記す_
