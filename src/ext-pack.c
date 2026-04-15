/*
 * ext-pack.c — ZIP archiver for razemOS / Picocomputer 6502
 *
 * Usage: pack <dirname> [/d]
 *   /d = DEFLATE with 4KB window + hash LZ77 (smaller, slower)
 *   (default) = STORE (fast, no compression)
 *
 * Output: <dirname>.zip (ZIP format, compatible with unzip)
 */

#include "commons.h"

#define APPVER "20260415.1453"

#define FNAMELEN 64

#ifndef AM_DIR
#define AM_DIR 0x10
#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_VOL 0x08
#define AM_ARC 0x20
#endif

/* ---- ZIP constants ------------------------------------------------------- */

#define ZIP_METHOD_STORE   0u
#define ZIP_METHOD_DEFLATE 8u
#define ZIP_FLAG_DD        0x0008u   /* bit 3: data descriptor present */

/* ---- LZ77 / DEFLATE parameters ----------------------------------------- */

#define LZ_WIN_SIZE    4096u
#define LZ_WIN_MASK    0x0FFFu
#define LZ_HASH_SIZE   256u
#define LZ_MIN_MATCH   3u
#define LZ_MAX_MATCH   32u           /* cap: keeps lookahead buffer small */
#define LZ_AHEAD_SIZE  (LZ_MAX_MATCH + 3u) /* 35 bytes */

/* ---- Archive limits ----------------------------------------------------- */

#define PACK_MAX_FILES  64u
#define PACK_FNAME_MAX  47u          /* max chars stored in cdir fname */
#define PACK_EXTNAME    ".zip"

/* ---- I/O chunk size ----------------------------------------------------- */

#define RB_SIZE  128u

/* ---- XRAM staging area for file writes ---------------------------------- */
/* write() is overridden by write_stub.c to go to UART; file writes must    */
/* copy data to XRAM first, then call write_xram(xram_addr, count, fd).     */
#define PACK_XRAM_STAGE 0xA000u  /* 128-byte staging window in XRAM */

static void tx_char(char c)            { TX_READY_SPIN; RIA.tx = c; }
static void tx_string(const char *s)   { while (*s) tx_char(*s++); }

static void tx_dec32(unsigned long v) {
    char buf[10];
    int  i = 10;
    if (!v) { tx_char('0'); return; }
    while (v && i) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    for (; i < 10; i++) tx_char(buf[i]);
}

/* ---- Central directory entry ------------------------------------------- */

typedef struct {
    char          fname[PACK_FNAME_MAX + 1u]; /* 48B: null-terminated */
    unsigned long crc32;                       /*  4B */
    unsigned long uncomp;                      /*  4B */
    unsigned long comp;                        /*  4B */
    unsigned long offset;                      /*  4B */
    unsigned int  fdate;                       /*  2B */
    unsigned int  ftime;                       /*  2B */
    unsigned char method;                      /*  1B */
    unsigned char fnlen;                       /*  1B */
    unsigned char _pad[2];                     /*  2B alignment */
} cdir_t;  /* 72B × 64 = 4608B */

static cdir_t         cdir[PACK_MAX_FILES];
static unsigned int   cdir_n;
static unsigned long  out_pos;   /* total bytes written to output file */

/* ---- I/O buffers -------------------------------------------------------- */

static unsigned char  io4[4];               /* scratch for le16/le32 */
static unsigned char  rb_buf[RB_SIZE];      /* chunk-read buffer */
static unsigned int   rb_len;
static unsigned int   rb_pos;
static int            rb_fd;

/* ---- LZ77 / DEFLATE state ----------------------------------------------- */

static unsigned char  lz_win[LZ_WIN_SIZE];         /* 4096B window */
static unsigned int   lz_ht[LZ_HASH_SIZE];         /*  512B hash table */
static unsigned char  lz_ahead[LZ_AHEAD_SIZE];     /*   35B lookahead */
static unsigned int   lz_ahead_len;
static unsigned int   lz_wpos;
static unsigned int   lz_wfill;

static unsigned char  dfl_bits;        /* accumulator */
static unsigned char  dfl_nbits;       /* bits in accumulator */
static unsigned char  dfl_obuf[128];   /* output buffer */
static unsigned char  dfl_opos;
static int            dfl_fd;
static unsigned long  dfl_comp_sz;     /* compressed bytes written for current file */

/* ---- CRC-32 (polynomial 0xEDB88320) ------------------------------------- */

static unsigned long crc32_upd(unsigned long crc, unsigned char b)
{
    unsigned char i;
    crc ^= (unsigned long)b;
    for (i = 0; i < 8u; i++) {
        if (crc & 1UL) crc = (crc >> 1) ^ 0xEDB88320UL;
        else           crc >>= 1;
    }
    return crc;
}

/* ---- File write via XRAM (all helpers track out_pos) -------------------- */

static void fw(int fd, const unsigned char *buf, unsigned int len)
{
    unsigned int i;
    RIA.addr0 = PACK_XRAM_STAGE;
    RIA.step0 = 1;
    for (i = 0; i < len; i++) RIA.rw0 = buf[i];
    write_xram(PACK_XRAM_STAGE, len, fd);
    out_pos += (unsigned long)len;
}

static void write_le16(int fd, unsigned int v) {
    io4[0] = (unsigned char)(v & 0xFFu);
    io4[1] = (unsigned char)(v >> 8u);
    fw(fd, io4, 2u);
}

static void write_le32(int fd, unsigned long v) {
    io4[0] = (unsigned char)(v        & 0xFFUL);
    io4[1] = (unsigned char)((v >> 8)  & 0xFFUL);
    io4[2] = (unsigned char)((v >> 16) & 0xFFUL);
    io4[3] = (unsigned char) (v >> 24);
    fw(fd, io4, 4u);
}

/* ---- ZIP header writers ------------------------------------------------- */

