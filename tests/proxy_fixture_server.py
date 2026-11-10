#!/usr/bin/env python3
"""Local HTTP CONNECT and SOCKS5 proxies in front of one TLS origin.

The origin certificate is for download.invalid, which does not resolve. A client
that reaches it did so through the proxy. Nothing here is a real credential.
"""
import base64
import hashlib
import os
import pathlib
import sys
import time
import select
import socket
import ssl
import subprocess
import tempfile
import threading

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
USER = "proxyuser"
PASSWORD = "proxypass"


class Log:
    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()

    def write(self, line):
        with self.lock:
            with open(self.path, "a", encoding="ascii") as handle:
                handle.write(line + "\n")


def relay(left, right):
    sockets = [left, right]
    try:
        while True:
            readable, _, _ = select.select(sockets, [], [], 5)
            if not readable:
                return
            for item in readable:
                data = item.recv(65536)
                if not data:
                    return
                (right if item is left else left).sendall(data)
    except OSError:
        return


def read_until(conn, marker, limit=8192):
    data = b""
    while marker not in data:
        chunk = conn.recv(4096)
        if not chunk:
            break
        data += chunk
        if len(data) > limit:
            break
    return data


def origin(conn, log):
    log.write("origin")
    head = read_until(conn, b"\r\n\r\n")
    line = head.split(b"\r\n", 1)[0].decode("latin1", "replace")
    parts = line.split(" ")
    path = parts[1] if len(parts) > 1 else "/"
    if path.startswith("/ws"):
        key = ""
        for header in head.decode("latin1", "replace").split("\r\n"):
            if header.lower().startswith("sec-websocket-key:"):
                key = header.split(":", 1)[1].strip()
        accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        conn.sendall(
            ("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode()
            + bytes([0x82, 0x02]) + b"ok")
        try:
            conn.recv(64)
        except OSError:
            pass
        return
    if path.startswith("/redirect"):
        conn.sendall(b"HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:1/nowhere\r\nContent-Length: 0\r\n\r\n")
        return
    body = b"fixture-ok"
    conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: " + str(len(body)).encode("ascii") +
                 b"\r\nConnection: close\r\n\r\n" + body)


def serve_tls(sock, context, log):
    while True:
        try:
            client, _ = sock.accept()
        except OSError:
            return
        try:
            tls = context.wrap_socket(client, server_side=True)
            origin(tls, log)
            tls.close()
        except OSError:
            try:
                client.close()
            except OSError:
                pass


def http_proxy(sock, log, name, user):
    while True:
        try:
            client, _ = sock.accept()
        except OSError:
            return
        threading.Thread(target=http_client, args=(client, log, name, user), daemon=True).start()


def http_client(client, log, name, user):
    try:
        head = read_until(client, b"\r\n\r\n")
        text = head.decode("latin1", "replace")
        line = text.split("\r\n", 1)[0]
        if user:
            expected = "Basic " + base64.b64encode(f"{USER}:{PASSWORD}".encode()).decode()
            authorized = any(header.lower().startswith("proxy-authorization:") and expected in header
                             for header in text.split("\r\n"))
            if not authorized:
                log.write(f"{name} auth-rejected")
                client.sendall(b"HTTP/1.1 407 Proxy Authentication Required\r\n"
                               b"Proxy-Authenticate: Basic realm=\"playden\"\r\nContent-Length: 0\r\n\r\n")
                return
        parts = line.split(" ")
        if len(parts) < 2 or parts[0] != "CONNECT":
            log.write(f"{name} not-connect")
            client.sendall(b"HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n")
            return
        target = parts[1]
        log.write(f"{name} connect {target}")
        host, _, port = target.rpartition(":")
        remote = socket.create_connection(("127.0.0.1", int(port)), timeout=5)
        client.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
        relay(client, remote)
        remote.close()
    except OSError:
        log.write(f"{name} failed")
    finally:
        client.close()


def socks_proxy(sock, log, name, user):
    while True:
        try:
            client, _ = sock.accept()
        except OSError:
            return
        threading.Thread(target=socks_client, args=(client, log, name, user), daemon=True).start()


def recvall(conn, size):
    data = b""
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            break
        data += chunk
    return data


