# Amark

Amark は、Markdown テキストと構文木を相互変換する C99 ライブラリです。

## 概要

- CommonMark に準拠したパースで、Markdown を意味を表す構文木（AST）に変換します。
- 各ノードは元の表記を保持し、無編集で書き戻すと入力とバイト単位で同じ Markdown になります。
- 構文木の参照と編集、HTML へのレンダリング、Markdown への書き戻しを、1 つのモデルの上で提供します。
- 方言（GFM など）は、公開の拡張 API で構文を組み合わせて表現します。
- C99 と C 標準ライブラリだけで書かれており、実行時の外部依存はありません。

## 対応環境

3 つの OS と 5 つのコンパイラ環境で、静的ライブラリと動的ライブラリの両方をビルド・テストします。

| OS | コンパイラ環境 |
|---|---|
| Linux | GCC |
| Linux | Clang |
| macOS | Clang（AppleClang） |
| Windows | MSVC（Visual Studio 2019 16.8 以降） |
| Windows | MinGW-w64（MSYS2 UCRT64 の GCC） |

必要なもの：

- CMake 3.21 以上
- C99 のコンパイラ（MSVC は C99 のモードがないため `/std:c11` でコンパイルします）
- Git（テストを有効にしたとき、テストフレームワークの Naos を FetchContent で取得するために使います）

## ビルド

構成、ビルド、テストは次のとおりです。

```sh
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

動的ライブラリとしてビルドするときは、`BUILD_SHARED_LIBS` を指定します。

```sh
cmake -B build -DBUILD_SHARED_LIBS=ON
```

インストールするときは、接頭辞を指定します。

```sh
cmake --install build --prefix <接頭辞>
```

### 主なオプション

「最上位」は、Amark を最上位のプロジェクトとしてビルドしている場合を指します（`add_subdirectory` や FetchContent で組み込んだ場合は該当しません）。

| オプション | 既定値 | 内容 |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | `ON` で `amark` を動的ライブラリにします。組み込み時は利用者の値に従います |
| `AMARK_BUILD_TESTS` | 最上位かつ `BUILD_TESTING` のとき `ON` | テストを作り、Naos を取得します |
| `AMARK_PACKAGE_TESTS` | `AMARK_BUILD_TESTS` と同じ | 取り込みのテストを登録します |
| `AMARK_INSTALL` | 最上位のとき `ON` | install 規則を作ります |
| `AMARK_WARNINGS_AS_ERRORS` | 最上位のとき `ON` | 警告をエラーにします。明示的に指定すれば、組み込み時もその指定に従います |
| `AMARK_SANITIZE` | 空 | `address`、`undefined` のリスト（例：`-DAMARK_SANITIZE="address;undefined"`）。最上位のときだけ有効です |
| `AMARK_FUZZ` | `OFF` | ファジングターゲットを作ります |
| `AMARK_FUZZ_SMOKE_SECONDS` | `60` | ファジングの短時間実行での、各ターゲットの秒数 |

## 取り込み

Amark は次の 4 つの方法で取り込めます。どの方法でも、ターゲット `Amark::amark` をリンクするだけでヘッダとライブラリを使えます。公開 API は `#include <amark/amark.h>` で使えます。

### find_package

インストール済みの Amark を探します。

```cmake
find_package(Amark 0.1 REQUIRED)
target_link_libraries(app PRIVATE Amark::amark)
```

版の互換性は、1.0 未満では同じメジャー版とマイナー版の版だけ、1.0 以降では同じメジャー版の版だけを互換と判定します。

### add_subdirectory

ソースツリーを直接取り込みます。

```cmake
add_subdirectory(amark)
target_link_libraries(app PRIVATE Amark::amark)
```

### FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(Amark
    GIT_REPOSITORY <Amark の Git リポジトリの URL>
    GIT_TAG v0.1.0)
FetchContent_MakeAvailable(Amark)
target_link_libraries(app PRIVATE Amark::amark)
```

`add_subdirectory` と FetchContent で組み込んだときは、Amark のテスト、ファジング、install 規則は既定で無効になり、Naos も取得しません。警告をエラーにする設定も既定で無効で、利用者のターゲットのコンパイルオプションは変更しません。静的版か動的版かは、利用者の `BUILD_SHARED_LIBS` に従います。

### pkg-config

インストール済みの Amark のコンパイルとリンクのフラグを得ます。

```sh
pkg-config --cflags --libs amark
```

インストールされている種類（静的版か動的版か）に合ったフラグが返ります。

### 静的版と動的版の選択

- 1 つのインストール先の接頭辞には、1 つの種類だけをインストールします。静的版と動的版の両方を使うときは、**接頭辞を分けて**インストールしてください。同じ接頭辞にインストールすることはサポートしません。
- `find_package(Amark)` で種類を指定しなければ、インストールされている種類を使います（`BUILD_SHARED_LIBS` は見ません）。
- 種類を指定するときは、コンポーネントに `static` か `shared` を指定します。

  ```cmake
  find_package(Amark 0.1 REQUIRED COMPONENTS shared)   # 動的版を要求する
  find_package(Amark 0.1 REQUIRED COMPONENTS static)   # 静的版を要求する
  ```

- 要求した種類がインストールされている種類と一致しなければ、パッケージは見つからないものとして扱われ、要求された種類とインストールされている種類がメッセージに示されます。
- 動的版を使う利用者には、`AMARK_SHARED` の定義が自動的に伝わります（`Amark::amark` をリンクする場合も、pkg-config の `--cflags` を使う場合も）。

## ライセンス

Amark は MIT ライセンスで配布します。著作権者は The Amark Authors です。全文は [LICENSE](LICENSE) を参照してください。
