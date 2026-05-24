# 🍈 Melon Stream Proxy

MJPEG WebSocket proxy for ESP32-CAM. Enables multi-device streaming.

## Architecture

```
ESP32-CAM → WebSocket → VPS Proxy → HTTP MJPEG → Multiple Clients
```

## Setup

### 1. Install dependencies
```bash
cd /var/www/melon-stream-proxy
npm install
```

### 2. Configure
Edit `.env`:
```bash
STREAM_SECRET=your-client-token
ESP32_TOKEN=your-esp32-token
PORT=8080
```

### 3. Start
```bash
# Manual
node server.js

# Systemd (auto-start)
sudo cp melon-stream-proxy.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable melon-stream-proxy
sudo systemctl start melon-stream-proxy
```

### 4. ESP32 Firmware
Flash `esp32-cam-websocket/esp32-cam-websocket.ino` to ESP32-CAM.

Edit WiFi and VPS settings:
```cpp
const char* WIFI_SSID     = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const char* WS_HOST       = "your-vps-ip";
const char* WS_PATH       = "/ws/stream?token=esp32-melon-01";
```

## Endpoints

| Endpoint | Auth | Description |
|----------|------|-------------|
| `GET /` | None | Status page |
| `GET /health` | None | Health check (JSON) |
| `GET /stream?token=xxx` | Token | MJPEG stream |
| `WS /ws/stream?token=xxx` | Token | ESP32 WebSocket |

## Usage

### View stream in browser
```
http://vps-ip:8080/stream?token=melon-cam-2024
```

### Embed in HTML
```html
<img src="http://vps-ip:8080/stream?token=melon-cam-2024" />
```

### Health check
```bash
curl http://vps-ip:8080/health
```

## Security

- **ESP32 token**: Only authorized ESP32 can push frames
- **Client token**: Only authorized clients can view stream
- **Max clients**: 50 concurrent viewers (configurable)
- **Frame timeout**: ESP32 marked offline after 10s without frames

## Architecture Details

1. ESP32 connects to VPS via WebSocket (reverse connection)
2. ESP32 sends raw JPEG frames as binary WebSocket messages
3. VPS buffer latest frame and broadcasts to all connected clients
4. Clients receive MJPEG stream (multipart/x-mixed-replace)
5. New clients get latest frame immediately (no waiting)

## Troubleshooting

**ESP32 can't connect**
- Check WiFi credentials
- Check VPS IP and port
- Check firewall: `ufw allow 8080`

**No frames received**
- Check ESP32 serial monitor
- Verify camera init OK
- Check WebSocket token matches

**Stream laggy**
- Reduce frame interval in ESP32 code
- Check network bandwidth
- Reduce resolution (VGA → QVGA)
