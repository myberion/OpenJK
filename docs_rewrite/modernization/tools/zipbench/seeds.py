# Test archives for the engine's pk3 reader: small ZIP files of the kinds a pk3 can be (stored and deflated files, a
# folder, a comment, no file at all), and ZIP64 ones: the game's own archives are not ZIP64, so these are the only
# ones that take that path. They seed fuzz_pk3.cpp and are read by pk3check.cpp.
# Usage: python -I seeds.py <folder> [many]
#   many: also an archive of 70,000 files, ZIP64 by its count (5 MB: for pk3check, too large to seed the fuzzer)
import os
import struct
import sys
import zipfile

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
text = b"".join(b"shader %d { map textures/wall%d.jpg }\n" % (i, i % 7) for i in range(400))


def make(name, files, comment=b""):
    path = os.path.join(out, name)
    with zipfile.ZipFile(path, "w") as z:
        for member, data, method in files:
            z.writestr(zipfile.ZipInfo(member), data, compress_type=method)
        z.comment = comment
    return path


def to_zip64(src):
    # The same archive with every entry's sizes and place moved to a ZIP64 field of its extra data (their 32-bit forms
    # all ones), and a ZIP64 end record with its locator before the end record, whose own fields are all ones: what a
    # tool writes for an archive of 4 GB or more, on a few hundred bytes.
    end = src.rindex(b"PK\x05\x06")
    count, _, cd_at = struct.unpack("<HII", src[end + 10:end + 20])
    cd = b""
    at = cd_at
    for _ in range(count):
        h = src[at:at + 46]
        csize, usize, n, m, k = struct.unpack("<IIHHH", h[20:34])
        local, = struct.unpack("<I", h[42:46])
        name = src[at + 46:at + 46 + n]
        extra = src[at + 46 + n:at + 46 + n + m] + struct.pack("<HHQQQ", 1, 24, usize, csize, local)
        comment = src[at + 46 + n + m:at + 46 + n + m + k]
        cd += h[:20] + struct.pack("<IIHH", 0xFFFFFFFF, 0xFFFFFFFF, n, len(extra)) + h[32:42] + struct.pack("<I", 0xFFFFFFFF) + name + extra + comment
        at += 46 + n + m + k
    end64 = struct.pack("<IQHHIIQQQQ", 0x06064B50, 44, 45, 45, 0, 0, count, count, len(cd), cd_at)
    locator = struct.pack("<IIQI", 0x07064B50, 0, cd_at + len(cd), 1)
    return src[:cd_at] + cd + end64 + locator + struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, 0xFFFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)


make("empty.zip", [])
make("stored.zip", [("a.txt", b"stored file", zipfile.ZIP_STORED)])
make("deflated.zip", [("scripts/a.shader", text, zipfile.ZIP_DEFLATED)])
mixed = make("mixed.zip", [("maps/", b"", zipfile.ZIP_STORED), ("maps/a.bin", bytes(range(256)) * 20, zipfile.ZIP_DEFLATED),
                           ("b.cfg", b"seta x 1\n", zipfile.ZIP_STORED), ("c.txt", text[:3000], zipfile.ZIP_DEFLATED)], b"a comment")
with open(mixed, "rb") as f:
    plain = f.read()
with open(os.path.join(out, "zip64.zip"), "wb") as f:
    f.write(to_zip64(plain))
if len(sys.argv) > 2 and sys.argv[2] == "many":
    make("many.zip", [("files/%05d.txt" % i, b"file %d\n" % i, zipfile.ZIP_STORED if i % 2 else zipfile.ZIP_DEFLATED) for i in range(70000)])