static void write_lfh(int fd, const char *fn, unsigned char fnlen,
                      unsigned int method,
                      unsigned int fdate, unsigned int ftime)
{
    write_le32(fd, 0x04034B50UL);        /* PK\x03\x04 */
    write_le16(fd, 20u);                 /* version needed: 2.0 */
    write_le16(fd, ZIP_FLAG_DD);         /* flags: data descriptor */
    write_le16(fd, method);
    write_le16(fd, ftime);
    write_le16(fd, fdate);
    write_le32(fd, 0UL);                 /* CRC-32: 0 (in DD) */
    write_le32(fd, 0UL);                 /* compressed size: 0 */
    write_le32(fd, 0UL);                 /* uncompressed size: 0 */
    write_le16(fd, (unsigned int)fnlen);
    write_le16(fd, 0u);                  /* extra field length */
    fw(fd, (const unsigned char *)fn, (unsigned int)fnlen);
}

static void write_dd(int fd, unsigned long crc,
                     unsigned long csz, unsigned long usz)
{
    write_le32(fd, 0x08074B50UL);  /* PK\x07\x08 */
    write_le32(fd, crc);
    write_le32(fd, csz);
    write_le32(fd, usz);
}

static void write_cdh(int fd, const cdir_t *e)
{
    write_le32(fd, 0x02014B50UL);        /* PK\x01\x02 */
    write_le16(fd, 20u);                 /* version made by: 2.0 */
    write_le16(fd, 20u);                 /* version needed: 2.0 */
    write_le16(fd, ZIP_FLAG_DD);
    write_le16(fd, (unsigned int)e->method);
    write_le16(fd, e->ftime);
    write_le16(fd, e->fdate);
    write_le32(fd, e->crc32);
    write_le32(fd, e->comp);
    write_le32(fd, e->uncomp);
    write_le16(fd, (unsigned int)e->fnlen);
    write_le16(fd, 0u);                  /* extra field */
    write_le16(fd, 0u);                  /* file comment */
    write_le16(fd, 0u);                  /* disk number start */
    write_le16(fd, 0u);                  /* internal attributes */
    write_le32(fd, 0UL);                 /* external attributes */
    write_le32(fd, e->offset);
    fw(fd, (const unsigned char *)e->fname, (unsigned int)e->fnlen);
}

static void write_eocd(int fd, unsigned int nfiles,
                       unsigned long cd_off, unsigned long cd_sz)
{
    write_le32(fd, 0x06054B50UL);  /* PK\x05\x06 */
    write_le16(fd, 0u);            /* disk number */
    write_le16(fd, 0u);            /* disk with CD start */
    write_le16(fd, nfiles);
    write_le16(fd, nfiles);
    write_le32(fd, cd_sz);
    write_le32(fd, cd_off);
    write_le16(fd, 0u);            /* comment length */
}

/* ---- Chunk reader ------------------------------------------------------- */

static void rb_init(int fd) {
    rb_fd = fd; rb_len = 0; rb_pos = 0;
}

static int rb_getbyte(void) {
    int got;
    if (rb_pos >= rb_len) {
        got = read(rb_fd, rb_buf, RB_SIZE);
        if (got <= 0) return -1;
        rb_len = (unsigned int)got;
        rb_pos = 0;
    }
    return (int)(unsigned char)rb_buf[rb_pos++];
}

/* ---- LE32 buffer helper ------------------------------------------------- */

static unsigned long read_le32_buf(const unsigned char *p)
{
    return (unsigned long)(unsigned char)p[0]
         | ((unsigned long)(unsigned char)p[1] << 8)
         | ((unsigned long)(unsigned char)p[2] << 16)
         | ((unsigned long)(unsigned char)p[3] << 24);
}

/* ---- Fixed-Huffman inflate lookup tables -------------------------------- */
/* Distance codes 0-23 cover distances 1-4096 (matches 4KB LZ77 window)     */

static const unsigned int dist_base[24] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073
};
static const unsigned char dist_xb[24] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10
};

/* Length symbols 257-285 (index = sym-257); sym 285 → len 258, 0 extra bits */
static const unsigned int len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const unsigned char len_xb[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};

/* ---- STORE mode --------------------------------------------------------- */

static unsigned long pack_store(int fd_out, unsigned long *pcrc)
{
    unsigned long  sz  = 0;
    unsigned long  crc = 0xFFFFFFFFUL;
    unsigned int   nr;
    unsigned int   i;
    int            got;

    while ((got = read(rb_fd, rb_buf, RB_SIZE)) > 0) {
        nr = (unsigned int)got;
        for (i = 0; i < nr; i++) crc = crc32_upd(crc, rb_buf[i]);
        fw(fd_out, rb_buf, nr);
        sz += (unsigned long)nr;
    }
    *pcrc = crc ^ 0xFFFFFFFFUL;
    return sz;
}

/* ---- DEFLATE bit I/O ---------------------------------------------------- */

static void dfl_flush_obuf(void)
{
    if (dfl_opos > 0) {
        dfl_comp_sz += (unsigned long)dfl_opos;
        fw(dfl_fd, dfl_obuf, dfl_opos);   /* fw also updates out_pos */
        dfl_opos = 0;
    }
}

static void dfl_put_bit(unsigned char bit)
{
    dfl_bits |= (unsigned char)(bit << dfl_nbits);
    if (++dfl_nbits == 8u) {
        dfl_obuf[dfl_opos++] = dfl_bits;
        dfl_bits = 0; dfl_nbits = 0;
        if (dfl_opos >= 128u) dfl_flush_obuf();
    }
}

/* Huffman code: MSB first (per RFC 1951 §3.1.1) */
static void emit_huff(unsigned int code, unsigned char nbits)
{
    while (nbits) {
        nbits--;
        dfl_put_bit((unsigned char)((code >> nbits) & 1u));
    }
}

