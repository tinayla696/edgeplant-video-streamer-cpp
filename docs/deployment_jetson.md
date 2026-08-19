# Deployment (Jetson)

## デプロイ前提

- ターゲット: EDGEPLANT T1 (Jetson TX2 / L4T)
- カメラ: V4L2互換 USB カメラ
- 目的: GPU エンコード経路の有効化

## チェックリスト

1. Jetson 上で GStreamer / nvvideo4linux2 が利用可能
2. `/dev/video0` が認識されている
3. 本アプリをビルドして起動できる
4. `POST /api/v1/config` で `use_test_source=false` を適用
5. ログ上でパイプライン起動失敗がない

## 想定運用設定（Simple Mode）

```json
{
  "mode": "simple",
  "platform": "jetson",
  "device": "/dev/video0",
  "codec": "H264",
  "target_ip": "<destination-ip>",
  "target_port": 5004,
  "use_test_source": false,
  "width": 1280,
  "height": 720,
  "framerate": 30
}
```

## 備考

`platform` は `auto`（既定値）、`jetson`、`generic` から選択します。`auto` は codec に対応する `nvv4l2h264enc` / `nvv4l2h265enc` の Factory を検出し、利用可能なら Jetson パイプラインを選択します。`generic` は `x264enc` / `x265enc`、`jetson` は NVIDIA エンコーダーを明示的に選択します。

Jetson TX2 では、H264/H265 それぞれについて以下を記録してください。

- 評価条件: 入力デバイス、解像度、フレームレート、ビットレート、連続稼働時間
- 測定値: glass-to-glass latency、CPU/GPU 使用率、ドロップフレーム、エラー回数
- 安定性: API ホットリロード、カメラ抜線、プロセス再起動、連続稼働
- 採用理由: 品質、遅延、リソース使用率、復旧性

実機測定値は環境依存のため、このリポジトリでは未測定です。Issue #3 の受け入れ時に Jetson 実機ログと合わせて追記してください。

## 実機確認手順（RTP受信 + 本PCブラウザ）

今回の確認条件:

- Jetson: `192.168.11.238`
- 本PC: `192.168.11.122`
- 入力: `/dev/video0`
- 映像: `1280x720@30fps`
- RTP受信ポート: `5004/udp`

### 1. ARM64実行ファイルをJetsonへ配置

本リポジトリから `artifacts/edgeplant-video-streamer-cpp-arm64` をJetsonへ転送し、実行権限を付与します。

```bash
scp artifacts/edgeplant-video-streamer-cpp-arm64 <jetson-user>@192.168.11.238:/tmp/
ssh <jetson-user>@192.168.11.238
chmod +x /tmp/edgeplant-video-streamer-cpp-arm64
file /tmp/edgeplant-video-streamer-cpp-arm64
```

Jetson側では実行前に、`/dev/video0`、GStreamer本体、`nvv4l2h264enc`、`nvvidconv` が利用可能であることを確認します。

```bash
ls -l /dev/video0
gst-inspect-1.0 nvv4l2h264enc nvvidconv
v4l2-ctl --device=/dev/video0 --list-formats-ext
```

### 2. Jetson側で配信開始

```bash
/tmp/edgeplant-video-streamer-cpp-arm64
```

別のJetsonターミナルから設定を適用します。

```bash
curl -s -X POST http://127.0.0.1:8080/api/v1/config \
  -H 'Content-Type: application/json' \
  -d '{"mode":"simple","platform":"jetson","device":"/dev/video0","codec":"H264","target_ip":"192.168.11.122","target_port":5004,"use_test_source":false}'
```

`status` が `running` になり、ログに `pipeline started` が出ることを確認します。

今回の Simple Mode は、`decodebin` の delayed linking を避けるため、入力を
`video/x-raw,width=1280,height=720,framerate=30/1` に固定しています。したがって、
次の確認結果に `YUYV` または `NV12` の 1280x720@30fps が含まれている必要があります。

```bash
v4l2-ctl --device=/dev/video0 --list-formats-ext
```

カメラが `MJPG` のみを提供する場合、この Simple Mode の caps とは一致しません。
その場合は実機の GStreamer プラグイン構成に合わせて、例えば以下のような入力経路を
`advanced` モードで検証してください。

```text
v4l2src device=/dev/video0 do-timestamp=true ! image/jpeg,width=1280,height=720,framerate=30/1 ! jpegparse ! nvv4l2decoder mjpeg=1 ! nvvidconv ! video/x-raw(memory:NVMM),format=NV12,width=1280,height=720,framerate=30/1 ! nvv4l2h264enc bitrate=4000000 insert-sps-pps=true iframeinterval=30 ! h264parse config-interval=1 ! rtph264pay pt=96 ! udpsink host=192.168.11.122 port=5004 sync=false async=false
```

