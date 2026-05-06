#!/usr/bin/env python3
"""
CC2Zoom — Bridge Workshop System MIDI CC to Zoom MS-70CDR+ SysEx

Receives standard MIDI CC from the Workshop System card and translates
to Zoom's proprietary SysEx parameter edit messages over USB MIDI.
"""

import argparse
import json
import logging
import re
import signal
import sys
import time

import mido

log = logging.getLogger("cc2zoom")


class ZoomSysEx:
    """Constructs and parses Zoom MS Plus Series SysEx messages."""

    ZOOM_MANUFACTURER = 0x52
    DEFAULT_DEVICE_ID = 0x6E  # MS Plus Series

    def __init__(self, device_id=None):
        self.device_id = device_id or self.DEFAULT_DEVICE_ID

    @staticmethod
    def identity_request():
        """Universal Identity Request (non-realtime SysEx)."""
        return mido.Message("sysex", data=[0x7E, 0x7F, 0x06, 0x01])

    def enable_editor(self):
        """Enable editor mode — required before parameter edits."""
        return mido.Message(
            "sysex", data=[self.ZOOM_MANUFACTURER, 0x00, self.device_id, 0x50]
        )

    def disable_editor(self):
        """Disable editor mode — send on shutdown."""
        return mido.Message(
            "sysex", data=[self.ZOOM_MANUFACTURER, 0x00, self.device_id, 0x51]
        )

    def param_edit(self, slot, param, value):
        """
        Parameter edit message.

        slot: 0-5 (effect slot on pedal)
        param: parameter index (0=on/off, 2-10=knobs)
        value: 0-127 (will be sent as 14-bit with MSB=0 for 7-bit range)
        """
        # 14-bit split: LSB = lower 7 bits, MSB = upper 7 bits
        lsb = value & 0x7F
        msb = (value >> 7) & 0x7F
        return mido.Message(
            "sysex",
            data=[
                self.ZOOM_MANUFACTURER,
                0x00,
                self.device_id,
                0x64,
                0x20,
                0x00,
                slot & 0x7F,
                param & 0x7F,
                lsb,
                msb,
                0x00,
                0x00,
                0x00,
            ],
        )

    def parse_identity_response(self, msg):
        """
        Parse identity response, return device_id if it's a Zoom device.
        Returns None if not a valid Zoom identity response.
        """
        if msg.type != "sysex":
            return None
        data = msg.data
        # Identity reply: 7E <dev> 06 02 <manufacturer> ...
        if len(data) >= 6 and data[2] == 0x06 and data[3] == 0x02:
            if data[4] == self.ZOOM_MANUFACTURER:
                # Device ID is in byte 1 of the response or we extract from family
                # For Zoom, the device_id we need is the one used in SysEx framing
                # It's typically at a fixed position in their reply
                if len(data) >= 10:
                    # Zoom identity: F0 7E 00 06 02 52 <family_lo> <family_hi> <model_lo> <model_hi> ...
                    family = data[5] | (data[6] << 7)
                    log.info(f"Zoom device detected, family code: 0x{family:04X}")
                    # MS Plus series family
                    return self.DEFAULT_DEVICE_ID
        return None


class CCMapping:
    """Maps a single CC number to a Zoom parameter with value scaling."""

    def __init__(self, cc, slot, param, scale_min=0, scale_max=127, description=""):
        self.cc = cc
        self.slot = slot
        self.param = param
        self.scale_min = scale_min
        self.scale_max = scale_max
        self.description = description
        self._last_scaled = -1

    def scale(self, cc_value):
        """Scale 7-bit CC (0-127) to [scale_min, scale_max] range."""
        if self.scale_min == 0 and self.scale_max == 127:
            return cc_value
        return self.scale_min + (cc_value * (self.scale_max - self.scale_min)) // 127

    def update(self, cc_value):
        """
        Returns scaled value if it changed, else None (deduplication).
        """
        scaled = self.scale(cc_value)
        if scaled != self._last_scaled:
            self._last_scaled = scaled
            return scaled
        return None


