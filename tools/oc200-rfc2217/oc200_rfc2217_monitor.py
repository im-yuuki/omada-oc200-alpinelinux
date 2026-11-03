#!/usr/bin/env python3
import argparse
import os
import select
import signal
import socket
import sys
import time
from datetime import datetime, timezone

HOST = "192.168.12.5"
PORT = 34567
BASE = os.path.dirname(os.path.abspath(__file__))
SOCKET_PATH = os.path.join(BASE, "oc200-rfc2217-control.sock")
WIRE_PATH = os.path.join(BASE, "oc200-console-wire.bin")
CONSOLE_PATH = os.path.join(BASE, "oc200-console.bin")
EVENT_PATH = os.path.join(BASE, "oc200-rfc2217.events.log")
PID_PATH = os.path.join(BASE, "oc200-rfc2217.pid")

IAC = 255
WILL = 251
WONT = 252
DO = 253
DONT = 254
SB = 250
SE = 240
SGA = 3
ECHO = 1
BINARY = 0
COM_PORT_OPTION = 44

ACCEPT_SERVER_WILL = {SGA, BINARY, COM_PORT_OPTION}
ACCEPT_SERVER_DO = {SGA, BINARY, COM_PORT_OPTION}


class TelnetParser:
    def __init__(self):
        self.state = "data"
        self.command = None

    def feed(self, data):
        output = bytearray()
        replies = bytearray()
        for value in data:
            if self.state == "data":
                if value == IAC:
                    self.state = "iac"
                else:
                    output.append(value)
            elif self.state == "iac":
                if value == IAC:
                    output.append(IAC)
                    self.state = "data"
                elif value in (WILL, WONT, DO, DONT):
                    self.command = value
                    self.state = "negotiation"
                elif value == SB:
                    self.state = "subnegotiation"
                else:
                    self.state = "data"
            elif self.state == "negotiation":
                option = value
                if self.command == WILL:
                    response = DO if option in ACCEPT_SERVER_WILL else DONT
                elif self.command == WONT:
                    response = DONT
                elif self.command == DO:
                    response = WILL if option in ACCEPT_SERVER_DO else WONT
                else:
                    response = WONT
                replies.extend((IAC, response, option))
                self.command = None
                self.state = "data"
            elif self.state == "subnegotiation":
                if value == IAC:
                    self.state = "subnegotiation_iac"
            else:
                if value == SE:
                    self.state = "data"
                elif value != IAC:
                    self.state = "subnegotiation"
        return bytes(output), bytes(replies)


def write_all(fd, data):
    view = memoryview(data)
    while view:
        written = os.write(fd, view)
        view = view[written:]


