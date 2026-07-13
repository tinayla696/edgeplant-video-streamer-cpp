# edgeplant-video-streamer-cpp

このドキュメントは、edgeplant-video-streamer-cpp の実装・運用・検証手順をまとめた公式ガイドです。

## 概要

edgeplant-video-streamer-cpp は、GStreamer Core API を利用して映像ソースを H.264/H.265 へエンコードし、RTP/UDP で配信する C++17 アプリケーションです。

- Simple Mode: パラメータ指定で標準パイプラインを自動構築
- Advanced Mode: 任意の GStreamer パイプライン文字列を直接実行
- HTTP API: `GET /api/v1/status`, `POST /api/v1/config` で運用時ホットリロード
- WebUI: ブラウザからRTP View情報と設定変更（簡易/高度）を操作

## 開発方針

本リポジトリでは Windows11 + WSL 開発を前提とし、開発中は `videotestsrc` を使ってハードウェア非依存で検証します。

- WSL 開発時: `use_test_source=true` でカメラ非依存テスト
- 後工程: WSL 側で USB カメラ有効化後に実カメラ試験
- 最終工程: 実機（Jetson TX2 / EDGEPLANT T1）へデプロイして GPU エンコード検証

## ドキュメント構成

- [Architecture](architecture.md): システム構造・パイプライン戦略
- [Implementation Log](implementation_log.md): 実装・検証の反映履歴
- [API Reference](api.md): HTTP API 仕様
- [Development (Windows11 + WSL)](development_wsl.md): ローカル開発手順
- [Test Plan](testing.md): テスト項目と合格基準
- [Deployment (Jetson)](deployment_jetson.md): 実機展開チェックリスト