"""Codec configurations for 32-bit ARM Windows, which Chromium does not carry.

Usage: gen-codec-configs.py <chromium src> [<ffmpeg config dir>]

Recreates the files Venetium's deps/arm-inputs.tsv describes, by the routes it
gives, and checks each against the sha256 recorded there when Venetium's tree
is beside this one:

  dav1d   config/win/arm/config.h
          = config/win/arm64/config.h with ARCH_AARCH64 1->0, ARCH_ARM 0->1
  libaom  source/config/win/arm-neon/config/*
          = linux/arm-neon/config/*; aom_config.h with CONFIG_GCC,
            HAVE_PTHREAD_H and HAVE_UNISTD_H 0 (cmake_update.sh's
            convert_to_windows)
  libvpx  source/config/win/arm-neon/*
          = linux/arm-neon/*; vpx_config.h with INLINE __inline,
            HAVE_PTHREAD_H/HAVE_PTHREAD_SETNAME_NP 0, CONFIG_GCC 0,
            CONFIG_MSVS 1 (CONFIG_MULTITHREAD stays 1: vp8's decoder threads
            need it); vpx_config.asm rewritten from it for COFF
            (ads2gas_apple's form, no .note.GNU-stack); vpx_config.c with the
            win32 target in its configuration string
  ffmpeg  chromium/config/Chrome/win/arm-neon/*
          copied from <ffmpeg config dir>, the output of Venetium's
          deps/ffmpeg-win-arm.sh (FFmpeg's own configure; venetium-w10m keeps
          the result in deps/ffmpeg/Chrome/win/arm-neon)
"""
import hashlib
import os
import re
import shutil
import sys


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8")


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(text.encode("utf-8"))


def set_define(text, name, value):
    pattern = re.compile(r"^(#define %s)(\s+)\S+$" % re.escape(name), re.M)
    if not pattern.search(text):
        raise SystemExit("no #define %s" % name)
    return pattern.sub(lambda m: m.group(1) + m.group(2) + value, text, count=1)


def dav1d(src):
    base = os.path.join(src, "third_party/dav1d/config/win")
    text = read(os.path.join(base, "arm64/config.h"))
    text = set_define(text, "ARCH_AARCH64", "0")
    text = set_define(text, "ARCH_ARM", "1")
    out = os.path.join(base, "arm/config.h")
    write(out, text)
    return [out]


def libaom(src):
    base = os.path.join(src, "third_party/libaom/source/config")
    out = []
    for name in sorted(os.listdir(os.path.join(base, "linux/arm-neon/config"))):
        text = read(os.path.join(base, "linux/arm-neon/config", name))
        if name == "aom_config.h":
            # cmake_update.sh's convert_to_windows, sed for sed: its
            # replacement "\1 0" keeps the spacing and adds one more space.
            for define, old, new in (("HAVE_PTHREAD_H", "1", "0"),
                                     ("HAVE_UNISTD_H", "1", "0"),
                                     ("CONFIG_GCC", "1", "0"),
                                     ("CONFIG_MSVS", "0", "1")):
                text = re.sub(r"(#define\s%s\s*)%s" % (define, old),
                              lambda m: m.group(1) + " " + new, text, count=1)
        path = os.path.join(base, "win/arm-neon/config", name)
        write(path, text)
        out.append(path)
    return out


def libvpx(src):
    base = os.path.join(src, "third_party/libvpx/source/config")
    linux = os.path.join(base, "linux/arm-neon")
    win = os.path.join(base, "win/arm-neon")
    out = []
    header = read(os.path.join(linux, "vpx_config.h"))
    header = re.sub(r"^#define INLINE(\s+)inline$", r"#define INLINE\1__inline",
                    header, count=1, flags=re.M)
    for define, value in (("HAVE_PTHREAD_H", "0"),
                          ("HAVE_PTHREAD_SETNAME_NP", "0"),
                          ("CONFIG_GCC", "0"), ("CONFIG_MSVS", "1")):
        header = set_define(header, define, value)
    values = dict(re.findall(r"^#define (\w+)\s+(\d+)$", header, re.M))
    for name in sorted(os.listdir(linux)):
        text = read(os.path.join(linux, name))
        if name == "vpx_config.h":
            text = header
        elif name == "vpx_config.asm":
            lines = []
            for line in text.splitlines():
                if ".note.GNU-stack" in line:
                    continue
                m = re.match(r"^\.equ (\w+) ,  (\d+)$", line)
                if m and m.group(1) in values:
                    line = ".equ %s ,  %s" % (m.group(1), values[m.group(1)])
                lines.append(line)
            text = "\n".join(lines) + "\n"
            text = text.replace("using the ads2gas.pl script.",
                                "using the ads2gas_apple.pl script.\n")
        elif name == "vpx_config.c":
            text = text.replace("--target=armv7-linux-gcc", "--target=armv7-win32-vs15")
        path = os.path.join(win, name)
        write(path, text)
        out.append(path)
    return out


def ffmpeg(src, generated):
    dest = os.path.join(src, "third_party/ffmpeg/chromium/config/Chrome/win/arm-neon")
    out = []
    for root, _, files in os.walk(generated):
        for name in files:
            rel = os.path.relpath(os.path.join(root, name), generated)
            path = os.path.join(dest, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            shutil.copyfile(os.path.join(root, name), path)
            out.append(path)
    return sorted(out)


def recorded_hashes(src):
    """Venetium's deps/arm-inputs.tsv, if Venetium's repository is beside us."""
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    tsv = os.path.join(os.path.dirname(here), "venetium", "deps", "arm-inputs.tsv")
    hashes = {}
    if os.path.exists(tsv):
        for line in read(tsv).splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) >= 3:
                hashes[parts[1]] = parts[2]
    return hashes


def main():
    src = os.path.abspath(sys.argv[1])
    files = dav1d(src) + libaom(src) + libvpx(src)
    if len(sys.argv) > 2:
        files += ffmpeg(src, sys.argv[2])
    hashes = recorded_hashes(src)
    same = differ = unknown = 0
    for path in files:
        rel = os.path.relpath(path, src).replace("\\", "/")
        digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
        want = hashes.get(rel)
        if want is None:
            unknown += 1
            state = "new"
        elif want == digest:
            same += 1
            state = "= Venetium"
        else:
            differ += 1
            state = "differs from Venetium's"
        print("  %-80s %s" % (rel, state))
    print("%d files: %d as Venetium recorded them, %d different, %d not recorded"
          % (len(files), same, differ, unknown))


if __name__ == "__main__":
    main()
