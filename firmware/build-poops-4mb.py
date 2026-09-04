"""Minimal poopsploit tree for a 4 MB no-PSRAM ESP32 -> sites/poops-4mb.

Stripped to fit ~2.94 MB storage:
  - payloads/ = ONLY kexp + elfldr (no sendable-ELF menu pool)
  - poopsploit/ = code minus the big decoration gifs (a.gif 1MB, the two cat gifs, cat.jpg)
  - offsets/ 7.00-12.00 kept; landing kept
poops.js fetches its own kexp/elfldr from "../payloads/" (root), so one payload pool.
Dropped images 404 gracefully (cosmetic cats) - the menu JS is untouched.
"""
import os, shutil
SL   = r"E:\01-EXPLOIT-HOST\ps-exploit-host\sites-live\poopsploit"
IDX  = r"E:\01-EXPLOIT-HOST\esp32\manuals-ap\index-src\index-poops.html"
OUT  = r"E:\01-EXPLOIT-HOST\esp32\manuals-ap\sites\poops-4mb"
DROP = {"a.gif", "hovering-cats.gif", "mmhmm-cats-ps5.gif", "cat.jpg"}
OWN  = ("kexp_2026_05_25.bin", "elfldr-ps5-1360.elf",
        # menu ELFs that fit the ~1.8 MB free storage (big 3 + etaHEN omitted):
        "ftpsrv-ps5.elf", "shsrv-ps5.elf", "klogsrv-ps5.elf", "gdbsrv-ps5.elf", "bridge.elf")


def trim_menu(out):
    """Rewrite poops.html PAYLOADS to only entries whose .elf is actually in /payloads/,
    so the menu shows no dead tabs. Removes WHOLE 2-line objects (never a half-edit -> the
    orphaned-object SyntaxError that caused the endless 'detecting' stall)."""
    import re
    ph=os.path.join(out,"poopsploit","poops.html")
    present=set(os.listdir(os.path.join(out,"payloads")))
    html=open(ph,encoding="utf-8").read()
    m=re.search(r"(const PAYLOADS = Object\.freeze\(\[)(.*?)(\]\);)", html, re.S)
    if not m:
        print("  !! PAYLOADS array not found - menu left intact"); return
    body=m.group(2)
    # split into entries on '},' boundaries, keep those whose name file exists
    entries=re.findall(r"\{.*?\}", body, re.S)
    kept=[e for e in entries if any(('name: "%s"'%n) in e or ("name: '%s'"%n) in e for n in present)]
    body = ",\n".join("    " + e.strip() for e in kept)
    new = m.group(1) + "\n" + body + "\n" + m.group(3)
    html=html[:m.start()]+new+html[m.end():]
    open(ph,"w",encoding="utf-8").write(html)
    print("  menu trimmed: %d/%d entries kept (%s)" %
          (len(kept),len(entries),", ".join(sorted(present-{'kexp_2026_05_25.bin','elfldr-ps5-1360.elf'}))))

def sz(p): return sum(os.path.getsize(os.path.join(r,f)) for r,_,fs in os.walk(p) for f in fs)

def main():
    if os.path.isdir(OUT): shutil.rmtree(OUT)
    os.makedirs(OUT)
    shutil.copy2(IDX, os.path.join(OUT, "index.html"))
    # offsets
    shutil.copytree(os.path.join(SL, "offsets"), os.path.join(OUT, "offsets"))
    # payloads = kexp + elfldr only
    pd = os.path.join(OUT, "payloads"); os.makedirs(pd)
    for f in OWN:
        shutil.copy2(os.path.join(SL, "payloads", f), os.path.join(pd, f))
    # poopsploit code minus big decorations
    src = os.path.join(SL, "poopsploit"); dst = os.path.join(OUT, "poopsploit"); os.makedirs(dst)
    dropped = 0
    for f in os.listdir(src):
        if f in DROP:
            dropped += os.path.getsize(os.path.join(src, f)); continue
        s = os.path.join(src, f)
        if os.path.isfile(s): shutil.copy2(s, os.path.join(dst, f))
    trim_menu(OUT)
    print("  offsets   %6.2f KB" % (sz(os.path.join(OUT,"offsets"))/1024))
    print("  payloads  %6.2f KB  (kexp+elfldr only)" % (sz(pd)/1024))
    print("  poops     %6.2f KB  (dropped %.2f MB of gifs/jpg)" % (sz(dst)/1024, dropped/1048576))
    print("  TOTAL     %6.2f MB" % (sz(OUT)/1048576))

if __name__ == "__main__":
    main()
