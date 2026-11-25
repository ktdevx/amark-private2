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
- 定数の集合は、閉じた選択肢の集合なら `typedef enum` で型を作る。固定幅の整数型（`uint32_t` など）に入れる値で、拡張で増える開いた集合、予約した範囲、ビットの印のどれかに当たるものは、`#define` で `u` を付けて定義する（例：ノード型の識別子、スロットの種類、編集の記録の印）。公開ヘッダでは無名の `enum` を使わない（内部の構造体の、閉じた状態を表すフィールドには使ってよい）。
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

### Code Style
自動整形ツール（`.clang-format` など）は置いていない。次の規約に手で従う。

- **波括弧は Allman 方式**とする。関数だけでなく、制御文（`if`/`else`/`for`/`while`/`switch`/`case`）、`struct`/`enum`/`union`、`extern "C"` の開き括弧も独立した行に置く。`else` も `}` とは別の行に書く。
- 例外：集成体の初期化子（`= {`）、マクロの本体（`do { ... } while (0)`）、プリプロセッサ行（`#define AMARK_BEGIN_DECLS extern "C" {`）は 1 行の中に書く。
- **演算の順序は括弧で明示する**。`&&`/`||` のオペランドになる比較は括弧で囲む。比較の左右に算術式（`+ - * / % << >>`）があれば、それも括弧で囲む。比較でないオペランド（変数、関数呼び出し、`!x`）には付けない。
- インデントは空白 4 つ（タブは使わない）。`if (`・`for (` のように、キーワードと `(` の間に空白を 1 つ置く。ポインタの `*` は名前の側に付ける（`amark_buffer_t *buffer`）。
- **処理のまとまり（ステップ）が変わるところに空行を 1 つ入れる**。1 つのステップの文は空行を挟まずに続ける。
  - 引数の検査、準備、本処理、結果の書き出し、後始末、といった区切りで空ける。テストでは「準備 / 操作とその確認 / 後始末」で区切る。
  - 制御文のブロックを閉じる `}` の次に同じ階層の文が続くときは、原則として空ける（次の行が `}`・`else`・プリプロセッサ行のときは空けない）。
  - 直前の文の結果を確かめる文は続けて書く（`p = amark_malloc(n);` と `if (p == NULL)`、`err = f();` と `CHECK(err == ...)`）。
  - 関数の最後で状態だけを返す `return`（`return AMARK_ERROR_NONE;` など）は、2 文以上の処理の後なら空行で分ける。直前の文で作った結果を返す `return` や、1 文だけの後の `return` は続けてよい。
  - `{` の直後と `}` の直前には空行を置かない。空行を 2 行続けない。
- コメントは `/* */` だけを使う（C99 でも `//` は使わない）。1 行は 100 字程度までを目安にする。

```c
amark_error_t amark_buffer_append_repeat(amark_buffer_t *buffer, unsigned char byte,
                                         size_t count)
{
    amark_error_t err;

    if (buffer == NULL)
    {
        return AMARK_ERROR_INVALID_ARGUMENT;
    }

    err = ensure_room(buffer, count);
    if (err != AMARK_ERROR_NONE)
    {
        return err;
    }

    memset(buffer->data + buffer->length, byte, count);
    buffer->length += count;
    buffer->data[buffer->length] = '\0';

    return AMARK_ERROR_NONE;
}

if ((b != 0) && (a > (SIZE_MAX / b)))
{
    return false;
}
```

### Testing
- テストは Naos で書き、`amark_add_test`（`naos_add_executable` を包み、共通のコンパイル設定とテスト支援ライブラリを付ける）で CTest に登録する。テストデータから生成するテストは `amark_add_generated_tests` を使う。
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
_updated_at: 2026-11-25（Code Style を追加・ブロック後の空行を追記、テストの登録関数を実装に合わせた）_
