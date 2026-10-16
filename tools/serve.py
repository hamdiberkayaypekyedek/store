#!/usr/bin/env python3
"""PKG Mağaza için yerel ağ sunucusu (Range/devam ettirme destekli).

PS4'ün indirme servisi büyük pkg'leri parça parça (HTTP Range) ister;
Python'un hazır http.server'ı bunu desteklemediği için bu betik kullanılır.

    python serve.py D:\\pkgler --port 8000
"""
import argparse
import functools
import http.server
import os
import re
import socket


class RangeHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Accept-Ranges", "bytes")
        super().end_headers()

    def send_head(self):
        self._remaining = None
        rng = self.headers.get("Range")
        path = self.translate_path(self.path)
        if not rng or not os.path.isfile(path):
            return super().send_head()
        m = re.match(r"bytes=(\d*)-(\d*)$", rng.strip())
        if not m:
            return super().send_head()
        size = os.path.getsize(path)
        first, last = m.groups()
        if first == "":
            start, end = max(0, size - int(last or 0)), size - 1
        else:
            start = int(first)
            end = int(last) if last else size - 1
        if start >= size:
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{size}")
            self.end_headers()
            return None
        end = min(end, size - 1)
        f = open(path, "rb")
        f.seek(start)
        self.send_response(206)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(end - start + 1))
        self.end_headers()
        self._remaining = end - start + 1
        return f

    def copyfile(self, source, outputfile):
        if self._remaining is None:
            return super().copyfile(source, outputfile)
        left = self._remaining
        while left > 0:
            chunk = source.read(min(1 << 20, left))
            if not chunk:
                break
            outputfile.write(chunk)
            left -= len(chunk)

    def log_message(self, fmt, *args):
        print(f"{self.client_address[0]}  {fmt % args}")


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))  # paket gönderilmez, sadece arayüz seçilir
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", help="catalog.json ve pkg dosyalarının olduğu klasör")
    ap.add_argument("--port", type=int, default=8000)
    args = ap.parse_args()

    root = os.path.abspath(args.folder)
    if not os.path.isfile(os.path.join(root, "catalog.json")):
        print("UYARI: catalog.json yok. Önce: python make_catalog.py", root)
    handler = functools.partial(RangeHandler, directory=root)
    server = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), handler)
    print(f"Sunucu açık: {root}")
    print("PS4'te /data/pkgstore/config.ini içine şunu yazın:")
    print(f"  catalog_url=http://{lan_ip()}:{args.port}/catalog.json")
    print("Durdurmak için Ctrl+C")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