def timestamp():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def log_event(message):
    line = f"{timestamp()} {message}\n".encode()
    fd = os.open(EVENT_PATH, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    try:
        write_all(fd, line)
    finally:
        os.close(fd)


def telnet_escape(data):
    return data.replace(bytes((IAC,)), bytes((IAC, IAC)))


class Monitor:
    def __init__(self):
        self.stop_requested = False
        self.serial = None
        self.parser = TelnetParser()
        self.next_connect = 0.0
        self.control_clients = []
        self.pending_tx = bytearray()

        os.makedirs(BASE, exist_ok=True)
        for path in (WIRE_PATH, CONSOLE_PATH):
            if not os.path.exists(path):
                os.close(os.open(path, os.O_WRONLY | os.O_CREAT, 0o600))
        self.wire_fd = os.open(WIRE_PATH, os.O_WRONLY | os.O_APPEND)
        self.console_fd = os.open(CONSOLE_PATH, os.O_WRONLY | os.O_APPEND)
        self.control_listener = self.make_control_listener()

    def make_control_listener(self):
        try:
            os.unlink(SOCKET_PATH)
        except FileNotFoundError:
            pass
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        listener.bind(SOCKET_PATH)
        os.chmod(SOCKET_PATH, 0o600)
        listener.listen(4)
        listener.setblocking(False)
        return listener

    def close_serial(self):
        if self.serial is not None:
            try:
                self.serial.close()
            except OSError:
                pass
            self.serial = None

    def connect_serial(self):
        now = time.monotonic()
        if self.serial is not None or now < self.next_connect:
            return
        try:
            serial = socket.create_connection((HOST, PORT), timeout=5)
            serial.setblocking(False)
            serial.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.serial = serial
            self.parser = TelnetParser()
            self.next_connect = now
            log_event(f"connected {HOST}:{PORT}")
            self.flush_pending_tx()
        except OSError as exc:
            self.next_connect = now + 2.0
            log_event(f"connect failed: {exc}")

    def disconnect_serial(self, reason):
        if self.serial is not None:
            log_event(f"disconnected: {reason}")
        self.close_serial()
        self.next_connect = time.monotonic() + 1.0

    def flush_pending_tx(self):
        if self.serial is None or not self.pending_tx:
            return
        try:
            payload = telnet_escape(bytes(self.pending_tx))
            self.serial.sendall(payload)
            self.pending_tx.clear()
        except OSError as exc:
            self.disconnect_serial(f"send failed: {exc}")

    def accept_controls(self):
        while True:
            try:
                client, _ = self.control_listener.accept()
                client.setblocking(False)
                self.control_clients.append(client)
            except BlockingIOError:
                return
            except OSError:
                return

    def close_control(self, client):
        try:
            client.close()
        except OSError:
            pass
        if client in self.control_clients:
            self.control_clients.remove(client)

    def read_control(self, client):
        try:
            data = client.recv(65536)
        except BlockingIOError:
            return
        except OSError:
            self.close_control(client)
            return
        if not data:
            self.close_control(client)
            return
        self.pending_tx.extend(data)
        self.flush_pending_tx()

    def read_serial(self):
        try:
            data = self.serial.recv(65536)
        except BlockingIOError:
            return
        except OSError as exc:
            self.disconnect_serial(f"read failed: {exc}")
            return
        if not data:
            self.disconnect_serial("peer closed")
            return

        write_all(self.wire_fd, data)
        serial_data, replies = self.parser.feed(data)
        if serial_data:
            write_all(self.console_fd, serial_data)
        if replies:
            try:
                self.serial.sendall(replies)
            except OSError as exc:
                self.disconnect_serial(f"negotiation send failed: {exc}")

    def run(self):
        log_event(f"monitor started pid={os.getpid()} target={HOST}:{PORT}")
        while not self.stop_requested:
            self.connect_serial()
            read_fds = [self.control_listener]
            read_fds.extend(self.control_clients)
            if self.serial is not None:
                read_fds.append(self.serial)
            try:
                readable, _, exceptional = select.select(read_fds, [], read_fds, 1.0)
            except InterruptedError:
                continue
            except OSError as exc:
                log_event(f"select failed: {exc}")
                time.sleep(1.0)
                continue

            if self.control_listener in readable:
                self.accept_controls()
            if self.serial is not None and self.serial in readable:
                self.read_serial()
            for client in list(self.control_clients):
                if client in readable or client in exceptional:
                    self.read_control(client)
            if self.serial is not None and self.serial in exceptional:
                self.disconnect_serial("socket exceptional condition")

        self.shutdown()

    def shutdown(self):
        self.close_serial()
        for client in list(self.control_clients):
            self.close_control(client)
        try:
            self.control_listener.close()
        except OSError:
            pass
        for fd in (self.wire_fd, self.console_fd):
            try:
                os.close(fd)
            except OSError:
                pass
        try:
            os.unlink(SOCKET_PATH)
        except FileNotFoundError:
            pass
        log_event("monitor stopped")

    def request_stop(self, *_):
        self.stop_requested = True


def send_control(data):
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(5)
    client.connect(SOCKET_PATH)
    client.sendall(data)
    client.shutdown(socket.SHUT_WR)
    client.close()


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="action", required=True)
    run_parser = subparsers.add_parser("run")
    run_parser.set_defaults(action="run")
    send_parser = subparsers.add_parser("send")
    send_parser.add_argument("data", help="text to send to the serial console")
    args = parser.parse_args()

    if args.action == "send":
        send_control(args.data.encode())
        return 0

    with open(PID_PATH, "w", encoding="ascii") as pid_file:
        pid_file.write(str(os.getpid()) + "\n")
    monitor = Monitor()
    signal.signal(signal.SIGTERM, monitor.request_stop)
    signal.signal(signal.SIGINT, monitor.request_stop)
    try:
        monitor.run()
    finally:
        try:
            os.unlink(PID_PATH)
        except FileNotFoundError:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
