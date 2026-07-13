# Implementation & Verification Log

## 対象期間

- 2026-07-13 までの実装・検証内容

## 実装ハイライト

- C++17 + CMake で配信アプリを構築
- GStreamer ベースの配信エンジンを実装
  - Simple Mode: 設定値から標準パイプラインを構築
  - Advanced Mode: `custom_pipeline` を `gst_parse_launch` で実行
- HTTP API と WebUI を内蔵
  - `GET /api/v1/status`
  - `POST /api/v1/config`
  - `GET /` (WebUI)
  - `GET /hls/*` (HLS静的配信)
- WebUI を強化
  - 状態表示、設定反映、モード切替（簡易/高度）
  - HLSプレビュー導線とコマンドコピー
  - 高度設定時の注意表示、Codec/Port ヒント表示

## Issue #1 対応状況

### 対応済み（リポジトリ反映）

- 実カメラ互換性向上
  - `v4l2src ! decodebin` を採用し、MJPEG/Raw系カメラのネゴシエーションを強化
- マルチステージコンテナ定義を追加
  - `Dockerfile.streamer`（build/runtime分離）
- マルチアーキテクチャCIを追加
  - `.github/workflows/ci.yml`
  - `linux/amd64` と `linux/arm64` を Buildx + QEMU でビルド

### 未完了（実機・物理依存のため手動検証が必要）

- 実カメラ接続時の全フォーマットでの動作保証
- 抜線時の現場条件での回復・運用確認
- Jetson実機上での長時間安定稼働確認

## 主要な改善内容

### 1. シャットダウン安定化

- `Stop()` 内のロック/スレッド join 順序を見直し、停止時ハングを回避
- HTTP accept ループをタイムアウト付き `select()` へ変更
- シグナル終了処理を `sig_atomic_t` ベースへ調整

### 2. HLSプレビュー安定化

- HLS配信設定を改善
  - `hlssink max-files=10 playlist-length=5 target-duration=1`
- Hls.js 再読込時に旧インスタンスを破棄して再生成
- `manifestLoadError` 対応
  - HTTPルーティング時にクエリ文字列 (`?_t=...`) を除去してパス解決

### 3. src 配下の構造整理

機能ごとにディレクトリ分割:

- `src/app/main.cpp`
- `src/config/config.hpp`
- `src/http/http_server.cpp`
- `src/http/http_server.hpp`
- `src/streaming/streamer.cpp`
- `src/streaming/streamer.hpp`

## 検証結果サマリ

### ビルド検証

- `cmake -S . -B build`
- `cmake --build build -j`
- `cmake --build build --clean-first -j`

いずれも成功を確認。

### API/動作検証

- `GET /api/v1/status` が `running` を返却
- `POST /api/v1/config` で設定変更と再起動成功
- 高度設定切替で配信内容変化（カラーバー/ball）を確認

### HLS検証

- `/tmp/edgeplant_hls/stream.m3u8` と `seg_*.ts` 生成を確認
- `GET /hls/stream.m3u8` が 200 応答
- `GET /hls/stream.m3u8?_t=...` も 200 応答を確認
- WebUI 上で再生確認

## 既知の注意点

- ブラウザは RTP/UDP を直接再生不可
- HLSプレビューは別プロセスのブリッジ（GStreamer）起動が必要
- Advanced Mode では、独自パイプラインが RTP(H264/H265, payload=96) を出力していないと HLSプレビュー不可

## 推奨運用

- 開発時は `use_test_source=true` を利用
- 問題発生時は次を優先確認
  - ポート競合 (`:8080`, 配信ポート)
  - HLSファイル生成有無
  - `/hls/stream.m3u8` のHTTP応答