class MidiPortManager:
    """Discovers and manages MIDI port connections."""

    def __init__(self, input_pattern, output_pattern):
        self.input_pattern = re.compile(input_pattern, re.IGNORECASE)
        self.output_pattern = re.compile(output_pattern, re.IGNORECASE)
        self.input_port = None
        self.output_port = None

    @staticmethod
    def list_ports():
        """Print all available MIDI ports."""
        print("Input ports:")
        for name in mido.get_input_names():
            print(f"  {name}")
        print("\nOutput ports:")
        for name in mido.get_output_names():
            print(f"  {name}")

    def _find_port(self, names, pattern):
        for name in names:
            if pattern.search(name):
                return name
        return None

    def connect(self):
        """
        Attempt to open input (Workshop card) and output (Zoom pedal) ports.
        Returns True if both connected, False otherwise.
        """
        self.close()

        input_name = self._find_port(mido.get_input_names(), self.input_pattern)
        output_name = self._find_port(mido.get_output_names(), self.output_pattern)

        if not input_name:
            log.warning("Workshop MIDI input not found (pattern: %s)", self.input_pattern.pattern)
            return False
        if not output_name:
            log.warning("Zoom MIDI output not found (pattern: %s)", self.output_pattern.pattern)
            return False

        try:
            self.input_port = mido.open_input(input_name)
            log.info("Opened input: %s", input_name)
        except (IOError, OSError) as e:
            log.error("Failed to open input %s: %s", input_name, e)
            return False

        try:
            self.output_port = mido.open_output(output_name)
            log.info("Opened output: %s", output_name)
        except (IOError, OSError) as e:
            log.error("Failed to open output %s: %s", output_name, e)
            self.close()
            return False

        # Also try to open Zoom as input for identity response
        zoom_input_name = self._find_port(mido.get_input_names(), self.output_pattern)
        self.zoom_input = None
        if zoom_input_name:
            try:
                self.zoom_input = mido.open_input(zoom_input_name)
                log.debug("Opened Zoom input for identity: %s", zoom_input_name)
            except (IOError, OSError):
                log.debug("Could not open Zoom input (non-fatal)")

        return True

    def close(self):
        """Close all open ports."""
        for port in (self.input_port, self.output_port, getattr(self, "zoom_input", None)):
            if port and not port.closed:
                try:
                    port.close()
                except (IOError, OSError):
                    pass
        self.input_port = None
        self.output_port = None
        self.zoom_input = None

    @property
    def connected(self):
        return (
            self.input_port is not None
            and not self.input_port.closed
            and self.output_port is not None
            and not self.output_port.closed
        )


