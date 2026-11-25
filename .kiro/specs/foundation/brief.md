# Brief: foundation

## Problem
Amark を開発する人は、Windows・Linux・macOS の 3 OS で静的/動的ライブラリとしてビルドし、Naos でテストし、CI で品質を保証できる土台を必要としている。これがないと、後に続く spec ごとにビルド構成やエラー処理、メモリ管理の規約がばらばらになり、ライブラリとしての一貫性（ABI、シンボルの公開範囲、移植性）が失われる。

## Current State
リポジトリには Kiro のスキルとテンプレートしかなく、コード・ビルド構成・CI はまだない（greenfield）。

## Desired Outcome
- `cmake -B build && cmake --build build && ctest --test-dir build` が 3 OS で通る。
- 静的ライブラリと動的ライブラリの両方をビルドでき、install できる。利用者は `find_package(Amark)`、`add_subdirectory`、FetchContent、pkg-config のいずれでも使える。
- GitHub Actions が、Linux（GCC、Clang）、macOS（Clang）、Windows（MSVC、MinGW）、サニタイザ（ASan/UBSan）のマトリクスでビルドとテストを行う。タグを付けたときにリリース成果物を作る。
- 後続の spec が使う共通ランタイムが揃っている。

## Approach
- CMake 3.21 以上を使う。ライブラリターゲットは `amark` とし、エイリアス `Amark::amark` を用意する。`BUILD_SHARED_LIBS` に従う構成と、静的版・動的版を同時に作る構成のどちらを採るかは design で決める。
- エクスポートマクロ（`AMARK_API`）を用意し、シンボルの可視性は既定で hidden にする。Windows の dllexport/dllimport にも対応する。
- Naos は FetchContent で v0.4.0 に固定し、`naos_add_executable` を使って CTest に登録する。
- 共通ランタイムとして次を用意する。
  - 差し替え可能なアロケータ（`amark_allocator_t`）
  - 伸長できるバイトバッファ
  - エラーコードと結果型の規約
  - バージョン情報の API
  - UTF-8 のデコードと検証、文字の分類（Unicode の空白・句読点。CommonMark が要求する分類）
- ファジングの基盤（libFuzzer のターゲットを足せる CMake の仕組み）を用意する。

## Scope
- **In**：
  - トップレベルの CMakeLists、オプション、install と export、`AmarkConfig.cmake`、`amark.pc`
  - コンパイラの警告設定（`-Wall -Wextra -Wpedantic -Werror` と `/W4 /WX`）
  - Naos との統合と、テスト用のヘルパー
  - GitHub Actions のワークフロー（CI とリリース）とサニタイザのジョブ
  - LICENSE（MIT、The Amark Authors）、README の骨子、ソースのライセンスヘッダの規約
  - 公開ヘッダの配置規約（`include/amark/`）、`amark.h`（全体をまとめるヘッダ）、バージョン API
  - アロケータ、バッファ、エラー、UTF-8 と文字の分類のユーティリティ
- **Out**：
  - 構文木、パーサ、レンダラの実装
  - CommonMark の準拠テスト（`inline-parser` が持つ）

## Boundary Candidates
- ビルド、パッケージング、CI（インフラ）
- 共通ランタイム（すべてのモジュールが依存する低レベルのユーティリティ）

## Out of Boundary
- ノード型や構文トークンの定義
- spec.txt の取り込みと準拠テストのランナー（使う側の spec が持つ）。ただし、テストデータからテストを生成する汎用の仕組みはここで用意してよい。

## Upstream / Downstream
- **Upstream**：なし（外部依存は CMake と Naos だけ）
- **Downstream**：すべての spec（`syntax-tree`、`html-renderer`、`block-parser`、`inline-parser`、`markdown-renderer`）

## Existing Spec Touchpoints
- **Extends**：なし
- **Adjacent**：`syntax-tree`（共通ランタイムの API を最初に使う spec）

## Constraints
- C99 のみを使う。コンパイラ固有の拡張は使わない。
- Naos は v0.x のため、タグで固定する。
- 公開 API は ABI の安定を意識する（不透明型、構造体のサイズに依存しない設計）。
- MIT ライセンス、Copyright (c) The Amark Authors。