### 3. 本PCでRTPをHLSへ変換

本PCに GStreamer の `gst-launch-1.0`、`rtph264depay`、`h264parse`、`mpegtsmux`、`hlssink` と Python 3 を用意し、以下を実行します。

WSL2 が既定の NAT モードの場合、`192.168.11.122` は Windows 側の WLAN アドレスであり、
WSL2 の `172.x.x.x` へ UDP パケットは自動転送されません。Jetson から WSL2 で直接受信するには、
Windows 側の `%UserProfile%\\.wslconfig` に以下を設定し、PowerShell で `wsl --shutdown` 後に
WSL2 を再起動してください。

```ini
[wsl2]
networkingMode=mirrored
```

再起動後、WSL2 側で `ip -br addr` を実行し、`192.168.11.122` が表示されることを確認します。
Windows Firewall が UDP 5004 を遮断する場合は、管理者 PowerShell で次を実行します。

```powershell
New-NetFirewallRule -DisplayName "EDGEPLANT RTP 5004" -Direction Inbound -Protocol UDP -LocalPort 5004 -Action Allow
```

WSL2 側で受信確認を行う場合、Jetson の `target_ip` は引き続き `192.168.11.122` にします。
`stream.m3u8` が `#EXT-X-ENDLIST` のみで `.ts` ファイルが生成されない場合は、RTP が未到達です。

```bash
mkdir -p /tmp/edgeplant_hls
rm -f /tmp/edgeplant_hls/stream.m3u8 /tmp/edgeplant_hls/seg_*.ts
gst-launch-1.0 -e \
  udpsrc port=5004 caps='application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000' ! \
  rtph264depay ! h264parse config-interval=1 ! mpegtsmux ! \
  hlssink max-files=6 playlist-length=3 target-duration=1 \
  playlist-location=/tmp/edgeplant_hls/stream.m3u8 \
  location=/tmp/edgeplant_hls/seg_%03d.ts
```

### 4. 本PCのブラウザで映像確認

RTPはブラウザで直接再生できないため、別ターミナルでHLSファイルをHTTP配信します。
```bash
cd /tmp/edgeplant_hls
python3 /path/to/edgeplant-video-streamer-cpp/tools/cors_http_server.py 8090
```

本PCのブラウザで `http://127.0.0.1:8090/index.html` を開き、映像を確認します。
映像が表示されない場合は、まず `ss -lunp | grep 5004` と HLSファイルの生成状況を確認してください。

設定送信と映像再生を同じブラウザで操作する場合は、Jetson WebUI を開きます。

```text
http://192.168.11.238:8080/
```

WebUI の `HLS再生URL` に次を入力して `HLSを読み込む` を押します。

```text
http://192.168.11.122:8090/stream.m3u8
```

この画面の `設定を適用して再生` ボタンから、`POST /api/v1/config` 相当の設定送信とHLS読み込みを実行できます。
WSL2 側の HLS 配信には CORS ヘッダーが必要なため、単純な `python3 -m http.server` ではなく、
リポジトリの CORS 対応サーバーを使用します。

```bash
cd /tmp/edgeplant_hls
python3 /path/to/edgeplant-video-streamer-cpp/tools/cors_http_server.py 8090
```

Jetson WebUIの設定状態を確認する場合は、別途 `http://192.168.11.238:8080/` を開きます。
ただし、今回のHLSファイルは本PC上に生成されるため、映像確認は本PC側の `8090` を使用します。

## 画像解析システムから直接RTPを受信する場合

画像解析アプリケーションがRTP/H264を直接受信できる場合、ブラウザ確認用のHLSブリッジと
`cors_http_server.py` は起動不要です。Jetsonの送信先を解析端末のIPへ設定し、解析側で次のような
受信経路を構築します。

```text
udpsrc port=5004 caps="application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000" ! rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! appsink
```

OpenCVやAI推論へ接続する場合は、`appsink` からBGRフレームを取得します。Jetson上でハードウェアデコードを
利用する場合は、解析アプリケーションのGStreamer環境に合わせて `avdec_h264` を `nvv4l2decoder` へ置き換えます。

HLSはブラウザ確認やHLS専用クライアントが必要な場合だけ使用します。画像解析とブラウザ確認を同時に行う場合は、
RTPを一つの受信プロセスへ集約し、`tee` から解析用 `appsink` とHLS出力へ分岐してください。
