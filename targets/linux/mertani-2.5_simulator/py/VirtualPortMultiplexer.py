#!/usr/bin/env python3
"""
Virtual COM Port Multiplexer
============================
Creates 4 virtual serial ports (esp32, ec20, rs485, rs232) and multiplexes
their data to/from a physical serial port (/dev/ttyACM0).

Packet format (with CRC):
  ID (1 byte) + length_low (1 byte) + length_high (1 byte)
  + data[0] … data[length-1] (N bytes)
  + crc_low (1 byte) + crc_high (1 byte)

CRC covers: ID + length_low + length_high + data  (everything before the CRC)
CRC algorithm: CRC-16/Modbus (poly=0x8005, init=0xFFFF, refin=True, refout=True, xorout=0x0000)

Port ID mapping:
  49 ('1') = esp32
  50 ('2') = ec20
  51 ('3') = rs485
  52 ('4') = rs232
"""

import os
import sys
import pty
import termios
import tty
import serial
import threading
import logging
import signal
import time
from typing import Dict, Optional

# ── Configuration ────────────────────────────────────────────────────────────

PHYSICAL_PORT = "/dev/ttyACM0"
PHYSICAL_BAUD = 921_600
SYMLINK_DIR   = "/tmp"

PORT_IDS: Dict[str, int] = {
    "esp32": 49,
    "ec20":  50,
    "rs485": 51,
    "rs232": 52,
}
ID_TO_NAME = {v: k for k, v in PORT_IDS.items()}

READ_TIMEOUT  = 0.05
RECV_BUF_SIZE = 16384

# ── Logging ───────────────────────────────────────────────────────────────────

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[logging.StreamHandler(sys.stdout)],
)
log = logging.getLogger(__name__)

# ── CRC-16/Modbus ─────────────────────────────────────────────────────────────

