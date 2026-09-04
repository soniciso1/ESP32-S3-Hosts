"""Assemble a P2JB-ONLY site tree for a single-site ESP image (SSID PS5-p2jb).

Why this shape (learned from the unified build + the poopsploit HW jailbreak):

    /index.html          P2JB firmware-detect landing (index-src/index-p2jb.html):
                         routes 12.00-12.70 to p2jb/p2jb.html, else a manual P2JB button.
    /p2jb/               p2jb.html + its JS/css/html + offsets/ + ui/. P2JB references
                         "offsets/", "ui/", "payloads/" WITHOUT "../", so they stay
                         NESTED here (unlike poopsploit, which reaches "../offsets/").
    /p2jb/payloads/      P2JB's OWN exploit blobs, fetched CLIENT-SIDE by p2jb_poops.js
                         via same-origin XHR: G.P2JB_KEXP_URL="payloads/kexp_2026_05_25.bin"
                         and G.P2JB_ELF_URL="payloads/elfldr-ps5-1360.elf". These MUST
                         live here or the exploit 404s on its own kernel shellcode/elfldr.
                         Kept minimal: just those two files (~416 KB).
    /payloads/           The user-facing payload MENU pool. p2jb.html POSTs
                         "api/payload/<name>"; https_srv.c's relay matches that at ANY
                         depth (strstr) and ALWAYS reads from SITE_MOUNT "/payloads/"
                         = the ROOT pool. The 3 big ELFs (etaHEN/pldmgr/autoloader) are
                         omitted for flash space; the p2jb.html menu arrays are left 100%
                         intact (a listed-but-absent payload just fails gracefully if
                         clicked - editing those arrays is what broke the JS before).

    python build-p2jb-tree.py
"""
import os, shutil, subprocess

SL   = r"E:\01-EXPLOIT-HOST\ps-exploit-host\sites-live"
IDX  = r"E:\01-EXPLOIT-HOST\esp32\manuals-ap\index-src\index-p2jb.html"
OUT  = r"E:\01-EXPLOIT-HOST\esp32\manuals-ap\sites\p2jb-esp"
GIF_THRESHOLD = 500 * 1024
BIG_ELFS = ("etaHEN.elf", "pldmgr.elf", "autoloader.elf")
# P2JB's own exploit blobs - the only files the NESTED /p2jb/payloads/ needs.
P2JB_OWN = ("kexp_2026_05_25.bin", "elfldr-ps5-1360.elf")

def recompress_gif(src, dst):
    vf = ("fps=8,scale=iw*0.5:ih*0.5:flags=lanczos,split[s0][s1];"
          "[s0]palettegen=max_colors=64[p];[s1][p]paletteuse=dither=bayer")
    r = subprocess.run(["ffmpeg", "-y", "-i", src, "-vf", vf, dst], capture_output=True)
    return r.returncode == 0 and os.path.exists(dst) and os.path.getsize(dst) > 0

def copy_tree(src, dst, skip_dirs=(), gif_compress=True):
    total = gifs = gifs_saved = 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for f in files:
            if f.endswith((".orig", ".bak")):
                continue
            sp = os.path.join(root, f)
            dp = os.path.join(dst, os.path.relpath(sp, src))
            os.makedirs(os.path.dirname(dp), exist_ok=True)
            ext = os.path.splitext(f)[1].lower()
            sz = os.path.getsize(sp)
            if gif_compress and ext == ".gif" and sz > GIF_THRESHOLD and recompress_gif(sp, dp):
                gifs += 1; gifs_saved += sz - os.path.getsize(dp)
                total += os.path.getsize(dp); continue
            shutil.copy2(sp, dp); total += sz
    return total, gifs, gifs_saved

def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    print("building p2jb-only tree -> %s\n" % OUT)

    shutil.copy2(IDX, os.path.join(OUT, "index.html"))
    print("  index.html     %6.2f KB  (P2JB firmware-detect landing)" %
          (os.path.getsize(IDX) / 1024))

    # /p2jb/  (everything except payloads/ [handled below] and api/ [server-side C]).
    t, g, s = copy_tree(os.path.join(SL, "p2jb"), os.path.join(OUT, "p2jb"),
                        skip_dirs=("payloads", "api"))
    print("  p2jb/          %6.2f MB  (%d gifs recompressed, saved %.2f MB)" %
          (t / 1048576, g, s / 1048576))

    # /p2jb/payloads/  = ONLY P2JB's own exploit blobs (client-side XHR reads these).
    nest = os.path.join(OUT, "p2jb", "payloads"); os.makedirs(nest, exist_ok=True)
    nb = 0
    for f in P2JB_OWN:
        src = os.path.join(SL, "p2jb", "payloads", f)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(nest, f)); nb += os.path.getsize(src)
        else:
            print("  !! MISSING p2jb own blob: %s" % f)
    print("  p2jb/payloads/ %6.2f KB  (%s - fetched client-side by p2jb_poops.js)" %
          (nb / 1024, ", ".join(P2JB_OWN)))

    # /payloads/  = the MENU relay pool (https_srv.c reads SITE_MOUNT/payloads/<name>),
    # everything from p2jb/payloads MINUS the 3 big ELFs.
    root_pl = os.path.join(OUT, "payloads"); os.makedirs(root_pl, exist_ok=True)
    src_pl = os.path.join(SL, "p2jb", "payloads")
    tot = n = 0
    for f in sorted(os.listdir(src_pl)):
        if f in BIG_ELFS:
            continue
        shutil.copy2(os.path.join(src_pl, f), os.path.join(root_pl, f))
        tot += os.path.getsize(os.path.join(root_pl, f)); n += 1
    print("  payloads/      %6.2f MB  (%d files, 3 big ELFs omitted; menu untouched)" %
          (tot / 1048576, n))

    # NO gzip - PS5 WebKit fails ES-module imports served Content-Encoding: gzip.
    print("  gzip DISABLED - PS5 WebKit fails ES-module imports served gzipped")

    total = sum(os.path.getsize(os.path.join(dp, f))
                for dp, _, fs in os.walk(OUT) for f in fs)
    print("\n  TOTAL on flash: %.2f MB" % (total / 1048576))

if __name__ == "__main__":
    main()
