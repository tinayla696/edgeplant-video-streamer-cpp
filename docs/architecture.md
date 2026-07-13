# Architecture

## システム構成

本アプリケーションは以下 3 コンポーネントで構成されます。

- Streamer: GStreamer パイプラインの生成、起動、停止、Bus監視
- HTTP Server: 設定取得/更新 API を提供
- Main Controller: 設定管理と排他制御、シグナル処理

```mermaid
graph TD
    Web[External Client] -->|GET /status\nPOST /config| Http[HTTP Server :8080]
    Http -->|lock| Ctrl[Main Controller]
    Ctrl -->|Start/Stop| Str[Streamer Engine]
    Str --> Gst[GStreamer Pipeline]
    Gst --> Net[RTP/UDP Output]
```

## 動作モード

### Simple Mode

アプリが入力パラメータから標準パイプラインを動的構築します。

- `use_test_source=true`: `videotestsrc` を入力に使用
- `use_test_source=false`: `v4l2src device=/dev/video*` を入力に使用
- エンコーダは実行環境で自動選択

自動選択のルール:

- Jetson系エンコーダが存在する場合: `nvv4l2h264enc` / `nvv4l2h265enc`
- それ以外: `x264enc` / `x265enc`

### Advanced Mode

ユーザー提供の `custom_pipeline` を `gst_parse_launch` で直接起動します。

## ホットリロード設計

`POST /api/v1/config` 受信時は以下順序で安全に反映します。

1. 排他ロックを獲得
2. 既存パイプラインを `GST_STATE_NULL` へ遷移
3. 旧パイプラインを unref して解放
4. 新設定で再起動
5. 起動失敗時は旧設定へロールバック

```mermaid
sequenceDiagram
    participant Client
    participant HTTP
    participant Main
    participant Streamer

    Client->>HTTP: POST /api/v1/config
    HTTP->>Main: update_config_handler
    Main->>Streamer: Stop()
    Main->>Streamer: Start(new config)
    alt start success
      Main-->>HTTP: 200 success
    else start failed
      Main->>Streamer: Start(old config) // rollback
      Main-->>HTTP: 500 error
    end
```
