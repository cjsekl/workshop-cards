#!/usr/bin/env python3
"""
Debug server for CC2Zoom — browser UI with sliders replaces the Workshop card.

Serves an HTML page with sliders for each configured CC mapping.
Slider changes are sent via POST and forwarded as Zoom SysEx.

Usage:
    python3 debug_server.py [--config config.json] [--port 8080] [-v]
"""

import argparse
import json
import logging
import signal
import sys
import threading
import time
from functools import partial
from http.server import HTTPServer, BaseHTTPRequestHandler
from io import BytesIO

import mido

from cc2zoom import ZoomSysEx, CCMapping, MidiPortManager

log = logging.getLogger("debug_server")


class DebugState:
    """Shared state between HTTP handler and keepalive thread."""

    def __init__(self, config_path):
        with open(config_path) as f:
            self.config = json.load(f)

        self.zoom = ZoomSysEx(self.config.get("device_id"))
        self.mappings = {}
        for m in self.config.get("mappings", []):
            scale = m.get("scale", [0, 127])
            self.mappings[m["cc"]] = CCMapping(
                cc=m["cc"],
                slot=m["slot"],
                param=m["param"],
                scale_min=scale[0],
                scale_max=scale[1],
                description=m.get("description", ""),
            )

        self.output_port = None
        self.editor_enabled = False
        self._last_keepalive = 0
        self._lock = threading.Lock()

    def connect_zoom(self):
        """Find and connect to Zoom output port."""
        pattern = self.config["midi_output"]["port_pattern"]
        import re
        pat = re.compile(pattern, re.IGNORECASE)

        for name in mido.get_output_names():
            if pat.search(name):
                try:
                    self.output_port = mido.open_output(name)
                    log.info("Connected to Zoom: %s", name)
                    self._enable_editor()
                    return True
                except (IOError, OSError) as e:
                    log.error("Failed to open %s: %s", name, e)
                    return False

        log.warning("Zoom not found (pattern: %s)", pattern)
        log.info("Running in dry-run mode (SysEx will be logged but not sent)")
        return False

    def _enable_editor(self):
        if self.output_port:
            try:
                self.output_port.send(self.zoom.enable_editor())
                self.editor_enabled = True
                self._last_keepalive = time.time()
                log.info("Editor mode enabled")
            except (IOError, OSError) as e:
                log.error("Failed to enable editor: %s", e)

    def disable_editor(self):
        if self.editor_enabled and self.output_port:
            try:
                self.output_port.send(self.zoom.disable_editor())
                log.info("Editor mode disabled")
            except (IOError, OSError):
                pass
        self.editor_enabled = False

    def keepalive(self):
        """Call periodically from keepalive thread."""
        if not self.editor_enabled or not self.output_port:
            return
        now = time.time()
        if now - self._last_keepalive >= 30.0:
            with self._lock:
                try:
                    self.output_port.send(self.zoom.enable_editor())
                    self._last_keepalive = now
                    log.debug("Keepalive sent")
                except (IOError, OSError):
                    pass

    def send_cc(self, cc_num, value):
        """Handle a CC value from the browser slider."""
        mapping = self.mappings.get(cc_num)
        if not mapping:
            return {"error": f"CC {cc_num} not mapped"}

        scaled = mapping.update(value)
        if scaled is None:
            return {"status": "no_change"}

        sysex = self.zoom.param_edit(mapping.slot, mapping.param, scaled)
        log.info(
            "CC%d=%d -> slot %d param %d val %d (%s)",
            cc_num, value, mapping.slot, mapping.param, scaled, mapping.description,
        )

        if self.output_port:
            with self._lock:
                try:
                    self.output_port.send(sysex)
                except (IOError, OSError) as e:
                    return {"error": str(e)}

        return {"status": "sent", "slot": mapping.slot, "param": mapping.param, "scaled": scaled}

    def close(self):
        self.disable_editor()
        if self.output_port:
            try:
                self.output_port.close()
            except (IOError, OSError):
                pass