class CC2Zoom:
    """Main orchestrator: connects devices, translates CC to SysEx."""

    def __init__(self, config_path):
        self.config = self._load_config(config_path)
        self.zoom = ZoomSysEx(self.config.get("device_id"))
        self.ports = MidiPortManager(
            self.config["midi_input"]["port_pattern"],
            self.config["midi_output"]["port_pattern"],
        )
        self.mappings = self._build_mappings()
        self.running = False
        self.editor_enabled = False
        self._last_keepalive = 0

    def _load_config(self, path):
        try:
            with open(path) as f:
                return json.load(f)
        except (FileNotFoundError, json.JSONDecodeError) as e:
            log.error("Config error: %s", e)
            sys.exit(1)

    def _build_mappings(self):
        """Build lookup table: {cc_number: CCMapping}."""
        table = {}
        for m in self.config.get("mappings", []):
            scale = m.get("scale", [0, 127])
            table[m["cc"]] = CCMapping(
                cc=m["cc"],
                slot=m["slot"],
                param=m["param"],
                scale_min=scale[0],
                scale_max=scale[1],
                description=m.get("description", ""),
            )
        return table

    def _detect_device_id(self):
        """Send identity request and wait for response (2s timeout)."""
        if not self.ports.output_port or not getattr(self.ports, "zoom_input", None):
            return

        log.info("Sending identity request...")
        try:
            self.ports.output_port.send(ZoomSysEx.identity_request())
        except (IOError, OSError):
            return

        deadline = time.time() + 2.0
        while time.time() < deadline:
            try:
                msg = self.ports.zoom_input.poll()
            except (IOError, OSError):
                return
            if msg is None:
                time.sleep(0.01)
                continue
            device_id = self.zoom.parse_identity_response(msg)
            if device_id is not None:
                self.zoom.device_id = device_id
                log.info("Auto-detected device_id: 0x%02X", device_id)
                return

        log.warning("Identity request timeout, using default device_id 0x%02X", self.zoom.device_id)

    def _enable_editor(self):
        """Send enable-editor command to Zoom."""
        try:
            self.ports.output_port.send(self.zoom.enable_editor())
            self.editor_enabled = True
            self._last_keepalive = time.time()
            log.info("Editor mode enabled")
        except (IOError, OSError) as e:
            log.error("Failed to enable editor: %s", e)
            self.editor_enabled = False

    def _disable_editor(self):
        """Send disable-editor command to Zoom."""
        if not self.editor_enabled:
            return
        try:
            self.ports.output_port.send(self.zoom.disable_editor())
            log.info("Editor mode disabled")
        except (IOError, OSError):
            pass
        self.editor_enabled = False

    def _keepalive(self):
        """Re-send enable-editor every 30s to prevent timeout."""
        now = time.time()
        if now - self._last_keepalive >= 30.0:
            try:
                self.ports.output_port.send(self.zoom.enable_editor())
                self._last_keepalive = now
                log.debug("Editor keepalive sent")
            except (IOError, OSError):
                pass

    def _handle_cc(self, msg):
        """Process a CC message: look up mapping, scale, send SysEx."""
        mapping = self.mappings.get(msg.control)
        if mapping is None:
            return

        scaled = mapping.update(msg.value)
        if scaled is None:
            return  # No change after dedup

        sysex = self.zoom.param_edit(mapping.slot, mapping.param, scaled)
        try:
            self.ports.output_port.send(sysex)
            log.debug(
                "CC%d=%d -> slot %d param %d val %d (%s)",
                msg.control,
                msg.value,
                mapping.slot,
                mapping.param,
                scaled,
                mapping.description,
            )
        except (IOError, OSError) as e:
            log.error("Send failed: %s", e)
            raise

    def _connect_loop(self):
        """Keep trying to connect until successful or stopped."""
        while self.running:
            if self.ports.connect():
                if self.config.get("device_id") is None:
                    self._detect_device_id()
                self._enable_editor()
                return True
            time.sleep(2.0)
        return False

    def run(self):
        """Main loop."""
        self.running = True

        # Signal handlers for clean shutdown
        def shutdown(signum, frame):
            log.info("Signal %d received, shutting down...", signum)
            self.running = False

        signal.signal(signal.SIGINT, shutdown)
        signal.signal(signal.SIGTERM, shutdown)

        log.info("CC2Zoom starting, %d mappings loaded", len(self.mappings))

        while self.running:
            if not self.ports.connected:
                if not self._connect_loop():
                    break

            try:
                msg = self.ports.input_port.poll()
                if msg is not None:
                    if msg.type == "control_change":
                        self._handle_cc(msg)
                else:
                    time.sleep(0.001)

                self._keepalive()

            except (IOError, OSError) as e:
                log.warning("Port error: %s — reconnecting...", e)
                self._disable_editor()
                self.ports.close()
                # Reset mapping dedup state so values resend after reconnect
                for m in self.mappings.values():
                    m._last_scaled = -1

        # Clean shutdown
        self._disable_editor()
        self.ports.close()
        log.info("CC2Zoom stopped")


def main():
    parser = argparse.ArgumentParser(
        description="Bridge Workshop System MIDI CC to Zoom MS-70CDR+ SysEx"
    )
    parser.add_argument(
        "--config", "-c", default="config.json", help="Path to config file (default: config.json)"
    )
    parser.add_argument("--verbose", "-v", action="store_true", help="Enable debug logging")
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

    bridge = CC2Zoom(args.config)
    bridge.run()


if __name__ == "__main__":
    main()