/* Extra bits: LSB first */
static void emit_extra(unsigned int val, unsigned char nbits)
{
    while (nbits--) {
        dfl_put_bit((unsigned char)(val & 1u));
        val >>= 1;
    }
}

static void dfl_literal(unsigned char b)
{
    if (b <= 143u) emit_huff((unsigned int)(0x30u + b), 8u);
    else           emit_huff((unsigned int)(0x190u + b - 144u), 9u);
}

static void dfl_eob(void)
{
    emit_huff(0u, 7u);   /* symbol 256: code 0000000 */
}

/* Emit length symbol + extra bits for match length 3-32 */
static void dfl_length(unsigned int len)
{
    unsigned int  sym;   /* 257-285: must be unsigned int, not char */
    unsigned char xb;
    unsigned int  xv;

    /* symbols 257-264: lengths 3-10, 0 extra bits */
    if (len <= 10u) {
        sym = 254u + len; /* 257..264 */
        xb = 0u; xv = 0u;
    } else if (len <= 12u) { sym = 265u; xb = 1u; xv = len - 11u; }
    else if (len <= 14u)   { sym = 266u; xb = 1u; xv = len - 13u; }
    else if (len <= 16u)   { sym = 267u; xb = 1u; xv = len - 15u; }
    else if (len <= 18u)   { sym = 268u; xb = 1u; xv = len - 17u; }
    else if (len <= 22u)   { sym = 269u; xb = 2u; xv = len - 19u; }
    else if (len <= 26u)   { sym = 270u; xb = 2u; xv = len - 23u; }
    else if (len <= 30u)   { sym = 271u; xb = 2u; xv = len - 27u; }
    else                   { sym = 272u; xb = 2u; xv = len - 31u; } /* 31-34 */

    /* fixed Huffman: sym 257-279 → 7-bit codes (sym-256); 280-287 → 8-bit 0xC0+ */
    if (sym <= 279u) emit_huff((unsigned int)(sym - 256u), 7u);
    else             emit_huff((unsigned int)(0xC0u + sym - 280u), 8u);
    if (xb) emit_extra(xv, xb);
}

/* Emit distance code + extra bits for distance 1-4096 */
static void dfl_distance(unsigned int dist)
{
    unsigned char xb;
    unsigned int  xv;
    unsigned char code;

    dist--;  /* make 0-based */
    if      (dist <    4u) { code = (unsigned char)dist; xb = 0u; xv = 0u;          }
    else if (dist <    6u) { code =  4u; xb =  1u; xv = dist -    4u; }
    else if (dist <    8u) { code =  5u; xb =  1u; xv = dist -    6u; }
    else if (dist <   12u) { code =  6u; xb =  2u; xv = dist -    8u; }
    else if (dist <   16u) { code =  7u; xb =  2u; xv = dist -   12u; }
    else if (dist <   24u) { code =  8u; xb =  3u; xv = dist -   16u; }
    else if (dist <   32u) { code =  9u; xb =  3u; xv = dist -   24u; }
    else if (dist <   48u) { code = 10u; xb =  4u; xv = dist -   32u; }
    else if (dist <   64u) { code = 11u; xb =  4u; xv = dist -   48u; }
    else if (dist <   96u) { code = 12u; xb =  5u; xv = dist -   64u; }
    else if (dist <  128u) { code = 13u; xb =  5u; xv = dist -   96u; }
    else if (dist <  192u) { code = 14u; xb =  6u; xv = dist -  128u; }
    else if (dist <  256u) { code = 15u; xb =  6u; xv = dist -  192u; }
    else if (dist <  384u) { code = 16u; xb =  7u; xv = dist -  256u; }
    else if (dist <  512u) { code = 17u; xb =  7u; xv = dist -  384u; }
    else if (dist <  768u) { code = 18u; xb =  8u; xv = dist -  512u; }
    else if (dist < 1024u) { code = 19u; xb =  8u; xv = dist -  768u; }
    else if (dist < 1536u) { code = 20u; xb =  9u; xv = dist - 1024u; }
    else if (dist < 2048u) { code = 21u; xb =  9u; xv = dist - 1536u; }
    else if (dist < 3072u) { code = 22u; xb = 10u; xv = dist - 2048u; }
    else                   { code = 23u; xb = 10u; xv = dist - 3072u; }

    emit_huff((unsigned int)code, 5u);
    if (xb) emit_extra(xv, xb);
}

/* ---- LZ77 helpers ------------------------------------------------------- */

static unsigned char lz_hash3(unsigned char a, unsigned char b, unsigned char c)
{
    return (unsigned char)((((unsigned int)a * 5u) + ((unsigned int)b * 3u) + c) & 0xFFu);
}

/* Add byte to circular window */
static void lz_push(unsigned char b)
{
    lz_win[lz_wpos] = b;
    lz_wpos = (unsigned int)((lz_wpos + 1u) & LZ_WIN_MASK);
    if (lz_wfill < LZ_WIN_SIZE) lz_wfill++;
}

/* Find a match at current lookahead position.
 * Returns match length (0 if < LZ_MIN_MATCH), sets *pdist. */
static unsigned int lz_find_match(unsigned int *pdist)
{
    unsigned char  h;
    unsigned int   cand;
    unsigned int   dist;
    unsigned int   max_len;
    unsigned int   len;
    unsigned int   wp;

    if (lz_ahead_len < LZ_MIN_MATCH) return 0u;

    h    = lz_hash3(lz_ahead[0], lz_ahead[1], lz_ahead[2]);
    cand = lz_ht[h];
    if (cand == 0xFFFFu || lz_wfill == 0u) return 0u;

    /* compute distance (1-based) */
    if (lz_wpos > cand) dist = lz_wpos - cand;
    else                dist = LZ_WIN_SIZE - cand + lz_wpos;
    if (dist == 0u || dist > lz_wfill) return 0u;

    /* cap: respect window, ahead buffer, and max match */
    max_len = lz_ahead_len;
    if (max_len > LZ_MAX_MATCH) max_len = LZ_MAX_MATCH;
    if (max_len > dist)         max_len = dist;   /* no run-length overlap */
    if (max_len < LZ_MIN_MATCH) return 0u;

    /* compare lookahead with window */
    len = 0u;
    wp  = cand;
    while (len < max_len) {
        if (lz_win[wp] != lz_ahead[len]) break;
        len++;
        wp = (unsigned int)((wp + 1u) & LZ_WIN_MASK);
    }
    if (len < LZ_MIN_MATCH) return 0u;

    *pdist = dist;
    return len;
}