def make_handler(state):
    """Create request handler class with access to shared state."""

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            # Suppress default access logs unless verbose
            log.debug(format, *args)

        def do_GET(self):
            if self.path == "/" or self.path == "/index.html":
                self.send_response(200)
                self.send_header("Content-Type", "text/html")
                self.end_headers()
                self.wfile.write(build_html(state.config).encode())
            elif self.path == "/status":
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({
                    "connected": state.output_port is not None,
                    "editor_enabled": state.editor_enabled,
                }).encode())
            else:
                self.send_response(404)
                self.end_headers()

        def do_POST(self):
            if self.path == "/cc":
                length = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(length)
                try:
                    data = json.loads(body)
                    result = state.send_cc(data["cc"], data["value"])
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(json.dumps(result).encode())
                except (json.JSONDecodeError, KeyError) as e:
                    self.send_response(400)
                    self.end_headers()
                    self.wfile.write(str(e).encode())
            else:
                self.send_response(404)
                self.end_headers()

    return Handler


def build_html(config):
    """Generate the debug UI HTML with sliders for each mapping."""
    mappings_json = json.dumps(config.get("mappings", []))
    return f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CC2Zoom Debug</title>
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
body {{
    font-family: -apple-system, system-ui, sans-serif;
    background: #1a1a2e;
    color: #e0e0e0;
    padding: 20px;
    min-height: 100vh;
}}
h1 {{
    font-size: 1.2em;
    color: #8be9fd;
    margin-bottom: 4px;
}}
.status {{
    font-size: 0.8em;
    color: #666;
    margin-bottom: 20px;
}}
.status.connected {{ color: #50fa7b; }}
.status.disconnected {{ color: #ff5555; }}
.slider-group {{
    background: #16213e;
    border-radius: 8px;
    padding: 16px;
    margin-bottom: 12px;
}}
.slider-header {{
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 8px;
}}
.slider-label {{
    font-size: 0.85em;
    color: #bd93f9;
}}
.slider-value {{
    font-size: 0.85em;
    font-family: monospace;
    color: #f8f8f2;
    min-width: 3em;
    text-align: right;
}}
.slider-meta {{
    font-size: 0.7em;
    color: #6272a4;
    margin-top: 4px;
}}
input[type="range"] {{
    width: 100%;
    height: 6px;
    -webkit-appearance: none;
    appearance: none;
    background: #0f3460;
    border-radius: 3px;
    outline: none;
}}
input[type="range"]::-webkit-slider-thumb {{
    -webkit-appearance: none;
    appearance: none;
    width: 20px;
    height: 20px;
    border-radius: 50%;
    background: #8be9fd;
    cursor: pointer;
}}
input[type="range"]::-moz-range-thumb {{
    width: 20px;
    height: 20px;
    border-radius: 50%;
    background: #8be9fd;
    cursor: pointer;
    border: none;
}}
.log {{
    margin-top: 20px;
    padding: 12px;
    background: #0d1117;
    border-radius: 6px;
    font-family: monospace;
    font-size: 0.75em;
    max-height: 200px;
    overflow-y: auto;
    color: #8b949e;
}}
.log-entry {{ padding: 2px 0; }}
.log-entry.sent {{ color: #50fa7b; }}
.log-entry.error {{ color: #ff5555; }}
</style>
</head>
<body>
<h1>CC2Zoom Debug</h1>
<div class="status" id="status">connecting...</div>
<div id="sliders"></div>
<div class="log" id="log"></div>

<script>
const mappings = {mappings_json};
const logEl = document.getElementById('log');
const statusEl = document.getElementById('status');
const slidersEl = document.getElementById('sliders');

function addLog(msg, cls) {{
    const el = document.createElement('div');
    el.className = 'log-entry ' + (cls || '');
    el.textContent = new Date().toLocaleTimeString() + ' ' + msg;
    logEl.prepend(el);
    while (logEl.children.length > 50) logEl.removeChild(logEl.lastChild);
}}

function checkStatus() {{
    fetch('/status').then(r => r.json()).then(s => {{
        if (s.connected && s.editor_enabled) {{
            statusEl.textContent = 'connected (editor enabled)';
            statusEl.className = 'status connected';
        }} else if (s.connected) {{
            statusEl.textContent = 'connected (editor off)';
            statusEl.className = 'status';
        }} else {{
            statusEl.textContent = 'zoom not found (dry-run mode)';
            statusEl.className = 'status disconnected';
        }}
    }}).catch(() => {{
        statusEl.textContent = 'server unreachable';
        statusEl.className = 'status disconnected';
    }});
}}

function sendCC(cc, value) {{
    fetch('/cc', {{
        method: 'POST',
        headers: {{'Content-Type': 'application/json'}},
        body: JSON.stringify({{cc, value}})
    }}).then(r => r.json()).then(result => {{
        if (result.status === 'sent') {{
            addLog(`CC${{cc}}=${{value}} → slot ${{result.slot}} param ${{result.param}} scaled=${{result.scaled}}`, 'sent');
        }} else if (result.error) {{
            addLog(`CC${{cc}} error: ${{result.error}}`, 'error');
        }}
    }}).catch(e => addLog('fetch error: ' + e, 'error'));
}}

// Build slider UI from config
mappings.forEach(m => {{
    const group = document.createElement('div');
    group.className = 'slider-group';
    group.innerHTML = `
        <div class="slider-header">
            <span class="slider-label">${{m.description || 'CC ' + m.cc}}</span>
            <span class="slider-value" id="val-${{m.cc}}">0</span>
        </div>
        <input type="range" min="0" max="127" value="0" id="slider-${{m.cc}}">
        <div class="slider-meta">CC ${{m.cc}} → slot ${{m.slot}} param ${{m.param}} (scale ${{m.scale[0]}}-${{m.scale[1]}})</div>
    `;
    slidersEl.appendChild(group);

    const slider = document.getElementById('slider-' + m.cc);
    const valDisplay = document.getElementById('val-' + m.cc);
    slider.addEventListener('input', () => {{
        const v = parseInt(slider.value);
        valDisplay.textContent = v;
        sendCC(m.cc, v);
    }});
}});

checkStatus();
setInterval(checkStatus, 5000);
addLog('Debug UI loaded, move sliders to send CC');
</script>
</body>
</html>"""


def keepalive_loop(state):
    """Background thread for editor keepalive."""
    while state.editor_enabled or state.output_port:
        state.keepalive()
        time.sleep(5)


def main():
    parser = argparse.ArgumentParser(description="CC2Zoom debug server with browser UI")
    parser.add_argument("--config", "-c", default="config.json", help="Config file path")
    parser.add_argument("--port", "-p", type=int, default=8080, help="HTTP port (default: 8080)")
    parser.add_argument("--verbose", "-v", action="store_true", help="Debug logging")
    parser.add_argument("--list-ports", action="store_true", help="List MIDI ports and exit")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        datefmt="%H:%M:%S",
    )

    if args.list_ports:
        MidiPortManager.list_ports()
        return

    state = DebugState(args.config)
    state.connect_zoom()

    # Start keepalive thread
    ka_thread = threading.Thread(target=keepalive_loop, args=(state,), daemon=True)
    ka_thread.start()

    handler_class = make_handler(state)
    server = HTTPServer(("0.0.0.0", args.port), handler_class)
    server.timeout = 1

    def shutdown(signum, frame):
        log.info("Shutting down...")
        state.close()
        server.server_close()
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    log.info("Debug UI at http://localhost:%d", args.port)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        state.close()


if __name__ == "__main__":
    main()
