#include "http/http_server.hpp"

#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>

namespace {

std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }

    return value.substr(start, end - start);
}

bool SendAll(int fd, const std::string& data) {
        size_t sent_total = 0;
        while (sent_total < data.size()) {
                const ssize_t sent_now = send(fd, data.data() + sent_total, data.size() - sent_total, 0);
                if (sent_now <= 0) {
                        return false;
                }
                sent_total += static_cast<size_t>(sent_now);
        }
        return true;
}

std::string GuessMimeType(const std::string& path) {
        if (path.size() >= 5 && path.substr(path.size() - 5) == ".m3u8") {
                return "application/vnd.apple.mpegurl";
        }
        if (path.size() >= 3 && path.substr(path.size() - 3) == ".ts") {
                return "video/mp2t";
        }
        if (path.size() >= 4 && path.substr(path.size() - 4) == ".mp4") {
                return "video/mp4";
        }
        if (path.size() >= 4 && path.substr(path.size() - 4) == ".txt") {
                return "text/plain; charset=utf-8";
        }
        return "application/octet-stream";
}

bool TryReadFile(const std::string& path, std::string* out_data) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
                return false;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        *out_data = ss.str();
        return true;
}

bool IsSafeHlsPath(const std::string& rel_path) {
        if (rel_path.empty()) {
                return false;
        }
        if (rel_path.find("..") != std::string::npos) {
                return false;
        }
        if (rel_path.find('\\') != std::string::npos) {
                return false;
        }
        return rel_path.front() == '/';
}

