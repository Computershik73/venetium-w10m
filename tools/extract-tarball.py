"""Unpack a Chromium source tarball on Windows.

Usage: extract-tarball.py <tarball.tar.xz> <dest dir>

The tarball's top directory (chromium-<version>/) is stripped, so the tree
lands directly in <dest dir>. Symbolic and hard links are not made as links:
git-for-windows checkouts of Chromium need symlink privileges for that, and
nothing the build reads cares. After the files are out, every link is
replaced by a copy of what it points to (a directory link by a copy of the
directory), in an order that lets links to links resolve.
"""
import os
import shutil
import sys
import tarfile


def main():
    tarball, dest = sys.argv[1], sys.argv[2]
    os.makedirs(dest, exist_ok=True)
    links = []
    n = 0
    with tarfile.open(tarball, mode="r|xz") as tf:
        for m in tf:
            parts = m.name.split("/", 1)
            if len(parts) < 2 or not parts[1]:
                continue
            rel = parts[1]
            out = os.path.join(dest, *rel.split("/"))
            if m.isdir():
                os.makedirs(out, exist_ok=True)
            elif m.issym() or m.islnk():
                if m.islnk():
                    target = m.linkname.split("/", 1)[1]
                else:
                    target = os.path.normpath(
                        os.path.join(os.path.dirname(rel), m.linkname)).replace("\\", "/")
                links.append((rel, target))
            elif m.isfile():
                os.makedirs(os.path.dirname(out), exist_ok=True)
                src = tf.extractfile(m)
                with open(out, "wb") as f:
                    shutil.copyfileobj(src, f, 1 << 20)
                n += 1
                if n % 20000 == 0:
                    print(f"{n} files", flush=True)
    print(f"{n} files, {len(links)} links", flush=True)

    pending = links
    while pending:
        left = []
        for rel, target in pending:
            out = os.path.join(dest, *rel.split("/"))
            src = os.path.join(dest, *target.split("/"))
            if os.path.isdir(src):
                shutil.copytree(src, out, dirs_exist_ok=True)
            elif os.path.isfile(src):
                os.makedirs(os.path.dirname(out), exist_ok=True)
                shutil.copyfile(src, out)
            else:
                left.append((rel, target))
        if len(left) == len(pending):
            for rel, target in left:
                print(f"unresolved link: {rel} -> {target}")
            break
        pending = left


if __name__ == "__main__":
    main()
