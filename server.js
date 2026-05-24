/**
 * Melon Stream Proxy
 * 
 * ESP32-CAM → WebSocket → VPS → HTTP MJPEG → Multiple Clients
 * 
 * Endpoints:
 *   WS  /ws/stream       — ESP32 connects here to push frames
 *   GET /stream           — Clients connect here to view MJPEG stream
 *   GET /stream?token=xxx — Authenticated stream access
 *   GET /health           — Health check
 */

const http = require('http');
const { WebSocketServer } = require('ws');
const crypto = require('crypto');
const fs = require('fs');

// Load .env file
try {
  const envFile = fs.readFileSync(__dirname + '/.env', 'utf8');
  envFile.split('\n').forEach(line => {
    line = line.trim();
    if (line && !line.startsWith('#')) {
      const [key, ...val] = line.split('=');
      if (key && val.length) process.env[key.trim()] = val.join('=').trim();
    }
  });
} catch (e) { /* no .env file */ }

// ===================== CONFIG =====================
const PORT = process.env.PORT || 8080;
const STREAM_SECRET = process.env.STREAM_SECRET || 'melon-cam-2024';
const ESP32_TOKEN = process.env.ESP32_TOKEN || 'esp32-melon-01';
const MAX_CLIENTS = 50;
const FRAME_TIMEOUT = 10000; // 10s without frame = ESP32 offline

// ===================== STATE =====================
let latestFrame = null;
let latestFrameTime = 0;
let esp32Connected = false;
let esp32Ws = null;
const clients = new Set();

// ===================== HTTP SERVER =====================
const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  
  // Health check
  if (url.pathname === '/health') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      status: 'ok',
      esp32: esp32Connected ? 'online' : 'offline',
      clients: clients.size,
      lastFrame: latestFrameTime ? new Date(latestFrameTime).toISOString() : null
    }));
    return;
  }

  // MJPEG stream for clients
  if (url.pathname === '/stream') {
    // Auth check
    const token = url.searchParams.get('token');
    if (token !== STREAM_SECRET) {
      res.writeHead(401, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'Unauthorized. Provide ?token=xxx' }));
      return;
    }

    // Max clients check
    if (clients.size >= MAX_CLIENTS) {
      res.writeHead(503, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'Max clients reached' }));
      return;
    }

    // Set MJPEG headers
    res.writeHead(200, {
      'Content-Type': 'multipart/x-mixed-replace; boundary=frame',
      'Cache-Control': 'no-cache, no-store, must-revalidate',
      'Pragma': 'no-cache',
      'Expires': '0',
      'Connection': 'close',
      'Access-Control-Allow-Origin': '*'
    });

    // Send latest frame immediately if available
    if (latestFrame) {
      sendFrame(res, latestFrame);
    }

    // Add client
    clients.add(res);
    console.log(`[Client+] Connected (${clients.size} total)`);

    // Remove client on close
    req.on('close', () => {
      clients.delete(res);
      console.log(`[Client-] Disconnected (${clients.size} total)`);
    });

    return;
  }

  // Status page
  if (url.pathname === '/') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      name: 'Melon Stream Proxy',
      version: '1.0.0',
      endpoints: {
        stream: '/stream?token=xxx',
        health: '/health',
        ws: 'ws://host/ws/stream'
      },
      esp32: esp32Connected ? 'online' : 'offline',
      clients: clients.size
    }));
    return;
  }

  res.writeHead(404);
  res.end('Not found');
});

// ===================== WEBSOCKET SERVER =====================
const wss = new WebSocketServer({ server, path: '/ws/stream' });

wss.on('connection', (ws, req) => {
  const ip = req.headers['x-forwarded-for'] || req.socket.remoteAddress;
  
  // Check ESP32 auth
  const url = new URL(req.url, `http://${req.headers.host}`);
  const token = url.searchParams.get('token');
  
  if (token !== ESP32_TOKEN) {
    console.log(`[ESP32] Rejected from ${ip} — invalid token`);
    ws.close(4001, 'Invalid token');
    return;
  }

  // ESP32 connected
  esp32Connected = true;
  esp32Ws = ws;
  console.log(`[ESP32+] Connected from ${ip}`);

  ws.on('message', (data) => {
    // Expecting raw JPEG frame
    if (data.length < 100) {
      // Skip tiny packets (might be ping/control)
      return;
    }

    latestFrame = data;
    latestFrameTime = Date.now();

    // Broadcast to all clients
    let sent = 0;
    for (const client of clients) {
      try {
        sendFrame(client, data);
        sent++;
      } catch (e) {
        clients.delete(client);
      }
    }
    if (sent > 0) {
      process.stdout.write(`\r[Frame] ${data.length} bytes → ${sent} clients`);
    }
  });

  ws.on('close', () => {
    esp32Connected = false;
    esp32Ws = null;
    console.log(`\n[ESP32-] Disconnected`);
  });

  ws.on('error', (err) => {
    console.error(`[ESP32] Error:`, err.message);
  });
});

// ===================== HELPERS =====================
function sendFrame(res, frameData) {
  try {
    res.write('--frame\r\n');
    res.write('Content-Type: image/jpeg\r\n');
    res.write(`Content-Length: ${frameData.length}\r\n`);
    res.write('\r\n');
    res.write(frameData);
    res.write('\r\n');
  } catch (e) {
    clients.delete(res);
  }
}

// ===================== FRAME TIMEOUT CHECK =====================
setInterval(() => {
  if (esp32Connected && latestFrameTime && (Date.now() - latestFrameTime > FRAME_TIMEOUT)) {
    console.log(`\n[ESP32] No frames for ${FRAME_TIMEOUT/1000}s — marking offline`);
    esp32Connected = false;
  }
}, 5000);

// ===================== START =====================
server.listen(PORT, () => {
  console.log(`🍈 Melon Stream Proxy`);
  console.log(`   HTTP:    http://0.0.0.0:${PORT}/stream?token=xxx`);
  console.log(`   WS:      ws://0.0.0.0:${PORT}/ws/stream?token=xxx`);
  console.log(`   Health:  http://0.0.0.0:${PORT}/health`);
  console.log(`   ESP32 token: ${ESP32_TOKEN}`);
  console.log(`   Stream secret: ${STREAM_SECRET}`);
});