/* ---- DEFLATE compress a file ------------------------------------------- */

static int pack_deflate(int fd_out, unsigned long *pcrc, unsigned long *puncomp)
{
    unsigned long  crc = 0xFFFFFFFFUL;
    unsigned long  usz = 0;
    unsigned char  h;
    unsigned int   i;
    unsigned int   match_len;
    unsigned int   dist;
    int            b;

    /* initialise state */
    lz_wpos = 0u; lz_wfill = 0u; lz_ahead_len = 0u;
    dfl_bits = 0u; dfl_nbits = 0u; dfl_opos = 0u; dfl_comp_sz = 0UL;
    dfl_fd = fd_out;
    for (i = 0u; i < LZ_HASH_SIZE; i++) lz_ht[i] = 0xFFFFu;

    /* fill initial lookahead */
    while (lz_ahead_len < LZ_AHEAD_SIZE) {
        b = rb_getbyte();
        if (b < 0) break;
        lz_ahead[lz_ahead_len++] = (unsigned char)b;
    }

    /* DEFLATE block header: BFINAL=1, BTYPE=01 (fixed Huffman), LSB first */
    dfl_put_bit(1u);  /* BFINAL */
    dfl_put_bit(1u);  /* BTYPE bit 0 */
    dfl_put_bit(0u);  /* BTYPE bit 1 */

    while (lz_ahead_len > 0u) {
        dist      = 0u;
        match_len = lz_find_match(&dist);

        if (match_len >= LZ_MIN_MATCH) {
            /* emit back-reference */
            dfl_length(match_len);
            dfl_distance(dist);

            /* consume match_len bytes: update window, hash, CRC */
            for (i = 0u; i < match_len; i++) {
                crc = crc32_upd(crc, lz_ahead[0]);
                usz++;
                if (lz_ahead_len >= LZ_MIN_MATCH) {
                    h = lz_hash3(lz_ahead[0], lz_ahead[1], lz_ahead[2]);
                    lz_ht[h] = lz_wpos;
                }
                lz_push(lz_ahead[0]);
                /* shift lookahead left by 1 */
                lz_ahead_len--;
                if (lz_ahead_len > 0u)
                    memmove(lz_ahead, lz_ahead + 1, lz_ahead_len);
                /* refill 1 byte */
                b = rb_getbyte();
                if (b >= 0) lz_ahead[lz_ahead_len++] = (unsigned char)b;
            }
        } else {
            /* emit literal */
            if (lz_ahead_len >= LZ_MIN_MATCH) {
                h = lz_hash3(lz_ahead[0], lz_ahead[1], lz_ahead[2]);
                lz_ht[h] = lz_wpos;
            }
            crc = crc32_upd(crc, lz_ahead[0]);
            usz++;
            dfl_literal(lz_ahead[0]);
            lz_push(lz_ahead[0]);

            lz_ahead_len--;
            if (lz_ahead_len > 0u)
                memmove(lz_ahead, lz_ahead + 1, lz_ahead_len);
            b = rb_getbyte();
            if (b >= 0) lz_ahead[lz_ahead_len++] = (unsigned char)b;
        }
    }

    dfl_eob();
    /* pad to byte boundary */
    while (dfl_nbits > 0u) dfl_put_bit(0u);
    dfl_flush_obuf();

    *pcrc    = crc ^ 0xFFFFFFFFUL;
    *puncomp = usz;
    return 0;
}

/* ==== UNPACK / INFLATE ==================================================== */

/* ---- Inflate bit reader (reuses dfl_bits / dfl_nbits) ------------------- */

static int infl_bit(void)
{
    int b;
    if (dfl_nbits == 0u) {
        b = rb_getbyte();
        if (b < 0) return -1;
        dfl_bits  = (unsigned char)b;
        dfl_nbits = 8u;
    }
    b = (int)(dfl_bits & 1u);
    dfl_bits  >>= 1;
    dfl_nbits--;
    return b;
}

/* Read n bits LSB-first (extra bits after Huffman code) */
static int infl_read_lsb(unsigned char n)
{
    unsigned int  v = 0u;
    unsigned char i;
    int b;
    for (i = 0u; i < n; i++) {
        b = infl_bit();
        if (b < 0) return -1;
        v |= (unsigned int)(unsigned char)b << i;
    }
    return (int)v;
}

/* Decode fixed Huffman lit/len symbol: 7/8/9 bits, MSB-accumulated        */
/* First bit read from stream = MSB of code (as emitted by emit_huff).      */
static int infl_decode_litlen(void)
{
    unsigned int  code = 0u;
    unsigned char i;
    int b;

    for (i = 0u; i < 7u; i++) {
        b = infl_bit();
        if (b < 0) return -1;
        code = (code << 1) | (unsigned int)(unsigned char)b;
    }
    if (code <= 23u) return (int)(256u + code); /* 7-bit: EOB(256), 257-279 */

    b = infl_bit(); if (b < 0) return -1;
    code = (code << 1) | (unsigned int)(unsigned char)b;  /* 8 bits */
    if (code >= 48u  && code <= 191u) return (int)(code - 48u);         /* lit 0-143   */
    if (code >= 192u && code <= 199u) return (int)(280u + code - 192u); /* sym 280-287 */

    b = infl_bit(); if (b < 0) return -1;
    code = (code << 1) | (unsigned int)(unsigned char)b;  /* 9 bits */
    if (code >= 400u && code <= 511u) return (int)(144u + code - 400u); /* lit 144-255 */

    return -2; /* invalid code */
}

