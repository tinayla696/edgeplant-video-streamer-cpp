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
  "device": "/dev/video0",
  "codec": "H264",
  "target_ip": "<destination-ip>",
  "target_port": 5004,
  "use_test_source": false
}
```

## 備考

実装上、Jetson 系エンコーダファクトリが検出されれば `nvv4l2h264enc` / `nvv4l2h265enc` を優先し、未検出環境では `x264enc` / `x265enc` へ自動フォールバックします。