def crc16_modbus(data: bytes) -> int:
    """
    Compute CRC-16/Modbus over *data*.

    Parameters
    ----------
    data : bytes
        The byte sequence to protect (header + payload, i.e. everything
        *before* the two CRC bytes).

    Returns
    -------
    int
        16-bit CRC value.  Transmit / compare as little-endian:
            crc_low  = crc & 0xFF
            crc_high = (crc >> 8) & 0xFF
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001   # reflected polynomial 0x8005
            else:
                crc >>= 1
    return crc & 0xFFFF


# ── Helpers ───────────────────────────────────────────────────────────────────

def make_packet(port_id: int, data: bytes) -> bytes:
    """
    Build a framed packet with a trailing CRC-16/Modbus checksum.

    Wire format:
        [port_id][len_lo][len_hi][data…][crc_lo][crc_hi]

    The CRC covers every byte *before* it (port_id + len_lo + len_hi + data).
    """
    length      = len(data)
    length_low  = length & 0xFF
    length_high = (length >> 8) & 0xFF
    header      = bytes([port_id, length_low, length_high])
    crc         = crc16_modbus(header + data)
    crc_low     = crc & 0xFF
    crc_high    = (crc >> 8) & 0xFF
    return header + data + bytes([crc_low, crc_high])


def hex_dump(data: bytes, max_bytes: int = 64) -> str:
    """Return a compact hex + ASCII representation for logging."""
    truncated = data[:max_bytes]
    hex_part  = " ".join(f"{b:02X}" for b in truncated)
    asc_part  = "".join(chr(b) if 32 <= b < 127 else "." for b in truncated)
    suffix    = f" … (+{len(data)-max_bytes} more)" if len(data) > max_bytes else ""
    return f"[{hex_part}]  '{asc_part}'{suffix}"


def set_raw_mode(fd: int) -> None:
    try:
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        attrs[1] &= ~termios.OPOST
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except termios.error as exc:
        log.warning("Failed to set raw mode on FD %d: %s", fd, exc)


# ── Virtual port ──────────────────────────────────────────────────────────────

class VirtualPort:
    def __init__(self, name: str, port_id: int, symlink_dir: str):
        self.name     = name
        self.port_id  = port_id
        self.symlink  = os.path.join(symlink_dir, name)

        self.master_fd, self.slave_fd = pty.openpty()
        set_raw_mode(self.slave_fd)
        self.slave_name = os.ttyname(self.slave_fd)

        if os.path.lexists(self.symlink):
            os.unlink(self.symlink)
        os.symlink(self.slave_name, self.symlink)

        log.info("Virtual port %-6s  slave=%s  symlink=%s  id=%d",
                 name, self.slave_name, self.symlink, port_id)

    def read(self, size: int = RECV_BUF_SIZE) -> Optional[bytes]:
        try:
            return os.read(self.master_fd, size)
        except (BlockingIOError, OSError):
            return None

    def write(self, data: bytes) -> None:
        try:
            os.write(self.master_fd, data)
        except OSError as exc:
            log.warning("Write to %s failed: %s", self.name, exc)

    def fileno(self) -> int:
        return self.master_fd

    def cleanup(self) -> None:
        if os.path.lexists(self.symlink):
            os.unlink(self.symlink)
        for fd in (self.master_fd, self.slave_fd):
            try:
                os.close(fd)
            except OSError:
                pass


# ── Physical port receiver ────────────────────────────────────────────────────

class PhysicalPortReader:
    """
    State-machine parser for the physical serial stream.

    States
    ------
    ST_HEADER   – waiting to accumulate 3 header bytes (id + len_lo + len_hi)
    ST_PAYLOAD  – waiting to accumulate  (length + 2) more bytes
                  (the payload itself plus the two CRC trailer bytes)

    On CRC failure the parser drops only the first byte of the *current*
    header candidate and retries from the next byte, so it can resync as
    quickly as possible.
    """

    ST_HEADER  = "HEADER"
    ST_PAYLOAD = "PAYLOAD"

    def __init__(self, ser: serial.Serial, vports: Dict[str, VirtualPort]):
        self.ser    = ser
        self.vports = vports
        self._buf   = bytearray()

        self._state      = self.ST_HEADER
        self._port_id    = 0
        self._length     = 0

        self._pkt_rx_ok   = 0
        self._pkt_rx_err  = 0
        self._bytes_total = 0

    # ------------------------------------------------------------------ #
    #  Parser core                                                         #
    # ------------------------------------------------------------------ #

    def _try_parse(self) -> None:
        """Consume as many complete frames from self._buf as possible."""
        while True:

            # ── HEADER phase ──────────────────────────────────────────── #
            if self._state == self.ST_HEADER:
                if len(self._buf) < 3:
                    break  # need more bytes

                port_id    = self._buf[0]
                length_low = self._buf[1]
                length_hi  = self._buf[2]
                length     = length_low | (length_hi << 8)

                log.debug(
                    "HDR  raw=%s  id=0x%02X(%d,'%s')  len_lo=0x%02X  len_hi=0x%02X  -> length=%d",
                    hex_dump(bytes(self._buf[:3])),
                    port_id, port_id,
                    chr(port_id) if 32 <= port_id < 127 else "?",
                    length_low, length_hi, length,
                )

                # Unknown id → drop one byte and slide forward (resync)
                if port_id not in ID_TO_NAME:
                    log.debug(
                        "RESYNC  noise/unknown id=0x%02X ('%s') -- dropping 1 byte.",
                        port_id,
                        chr(port_id) if 32 <= port_id < 127 else "?",
                    )
                    self._buf = self._buf[1:]
                    continue

                # Zero-length payload is invalid
                if length == 0:
                    log.warning(
                        "HDR  zero-length packet for id=0x%02X ('%s'), discarding header.",
                        port_id, chr(port_id) if 32 <= port_id < 127 else "?",
                    )
                    self._buf = self._buf[3:]
                    self._pkt_rx_err += 1
                    continue

                # Implausibly large length → resync
                if length > 8192:
                    log.error(
                        "IMPLAUSIBLE length=%d for known id=0x%02X -- "
                        "discarding header and resyncing.  "
                        "Buffer head (16 B): %s",
                        length, port_id,
                        hex_dump(bytes(self._buf[:16])),
                    )
                    self._buf = self._buf[3:]
                    self._pkt_rx_err += 1
                    continue

                self._port_id = port_id
                self._length  = length
                self._buf     = self._buf[3:]   # consume the 3 header bytes
                self._state   = self.ST_PAYLOAD

            # ── PAYLOAD + CRC phase ───────────────────────────────────── #
            elif self._state == self.ST_PAYLOAD:
                # We need exactly (payload bytes) + 2 (CRC bytes)
                total_needed = self._length + 2
                have         = len(self._buf)

                if have < total_needed:
                    log.debug(
                        "WAIT  id=0x%02X ('%s')  need %d bytes (payload=%d + CRC=2), "
                        "have %d  (waiting for %d more).  Partial data: %s",
                        self._port_id,
                        chr(self._port_id) if 32 <= self._port_id < 127 else "?",
                        total_needed,
                        self._length,
                        have,
                        total_needed - have,
                        hex_dump(bytes(self._buf)),
                    )
                    break  # not an error – just wait for more data

                payload  = bytes(self._buf[:self._length])
                crc_low  = self._buf[self._length]
                crc_high = self._buf[self._length + 1]
                rx_crc   = crc_low | (crc_high << 8)

                # Reconstruct the header bytes to recompute the CRC.
                # CRC covers: [port_id, len_lo, len_hi] + payload
                length_low  = self._length & 0xFF
                length_high = (self._length >> 8) & 0xFF
                header      = bytes([self._port_id, length_low, length_high])
                calc_crc    = crc16_modbus(header + payload)

                if rx_crc != calc_crc:
                    # ── CRC failure ──────────────────────────────────── #
                    # Strategy: restore the header bytes back into the
                    # buffer (shift by 1), then let the HEADER phase drop
                    # the first byte and try again from the next candidate.
                    log.warning(
                        "CRC FAIL  id=0x%02X ('%s')  payload_len=%d  "
                        "rx_crc=0x%04X  calc_crc=0x%04X  "
                        "header+payload=%s  -- dropping 1 byte and resyncing.",
                        self._port_id,
                        chr(self._port_id) if 32 <= self._port_id < 127 else "?",
                        self._length,
                        rx_crc,
                        calc_crc,
                        hex_dump(header + payload),
                    )
                    # Put the header back (without the first byte) so the
                    # parser can try interpreting byte 1 of the old header as
                    # a new candidate ID.
                    self._buf   = bytearray(header[1:]) + bytearray(self._buf)
                    self._state = self.ST_HEADER
                    self._pkt_rx_err += 1
                    continue

                # ── CRC OK ───────────────────────────────────────────── #
                self._buf   = self._buf[total_needed:]   # consume payload + CRC
                self._state = self.ST_HEADER
                self._pkt_rx_ok += 1

                port_name = ID_TO_NAME[self._port_id]
                log.info(
                    "PKT OK  #%d  id=0x%02X ('%s'=%s)  len=%d  crc=0x%04X  data=%s",
                    self._pkt_rx_ok,
                    self._port_id,
                    chr(self._port_id) if 32 <= self._port_id < 127 else "?",
                    port_name,
                    self._length,
                    rx_crc,
                    hex_dump(payload),
                )

                vport = self.vports.get(port_name)
                if vport:
                    vport.write(payload)
                else:
                    log.error("No VirtualPort object for name '%s'", port_name)

    # ------------------------------------------------------------------ #
    #  Main loop                                                           #
    # ------------------------------------------------------------------ #

    def run_forever(self) -> None:
        log.info("Physical RX thread started on %s", self.ser.port)

        while True:
            try:
                waiting = self.ser.in_waiting
                raw = self.ser.read(waiting if waiting > 0 else 1)

                if not raw:
                    # Timeout — log if we're stuck mid-packet
                    if self._buf:
                        log.warning(
                            "TIMEOUT  state=%s  buf_len=%d  "
                            "need=%s  buf=%s",
                            self._state,
                            len(self._buf),
                            (self._length + 2) if self._state == self.ST_PAYLOAD else "n/a",
                            hex_dump(bytes(self._buf)),
                        )
                    continue

                self._bytes_total += len(raw)
                log.debug(
                    "READ  %d bytes (total=%d, in_buf=%d)  raw=%s",
                    len(raw), self._bytes_total, len(self._buf) + len(raw),
                    hex_dump(raw),
                )

                self._buf.extend(raw)
                self._try_parse()

                total = self._pkt_rx_ok + self._pkt_rx_err
                if total > 0 and total % 50 == 0:
                    log.info(
                        "STATS  ok=%d  err=%d  bytes_rx=%d  buf_pending=%d",
                        self._pkt_rx_ok, self._pkt_rx_err,
                        self._bytes_total, len(self._buf),
                    )

            except serial.SerialException as exc:
                log.error("Physical port read error: %s – retrying in 1 s", exc)
                time.sleep(1)
            except Exception as exc:
                log.exception("Unexpected error in physical RX: %s", exc)
                time.sleep(0.1)


# ── Virtual port readers ──────────────────────────────────────────────────────

class VirtualPortReader:
    def __init__(self, vport: VirtualPort, ser: serial.Serial, write_lock: threading.Lock):
        self.vport      = vport
        self.ser        = ser
        self.write_lock = write_lock
        self._pkt_tx    = 0

    def run_forever(self) -> None:
        import fcntl
        import select

        flags = fcntl.fcntl(self.vport.master_fd, fcntl.F_GETFL)
        fcntl.fcntl(self.vport.master_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

        log.info("Virtual TX thread started for '%s' (Zero-Latency Event Mode)", self.vport.name)

        fd = self.vport.master_fd

        while True:
            try:
                readable, _, _ = select.select([fd], [], [])

                if fd in readable:
                    data = self.vport.read()

                    if data:
                        packet = make_packet(self.vport.port_id, data)
                        self._pkt_tx += 1

                        log.info(
                            "TX  #%d  id=0x%02X (%s)  data_len=%d  "
                            "crc=0x%04X  data=%s  packet=%s",
                            self._pkt_tx,
                            self.vport.port_id, self.vport.name,
                            len(data),
                            crc16_modbus(
                                bytes([
                                    self.vport.port_id,
                                    len(data) & 0xFF,
                                    (len(data) >> 8) & 0xFF,
                                ]) + data
                            ),
                            hex_dump(data),
                            hex_dump(packet),
                        )

                        with self.write_lock:
                            self.ser.write(packet)
                            self.ser.flush()

            except serial.SerialException as exc:
                log.error("Physical port write error (%s): %s – retrying in 1 s",
                          self.vport.name, exc)
                time.sleep(1)
            except OSError as exc:
                log.debug("OS event in TX thread (%s): %s", self.vport.name, exc)
                time.sleep(0.1)
            except Exception as exc:
                log.exception("Unexpected error in virtual TX (%s): %s", self.vport.name, exc)
                time.sleep(0.1)


# ── Multiplexer ───────────────────────────────────────────────────────────────

class Multiplexer:
    def __init__(self):
        self.vports: Dict[str, VirtualPort] = {}
        self.ser: Optional[serial.Serial]   = None
        self.threads: list                  = []

    def setup_virtual_ports(self) -> None:
        for name, port_id in PORT_IDS.items():
            self.vports[name] = VirtualPort(name, port_id, SYMLINK_DIR)

    def open_physical_port(self) -> None:
        log.info("Opening physical port %s at %d baud …", PHYSICAL_PORT, PHYSICAL_BAUD)
        self.ser = serial.Serial(
            port=PHYSICAL_PORT,
            baudrate=PHYSICAL_BAUD,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=3.0,
        )
        log.info("Physical port opened: %s", self.ser.name)

    def start_threads(self) -> None:
        write_lock = threading.Lock()

        for vport in self.vports.values():
            t = threading.Thread(
                target=VirtualPortReader(vport, self.ser, write_lock).run_forever,
                name=f"tx-{vport.name}",
                daemon=True,
            )
            t.start()
            self.threads.append(t)

        t = threading.Thread(
            target=PhysicalPortReader(self.ser, self.vports).run_forever,
            name="rx-physical",
            daemon=True,
        )
        t.start()
        self.threads.append(t)

    def cleanup(self) -> None:
        log.info("Shutting down …")
        for vport in self.vports.values():
            vport.cleanup()
        if self.ser and self.ser.is_open:
            self.ser.close()
        log.info("Bye.")

    def run(self) -> None:
        self.setup_virtual_ports()
        self.open_physical_port()
        self.start_threads()

        log.info("Multiplexer running. Press Ctrl+C to stop.")
        for name, vport in self.vports.items():
            log.info("  %-6s  →  %s  (symlink: %s)", name, vport.slave_name, vport.symlink)

        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        finally:
            self.cleanup()


def main() -> None:
    mux = Multiplexer()

    def _sig(sig, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, _sig)
    signal.signal(signal.SIGINT,  _sig)
    mux.run()


if __name__ == "__main__":
    main()