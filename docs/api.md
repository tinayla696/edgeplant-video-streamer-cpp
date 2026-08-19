# API Reference

Base URL: `http://<host>:8080`

## GET /

サンプルWebUIを返します。

- RTP配信 View: 現在ステータスと外部プレーヤー用コマンド表示
- 設定変更: 簡易設定 / 高度設定（カスタムパイプライン）

注意:

- 一般的なWebブラウザはRTP/UDPを直接再生できません。
- WebUIは、受信確認用のGStreamerコマンドを表示する設計です。
- WebUI内には実験機能としてHLSプレビュー導線があります（別途HLSブリッジ起動が必要）。
- HLSブリッジは、WebUI表示のGStreamerコマンド利用を推奨します。
- HLS欄には、現在の配信Codec/Portと高度設定時の注意メッセージが表示されます。

## GET /hls/*

WebUI実験機能向けの静的ファイル配信ルートです。

- 例: `/hls/stream.m3u8`, `/hls/seg_001.ts`
- 配信元ディレクトリ: `/tmp/edgeplant_hls`
- ファイル未生成時は `404 Not Found`

## GET /api/v1/status

現在の配信状態と設定値を返します。

レスポンス例:

```json
{
  "status": "running",
  "mode": "simple",
  "platform": "auto",
  "device": "/dev/video0",
  "codec": "H264",
  "width": 1280,
  "height": 720,
  "framerate": 30,
  "target_ip": "127.0.0.1",
  "target_port": 5004,
  "custom_pipeline": "",
  "use_test_source": true
}
```

## POST /api/v1/config

配信設定を部分更新し、パイプラインを再起動します。

### 受け付けフィールド

- `mode`: `simple` または `advanced`
- `platform`: `auto`、`jetson`、または `generic`
- `device`: 例 `/dev/video0`
- `codec`: `H264` または `H265`
- `width`: 映像幅（正の整数、既定値 `1280`）
- `height`: 映像高さ（正の整数、既定値 `720`）
- `framerate`: フレームレート（正の整数、既定値 `30`）
- `target_ip`: 送信先IP
- `target_port`: 1..65535
- `use_test_source`: `true`/`false`
- `custom_pipeline`: advancedモードで必須

### リクエスト例 (Simple)

```json
{
  "mode": "simple",
  "platform": "auto",
  "codec": "H264",
  "target_ip": "127.0.0.1",
  "target_port": 5004,
  "use_test_source": true
}
```

### リクエスト例 (Advanced)

```json
{
  "mode": "advanced",
  "custom_pipeline": "videotestsrc is-live=true ! videoconvert ! x264enc tune=zerolatency ! rtph264pay pt=96 ! udpsink host=127.0.0.1 port=5004 sync=false"
}
```

### レスポンス例

成功:

```json
{
  "result": "success",
  "message": "Pipeline reloaded successfully."
}
```

失敗:

```json
{
  "result": "error",
  "message": "failed to reload pipeline: <reason>"
}
```