/* Decode 5-bit distance code, MSB-accumulated */
static int infl_decode_dist(void)
{
    unsigned int  code = 0u;
    unsigned char i;
    int b;
    for (i = 0u; i < 5u; i++) {
        b = infl_bit();
        if (b < 0) return -1;
        code = (code << 1) | (unsigned int)(unsigned char)b;
    }
    return (int)code;
}

/* ---- Inflate output helpers (reuse dfl_obuf/dfl_opos, lz_win/lz_wpos) -- */

static int unpack_out_fd; /* output fd set by caller before inflate/store    */

static void unpack_flush_obuf(void)
{
    unsigned int i;
    if (dfl_opos > 0u) {
        RIA.addr0 = PACK_XRAM_STAGE;
        RIA.step0 = 1;
        for (i = 0u; i < dfl_opos; i++) RIA.rw0 = dfl_obuf[i];
        write_xram(PACK_XRAM_STAGE, dfl_opos, unpack_out_fd);
        dfl_opos = 0u;
    }
}

static void unpack_emit(unsigned char b)
{
    lz_win[lz_wpos] = b;
    lz_wpos = (lz_wpos + 1u) & LZ_WIN_MASK;
    dfl_obuf[dfl_opos++] = b;
    if (dfl_opos >= 128u) unpack_flush_obuf();
}

/* ---- Inflate one fixed-Huffman block (BTYPE=01) ------------------------- */
/* Accumulates into *pcrc (pre-init to 0xFFFFFFFF) and *pusz.                */

static int inflate_fixed_block(unsigned long *pcrc, unsigned long *pusz)
{
    int          sym, dc, xb, extra;
    unsigned int length, dist, src, i;
    unsigned char bk;

    while (1) {
        sym = infl_decode_litlen();
        if (sym < 0) return -1;

        if (sym == 256) return 0; /* end-of-block */

        if (sym < 256) {
            *pcrc = crc32_upd(*pcrc, (unsigned char)sym);
            unpack_emit((unsigned char)sym);
            (*pusz)++;
        } else {
            /* length/distance back-reference: sym 257-285 */
            sym -= 257;
            if (sym > 28) return -1;
            xb     = (int)len_xb[(unsigned char)sym];
            extra  = xb ? infl_read_lsb((unsigned char)xb) : 0;
            if (extra < 0) return -1;
            length = len_base[(unsigned char)sym] + (unsigned int)extra;

            dc = infl_decode_dist();
            if (dc < 0 || dc > 23) return -1;
            xb    = (int)dist_xb[(unsigned char)dc];
            extra = xb ? infl_read_lsb((unsigned char)xb) : 0;
            if (extra < 0) return -1;
            dist = dist_base[(unsigned char)dc] + (unsigned int)extra;
            if (dist == 0u || dist > LZ_WIN_SIZE) return -2; /* beyond window */

            src = (unsigned int)((lz_wpos + LZ_WIN_SIZE - dist) & LZ_WIN_MASK);
            for (i = 0u; i < length; i++) {
                bk = lz_win[(unsigned int)((src + i) & LZ_WIN_MASK)];
                *pcrc = crc32_upd(*pcrc, bk);
                unpack_emit(bk);
                (*pusz)++;
            }
        }
    }
}

/* ---- Inflate a DEFLATE stream from fd_in → unpack_out_fd ---------------- */
/* Returns 0 on success, <0 on error.                                         */

static int unpack_inflate(int fd_in, unsigned long *pcrc, unsigned long *pusz)
{
    int          bfinal, btype, rc, b, i;
    unsigned int blen;

    *pcrc = 0xFFFFFFFFUL;
    *pusz = 0UL;
    dfl_bits  = 0u;
    dfl_nbits = 0u;
    dfl_opos  = 0u;
    lz_wpos   = 0u;
    rb_init(fd_in);

    do {
        bfinal = infl_bit();   if (bfinal < 0) return -1;
        btype  = infl_read_lsb(2u); if (btype  < 0) return -1;

        if (btype == 0) {
            /* BTYPE=00: non-compressed block — align, copy LEN bytes */
            dfl_nbits = 0u; dfl_bits = 0u;
            b = rb_getbyte(); if (b < 0) return -1;
            blen  = (unsigned int)(unsigned char)b;
            b = rb_getbyte(); if (b < 0) return -1;
            blen |= (unsigned int)(unsigned char)b << 8;
            rb_getbyte(); rb_getbyte(); /* NLEN — discard */
            for (i = 0; i < (int)blen; i++) {
                b = rb_getbyte(); if (b < 0) return -1;
                *pcrc = crc32_upd(*pcrc, (unsigned char)b);
                unpack_emit((unsigned char)b);
                (*pusz)++;
            }
        } else if (btype == 1) {
            /* BTYPE=01: fixed Huffman */
            rc = inflate_fixed_block(pcrc, pusz);
            if (rc < 0) return rc;
        } else {
            tx_string(EXCLAMATION "Unsupported DEFLATE type" NEWLINE);
            return -3;
        }
    } while (!bfinal);

    unpack_flush_obuf();
    *pcrc ^= 0xFFFFFFFFUL;
    return 0;
}

/* ---- Copy raw STORE data from fd_in → unpack_out_fd --------------------- */

