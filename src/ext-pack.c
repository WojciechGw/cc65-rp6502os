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

#define APPVER "20260411.1803"

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

/* ---- TX helpers --------------------------------------------------------- */

#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define TX_READY_SPIN while(!TX_READY)

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

    if (argc == 1 && strcmp(argv[0], "/?") == 0) {
        tx_string(NEWLINE
            "Command : pack" NEWLINE NEWLINE
            "Create a ZIP archive from a directory" NEWLINE NEWLINE
            "Usage:" NEWLINE
            "  pack <dirname>      STORE (no compression)" NEWLINE
            "  pack <dirname> /d   DEFLATE (4KB window LZ77, smaller/slower)" NEWLINE
            "Output: <dirname>.zip" NEWLINE);
        return 0;
    }
    if (argc == 0 || argv[0][0] == '/') {
        tx_string(NEWLINE "Usage: pack <dirname> [/d]" NEWLINE);
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