std::string BuildWebUiPage() {
        return R"HTML(<!doctype html>
<html lang="ja">
<head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>edgeplant-video-streamer-cpp WebUI</title>
    <script src="https://cdn.jsdelivr.net/npm/hls.js@1"></script>
    <style>
        :root { color-scheme: dark; }
        * { box-sizing: border-box; }
        body {
            font-family: "Avenir Next", "Helvetica Neue", "Yu Gothic UI", "Meiryo", sans-serif;
            margin: 0;
            background:
                radial-gradient(circle at 10% 0%, #1f3b66 0%, rgba(31, 59, 102, 0.1) 30%),
                radial-gradient(circle at 85% 15%, #355d38 0%, rgba(53, 93, 56, 0.08) 33%),
                linear-gradient(160deg, #070b14 0%, #0c1220 45%, #151223 100%);
            color: #ebf2ff;
            min-height: 100vh;
        }
        .wrap { max-width: 1240px; margin: 0 auto; padding: 24px; }
        .title {
            margin: 0 0 8px;
            font-size: clamp(24px, 4vw, 36px);
            letter-spacing: 0.02em;
        }
        .subtitle { margin: 0 0 20px; color: #9eb5d4; }
        h2 { margin: 0 0 14px 0; font-size: 20px; letter-spacing: 0.02em; }
        .grid { display: grid; gap: 16px; grid-template-columns: repeat(auto-fit, minmax(340px, 1fr)); }
        .card {
            background: linear-gradient(180deg, rgba(16, 24, 40, 0.88) 0%, rgba(10, 16, 30, 0.93) 100%);
            border: 1px solid #2f4468;
            border-radius: 16px;
            padding: 18px;
            box-shadow: 0 18px 45px rgba(0, 0, 0, 0.35);
            backdrop-filter: blur(4px);
        }
        .kv { display: grid; grid-template-columns: 140px 1fr; gap: 6px 8px; margin-bottom: 10px; }
        .row { display: grid; gap: 10px; margin-bottom: 12px; }
        .row2 { display: grid; gap: 10px; grid-template-columns: 1fr 1fr; margin-bottom: 12px; }
        label { font-size: 13px; color: #c8d9f4; }
        input, select, textarea, button { border-radius: 10px; border: 1px solid #44618f; padding: 10px 12px; font-size: 14px; }
        input, select, textarea { background: #0a1323; color: #e2e8f0; }
        button {
            background: linear-gradient(90deg, #0f5fe6 0%, #1295e3 100%);
            color: white;
            cursor: pointer;
            border: 0;
            font-weight: 600;
        }
        button.secondary { background: linear-gradient(90deg, #364e73 0%, #43607f 100%); }
        textarea { min-height: 120px; width: 100%; }
        pre { background: #020617; border: 1px solid #334155; border-radius: 10px; padding: 10px; overflow: auto; white-space: pre-wrap; }
        .msg { min-height: 20px; color: #8cd4ff; }
        .warn { color: #fbbf24; }
        .ok { color: #4ade80; }
        .hidden { display: none; }
        .video-wrap { aspect-ratio: 16 / 9; width: 100%; border-radius: 12px; border: 1px solid #3b577e; background: #030814; display: grid; place-items: center; overflow: hidden; }
        .video-wrap video { width: 100%; height: 100%; object-fit: contain; background: #010409; }
        .mono { font-family: "Consolas", "SFMono-Regular", monospace; font-size: 12px; }
        .hint { margin: 8px 0 10px; color: #9ab0ce; font-size: 13px; }
    </style>
</head>
<body>
    <div class="wrap">
        <h1 class="title">edgeplant-video-streamer-cpp WebUI</h1>
        <p class="subtitle">RTP配信のViewとストリーム設定変更を行うサンプル画面</p>

        <div class="grid">
            <section class="card">
                <h2>RTP配信 View</h2>
                <div class="kv" id="status"></div>
                <p class="warn">ブラウザはRTP/UDPを直接再生できないため、外部プレーヤー向けコマンドを利用してください。</p>
                <label for="viewerCmd">Viewer Command (GStreamer)</label>
                <pre id="viewerCmd"></pre>
                <button type="button" id="copyCmd" class="secondary">コマンドをコピー</button>

                <h2 style="margin-top: 20px;">WebUI内映像表示（実験）</h2>
                <p class="hint">HLSブリッジ起動後、設定を適用するとこの画面で映像を再生します。</p>
                <div class="video-wrap">
                    <video id="hlsVideo" controls muted playsinline></video>
                </div>
                <p class="hint" id="hlsState">状態: 未接続</p>
                <div class="row">
                    <label for="hlsSource">HLS再生URL</label>
                    <input id="hlsSource" value="http://192.168.11.122:8090/stream.m3u8" />
                </div>
                <label for="hlsCmd">HLS Bridge Command (GStreamer)</label>
                <pre id="hlsCmd" class="mono"></pre>
                <p class="hint" id="hlsCodecHint"></p>
                <div class="row2">
                    <button type="button" id="copyHlsCmd" class="secondary">HLSコマンドをコピー</button>
                    <button type="button" id="loadHls">HLSを読み込む</button>
                </div>
            </section>

            <section class="card">
                <h2>ストリーム設定</h2>
                <div class="row2">
                    <div>
                        <label><input type="radio" name="mode" value="simple" checked> 簡易設定</label>
                    </div>
                    <div>
                        <label><input type="radio" name="mode" value="advanced"> 高度設定</label>
                    </div>
                </div>

                <div id="simpleBox">
                    <div class="row2">
                        <div>
                            <label for="platform">Platform</label>
                            <select id="platform">
                                <option value="auto">auto (検出)</option>
                                <option value="jetson">Jetson (NVIDIA)</option>
                                <option value="generic">Generic (CPU)</option>
                            </select>
                        </div>
                        <div>
                            <label for="codec">Codec</label>
                            <select id="codec">
                                <option>H264</option>
                                <option>H265</option>
                            </select>
                        </div>
                        <div>
                            <label for="useTest">Source</label>
                            <select id="useTest">
                                <option value="true">videotestsrc</option>
                                <option value="false">v4l2src (USB Camera)</option>
                            </select>
                        </div>
                    </div>
                    <div class="row2">
                        <div><label for="width">Width</label><input id="width" type="number" min="1" value="1280" /></div>
                        <div><label for="height">Height</label><input id="height" type="number" min="1" value="720" /></div>
                        <div><label for="framerate">FPS</label><input id="framerate" type="number" min="1" value="30" /></div>
                    </div>
                    <div class="row">
                        <label for="device">Device</label>
                        <input id="device" value="/dev/video0" />
                    </div>
                    <div class="row2">
                        <div>
                            <label for="targetIp">Target IP</label>
                            <input id="targetIp" value="127.0.0.1" />
                        </div>
                        <div>
                            <label for="targetPort">Target Port</label>
                            <input id="targetPort" type="number" min="1" max="65535" value="5004" />
                        </div>
                    </div>
                </div>

                <div id="advancedBox" class="hidden">
                    <div class="row">
                        <label for="customPipeline">Custom Pipeline</label>
                        <textarea id="customPipeline">videotestsrc is-live=true ! videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 ! h264parse config-interval=1 ! rtph264pay pt=96 ! udpsink host=127.0.0.1 port=5004 sync=false async=false</textarea>
                    </div>
                </div>

                <div class="row2">
                    <button type="button" id="applyBtn">設定を適用して再生</button>
                    <button type="button" id="reloadBtn" class="secondary">状態を再取得</button>
                </div>
                <div id="msg" class="msg"></div>
            </section>
        </div>
    </div>

    <script>
        const statusEl = document.getElementById('status');
        const msgEl = document.getElementById('msg');
        const viewerCmdEl = document.getElementById('viewerCmd');
        const hlsCmdEl = document.getElementById('hlsCmd');
        const hlsStateEl = document.getElementById('hlsState');
        const hlsVideoEl = document.getElementById('hlsVideo');
        const hlsCodecHintEl = document.getElementById('hlsCodecHint');
        const hlsSourceEl = document.getElementById('hlsSource');

        const modeRadios = [...document.querySelectorAll('input[name="mode"]')];
        const simpleBox = document.getElementById('simpleBox');
        const advancedBox = document.getElementById('advancedBox');

        const codecEl = document.getElementById('codec');
        const platformEl = document.getElementById('platform');
        const useTestEl = document.getElementById('useTest');
        const widthEl = document.getElementById('width');
        const heightEl = document.getElementById('height');
        const framerateEl = document.getElementById('framerate');
        const deviceEl = document.getElementById('device');
        const targetIpEl = document.getElementById('targetIp');
        const targetPortEl = document.getElementById('targetPort');
        const customPipelineEl = document.getElementById('customPipeline');
        let lastStatus = null;
        let hlsInstance = null;
        let hlsRetryTimer = null;

        function activeMode() {
            return modeRadios.find(r => r.checked)?.value || 'simple';
        }

        function renderMode() {
            const m = activeMode();
            simpleBox.classList.toggle('hidden', m !== 'simple');
            advancedBox.classList.toggle('hidden', m !== 'advanced');
        }

        function showMessage(text, cls) {
            msgEl.textContent = text;
            msgEl.className = 'msg ' + (cls || '');
        }

        function clearHlsRetryTimer() {
            if (hlsRetryTimer) {
                clearTimeout(hlsRetryTimer);
                hlsRetryTimer = null;
            }
        }

        function hlsCommandFromStatus(s) {
            const codec = s.codec === 'H265' ? 'H265' : 'H264';
            const depay = codec === 'H265' ? 'rtph265depay ! h265parse' : 'rtph264depay ! h264parse config-interval=1';
            const caps = 'application/x-rtp,media=video,encoding-name=' + codec + ',payload=96,clock-rate=90000';
            return [
                'mkdir -p /tmp/edgeplant_hls',
                'gst-launch-1.0 -q udpsrc port=' + s.target_port + ' caps="' + caps + '" ! ' +
                    depay + ' ! mpegtsmux ! hlssink max-files=6 playlist-length=3 target-duration=1 ' +
                    'playlist-location=/tmp/edgeplant_hls/stream.m3u8 ' +
                    'location=/tmp/edgeplant_hls/seg_%03d.ts'
            ].join('\n');
        }

        function viewerCommandFromStatus(s) {
            const enc = s.codec === 'H265' ? 'H265' : 'H264';
            const depay = enc === 'H265' ? 'rtph265depay ! h265parse ! avdec_h265' : 'rtph264depay ! h264parse ! avdec_h264';
            return 'gst-launch-1.0 -v udpsrc port=' + s.target_port +
                ' caps="application/x-rtp,media=video,encoding-name=' + enc +
                ',payload=96,clock-rate=90000" ! ' + depay + ' ! videoconvert ! autovideosink sync=false';
        }

        function hlsHintFromStatus(s) {
            const codec = s.codec === 'H265' ? 'H265' : 'H264';
            const modeWarn = s.mode === 'advanced'
                ? ' / 高度設定では独自パイプラインがRTP(H26x/PT=96)でない場合、HLSプレビューできません。'
                : '';
            return '現在の配信Codec: ' + codec + ' (port=' + s.target_port + ')' + modeWarn;
        }

        function renderStatus(s) {
            lastStatus = s;
            statusEl.innerHTML = [
                ['Status', s.status],
                ['Mode', s.mode],
                ['Platform', s.platform],
                ['Codec', s.codec],
                ['Target', s.target_ip + ':' + s.target_port],
                ['Source', s.use_test_source ? 'videotestsrc' : ('v4l2src (' + s.device + ')')]
            ].map(kv => '<div><strong>' + kv[0] + '</strong></div><div>' + kv[1] + '</div>').join('');
            viewerCmdEl.textContent = viewerCommandFromStatus(s);
            hlsCmdEl.textContent = hlsCommandFromStatus(s);
            hlsCodecHintEl.textContent = hlsHintFromStatus(s);
        }

        function fillForm(s) {
            modeRadios.forEach(r => { r.checked = (r.value === s.mode); });
            platformEl.value = s.platform || 'auto';
            codecEl.value = s.codec || 'H264';
            useTestEl.value = s.use_test_source ? 'true' : 'false';
            widthEl.value = String(s.width || 1280);
            heightEl.value = String(s.height || 720);
            framerateEl.value = String(s.framerate || 30);
            deviceEl.value = s.device || '/dev/video0';
            targetIpEl.value = s.target_ip || '127.0.0.1';
            targetPortEl.value = String(s.target_port || 5004);
            customPipelineEl.value = s.custom_pipeline || customPipelineEl.value;
            renderMode();
        }

        async function getStatus() {
            const res = await fetch('/api/v1/status');
            if (!res.ok) {
                throw new Error('status API failed: HTTP ' + res.status);
            }
            return await res.json();
        }

        async function refreshStatusOnly() {
            try {
                const s = await getStatus();
                renderStatus(s);
            } catch (e) {
                showMessage(String(e), 'warn');
            }
        }

        async function refreshAll() {
            try {
                const s = await getStatus();
                renderStatus(s);
                fillForm(s);
            } catch (e) {
                showMessage(String(e), 'warn');
            }
        }

        async function applyConfig() {
            const mode = activeMode();
            const payload = {
                mode,
                platform: platformEl.value,
                codec: codecEl.value,
                device: deviceEl.value,
                target_ip: targetIpEl.value,
                target_port: Number(targetPortEl.value),
                use_test_source: useTestEl.value === 'true'
                ,width: Number(widthEl.value)
                ,height: Number(heightEl.value)
                ,framerate: Number(framerateEl.value)
            };

            if (mode === 'advanced') {
                payload.custom_pipeline = customPipelineEl.value;
            }

            try {
                const res = await fetch('/api/v1/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(payload)
                });
                const data = await res.json();
                if (!res.ok) {
                    throw new Error(data.message || ('HTTP ' + res.status));
                }
                showMessage(data.message || 'applied', 'ok');
                await refreshAll();
                await loadHlsPreview();
            } catch (e) {
                showMessage(String(e), 'warn');
            }
        }

        async function loadHlsPreview() {
            const hlsUrl = hlsSourceEl.value.trim();
            if (!hlsUrl) {
                hlsStateEl.textContent = '状態: HLS再生URLが未設定です';
                return;
            }
            clearHlsRetryTimer();
            hlsStateEl.textContent = '状態: 読み込み中...';

            if (lastStatus && lastStatus.mode === 'advanced') {
                hlsStateEl.textContent = '状態: 読み込み中...（高度設定の独自パイプラインではHLS未生成の可能性があります）';
            }

            if (window.Hls && window.Hls.isSupported()) {
                if (hlsInstance) {
                    hlsInstance.destroy();
                    hlsInstance = null;
                }
                hlsInstance = new window.Hls({
                    liveDurationInfinity: true,
                    liveSyncDurationCount: 1,
                    liveMaxLatencyDurationCount: 3
                });
                hlsInstance.loadSource(hlsUrl + (hlsUrl.includes('?') ? '&' : '?') + '_t=' + Date.now());
                hlsInstance.attachMedia(hlsVideoEl);
                hlsInstance.on(window.Hls.Events.MANIFEST_PARSED, function () {
                    hlsStateEl.textContent = '状態: HLS再生中';
                    hlsVideoEl.play().catch(function () {});
                });
                hlsInstance.on(window.Hls.Events.ERROR, function (_event, data) {
                    const detail = data && data.details ? data.details : 'unknown';
                    const fatal = data && data.fatal;
                    const errType = data && data.type;

                    if (fatal && hlsInstance) {
                        // Retry loading on transient network failures common in live HLS.
                        if (errType === window.Hls.ErrorTypes.NETWORK_ERROR) {
                            hlsStateEl.textContent = '状態: HLS再接続中 (' + detail + ')';
                            clearHlsRetryTimer();
                            hlsRetryTimer = setTimeout(function () {
                                if (hlsInstance) {
                                    hlsInstance.startLoad();
                                }
                            }, 1000);
                            return;
                        }

                        // Try to recover decoder-related failures without full teardown.
                        if (errType === window.Hls.ErrorTypes.MEDIA_ERROR) {
                            hlsStateEl.textContent = '状態: HLS復旧中 (' + detail + ')';
                            hlsInstance.recoverMediaError();
                            return;
                        }

                        hlsInstance.destroy();
                        hlsInstance = null;
                    }
                    hlsStateEl.textContent = '状態: HLS読み込み失敗 (' + detail + ')';
                });
                return;
            }

            if (hlsVideoEl.canPlayType('application/vnd.apple.mpegurl')) {
                hlsVideoEl.src = hlsUrl;
                hlsVideoEl.play().catch(function () {});
                hlsStateEl.textContent = '状態: ネイティブHLS再生試行中';
            } else {
                hlsStateEl.textContent = '状態: このブラウザはHLS再生に未対応です';
            }
        }

        modeRadios.forEach(r => r.addEventListener('change', renderMode));
        document.getElementById('reloadBtn').addEventListener('click', refreshAll);
        document.getElementById('applyBtn').addEventListener('click', applyConfig);
        document.getElementById('copyCmd').addEventListener('click', async () => {
            try {
                await navigator.clipboard.writeText(viewerCmdEl.textContent);
                showMessage('viewer command copied', 'ok');
            } catch {
                showMessage('copy failed', 'warn');
            }
        });
        document.getElementById('copyHlsCmd').addEventListener('click', async () => {
            try {
                await navigator.clipboard.writeText(hlsCmdEl.textContent);
                showMessage('HLS command copied', 'ok');
            } catch {
                showMessage('copy failed', 'warn');
            }
        });
        document.getElementById('loadHls').addEventListener('click', loadHlsPreview);

        renderMode();
        refreshAll();
        setInterval(refreshStatusOnly, 3000);
    </script>
</body>
</html>)HTML";
}

}  // namespace

HttpServer::HttpServer()
    : server_fd_(-1),
      running_(false) {}

HttpServer::~HttpServer() {
    Stop();
}

bool HttpServer::Start(int port,
                       GetStatusHandler get_status_handler,
                       UpdateConfigHandler update_config_handler,
                       std::string* error_message) {
    Stop();

    get_status_handler_ = std::move(get_status_handler);
    update_config_handler_ = std::move(update_config_handler);

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        if (error_message) {
            *error_message = std::string("socket create failed: ") + std::strerror(errno);
        }
        return false;
    }

    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        if (error_message) {
            *error_message = std::string("setsockopt failed: ") + std::strerror(errno);
        }
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (error_message) {
            *error_message = std::string("bind failed: ") + std::strerror(errno);
        }
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (listen(server_fd_, 16) < 0) {
        if (error_message) {
            *error_message = std::string("listen failed: ") + std::strerror(errno);
        }
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    server_thread_ = std::thread(&HttpServer::AcceptLoop, this);
    return true;
}

void HttpServer::Stop() {
    running_ = false;

    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void HttpServer::AcceptLoop() {
    const int listen_fd = server_fd_;

    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200 * 1000;

        const int select_ret = select(listen_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (select_ret == 0) {
            continue;
        }
        if (select_ret < 0) {
            if (!running_ || errno == EBADF || errno == EINVAL) {
                break;
            }
            continue;
        }

        if (!FD_ISSET(listen_fd, &read_fds)) {
            continue;
        }

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (!running_ || errno == EBADF || errno == EINVAL) {
                break;
            }
            continue;
        }

        std::string method;
        std::string path;
        std::string body;

        const int read_status = ReadRequest(client_fd, &method, &path, &body);
        if (read_status != 0) {
            const std::string resp = BuildHttpResponse(
                read_status,
                "Bad Request",
                "{\"result\":\"error\",\"message\":\"Malformed HTTP request\"}");
            send(client_fd, resp.c_str(), resp.size(), 0);
            close(client_fd);
            continue;
        }

        const size_t query_pos = path.find_first_of("?#");
        const std::string route_path = (query_pos == std::string::npos) ? path : path.substr(0, query_pos);

        std::string response;
        if (method == "GET" && (route_path == "/" || route_path == "/index.html")) {
            response = BuildHttpResponse(200, "OK", BuildWebUiPage(), "text/html; charset=utf-8");
        } else if (method == "GET" && route_path.rfind("/hls/", 0) == 0) {
            const std::string rel_path = route_path.substr(4);
            if (!IsSafeHlsPath(rel_path)) {
                response = BuildHttpResponse(400, "Bad Request", "invalid path", "text/plain; charset=utf-8");
            } else {
                const std::string full_path = std::string("/tmp/edgeplant_hls") + rel_path;
                std::string file_data;
                if (!TryReadFile(full_path, &file_data)) {
                    response = BuildHttpResponse(404, "Not Found", "not found", "text/plain; charset=utf-8");
                } else {
                    response = BuildHttpResponse(200, "OK", file_data, GuessMimeType(full_path));
                }
            }
        } else if (method == "GET" && route_path == "/api/v1/status") {
            response = BuildHttpResponse(200, "OK", get_status_handler_());
        } else if (method == "POST" && route_path == "/api/v1/config") {
            const auto [status, json] = update_config_handler_(body);
            response = BuildHttpResponse(status, status == 200 ? "OK" : "Bad Request", json);
        } else {
            response = BuildHttpResponse(
                404,
                "Not Found",
                "{\"result\":\"error\",\"message\":\"endpoint not found\"}");
        }

        SendAll(client_fd, response);
        close(client_fd);
    }
}

std::string HttpServer::BuildHttpResponse(int status_code,
                                          const std::string& status_text,
                                                        const std::string& body,
                                                        const std::string& content_type) const {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n"
       << "\r\n"
         << body;
    return ss.str();
}

int HttpServer::ReadRequest(int client_fd,
                            std::string* method,
                            std::string* path,
                            std::string* body) const {
    constexpr size_t kMaxRequest = 1024 * 1024;
    std::string request;
    request.reserve(4096);

    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return 400;
        }
        request.append(buffer, static_cast<size_t>(n));
        if (request.size() > kMaxRequest) {
            return 400;
        }
    }

    const size_t header_end = request.find("\r\n\r\n");
    const std::string headers = request.substr(0, header_end);
    std::string payload = request.substr(header_end + 4);

    const size_t first_line_end = headers.find("\r\n");
    const std::string request_line = first_line_end == std::string::npos
                                         ? headers
                                         : headers.substr(0, first_line_end);

    std::istringstream rl(request_line);
    std::string version;
    if (!(rl >> *method >> *path >> version)) {
        return 400;
    }

    size_t content_length = 0;
    size_t header_pos = first_line_end == std::string::npos ? std::string::npos : first_line_end + 2;
    while (header_pos != std::string::npos && header_pos < headers.size()) {
        const size_t line_end = headers.find("\r\n", header_pos);
        const std::string line = headers.substr(header_pos, line_end - header_pos);

        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string key = Trim(line.substr(0, colon));
            const std::string value = Trim(line.substr(colon + 1));
            if (key == "Content-Length") {
                try {
                    content_length = static_cast<size_t>(std::stoul(value));
                } catch (...) {
                    return 400;
                }
            }
        }

        if (line_end == std::string::npos) {
            break;
        }
        header_pos = line_end + 2;
    }

    while (payload.size() < content_length) {
        const ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return 400;
        }
        payload.append(buffer, static_cast<size_t>(n));
        if (payload.size() > kMaxRequest) {
            return 400;
        }
    }

    if (payload.size() > content_length) {
        payload.resize(content_length);
    }

    *body = std::move(payload);
    return 0;
}
