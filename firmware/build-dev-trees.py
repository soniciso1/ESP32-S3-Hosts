"""Assemble the two -dev ESP site trees:

  POOPS-DEV  (poopsploit devkit, 7.00-12.00)  -> sites/poops-dev-esp
  P2JB-DEV   (p2jb devkit,      12.00-12.70)  -> sites/p2jb-dev-esp

Same shapes proven by the retail images this session:

  POOPS-DEV (poopsploit-style, single root payload pool):
    /index.html      index-poops-dev.html landing (routes 7.00-12.00)
    /offsets/        poopsploit-dev's offsets (poops.html fetches "../offsets/")
    /payloads/       poopsploit-dev's payload pool minus 3 big ELFs. poops.js fetches
                     its OWN kexp/elfldr from "../payloads/" (root) too, so ONE pool.
    /poopsploit/     poops.html + siblings (self-contained; only needs ../offsets ../payloads)

  P2JB-DEV (p2jb-style, dual payloads):
    /index.html      index-p2jb-dev.html landing (routes 12.00-12.70, v=204)
    /p2jb/           p2jb.html + js + offsets/ + ui/ (p2jb-dev is flat -> nests here)
    /p2jb/payloads/  kexp + elfldr only (client-side XHR: G.P2JB_KEXP_URL="payloads/..")
    /payloads/       menu relay pool minus 3 big ELFs (https_srv.c reads SITE_MOUNT/payloads)

No gzip (PS5 WebKit fails gzipped ES-module imports).

  python build-dev-trees.py
"""
import os, shutil, subprocess

IDX_DIR   = r"E:\01-EXPLOIT-HOST\esp32\manuals-ap\index-src"
POOPS_SRC = r"E:\01-EXPLOIT-HOST\ps-exploit-host\publish\poopsploit-dev"     # real devkit poopsploit
P2JB_SRC  = r"E:\01-EXPLOIT-HOST\ps-exploit-host\sites-live\p2jb-dev"        # real devkit p2jb
OUT_DIR   = r"E:\01-EXPLOIT-HOST\esp32\manuals-ap\sites"
GIF_THRESHOLD = 500 * 1024
BIG_ELFS  = ("etaHEN.elf", "pldmgr.elf", "autoloader.elf")
P2JB_OWN  = ("kexp_2026_05_25.bin", "elfldr-ps5-1360.elf")

def recompress_gif(src, dst):
    vf = ("fps=8,scale=iw*0.5:ih*0.5:flags=lanczos,split[s0][s1];"
          "[s0]palettegen=max_colors=64[p];[s1][p]paletteuse=dither=bayer")
    r = subprocess.run(["ffmpeg", "-y", "-i", src, "-vf", vf, dst], capture_output=True)
    return r.returncode == 0 and os.path.exists(dst) and os.path.getsize(dst) > 0

def copy_tree(src, dst, skip_dirs=(), gif_compress=True):
    total = gifs = saved = 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for f in files:
            if f.endswith((".orig", ".bak")):
                continue
            sp = os.path.join(root, f)
            dp = os.path.join(dst, os.path.relpath(sp, src))
            os.makedirs(os.path.dirname(dp), exist_ok=True)
            sz = os.path.getsize(sp)
            if gif_compress and f.lower().endswith(".gif") and sz > GIF_THRESHOLD and recompress_gif(sp, dp):
                gifs += 1; saved += sz - os.path.getsize(dp); total += os.path.getsize(dp); continue
            shutil.copy2(sp, dp); total += sz
    return total, gifs, saved

def payload_pool(src_pl, dst_pl):
    os.makedirs(dst_pl, exist_ok=True)
    tot = n = 0
    for f in sorted(os.listdir(src_pl)):
        if f in BIG_ELFS:
            continue
        shutil.copy2(os.path.join(src_pl, f), os.path.join(dst_pl, f))
        tot += os.path.getsize(os.path.join(dst_pl, f)); n += 1
    return tot, n

def fresh(p):
    if os.path.isdir(p):
        shutil.rmtree(p)
    os.makedirs(p)

def build_poops_dev():
    out = os.path.join(OUT_DIR, "poops-dev-esp"); fresh(out)
    print("== POOPS-DEV -> %s ==" % out)
    shutil.copy2(os.path.join(IDX_DIR, "index-poops-dev.html"), os.path.join(out, "index.html"))
    t, _, _ = copy_tree(os.path.join(POOPS_SRC, "offsets"), os.path.join(out, "offsets"))
    print("  offsets/     %6.2f MB" % (t / 1048576))
    tot, n = payload_pool(os.path.join(POOPS_SRC, "payloads"), os.path.join(out, "payloads"))
    print("  payloads/    %6.2f MB  (%d files, 3 big ELFs omitted; menu untouched)" % (tot / 1048576, n))
    t, g, s = copy_tree(os.path.join(POOPS_SRC, "poopsploit"), os.path.join(out, "poopsploit"))
    print("  poopsploit/  %6.2f MB  (%d gifs recompressed, saved %.2f MB)" % (t / 1048576, g, s / 1048576))
    _total(out)

def build_p2jb_dev():
    out = os.path.join(OUT_DIR, "p2jb-dev-esp"); fresh(out)
    print("== P2JB-DEV -> %s ==" % out)
    shutil.copy2(os.path.join(IDX_DIR, "index-p2jb-dev.html"), os.path.join(out, "index.html"))
    t, g, s = copy_tree(P2JB_SRC, os.path.join(out, "p2jb"), skip_dirs=("payloads", "api"))
    print("  p2jb/        %6.2f MB  (%d gifs recompressed, saved %.2f MB)" % (t / 1048576, g, s / 1048576))
    nest = os.path.join(out, "p2jb", "payloads"); os.makedirs(nest, exist_ok=True)
    nb = 0
    for f in P2JB_OWN:
        src = os.path.join(P2JB_SRC, "payloads", f)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(nest, f)); nb += os.path.getsize(src)
        else:
            print("  !! MISSING p2jb-dev own blob: %s" % f)
    print("  p2jb/payloads/ %4.2f KB  (%s - client-side XHR)" % (nb / 1024, ", ".join(P2JB_OWN)))
    tot, n = payload_pool(os.path.join(P2JB_SRC, "payloads"), os.path.join(out, "payloads"))
    print("  payloads/    %6.2f MB  (%d files, 3 big ELFs omitted; menu untouched)" % (tot / 1048576, n))
    _total(out)

def _total(out):
    total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(out) for f in fs)
    print("  TOTAL on flash: %.2f MB\n" % (total / 1048576))

if __name__ == "__main__":
    build_poops_dev()
    build_p2jb_dev()
