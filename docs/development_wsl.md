# Development (Windows11 + WSL)

## 前提

このプロジェクトは WSL 環境での開発を前提にしています。

- Windows11 + WSL2 (Ubuntu 推奨)
- WSL 内に GStreamer 1.0 系を導入
- カメラ/Jetsonハードウェアなしでも開発可能

## 開発時の基本方針

WSL 開発時は `videotestsrc` を利用します。

- デフォルト設定は `use_test_source=true`
- カメラ未接続でも `GET /api/v1/status` と `POST /api/v1/config` を確認可能

## ビルド

```bash
cmake -S . -B build
cmake --build build -j
```

## 実行

```bash
./build/edgeplant-video-streamer-cpp
```

## 動作確認

```bash
curl -s http://127.0.0.1:8080/api/v1/status
curl -s -X POST http://127.0.0.1:8080/api/v1/config \
  -H "Content-Type: application/json" \
  -d '{"mode":"simple","codec":"H264","target_ip":"127.0.0.1","target_port":5004,"use_test_source":true}'
```

## WebUI

ブラウザで以下にアクセスします。

```text
http://127.0.0.1:8080/
```

WebUIで可能な操作:

- RTP View（現在の状態、外部プレーヤー向けコマンド表示）
- 簡易設定 / 高度設定の切り替え
- 高度設定でカスタムパイプラインの直接適用
- HLS欄で現在の配信Codec/Portを表示
- 高度設定時は、HLSプレビュー非対応パイプラインの可能性をUI上で注意表示

## WebUI内での映像表示（実験）

WebUIは実験機能としてHLSプレビューを提供します。以下を別ターミナルで実行してください（推奨）。

```bash
mkdir -p /tmp/edgeplant_hls
gst-launch-1.0 -q \
  udpsrc port=5004 caps="application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000" ! \
  rtph264depay ! h264parse config-interval=1 ! mpegtsmux ! \
  hlssink max-files=10 playlist-length=5 target-duration=1 \
  playlist-location=/tmp/edgeplant_hls/stream.m3u8 \
  location=/tmp/edgeplant_hls/seg_%03d.ts
```

その後、WebUIの「HLSを読み込む」を押すとプレビューを試行できます。

補足:

- HLS欄には現在の配信Codec（H264/H265）とPortが表示されます。
- `mode=advanced` の場合、独自パイプラインが RTP(H26x/PT=96) でなければHLSプレビューは成功しません。

ffmpegを使う場合は、SDPに `c=IN IP4 127.0.0.1` 行が必要です。

## USBカメラ有効化後の切り替え

WSL 側で USB カメラが有効になったら、以下で実カメラ入力に切り替えます。

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/config \
  -H "Content-Type: application/json" \
  -d '{"mode":"simple","device":"/dev/video0","use_test_source":false}'
```
