#!/usr/bin/env python3
"""
TTF binary parser - analyzes TTF fonts for PDF embedding.
Uses only struct module, no external libraries.
"""

import struct
import sys
import os

def read_u8(data, offset):
    return struct.unpack_from('>B', data, offset)[0]

def read_u16(data, offset):
    return struct.unpack_from('>H', data, offset)[0]

def read_i16(data, offset):
    return struct.unpack_from('>h', data, offset)[0]

def read_u32(data, offset):
    return struct.unpack_from('>I', data, offset)[0]

def read_i32(data, offset):
    return struct.unpack_from('>i', data, offset)[0]

def tag_to_str(data, offset):
    return data[offset:offset+4].decode('latin-1')

def analyze_ttf(path):
    print("=" * 70)
    print(f"FILE: {path}")
    print("=" * 70)

    with open(path, 'rb') as f:
        data = f.read()

    file_size = len(data)
    print(f"File size: {file_size} bytes ({file_size/1024:.1f} KB)")

    # --- Offset Table ---
    sfVersion = read_u32(data, 0)
    numTables  = read_u16(data, 4)
    # searchRange, entrySelector, rangeShift at 6,8,10
    print(f"sfVersion: 0x{sfVersion:08X}  numTables: {numTables}")

    # --- Table Directory ---
    tables = {}
    for i in range(numTables):
        base = 12 + i * 16
        tag    = tag_to_str(data, base)
        chksum = read_u32(data, base + 4)
        offset = read_u32(data, base + 8)
        length = read_u32(data, base + 12)
        tables[tag] = (offset, length, chksum)

    interesting = ['cmap','hmtx','head','hhea','maxp','post','glyf','loca','name','OS/2']
    print("\n--- Table offsets & lengths ---")
    for tag in interesting:
        if tag in tables:
            off, ln, _ = tables[tag]
            print(f"  {tag:<6}  offset={off:8d}  length={ln:8d}")
        else:
            print(f"  {tag:<6}  NOT PRESENT")

    # --- maxp ---
    if 'maxp' in tables:
        off, ln, _ = tables['maxp']
        version    = read_u32(data, off)
        numGlyphs  = read_u16(data, off + 4)
        print(f"\n--- maxp ---")
        print(f"  version:   0x{version:08X}")
        print(f"  numGlyphs: {numGlyphs}")
    else:
        numGlyphs = 0
        print("\nmaxp: NOT FOUND")

    # --- head ---
    if 'head' in tables:
        off, ln, _ = tables['head']
        unitsPerEm   = read_u16(data, off + 18)
        indexToLocFmt= read_i16(data, off + 50)
        print(f"\n--- head ---")
        print(f"  unitsPerEm:      {unitsPerEm}")
        print(f"  indexToLocFormat:{indexToLocFmt}  (0=short, 1=long)")
    else:
        unitsPerEm = 0
        indexToLocFmt = 0
        print("\nhead: NOT FOUND")

    # --- hhea ---
    if 'hhea' in tables:
        off, ln, _ = tables['hhea']
        ascender    = read_i16(data, off + 4)
        descender   = read_i16(data, off + 6)
        lineGap     = read_i16(data, off + 8)
        numOfLongHorMetrics = read_u16(data, off + 34)
        print(f"\n--- hhea ---")
        print(f"  ascender:   {ascender}")
        print(f"  descender:  {descender}")
        print(f"  lineGap:    {lineGap}")
        print(f"  numOfLongHorMetrics: {numOfLongHorMetrics}")
    else:
        numOfLongHorMetrics = 0
        print("\nhhea: NOT FOUND")

    # --- OS/2 ---
    if 'OS/2' in tables:
        off, ln, _ = tables['OS/2']
        os2_version       = read_u16(data, off)
        sTypoAscender     = read_i16(data, off + 68)
        sTypoDescender    = read_i16(data, off + 70)
        sTypoLineGap      = read_i16(data, off + 72)
        usWinAscent       = read_u16(data, off + 74)
        usWinDescent      = read_u16(data, off + 76)
        # fsType at offset 8 (2 bytes)
        fsType            = read_u16(data, off + 8)
        print(f"\n--- OS/2 ---")
        print(f"  version:        {os2_version}")
        print(f"  fsType:         0x{fsType:04X}")
        print(f"  sTypoAscender:  {sTypoAscender}")
        print(f"  sTypoDescender: {sTypoDescender}")
        print(f"  sTypoLineGap:   {sTypoLineGap}")
        print(f"  usWinAscent:    {usWinAscent}")
        print(f"  usWinDescent:   {usWinDescent}")
    else:
        print("\nOS/2: NOT FOUND")

    # --- cmap ---
    print(f"\n--- cmap ---")
    cmap_glyph = {}   # codepoint -> glyph_id
    if 'cmap' not in tables:
        print("  cmap: NOT FOUND")
    else:
        cmap_off, cmap_len, _ = tables['cmap']
        cmap_version    = read_u16(data, cmap_off)
        numSubtables    = read_u16(data, cmap_off + 2)
        print(f"  cmap version: {cmap_version}, numSubtables: {numSubtables}")

        best_subtable_offset = None
        best_priority = -1

        for i in range(numSubtables):
            base = cmap_off + 4 + i * 8
            platform  = read_u16(data, base)
            encoding  = read_u16(data, base + 2)
            sub_off   = read_u32(data, base + 4)
            fmt       = read_u16(data, cmap_off + sub_off)
            print(f"    subtable {i}: platform={platform} encoding={encoding} format={fmt} sub_offset={sub_off}")

            # Priority: platform 3 enc 1 format 4 = Unicode BMP Windows (best for PDF)
            # Then platform 0 format 4
            priority = -1
            if platform == 3 and encoding == 1 and fmt == 4:
                priority = 10
            elif platform == 0 and fmt == 4:
                priority = 5
            elif platform == 3 and encoding == 0 and fmt == 4:
                priority = 3
            elif fmt == 4:
                priority = 1

            if priority > best_priority:
                best_priority = priority
                best_subtable_offset = cmap_off + sub_off
                best_platform = platform
                best_encoding = encoding

        if best_subtable_offset is None:
            print("  No format 4 subtable found!")
        else:
            print(f"  Selected subtable: platform={best_platform} encoding={best_encoding} at file offset {best_subtable_offset}")
            # Parse format 4
            off4 = best_subtable_offset
            fmt4     = read_u16(data, off4)
            length4  = read_u16(data, off4 + 2)
            language = read_u16(data, off4 + 4)
            segCount2= read_u16(data, off4 + 6)
            segCount = segCount2 // 2
            print(f"  format4: segCount={segCount}")

            endCounts   = [read_u16(data, off4 + 14 + i*2) for i in range(segCount)]
            # reserved pad at off4+14+segCount*2
            startCounts = [read_u16(data, off4 + 16 + segCount*2 + i*2) for i in range(segCount)]
            idDeltas    = [read_i16(data, off4 + 16 + segCount*4 + i*2) for i in range(segCount)]
            idRangeOffsets_pos = off4 + 16 + segCount*6
            idRangeOffsets = [read_u16(data, idRangeOffsets_pos + i*2) for i in range(segCount)]

            def get_glyph_id(codepoint):
                for i in range(segCount):
                    if codepoint <= endCounts[i]:
                        if codepoint < startCounts[i]:
                            return 0
                        if idRangeOffsets[i] == 0:
                            return (codepoint + idDeltas[i]) & 0xFFFF
                        else:
                            idx_pos = idRangeOffsets_pos + i*2 + idRangeOffsets[i] + (codepoint - startCounts[i]) * 2
                            gid = read_u16(data, idx_pos)
                            if gid == 0:
                                return 0
                            return (gid + idDeltas[i]) & 0xFFFF
                return 0

            # Code points of interest
            codepoints = [
                (0x0020, 'U+0020 space'),
                (0x0041, 'U+0041 A'),
                (0x0061, 'U+0061 a'),
                (0x00B7, 'U+00B7 middle dot'),
                (0x0105, 'U+0105 ą'),
                (0x0107, 'U+0107 ć'),
                (0x0119, 'U+0119 ę'),
                (0x0142, 'U+0142 ł'),
                (0x0144, 'U+0144 ń'),
                (0x00F3, 'U+00F3 ó'),
                (0x015B, 'U+015B ś'),
                (0x017A, 'U+017A ź'),
                (0x017C, 'U+017C ż'),
                (0x0104, 'U+0104 Ą'),
                (0x0106, 'U+0106 Ć'),
                (0x0118, 'U+0118 Ę'),
                (0x0141, 'U+0141 Ł'),
                (0x0143, 'U+0143 Ń'),
                (0x00D3, 'U+00D3 Ó'),
                (0x015A, 'U+015A Ś'),
                (0x0179, 'U+0179 Ź'),
                (0x017B, 'U+017B Ż'),
            ]

            print(f"\n  Glyph IDs from cmap:")
            for cp, name in codepoints:
                gid = get_glyph_id(cp)
                cmap_glyph[cp] = gid
                print(f"    {name:<25} -> GID {gid}")

    # --- hmtx ---
    print(f"\n--- hmtx (advance widths) ---")
    if 'hmtx' in tables:
        hmtx_off, hmtx_len, _ = tables['hmtx']

        def get_advance_width(gid):
            if gid < numOfLongHorMetrics:
                return read_u16(data, hmtx_off + gid * 4)
            else:
                # Use last long hor metric advance width
                last_aw = read_u16(data, hmtx_off + (numOfLongHorMetrics - 1) * 4)
                return last_aw

        def get_lsb(gid):
            if gid < numOfLongHorMetrics:
                return read_i16(data, hmtx_off + gid * 4 + 2)
            else:
                extra = gid - numOfLongHorMetrics
                return read_i16(data, hmtx_off + numOfLongHorMetrics * 4 + extra * 2)

        # GID 0 (.notdef)
        aw0 = get_advance_width(0)
        lsb0 = get_lsb(0)
        print(f"  GID 0 (.notdef): advanceWidth={aw0}  LSB={lsb0}")

        # GID for space
        if 0x0020 in cmap_glyph:
            gid_space = cmap_glyph[0x0020]
            if gid_space != 0:
                aw_sp = get_advance_width(gid_space)
                lsb_sp = get_lsb(gid_space)
                print(f"  GID {gid_space} (space U+0020): advanceWidth={aw_sp}  LSB={lsb_sp}")

        # GID for A
        if 0x0041 in cmap_glyph:
            gid_A = cmap_glyph[0x0041]
            if gid_A != 0:
                aw_A = get_advance_width(gid_A)
                lsb_A = get_lsb(gid_A)
                print(f"  GID {gid_A} (A U+0041):     advanceWidth={aw_A}  LSB={lsb_A}")

        # All requested codepoints
        print(f"\n  Advance widths for all requested codepoints:")
        for cp, name in [
            (0x0020, 'space'), (0x0041, 'A'), (0x0061, 'a'), (0x00B7, 'mid.dot'),
            (0x0105, 'ą'), (0x0107, 'ć'), (0x0119, 'ę'), (0x0142, 'ł'),
            (0x0144, 'ń'), (0x00F3, 'ó'), (0x015B, 'ś'), (0x017A, 'ź'),
            (0x017C, 'ż'), (0x0104, 'Ą'), (0x0106, 'Ć'), (0x0118, 'Ę'),
            (0x0141, 'Ł'), (0x0143, 'Ń'), (0x00D3, 'Ó'), (0x015A, 'Ś'),
            (0x0179, 'Ź'), (0x017B, 'Ż'),
        ]:
            gid = cmap_glyph.get(cp, 0)
            if gid != 0:
                aw = get_advance_width(gid)
                lsb = get_lsb(gid)
                print(f"    {name:<10} cp=U+{cp:04X} GID={gid:4d}  aw={aw:5d}  lsb={lsb:5d}")
            else:
                print(f"    {name:<10} cp=U+{cp:04X} GID=   0  (missing or .notdef)")
    else:
        print("  hmtx: NOT FOUND")

    # --- post ---
    if 'post' in tables:
        off, ln, _ = tables['post']
        post_version   = read_u32(data, off)
        italicAngle_i  = read_i32(data, off + 4)   # Fixed 16.16
        italicAngle    = italicAngle_i / 65536.0
        underlinePos   = read_i16(data, off + 8)
        underlineThick = read_i16(data, off + 10)
        isFixedPitch   = read_u32(data, off + 12)
        print(f"\n--- post ---")
        print(f"  version:         0x{post_version:08X}")
        print(f"  italicAngle:     {italicAngle:.4f}")
        print(f"  underlinePos:    {underlinePos}")
        print(f"  underlineThick:  {underlineThick}")
        print(f"  isFixedPitch:    {isFixedPitch}")

    # --- name table: extract Family, Subfamily, Full name, PostScript name ---
    if 'name' in tables:
        off, ln, _ = tables['name']
        name_format = read_u16(data, off)
        count        = read_u16(data, off + 2)
        stringOffset = read_u16(data, off + 4)
        storage_base = off + stringOffset

        names = {}
        for i in range(count):
            rec_off = off + 6 + i * 12
            platform  = read_u16(data, rec_off)
            encoding  = read_u16(data, rec_off + 2)
            language  = read_u16(data, rec_off + 4)
            nameID    = read_u16(data, rec_off + 6)
            length    = read_u16(data, rec_off + 8)
            strOffset = read_u16(data, rec_off + 10)

            raw = data[storage_base + strOffset : storage_base + strOffset + length]
            if platform == 3:  # Windows UTF-16
                try:
                    s = raw.decode('utf-16-be')
                except:
                    s = repr(raw)
            elif platform == 1:  # Mac
                try:
                    s = raw.decode('latin-1')
                except:
                    s = repr(raw)
            else:
                s = repr(raw)

            key = (nameID, platform)
            if key not in names:
                names[key] = s

        labels = {1: 'Family', 2: 'Subfamily', 4: 'Full name', 6: 'PostScript name'}
        print(f"\n--- name (key strings, platform 3) ---")
        for nid, label in labels.items():
            val = names.get((nid, 3), names.get((nid, 1), '(not found)'))
            print(f"  nameID {nid} {label:<20}: {val}")

    print()


def main():
    files = [
        r"c:\@prg\@picocomputer\@software\git\cc65-rp6502os\assets\ttf\DroidSansMono.ttf",
        r"c:\@prg\@picocomputer\@software\git\cc65-rp6502os\assets\ttf\DroidSerif-Regular.ttf",
        r"c:\@prg\@picocomputer\@software\git\cc65-rp6502os\assets\ttf\DroidSerif-Italic.ttf",
    ]
    for f in files:
        if os.path.exists(f):
            analyze_ttf(f)
        else:
            print(f"FILE NOT FOUND: {f}")

if __name__ == '__main__':
    main()