def socks_client(client, log, name, user):
    try:
        hello = recvall(client, 2)
        if len(hello) < 2 or hello[0] != 5:
            return
        methods = recvall(client, hello[1])
        if user:
            if 2 not in methods:
                client.sendall(b"\x05\xff")
                return
            client.sendall(b"\x05\x02")
            auth = recvall(client, 2)
            if len(auth) < 2:
                return
            name_bytes = recvall(client, auth[1])
            plen = recvall(client, 1)
            secret = recvall(client, plen[0]) if plen else b""
            if name_bytes.decode() != USER or secret.decode() != PASSWORD:
                log.write(f"{name} auth-rejected")
                client.sendall(b"\x01\x01")
                return
            client.sendall(b"\x01\x00")
        else:
            client.sendall(b"\x05\x00")
        request = recvall(client, 4)
        if len(request) < 4 or request[1] != 1:
            return
        if request[3] == 3:
            length = recvall(client, 1)
            host = recvall(client, length[0]).decode()
            log.write(f"{name} domain {host}")
        elif request[3] == 1:
            host = ".".join(str(b) for b in recvall(client, 4))
            log.write(f"{name} ip {host}")
        else:
            log.write(f"{name} atyp {request[3]}")
            return
        port_bytes = recvall(client, 2)
        port = int.from_bytes(port_bytes, "big")
        remote = socket.create_connection(("127.0.0.1", port), timeout=5)
        client.sendall(b"\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00")
        relay(client, remote)
        remote.close()
    except OSError:
        log.write(f"{name} failed")
    finally:
        client.close()


def listen():
    sock = socket.socket()
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", 0))
    sock.listen(32)
    return sock


def hang(sock):
    while True:
        try:
            client, _ = sock.accept()
        except OSError:
            return

        def hold(connection):
            try:
                time.sleep(30)
            finally:
                connection.close()

        threading.Thread(target=hold, args=(client,), daemon=True).start()


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: proxy_fixture_server.py TEST_BINARY")
    work = pathlib.Path(tempfile.mkdtemp(prefix="playden-proxy-"))
    cert = work / "cert.pem"
    key = work / "key.pem"
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-keyout", str(key), "-out", str(cert),
        "-days", "2", "-nodes", "-subj", "/CN=download.invalid",
        "-addext", "subjectAltName=DNS:download.invalid",
    ], check=True, capture_output=True)
    log = Log(str(work / "events.log"))
    origin_sock = listen()
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    threading.Thread(target=serve_tls, args=(origin_sock, context, log), daemon=True).start()
    servers = {
        "http": listen(),
        "http_auth": listen(),
        "socks": listen(),
        "socks_auth": listen(),
        "hang": listen(),
        "plain": listen(),
    }
    threading.Thread(target=http_proxy, args=(servers["http"], log, "http", False), daemon=True).start()
    threading.Thread(target=http_proxy, args=(servers["http_auth"], log, "http-auth", True), daemon=True).start()
    threading.Thread(target=socks_proxy, args=(servers["socks"], log, "socks", False), daemon=True).start()
    threading.Thread(target=socks_proxy, args=(servers["socks_auth"], log, "socks-auth", True), daemon=True).start()
    threading.Thread(target=hang, args=(servers["hang"],), daemon=True).start()

    def plain_loop():
        while True:
            client, _ = servers["plain"].accept()
            log.write("plain")
            read_until(client, b"\r\n\r\n")
            client.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok")
            client.close()

    threading.Thread(target=plain_loop, daemon=True).start()
    lines = [
        f"ca={cert}",
        f"log={work / 'events.log'}",
        f"origin={origin_sock.getsockname()[1]}",
        f"http={servers['http'].getsockname()[1]}",
        f"http_auth={servers['http_auth'].getsockname()[1]}",
        f"socks={servers['socks'].getsockname()[1]}",
        f"socks_auth={servers['socks_auth'].getsockname()[1]}",
        f"hang={servers['hang'].getsockname()[1]}",
        f"plain={servers['plain'].getsockname()[1]}",
    ]
    endpoints = work / "endpoints.txt"
    endpoints.write_text("\n".join(lines) + "\n")
    return subprocess.run([sys.argv[1], str(endpoints)], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
