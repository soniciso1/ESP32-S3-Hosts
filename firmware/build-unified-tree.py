"""Assemble ONE unified site: poopsploit + p2jb (+ umtx2), one firmware image, one
SSID, firmware-autodetected landing page - matching the architecture proven by
owendswang/ps5-webkit-autoloader-esp32, using OUR OWN poopsploit/p2jb builds.

Layout (why it's shaped this way):

    /index.html          firmware-detect landing: routes to poopsploit/ or p2jb/ or umtx2/
    /offsets/             poopsploit's firmware-offset scripts. poops.html fetches
                          "../offsets/<fw>.js" - one level above ITS OWN directory - so
                          this must sit at the tree ROOT, sibling to /poopsploit/.
    /payloads/            ONE shared ELF pool. poops.html fetches "../payloads/<name>"
                          (same reasoning - root, sibling to /poopsploit/) and p2jb's
                          server-side relay (https_srv.c) hardcodes this exact path.
                          Sharing it here, instead of a copy per site, is the actual
                          size win over the old per-site-image approach.
    /poopsploit/          poops.html + its sibling JS/css/html - unmodified paths
                          (poops.html already expects "../offsets/", "../payloads/").
    /p2jb/                p2jb.html + its own offsets/, ui/, siblings - p2jb references
                          "offsets/" and "ui/" WITHOUT "../", so its offsets stay
                          nested here rather than being merged into the shared /offsets/.
    /umtx2/                copied wholesale from ps5-webkit-autoloader-esp32 (MIT-ish,
                          see that repo's LICENSE) - self-contained, own offsets/psfree/.

Payload arrays in poops.html / p2jb.html / elf.html are ALREADY stripped of
etaHEN.elf / pldmgr.elf / autoloader.elf (done directly on sites-live/ earlier).
The big 3 .elf files themselves are already deleted from sites-live/ too.

Media: only the oversized GIFs get ffmpeg-recompressed (fps8, half-size, 64-colour
palette); everything else (ui/*.png etc.) is kept verbatim - stripping small functional
images is what caused the blank-page/dead-links regression earlier.

    python build-unified-tree.py                 # poopsploit + p2jb + umtx2
    python build-unified-tree.py --no-umtx2       # skip the known-broken umtx2 (upstream
                                                   # marks it "System out of memory")
"""
import argparse, os, shutil, subprocess

SL = r"E:\01-EXPLOIT-HOST\ps-exploit-host\sites-live"
REF_UMTX2 = (r"E:\TEMP\claude\C--WINDOWS-system32\275ed847-eba4-46ad-81db-04aa5a1c0f4b"
             r"\scratchpad\ps5-webkit-autoloader-esp32\autoloader\app\0.4.0\umtx2")
OUT = r"E:\01-EXPLOIT-HOST\esp32\manuals-ap\sites\unified"
GIF_THRESHOLD = 500 * 1024

def recompress_gif(src, dst):
    vf = ("fps=8,scale=iw*0.5:ih*0.5:flags=lanczos,split[s0][s1];"
          "[s0]palettegen=max_colors=64[p];[s1][p]paletteuse=dither=bayer")
    r = subprocess.run(["ffmpeg", "-y", "-i", src, "-vf", vf, dst], capture_output=True)
    return r.returncode == 0 and os.path.exists(dst) and os.path.getsize(dst) > 0

