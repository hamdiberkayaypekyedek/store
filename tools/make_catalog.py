#!/usr/bin/env python3
"""Bir klasördeki .pkg dosyalarından PKG Mağaza için catalog.json üretir.

Her pkg'nin içindeki param.sfo'dan başlık/sürüm/ID, icon0.png'den kapak ve
(varsa) pic1.png'den arka plan görseli çıkarılır.

    python make_catalog.py D:\\pkgler --name "Mağazam"
"""
import argparse
import json
import os
import struct
import urllib.parse

ENTRY_PARAM_SFO = 0x1000
ENTRY_ICON0 = 0x1200
ENTRY_PIC1 = (0x1241, 0x1220)  # pic1.png, yoksa pic0.png
PNG_MAGIC = b"\x89PNG"

CATEGORY_NAMES = {"gd": "Oyunlar & Uygulamalar", "gp": "Güncellemeler", "ac": "Ek İçerikler"}
PACKAGE_TYPES = {"gd": "PS4GD", "gp": "PS4DP", "ac": "PS4AC"}


def read_pkg(path):
    with open(path, "rb") as f:
        head = f.read(0x20)
        if len(head) < 0x20 or head[:4] != b"\x7fCNT":
            raise ValueError("PS4 pkg dosyası değil")
        count = struct.unpack(">I", head[0x10:0x14])[0]
        table = struct.unpack(">I", head[0x18:0x1C])[0]
        f.seek(table)
        raw = f.read(count * 32)
        entries = {}
        for i in range(count):
            eid, _name, _f1, _f2, off, size = struct.unpack(">IIIIII", raw[i * 32:i * 32 + 24])
            entries[eid] = (off, size)

        def read(eid):
            if eid not in entries:
                return None
            off, size = entries[eid]
            f.seek(off)
            return f.read(size)

        sfo = parse_sfo(read(ENTRY_PARAM_SFO) or b"")
        icon = read(ENTRY_ICON0)
        pic = None
        for eid in ENTRY_PIC1:
            data = read(eid)
            if data and data.startswith(PNG_MAGIC):
                pic = data
                break
        if icon and not icon.startswith(PNG_MAGIC):
            icon = None  # şifreli girdi
        return sfo, icon, pic


def parse_sfo(b):
    if len(b) < 20 or b[:4] != b"\x00PSF":
        return {}
    key_tab, data_tab, count = struct.unpack("<III", b[8:20])
    out = {}
    for i in range(count):
        ko, fmt, ln, _mx, do = struct.unpack("<HHIII", b[20 + i * 16:36 + i * 16])
        start = key_tab + ko
        key = b[start:b.index(b"\x00", start)].decode("utf-8", "replace")
        data = b[data_tab + do:data_tab + do + ln]
        if fmt == 0x0404:
            out[key] = struct.unpack("<I", data[:4])[0]
        else:
            out[key] = data.rstrip(b"\x00").decode("utf-8", "replace")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", help=".pkg dosyalarının olduğu klasör (alt klasörler dahil)")
    ap.add_argument("--name", default="PKG Mağaza", help="mağaza adı")
    args = ap.parse_args()

    root = os.path.abspath(args.folder)
    covers = os.path.join(root, "covers")
    os.makedirs(covers, exist_ok=True)
    categories = {}

    for dirpath, _dirs, files in os.walk(root):
        for fn in sorted(files):
            if not fn.lower().endswith(".pkg"):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            try:
                sfo, icon, pic = read_pkg(path)
            except Exception as e:  # noqa: BLE001
                print(f"atlandı: {rel} ({e})")
                continue

            title_id = str(sfo.get("TITLE_ID", "")) or os.path.splitext(fn)[0]
            cat = str(sfo.get("CATEGORY", "gd"))
            ver = str(sfo.get("APP_VER") or sfo.get("VERSION") or "")
            slug = f"{title_id}_{cat}_{ver}".replace(".", "_")
            item = {
                "id": title_id,
                "title": str(sfo.get("TITLE", fn)),
                "version": ver,
                "size": os.path.getsize(path),
                "pkg": urllib.parse.quote(rel),
                "package_type": PACKAGE_TYPES.get(cat, "PS4GD"),
                "description": str(sfo.get("CONTENT_ID", "")),
            }
            if icon:
                with open(os.path.join(covers, slug + ".png"), "wb") as out:
                    out.write(icon)
                item["cover"] = f"covers/{slug}.png"
            if pic:
                with open(os.path.join(covers, slug + "_bg.png"), "wb") as out:
                    out.write(pic)
                item["background"] = f"covers/{slug}_bg.png"

            name = CATEGORY_NAMES.get(cat, "Diğer")
            categories.setdefault(name, []).append(item)
            print(f"eklendi: [{name}] {item['title']} ({rel})")

    catalog = {
        "store_name": args.name,
        "categories": [
            {"name": name, "items": sorted(items, key=lambda i: i["title"].lower())}
            for name, items in categories.items()
        ],
    }
    out_path = os.path.join(root, "catalog.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(catalog, f, ensure_ascii=False, indent=2)
    total = sum(len(c["items"]) for c in catalog["categories"])
    print(f"\n{total} içerik -> {out_path}")


if __name__ == "__main__":
    main()
