"""
Smart Helmet - Wireless Presentation Web Bridge (Flask + SSE)
Author: Antigravity Assistant

Features:
1. Connects to the ESP32-S3 over COM6 (115200 baud).
2. Serves the futuristic Cyberpunk Telemetry HUD on http://localhost:5000
3. Broadcasts real-time serial logs wirelessly to any connected browser on your Wi-Fi!
"""

import sys
import os
import time
import socket
import queue
import threading
from pathlib import Path
from flask import Flask, Response, send_file

try:
    import serial
except ImportError:
    print("[ERROR] pyserial is required. Run: pip install pyserial")
    sys.exit(1)

app = Flask(__name__)
DASHBOARD_FILE = Path(__file__).resolve().parent / "smart_helmet_dashboard.html"

# Global client listener queues
client_queues = []
queue_lock = threading.Lock()

def get_local_ip():
    """Gets local network IP to display on projector / presentation screen."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def serial_reader_thread(port="COM6", baud=115200):
    print(f"\n[SERIAL] Connecting to {port} at {baud} baud...")
    while True:
        try:
            with serial.Serial(port, baud, timeout=1) as ser:
                print(f"[SERIAL] Connected to {port}! Streaming data live...")
                while True:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        # Broadcast to all connected web clients
                        with queue_lock:
                            for q in client_queues[:]:
                                try:
                                    q.put_nowait(line)
                                except Exception:
                                    client_queues.remove(q)
        except Exception as e:
            print(f"[SERIAL] Waiting for {port} ({e}). Retrying in 2s...")
            time.sleep(2)

@app.route('/')
def index():
    return send_file(DASHBOARD_FILE)

@app.route('/stream')
def stream():
    def event_stream():
        q = queue.Queue(maxsize=100)
        with queue_lock:
            client_queues.append(q)
        try:
            yield "data: [SYSTEM] Connected to Live Helmet Telemetry Stream\n\n"
            while True:
                line = q.get()
                yield f"data: {line}\n\n"
        except GeneratorExit:
            with queue_lock:
                if q in client_queues:
                    client_queues.remove(q)

    return Response(event_stream(), mimetype="text/event-stream")

if __name__ == '__main__':
    port_name = sys.argv[1] if len(sys.argv) > 1 else "COM6"
    
    # Start background serial worker
    t = threading.Thread(target=serial_reader_thread, args=(port_name, 115200), daemon=True)
    t.start()

    local_ip = get_local_ip()
    print("=" * 70)
    print("SMART HELMET // LIVE PRESENTATION WEB SERVER")
    print("=" * 70)
    print(f" -> Local Laptop URL    : http://localhost:5000")
    print(f" -> Wireless Wi-Fi URL  : http://{local_ip}:5000")
    print("=" * 70)
    print("Open this URL on your laptop, projector, tablet, or phone!")
    print("Press Ctrl + C to stop the server.")
    print("=" * 70)

    # Disable Flask banner logs for cleaner output
    import logging
    log = logging.getLogger('werkzeug')
    log.setLevel(logging.ERROR)

    app.run(host="0.0.0.0", port=5000, threaded=True)