def copy_tree(src, dst, skip_dirs=(), gif_compress=True):
    total = 0
    gifs = gifs_saved = 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for f in files:
            if f.endswith((".orig", ".bak")):
                continue
            sp = os.path.join(root, f)
            rel = os.path.relpath(sp, src)
            dp = os.path.join(dst, rel)
            os.makedirs(os.path.dirname(dp), exist_ok=True)
            ext = os.path.splitext(f)[1].lower()
            sz = os.path.getsize(sp)
            if gif_compress and ext == ".gif" and sz > GIF_THRESHOLD:
                if recompress_gif(sp, dp):
                    gifs += 1; gifs_saved += sz - os.path.getsize(dp)
                    total += os.path.getsize(dp)
                    continue
            shutil.copy2(sp, dp)
            total += sz
    return total, gifs, gifs_saved

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-umtx2", action="store_true")
    ap.add_argument("--gzip", action="store_true", help="gzip text assets (BREAKS ES modules on PS5 WebKit - leave off)")
    a = ap.parse_args()

    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)

    print("building unified tree -> %s\n" % OUT)

    idx_src = r"E:\01-EXPLOIT-HOST\esp32\manuals-ap\index-src\index.html"
    shutil.copy2(idx_src, os.path.join(OUT, "index.html"))
    print("  index.html     %6.2f KB  (firmware-detect landing)" %
          (os.path.getsize(idx_src) / 1024))

    # /offsets/  (poopsploit's, shared at root per poops.html's "../offsets/")
    t, g, s = copy_tree(os.path.join(SL, "poopsploit", "offsets"), os.path.join(OUT, "offsets"))
    print("  offsets/       %6.2f MB" % (t / 1048576))

    # /payloads/  (shared pool). Copy from the FULL restored site, then delete ONLY the
    # 3 big .elf FILES for flash space. The poops.html/p2jb.html menus are left 100%
    # intact (valid JS) - listing a payload whose file is absent just fails gracefully
    # if clicked. Editing those menu arrays is what broke the JS syntax before
    # (orphaned "{ title:" objects) and caused the endless "detecting..." stall.
    t, g, s = copy_tree(os.path.join(SL, "poopsploit", "payloads"), os.path.join(OUT, "payloads"))
    for big in ("etaHEN.elf", "pldmgr.elf", "autoloader.elf"):
        bp = os.path.join(OUT, "payloads", big)
        if os.path.isfile(bp):
            os.remove(bp)
    print("  payloads/      %6.2f MB  (%d files, 3 big ELFs omitted for space; menus untouched)" %
          (sum(os.path.getsize(os.path.join(OUT,"payloads",f)) for f in os.listdir(os.path.join(OUT,"payloads")))/1048576,
           len(os.listdir(os.path.join(OUT, "payloads")))))

    # /poopsploit/  (poops.html + siblings - the INNER poopsploit/poopsploit/ dir)
    t, g, s = copy_tree(os.path.join(SL, "poopsploit", "poopsploit"), os.path.join(OUT, "poopsploit"))
    print("  poopsploit/    %6.2f MB  (%d gifs recompressed, saved %.2f MB)" %
          (t / 1048576, g, s / 1048576))

    # /p2jb/  (everything except its own payloads/ [now shared at root] and api/
    # [now server-side in C, no static files needed])
    t, g, s = copy_tree(os.path.join(SL, "p2jb"), os.path.join(OUT, "p2jb"),
                        skip_dirs=("payloads", "api"))
    print("  p2jb/          %6.2f MB  (%d gifs recompressed, saved %.2f MB)" %
          (t / 1048576, g, s / 1048576))

    if not a.no_umtx2:
        t, g, s = copy_tree(REF_UMTX2, os.path.join(OUT, "umtx2"), gif_compress=False)
        print("  umtx2/         %6.2f MB  (upstream marks this range NOT SUPPORTED -"
              " 'System out of memory'; included per request, caveat stands)" % (t / 1048576))

    # gzip text assets in place (delete the plain original). The ESP serves foo.gz with
    # Content-Encoding: gzip; the console decompresses transparently. This shrinks the
    # big transfers - poops.js is 293 KB raw, ~70 KB gzipped - which is the difference
    # between a reliable load and a stall over the marginal AP link, and it saves flash.
    # .elf/.bin payloads are already compressed formats and are left raw.
    if not a.gzip:
        print("  gzip DISABLED - PS5 WebKit fails ES-module imports served gzipped")
    import gzip as _gz
    GZ_EXT = () if not a.gzip else (".js", ".html", ".htm", ".css", ".json", ".svg", ".map", ".txt")
    gzc = 0; raw = 0; comp = 0
    for dp, _, fs in os.walk(OUT):
        for f in fs:
            if not f.lower().endswith(GZ_EXT):
                continue
            p = os.path.join(dp, f)
            data = open(p, "rb").read()
            with _gz.open(p + ".gz", "wb", compresslevel=9) as g:
                g.write(data)
            raw += len(data); comp += os.path.getsize(p + ".gz")
            os.remove(p)
            gzc += 1
    print("\n  gzipped %d text files: %.2f MB -> %.2f MB" % (gzc, raw / 1048576, comp / 1048576))

    total = sum(os.path.getsize(os.path.join(dp, f))
                for dp, _, fs in os.walk(OUT) for f in fs)
    print("  TOTAL on flash: %.2f MB" % (total / 1048576))

if __name__ == "__main__":
    main()
