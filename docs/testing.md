# Test Plan

## 目的

- WSL での開発検証を完了する
- USBカメラ有効化後の実入力試験を通過する
- 実機デプロイ前にAPI/再起動/安定性を確認する

## テスト項目

### 1. 起動テスト（WSL / videotestsrc）

- 条件: `use_test_source=true`
- 期待: アプリ起動後に `GET /api/v1/status` が `running` を返す

### 2. 設定反映テスト

- 条件: `POST /api/v1/config` で `codec`, `target_ip`, `target_port` を変更
- 期待: `result=success`、以後の `status` へ反映される

### 3. Advanced Mode テスト

- 条件: `mode=advanced` と有効な `custom_pipeline` を送信
- 期待: 再起動成功し `status.mode=advanced`

### 4. 異常系テスト

- 条件: `target_port=70000` など不正リクエスト
- 期待: HTTP 400 とエラーメッセージ

### 5. USBカメラ入力テスト（WSL）

- 条件: USBカメラをWSLへ有効化後 `use_test_source=false`
- 期待: 起動継続、API応答正常、パイプラインエラーが発生しない

## 合格基準

- 全テスト項目で期待結果を満たす
- 異常系でクラッシュしない
- 再設定時にプロセスが継続動作する