static int unpack_store(int fd_in, unsigned long comp_sz,
                        unsigned long *pcrc, unsigned long *pusz)
{
    unsigned long remaining = comp_sz;
    unsigned long crc       = 0xFFFFFFFFUL;
    unsigned int  nr, j;
    int           got;

    while (remaining > 0UL) {
        nr  = (remaining > (unsigned long)RB_SIZE)
              ? (unsigned int)RB_SIZE : (unsigned int)remaining;
        got = read(fd_in, rb_buf, nr);
        if (got <= 0) { tx_string(EXCLAMATION "Read error" NEWLINE); return -1; }
        nr = (unsigned int)got;
        for (j = 0u; j < nr; j++) crc = crc32_upd(crc, rb_buf[j]);
        RIA.addr0 = PACK_XRAM_STAGE;
        RIA.step0 = 1;
        for (j = 0u; j < nr; j++) RIA.rw0 = rb_buf[j];
        write_xram(PACK_XRAM_STAGE, nr, unpack_out_fd);
        remaining -= (unsigned long)nr;
    }
    *pcrc = crc ^ 0xFFFFFFFFUL;
    *pusz = comp_sz;
    return 0;
}

/* ---- Main unpack entry point -------------------------------------------- */

static int do_unpack(const char *arcpath)
{
    static f_stat_t      arc_stat;
    static char          dest_dir[FNAMELEN + 1];
    static char          outpath[FNAMELEN * 2 + 2];
    static unsigned char eocd_buf[22];
    static unsigned char cdhbuf[46];
    static unsigned char lfhbuf[30];

    unsigned long fsize, cd_offset, cdh_pos, local_off, data_start;
    unsigned long crc_got, crc_exp, usz_got, usz_exp;
    unsigned int  nfiles, fnlen_orig, fnlen, extra_len, comment_len;
    unsigned int  lfh_fnlen, lfh_extra, j;
    int           fd, fd_out, rc, n, i;
    const char   *fname;

    /* 1. Get file size */
    if (f_stat(arcpath, &arc_stat) < 0) {
        tx_string(EXCLAMATION "Not found: "); tx_string(arcpath); tx_string(NEWLINE);
        return 1;
    }
    fsize = arc_stat.fsize;
    if (fsize < 22UL) { tx_string(EXCLAMATION "Not a valid ZIP" NEWLINE); return 1; }

    /* 2. Destination directory = arcpath without last extension */
    n = (int)strlen(arcpath);
    {
        int dot = -1;
        for (i = n - 1; i >= 0; i--) {
            if (arcpath[i] == '.') { dot = i; break; }
            if (arcpath[i] == '/' || arcpath[i] == '\\') break;
        }
        if (dot < 0) dot = n;
        if (dot > FNAMELEN) dot = FNAMELEN;
        memcpy(dest_dir, arcpath, (unsigned int)dot);
        dest_dir[dot] = '\0';
    }

    /* 3. Open archive */
    fd = open(arcpath, O_RDONLY);
    if (fd < 0) { tx_string(EXCLAMATION "Cannot open archive" NEWLINE); return 1; }

    /* 4. Read EOCD (last 22 bytes) */
    if (lseek(fd, (off_t)(fsize - 22UL), SEEK_SET) < 0) {
        tx_string(EXCLAMATION "Seek failed" NEWLINE); close(fd); return 1;
    }
    if (read(fd, eocd_buf, 22) != 22) {
        tx_string(EXCLAMATION "EOCD read failed" NEWLINE); close(fd); return 1;
    }
    if (eocd_buf[0] != 0x50 || eocd_buf[1] != 0x4B ||
        eocd_buf[2] != 0x05 || eocd_buf[3] != 0x06) {
        tx_string(EXCLAMATION "Not a valid ZIP" NEWLINE); close(fd); return 1;
    }
    nfiles    = (unsigned int)(unsigned char)eocd_buf[10] |
                ((unsigned int)(unsigned char)eocd_buf[11] << 8);
    cd_offset = read_le32_buf(eocd_buf + 16);

    if (nfiles == 0u) { tx_string(EXCLAMATION "Empty archive" NEWLINE); close(fd); return 1; }
    if (nfiles > PACK_MAX_FILES) {
        tx_string(EXCLAMATION "Too many files (max 64)" NEWLINE); close(fd); return 1;
    }

    /* 5. Read Central Directory */
    cdh_pos = cd_offset;
    cdir_n  = 0u;
    for (j = 0u; j < nfiles && cdir_n < PACK_MAX_FILES; j++) {
        if (lseek(fd, (off_t)cdh_pos, SEEK_SET) < 0) break;
        if (read(fd, cdhbuf, 46) != 46) break;
        if (cdhbuf[0] != 0x50 || cdhbuf[1] != 0x4B ||
            cdhbuf[2] != 0x01 || cdhbuf[3] != 0x02) break;

        fnlen_orig  = (unsigned int)(unsigned char)cdhbuf[28] |
                      ((unsigned int)(unsigned char)cdhbuf[29] << 8);
        extra_len   = (unsigned int)(unsigned char)cdhbuf[30] |
                      ((unsigned int)(unsigned char)cdhbuf[31] << 8);
        comment_len = (unsigned int)(unsigned char)cdhbuf[32] |
                      ((unsigned int)(unsigned char)cdhbuf[33] << 8);

        fnlen = (fnlen_orig > PACK_FNAME_MAX) ? PACK_FNAME_MAX : fnlen_orig;
        if (read(fd, cdir[cdir_n].fname, fnlen) != (int)fnlen) break;
        cdir[cdir_n].fname[fnlen] = '\0';
        cdir[cdir_n].fnlen  = (unsigned char)fnlen;

        cdir[cdir_n].method = (unsigned char)((unsigned int)(unsigned char)cdhbuf[10] |
                               ((unsigned int)(unsigned char)cdhbuf[11] << 8));
        cdir[cdir_n].crc32  = read_le32_buf(cdhbuf + 16);
        cdir[cdir_n].comp   = read_le32_buf(cdhbuf + 20);
        cdir[cdir_n].uncomp = read_le32_buf(cdhbuf + 24);
        cdir[cdir_n].offset = read_le32_buf(cdhbuf + 42);

        cdh_pos += 46UL + (unsigned long)fnlen_orig
                        + (unsigned long)extra_len
                        + (unsigned long)comment_len;
        cdir_n++;
    }
    if (cdir_n == 0u) {
        tx_string(EXCLAMATION "Cannot read central directory" NEWLINE);
        close(fd); return 1;
    }

    /* 6. Create destination directory (ignore error if exists) */
    tx_string(NEWLINE "Extracting to: "); tx_string(dest_dir); tx_string(NEWLINE);
    f_mkdir(dest_dir);

    /* 7. Extract each file */
    rc = 0;
    for (j = 0u; j < cdir_n; j++) {
        /* strip first path component (dirname/) from stored name */
        fname = cdir[j].fname;
        {
            const char *p = fname;
            while (*p && *p != '/' && *p != '\\') p++;
            if (*p) fname = p + 1;
        }
        if (!*fname) continue; /* skip directory-only entries */

        /* build output path: dest_dir/fname */
        {
            unsigned int dlen = (unsigned int)strlen(dest_dir);
            unsigned int flen = (unsigned int)strlen(fname);
            if (dlen + 1u + flen >= (unsigned int)(sizeof(outpath) - 1u)) {
                tx_string(EXCLAMATION "Path too long, skipped" NEWLINE); continue;
            }
            memcpy(outpath, dest_dir, dlen);
            outpath[dlen] = '/';
            memcpy(outpath + dlen + 1u, fname, flen + 1u);
        }

        tx_string("  < "); tx_string(outpath);

        /* seek to Local File Header */
        local_off = cdir[j].offset;
        if (lseek(fd, (off_t)local_off, SEEK_SET) < 0) {
            tx_string(" [SKIP: seek]" NEWLINE); continue;
        }
        if (read(fd, lfhbuf, 30) != 30) {
            tx_string(" [SKIP: LFH]" NEWLINE); continue;
        }
        if (lfhbuf[0] != 0x50 || lfhbuf[1] != 0x4B ||
            lfhbuf[2] != 0x03 || lfhbuf[3] != 0x04) {
            tx_string(" [SKIP: bad LFH sig]" NEWLINE); continue;
        }
        lfh_fnlen = (unsigned int)(unsigned char)lfhbuf[26] |
                    ((unsigned int)(unsigned char)lfhbuf[27] << 8);
        lfh_extra = (unsigned int)(unsigned char)lfhbuf[28] |
                    ((unsigned int)(unsigned char)lfhbuf[29] << 8);
        data_start = local_off + 30UL
                   + (unsigned long)lfh_fnlen
                   + (unsigned long)lfh_extra;

        if (lseek(fd, (off_t)data_start, SEEK_SET) < 0) {
            tx_string(" [SKIP: data seek]" NEWLINE); continue;
        }

        /* open output file (overwrite if exists) */
        fd_out = open(outpath, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd_out < 0) { tx_string(" [SKIP: create]" NEWLINE); continue; }
        unpack_out_fd = fd_out;

        /* decompress or copy */
        crc_got = 0UL; usz_got = 0UL;
        if (cdir[j].method == (unsigned char)ZIP_METHOD_STORE)
            rc = unpack_store(fd, cdir[j].comp, &crc_got, &usz_got);
        else if (cdir[j].method == (unsigned char)ZIP_METHOD_DEFLATE)
            rc = unpack_inflate(fd, &crc_got, &usz_got);
        else {
            tx_string(" [SKIP: unknown method]" NEWLINE);
            close(fd_out); continue;
        }
        close(fd_out);

        if (rc < 0) { tx_string(" [ERROR]" NEWLINE); rc = 1; continue; }

        crc_exp = cdir[j].crc32;
        usz_exp = cdir[j].uncomp;
        tx_char(' '); tx_dec32(usz_got); tx_string(" B");
        if (crc_got != crc_exp || usz_got != usz_exp)
            tx_string(" [CRC/SIZE MISMATCH]");
        tx_string(NEWLINE);
    }

    close(fd);
    tx_string(NEWLINE "Done: ");
    tx_dec32((unsigned long)cdir_n);
    tx_string(" file(s) -> ");
    tx_string(dest_dir);
    tx_string(NEWLINE);
    return rc;
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int          i;
    int          rc;
    int          dirdes;
    int          fd_out;
    int          fd_in;
    int          use_deflate;
    unsigned int  method;
    unsigned char fnlen;
    unsigned long local_off;
    unsigned long comp;
    unsigned long uncomp;
    unsigned long crc;
    unsigned long cd_start;
    unsigned long cd_size;
    unsigned int  dlen;
    unsigned int  flen;
    const char   *dirname;
    static f_stat_t dent;
    static char  arc_path[FNAMELEN + 8];           /* "dirname.zip\0" */
    static char  src_path[FNAMELEN + 1 + FNAMELEN]; /* "dirname/filename\0" */
    static char  arc_name[PACK_FNAME_MAX + 1];     /* "dirname/filename\0" for archive */

    /* /x: unpack mode — pack /x archive.zip */
    if (argc >= 2 && argv[0][0] == '/' && (argv[0][1] == 'x' || argv[0][1] == 'X'))
        return do_unpack(argv[1]);

    if (argc == 1 && strcmp(argv[0], "/?") == 0) {
        tx_string(NEWLINE
            "Command : pack" NEWLINE NEWLINE
            "Pack (create) or unpack (extract) a ZIP archive" NEWLINE NEWLINE
            "Usage:" NEWLINE
            "  pack <dirname>       create: STORE (fast, no compression)" NEWLINE
            "  pack <dirname> /d    create: DEFLATE (4KB LZ77, smaller/slower)" NEWLINE
            "  pack /x <file.zip>   extract to <file> directory" NEWLINE
            "Output: <dirname>.zip" NEWLINE);
        return 0;
    }
    if (argc == 0 || argv[0][0] == '/') {
        tx_string(NEWLINE "Usage: pack <dirname> [/d] | pack /x <archive.zip>" NEWLINE);
        return 1;
    }

    dirname     = argv[0];
    use_deflate = 0;
    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '/' && (argv[i][1] == 'd' || argv[i][1] == 'D'))
            use_deflate = 1;
    }
    method = use_deflate ? ZIP_METHOD_DEFLATE : ZIP_METHOD_STORE;

    /* build output path: dirname + ".zip" */
    strncpy(arc_path, dirname, FNAMELEN);
    arc_path[FNAMELEN] = '\0';
    strncat(arc_path, PACK_EXTNAME, sizeof(PACK_EXTNAME) + 1);

    /* pre-scan: verify directory exists and is not empty */
    dirdes = f_opendir(dirname);
    if (dirdes < 0) {
        tx_string(EXCLAMATION "Cannot open: ");
        tx_string(dirname);
        tx_string(NEWLINE);
        return 1;
    }
    rc = 0;
    while (1) {
        if (f_readdir(&dent, dirdes) < 0) { rc = -1; break; }
        if (!dent.fname[0]) break;
        if (!(dent.fattrib & AM_DIR)) { rc = 1; break; }
    }
    f_closedir(dirdes);
    if (rc <= 0) {
        tx_string(EXCLAMATION "Directory is empty: ");
        tx_string(dirname);
        tx_string(NEWLINE);
        return 1;
    }

    tx_string(NEWLINE "Creating ");
    tx_string(arc_path);
    if (use_deflate) tx_string(" [DEFLATE]");
    tx_string(NEWLINE);

    dirdes = f_opendir(dirname);
    if (dirdes < 0) {
        tx_string(EXCLAMATION "Cannot open: ");
        tx_string(dirname);
        tx_string(NEWLINE);
        return 1;
    }

    fd_out = open(arc_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd_out < 0) {
        f_closedir(dirdes);
        tx_string(EXCLAMATION "Cannot create output file" NEWLINE);
        return 1;
    }

    cdir_n  = 0u;
    out_pos = 0UL;
    rc      = 0;

    while (1) {
        if (f_readdir(&dent, dirdes) < 0) { rc = 1; break; }
        if (!dent.fname[0]) break;                   /* end of directory */
        if (dent.fattrib & AM_DIR) continue;          /* skip subdirs */
        if (cdir_n >= PACK_MAX_FILES) {
            tx_string(EXCLAMATION "Too many files (max 64), truncated" NEWLINE);
            break;
        }

        /* source path: dirname/filename */
        dlen = (unsigned int)strlen(dirname);
        if (dlen >= FNAMELEN) dlen = FNAMELEN - 1u;
        memcpy(src_path, dirname, dlen);
        src_path[dlen] = '/';
        strncpy(src_path + dlen + 1, dent.fname, FNAMELEN);
        src_path[FNAMELEN + dlen] = '\0';

        /* archive name: dirname/filename (truncated to fit PACK_FNAME_MAX) */
        if (dlen > 20u) dlen = 20u;
        memcpy(arc_name, dirname, dlen);
        arc_name[dlen] = '/';
        flen = (unsigned int)strlen(dent.fname);
        if (flen > (unsigned int)(PACK_FNAME_MAX - dlen - 1u))
            flen = (unsigned int)(PACK_FNAME_MAX - dlen - 1u);
        memcpy(arc_name + dlen + 1u, dent.fname, flen);
        fnlen = (unsigned char)(dlen + 1u + flen);
        arc_name[fnlen] = '\0';

        tx_string("  + ");
        tx_string(arc_name);

        /* record local file header offset */
        local_off = out_pos;
        write_lfh(fd_out, arc_name, fnlen, method, dent.fdate, dent.ftime);

        fd_in = open(src_path, O_RDONLY);
        if (fd_in < 0) {
            tx_string(" [SKIP: open failed]" NEWLINE);
            comp = 0UL; uncomp = 0UL; crc = 0UL;
        } else {
            rb_init(fd_in);
            if (use_deflate) {
                pack_deflate(fd_out, &crc, &uncomp);
                comp = dfl_comp_sz;
            } else {
                uncomp = pack_store(fd_out, &crc);
                comp   = uncomp;
            }
            close(fd_in);

            tx_char(' ');
            tx_dec32(uncomp);
            if (use_deflate && comp != uncomp) {
                tx_string(" -> ");
                tx_dec32(comp);
            }
            tx_string(" B" NEWLINE);
        }

        write_dd(fd_out, crc, comp, uncomp);

        /* save central directory entry */
        memcpy(cdir[cdir_n].fname, arc_name, fnlen + 1u);
        cdir[cdir_n].fnlen  = fnlen;
        cdir[cdir_n].crc32  = crc;
        cdir[cdir_n].comp   = comp;
        cdir[cdir_n].uncomp = uncomp;
        cdir[cdir_n].offset = local_off;
        cdir[cdir_n].fdate  = dent.fdate;
        cdir[cdir_n].ftime  = dent.ftime;
        cdir[cdir_n].method = (unsigned char)method;
        cdir_n++;
    }

    f_closedir(dirdes);

    /* write central directory */
    cd_start = out_pos;
    for (i = 0; i < (int)cdir_n; i++) write_cdh(fd_out, &cdir[i]);
    cd_size = out_pos - cd_start;

    write_eocd(fd_out, cdir_n, cd_start, cd_size);
    close(fd_out);

    tx_string(NEWLINE "Done: ");
    tx_dec32((unsigned long)cdir_n);
    tx_string(" file(s), ");
    tx_dec32(out_pos);
    tx_string(" bytes -> ");
    tx_string(arc_path);
    tx_string(NEWLINE);

    return rc;
}
