# Test Plan

## 目的

- WSL での開発検証を完了する
- USBカメラ有効化後の実入力試験を通過する
- 実機デプロイ前にAPI/再起動/安定性を確認する

## テスト項目

### 0. Issue #1 事前確認

- `Dockerfile.streamer` が存在すること
- `.github/workflows/ci.yml` が存在すること
- CIで `cmake-build` と `docker-multiarch-build` が実行されること

### 1. 起動テスト（WSL / videotestsrc）

- 条件: `use_test_source=true`
- 期待: アプリ起動後に `GET /api/v1/status` が `running` を返す

### 2. 設定反映テスト

- 条件: `POST /api/v1/config` で `platform`, `codec`, `target_ip`, `target_port` を変更
- 期待: `result=success`、以後の `status` へ反映される

### 2.1 Simple Pipeline Factory テスト

- `platform=generic, codec=H264` で `x264enc` が選択される
- `platform=generic, codec=H265` で `x265enc` が選択される
- Jetson 実機で `platform=jetson` を指定し、H264/H265 の起動ログを取得する
- `platform=auto` では対応する NVIDIA encoder factory の有無に応じて自動選択される
- `width`, `height`, `framerate` が Simple Mode の入力 caps に反映される

### 3. Advanced Mode テスト

- 条件: `mode=advanced` と有効な `custom_pipeline` を送信
- 期待: 再起動成功し `status.mode=advanced`

### 4. 異常系テスト

- 条件: `target_port=70000` など不正リクエスト
- 期待: HTTP 400 とエラーメッセージ

### 5. USBカメラ入力テスト（WSL）

- 条件: USBカメラをWSLへ有効化後 `use_test_source=false`
- 期待: 起動継続、API応答正常、パイプラインエラーが発生しない

確認コマンド例:

```bash
v4l2-ctl --device=/dev/video0 --list-formats-ext
curl -s -X POST http://127.0.0.1:8080/api/v1/config \
	-H "Content-Type: application/json" \
	-d '{"mode":"simple","use_test_source":false,"device":"/dev/video0","codec":"H264","target_ip":"127.0.0.1","target_port":5004}'
```

### 6. カメラ抜線時の異常ハンドリング

- 条件: 配信中にUSBカメラを抜線
- 期待: `GST_MESSAGE_ERROR` を受信し、プロセスはクラッシュせずに `running=false` へ遷移

### 7. ホットリロード排他制御

- 条件: カメラ配信中に `POST /api/v1/config` で codec/mode を変更
- 期待: `Stop()` -> `Start()` が排他制御下で成功し、クラッシュ・ハングしない

### 7.1 起動失敗時のロールバックとPLAYING到達確認

- 条件: 非対応 caps または存在しないGStreamer elementを含む設定を送信
- 期待: パイプラインがPLAYINGへ到達するまでAPI成功を返さない
- 期待: 起動失敗時はHTTP 500、`rollback=succeeded`、旧設定のstatusが`running`
- 確認: API直後と3秒後のstatus、Jetsonログ、RTP/HLSセグメント生成を記録する

### 8. SIGTERM シャットダウン

- 条件: 実行中プロセスへ `SIGTERM` 送信
- 期待: HTTPループとGStreamerパイプラインが正常終了する

### 9. Jetson H265・長時間安定稼働（Issue #9）

- 条件: `platform=jetson`, `codec=H265`, `/dev/video0`, `1280x720@30fps`
- 記録: 起動ログ、連続稼働時間、CPU/GPU使用率、ドロップ、GStreamerエラー
- 操作: APIホットリロード、プロセス再起動、カメラ抜線・復旧
- 合格: H265配信が継続し、異常操作後の復旧結果をログとstatusで確認できる

### 10. HLS遅延測定（Issue #5）

- 現状: `playlist-length=3`, `target-duration=1`、Hls.jsライブ追従設定で実測約3秒
- 条件: 撮影時刻とブラウザ表示時刻を同一映像内の時刻表示で比較
- 比較: HLS設定変更前後、RTP直接再生、低遅延方式を同じ条件で測定
- 合格: 遅延、CPU/GPU負荷、安定性を記録し採用方式を決定

## 合格基準

- 全テスト項目で期待結果を満たす
- 異常系でクラッシュしない
- 再設定時にプロセスが継続動作する
