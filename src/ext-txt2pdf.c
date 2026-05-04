/*
 * ext-txt2pdf.c — plain-text to PDF converter for razemOS / Picocomputer 6502
 *
 * Usage: txt2pdf <file.txt> [/s N] [/m L R T B] [/n d|p]
 *   Formatting is always active based on file extension:
 *           .asm — lines starting with ';' printed in italic (Courier-Oblique)
 *           .txt/.md — lines starting with '#'/'##'/... printed as headings
 *                      font size = base + hashes*2 pt
 *   /s N  : set base font size in pt (default 10); affects char width, line height,
 *           and heading sizes (heading = base + hashes*2 pt)
 *   All margin arguments are optional; default = 20 mm each.
 *   Output: input name with extension replaced by .pdf (or .pdf appended).
 *
 * PDF format: PDF-1.4, A4 page, Courier (Base14 — no embedding needed).
 *
 * write() is hijacked to UART by write_stub.c, so all file output goes through
 * write_xram() with a small XRAM staging window.
 *
 * Two-pass strategy per page content stream:
 *   Pass A (dry run): measure stream byte length by counting without writing.
 *   Pass B (write):   emit real data with the exact /Length already known.
 * This avoids needing to seek back in the output to patch the length field.
 */

#include "commons.h"

#define APPVER "20260504.0609"

/* ---- compile-time constants ---------------------------------------------- */

#define FNAMELEN      64u
#define MAX_PAGES     64u
#define LINE_BUF_LEN  132u    /* max characters per source line kept in RAM */

/* A4 page size in points */
#define PAGE_W_PT   595u
#define PAGE_H_PT   842u

/* Default base font size and margins */
#define DEF_FONT_SIZE  10u
#define DEF_MARGIN     20u

/* 1 mm = 2.835 pt, integer approximation */
#define MM_TO_PT(mm)  ((unsigned)(((unsigned long)(mm) * 2835UL + 500UL) / 1000UL))

/* XRAM staging window for write_xram() calls */
#define XRAM_STAGE    0xA000u
#define STAGE_SIZE    128u

/* ---- PDF object numbering -------------------------------------------------
 * obj 1         : Catalog
 * obj 2         : Pages
 * obj 3         : Font /F1 — Courier (normal)
 * obj 4         : Font /F2 — Courier-Oblique (italic, used only when /f active)
 * obj 5..5+N-1  : Page objects  (N = actual page count, max MAX_PAGES)
 * obj 5+N..4+2N : Content streams (consecutive after pages)
 * Total max objects: 4 + MAX_PAGES*2 = 132
 * Stream object for page i = OBJ_FIRST_PAGE + page_count + i  (runtime)
 * --------------------------------------------------------------------------- */
#define OBJ_CATALOG      1u
#define OBJ_PAGES        2u
#define OBJ_FONT_NORMAL  3u   /* /F1 Courier */
#define OBJ_FONT_ITALIC  4u   /* /F2 Courier-Oblique */
#define OBJ_FIRST_PAGE   5u
#define MAX_OBJS         (4u + 2u + MAX_PAGES * 2u + 2u)  /* +2 title, +2 TOC */

/* page numbering modes */
#define PGNUM_NONE  0u   /* no page numbers */
#define PGNUM_FULL  1u   /* "strona N z T" */
#define PGNUM_SHORT 2u   /* "- N -" */

/* TOC: max entries = MAX_PAGES (one h==1 heading per page at most) */
#define TOC_MAX     MAX_PAGES

/* ---- formatting mode ----------------------------------------------------- */

/* file type detected from extension (always active) */
#define FMT_NONE   0u
#define FMT_ASM    1u   /* .asm: italic comments */
#define FMT_MD     2u   /* .txt / .md: heading sizes */

/*
 * Heading metrics are computed at runtime from base_size and hash count:
 *   fsize = base_size + hashes * 2
 *   fpre  = hashes * 2                      (vertical gap before the line)
 *   fadv  = fsize + 6                        (line advance after the line)
 *   fw    = fsize * 6 / 10                  (char width for column count)
 */
#define HEAD_SIZE(base, h)  ((base) + (unsigned)(h) * 2u)
#define HEAD_PRE(h)         ((unsigned)(h) * 2u)
#define HEAD_ADV(base, h)   (HEAD_SIZE(base, h) + 6u)

/* ---- globals -------------------------------------------------------------- */

static int           pdf_fd;
static unsigned long pdf_pos;            /* running byte offset in output file */
static unsigned long obj_off[MAX_OBJS + 1u]; /* 1-based: obj_off[n] = file offset */

static char inname[FNAMELEN];
static char outname[FNAMELEN];

static unsigned left_pt, right_pt, top_pt, bottom_pt;
static unsigned chars_per_line;  /* columns at base_size normal font */
static unsigned lines_per_page;  /* rows at base_size normal line height */

static unsigned base_size;       /* base font size in pt (default DEF_FONT_SIZE) */
static unsigned font_w_pt;       /* char advance at base_size: base_size * 6 / 10 */
static unsigned font_h_pt;       /* normal line height: base_size * 12 / 10 */

static uint8_t  fmt_mode;        /* FMT_NONE / FMT_ASM / FMT_MD */
static uint8_t  page_num_mode;   /* PGNUM_NONE / PGNUM_FULL / PGNUM_SHORT */
static uint8_t  want_toc;        /* 1 if @NX header found */
static uint8_t  toc_depth;       /* max heading level for TOC (1..9) */
static uint8_t  toc_at_start;    /* 1 = TOC before content, 0 = after */
static off_t    file_body_start; /* file offset after @NX line */

/* TOC entries collected during count_pages */
static uint8_t  toc_page[TOC_MAX];   /* content page number (1-based) for each entry */
static uint8_t  toc_hashes[TOC_MAX]; /* heading level (hash count) for each entry */
static uint8_t  toc_count;           /* number of TOC entries */
static char     toc_title[LINE_BUF_LEN]; /* optional title after "@NX " on header line */

static uint8_t  has_title_page;          /* 1 if '^' line found after header */
static char     doc_title[LINE_BUF_LEN]; /* document title (text after '^') */
static char     doc_author[LINE_BUF_LEN]; /* author line (line after '^' line) */

/* ---- XRAM-based file write helpers --------------------------------------- */

static void pdf_write_chunk(const char *buf, unsigned len) {
    unsigned done = 0u;
    unsigned chunk, i;

    while (done < len) {
        chunk = len - done;
        if (chunk > STAGE_SIZE) chunk = STAGE_SIZE;

        RIA.addr0 = XRAM_STAGE;
        RIA.step0 = 1;
        for (i = 0u; i < chunk; i++) {
            RIA.rw0 = (uint8_t)buf[done + i];
        }

        write_xram(XRAM_STAGE, chunk, pdf_fd);
        pdf_pos += (unsigned long)chunk;
        done    += chunk;
    }
}

static void pdf_puts(const char *s) {
    unsigned len = 0u;
    while (s[len]) len++;
    pdf_write_chunk(s, len);
}

static void pdf_putc(char c) {
    pdf_write_chunk(&c, 1u);
}

static void pdf_puti(unsigned long v) {
    char tmp[12];
    int  i = 12;
    if (v == 0UL) { pdf_putc('0'); return; }
    while (v && i) { tmp[--i] = (char)('0' + (v % 10UL)); v /= 10UL; }
    pdf_write_chunk(tmp + i, (unsigned)(12 - i));
}

static void begin_obj(unsigned n) {
    obj_off[n] = pdf_pos;
    pdf_puti((unsigned long)n);
    pdf_puts(" 0 obj\n");
}

static void end_obj(void) {
    pdf_puts("endobj\n");
}

/* ---- line reader --------------------------------------------------------- */

/*
 * Read one text line from fd into buf (at most maxlen chars, NUL-terminated).
 * Consumes the line terminator (CR, LF, or CRLF).
 * Returns: number of characters placed in buf (>= 0), or -1 on EOF/error.
 * An empty line returns 0. EOF with no characters returns -1.
 */
static int read_line(int fd, char *buf, unsigned maxlen) {
    unsigned n = 0u;
    char c;
    int  r;
    uint8_t got_char = 0u;

    for (;;) {
        r = read(fd, &c, 1u);
        if (r <= 0) {
            if (!got_char) { buf[0] = 0; return -1; }
            break;
        }
        got_char = 1u;
        if (c == '\r') {
            r = read(fd, &c, 1u);
            if (r > 0 && c != '\n') lseek(fd, -1, SEEK_CUR);
            break;
        }
        if (c == '\n') break;
        if (n < maxlen) buf[n++] = c;
    }
    buf[n] = 0;
    return (int)n;
}

/* ---- text-op helpers ----------------------------------------------------- */

/*
 * Count bytes that "(text) Tj\nT*\n" would emit for src[0..len).
 * Parens and backslash need escaping with '\'.
 * Total = 1('(') + len + escapes + 8(") Tj\nT*\n")
 */
static unsigned long measure_text_op(const char *src, unsigned len) {
    unsigned long sz = 9UL;
    unsigned i;
    sz += (unsigned long)len;
    for (i = 0u; i < len; i++) {
        if (src[i] == '(' || src[i] == ')' || src[i] == '\\') sz++;
    }
    return sz;
}

static void emit_text_op(const char *src, unsigned len) {
    unsigned i;
    char esc[2];
    esc[1] = 0;
    pdf_putc('(');
    for (i = 0u; i < len; i++) {
        char ch = src[i];
        if (ch == '(' || ch == ')' || ch == '\\') {
            esc[0] = '\\'; pdf_write_chunk(esc, 1u);
        }
        esc[0] = ch; pdf_write_chunk(esc, 1u);
    }
    pdf_puts(") Tj\nT*\n");
}

/*
 * Measure bytes for a heading line with pre-gap, font change, text, post-advance.
 * Sequence emitted by emit_heading_op:
 *   "0 -PRE Td\n"  — space before heading
 *   "/F1 SS Tf\n"  — switch font size
 *   "(text) Tj\n"  — text (no T* — manual Td)
 *   "0 -ADV Td\n"  — advance by heading line height
 *   "/F1 10 Tf\n"  — restore normal font size
 */
static unsigned long measure_td(unsigned v) {
    /* "0 -V Td\n": "0 -"(3) + digits(V) + " Td\n"(4) = 7 + digits */
    unsigned long sz = 8UL; /* 7 fixed + 1 base digit */
    if (v >= 10u)  sz++;
    if (v >= 100u) sz++;
    return sz;
}

static unsigned long measure_heading_op(const char *src, unsigned len,
                                        unsigned font_size, unsigned adv,
                                        unsigned pre) {
    unsigned long sz = 0UL;
    unsigned i;
    /* "0 -PRE Td\n" */
    sz += measure_td(pre);
    /* "/F1 SS Tf\n": "/F1 "(4) + digits(SS) + " Tf\n"(4) = 8 + digits(SS) */
    sz += 9UL;                    /* 8 fixed + 1 base digit (SS always >= 1) */
    if (font_size >= 10u) sz++;
    if (font_size >= 100u) sz++;
    /* "(text) Tj\n": '('(1) + len + escapes + ") Tj\n"(5) = 6 + len + escapes */
    sz += 6UL;
    sz += (unsigned long)len;
    for (i = 0u; i < len; i++) {
        if (src[i] == '(' || src[i] == ')' || src[i] == '\\') sz++;
    }
    /* "0 -ADV Td\n" */
    sz += measure_td(adv);
    /* "/F1 BS Tf\n": restore base_size — same formula as heading Tf line */
    sz += 9UL;
    if (base_size >= 10u) sz++;
    if (base_size >= 100u) sz++;
    return sz;
}

static void emit_heading_op(const char *src, unsigned len,
                            unsigned font_size, unsigned adv, unsigned pre) {
    unsigned i;
    char esc[2];
    esc[1] = 0;

    /* space before heading */
    pdf_puts("0 -");
    pdf_puti((unsigned long)pre);
    pdf_puts(" Td\n");

    /* switch to heading size */
    pdf_puts("/F1 ");
    pdf_puti((unsigned long)font_size);
    pdf_puts(" Tf\n");

    /* emit text (no T* — manual Td advance) */
    pdf_putc('(');
    for (i = 0u; i < len; i++) {
        char ch = src[i];
        if (ch == '(' || ch == ')' || ch == '\\') {
            esc[0] = '\\'; pdf_write_chunk(esc, 1u);
        }
        esc[0] = ch; pdf_write_chunk(esc, 1u);
    }
    pdf_puts(") Tj\n");

    /* advance by heading line height */
    pdf_puts("0 -");
    pdf_puti((unsigned long)adv);
    pdf_puts(" Td\n");

    /* restore base font size */
    pdf_puts("/F1 ");
    pdf_puti((unsigned long)base_size);
    pdf_puts(" Tf\n");
}

/*
 * Measure bytes for italic comment line:
 *   "/F2 BS Tf\n"  — switch to italic
 *   "(text) Tj\nT*\n"
 *   "/F1 BS Tf\n"  — restore normal
 * "/FN BS Tf\n": "/FN "(4) + digits(BS) + " Tf\n"(4) = 8 + digits
 */
static unsigned long measure_font_switch(void) {
    unsigned long sz = 9UL; /* 8 fixed + 1 base digit */
    if (base_size >= 10u)  sz++;
    if (base_size >= 100u) sz++;
    return sz;
}

static unsigned long measure_italic_op(const char *src, unsigned len) {
    return measure_font_switch() + measure_text_op(src, len) + measure_font_switch();
}

static void emit_italic_op(const char *src, unsigned len) {
    pdf_puts("/F2 ");
    pdf_puti((unsigned long)base_size);
    pdf_puts(" Tf\n");
    emit_text_op(src, len);
    pdf_puts("/F1 ");
    pdf_puti((unsigned long)base_size);
    pdf_puts(" Tf\n");
}

/* ---- line formatting classifier ------------------------------------------ */

/*
 * Count leading '#' chars in buf, or 0 if not a heading line.
 * A heading must be followed by '>', ' ', or end of string.
 * If the '#' block is followed immediately by '>' (before any space/text),
 * *force_np is set to 1 (force new page before this heading).
 * Used only in FMT_MD mode.
 */
static unsigned count_hashes(const char *buf, uint8_t *force_np) {
    unsigned n = 0u;
    *force_np = 0u;
    while (buf[n] == '#') n++;
    if (n == 0u) return 0u;
    if (buf[n] == '>') { *force_np = 1u; n++; } /* consume the '>' */
    /* must be followed by space or end of string */
    if (buf[n] != ' ' && buf[n] != 0) return 0u;
    return n - (unsigned)*force_np; /* return hash count without the '>' */
}

/* ---- content stream ------------------------------------------------------- */

/*
 * EMIT_S / EMIT_I / EMIT_C macros: measure or write depending on measure_only.
 */
#define EMIT_S(s)   do { if (mo) { const char *_p=(s); unsigned _l=0u; while(_p[_l])_l++; sz+=(unsigned long)_l; } else pdf_puts(s); } while(0)
#define EMIT_I(v)   do { unsigned long _v=(unsigned long)(v); char _t[12]; int _i=12; \
                         if(!_v){if(mo)sz++;else pdf_putc('0');}           \
                         else{while(_v&&_i){_t[--_i]=(char)('0'+(_v%10UL));_v/=10UL;} \
                              if(mo)sz+=(unsigned long)(12-_i);else pdf_write_chunk(_t+_i,(unsigned)(12-_i));} \
                    } while(0)

/*
 * Compute advance in "lines_per_page units" for a heading.
 * heading_adv is the actual pt advance; FONT_H_PT is the normal line height.
 * We use ceiling division so partial rows are counted.
 */
static unsigned heading_line_units(unsigned adv) {
    return (adv + font_h_pt - 1u) / font_h_pt;
}

/*
 * Measure (dry-run) or emit the BT...ET block for one page.
 * Reads lines from current fd position; stops after lines_per_page rows consumed
 * or EOF.  "rows" for a heading = ceil(adv / FONT_H_PT).
 *
 * Returns byte count when measure_only, 0 otherwise.
 * After return, fd is positioned past all lines consumed for this page.
 */
static unsigned long content_block(int in_fd, uint8_t mo) {
    char     lbuf[LINE_BUF_LEN];
    int      n;
    unsigned cur_line = 0u;   /* rows consumed so far */
    unsigned long sz  = 0UL;

    /* BT header */
    EMIT_S("BT\n/F1 ");
    EMIT_I(base_size);
    EMIT_S(" Tf\n");
    EMIT_I(left_pt);
    EMIT_S(" ");
    EMIT_I(PAGE_H_PT - top_pt - font_h_pt);
    EMIT_S(" Td\n0 -");
    EMIT_I(font_h_pt);
    EMIT_S(" TD\n");

    while (cur_line < lines_per_page) {
        unsigned offset    = 0u;
        unsigned remaining;
        unsigned hashes    = 0u;
        uint8_t  italic    = 0u;
        uint8_t  force_np  = 0u;
        unsigned row_adv   = 1u;   /* how many lines_per_page rows this line uses */

        n = read_line(in_fd, lbuf, LINE_BUF_LEN - 1u);
        if (n < 0) break;

        remaining = (unsigned)n;

        /* classify the line for formatting */
        if (fmt_mode == FMT_ASM && remaining > 0u && lbuf[0] == ';') {
            italic = 1u;
        } else if ((fmt_mode == FMT_MD || want_toc) && remaining > 0u) {
            hashes = count_hashes(lbuf, &force_np);
            if (fmt_mode != FMT_MD) hashes = 0u; /* detect only for force_np */
        }

        /* force new page: push line back and break to end current page */
        if (force_np && cur_line > 0u) {
            lseek(in_fd, -(off_t)(remaining + 1u), SEEK_CUR);
            break;
        }

        if (hashes > 0u) {
            /* skip "###" + optional ">" + optional " " */
            unsigned gt   = force_np ? 1u : 0u;
            unsigned skip = hashes + gt + (lbuf[hashes + gt] == ' ' ? 1u : 0u);
            const char *text = lbuf + skip;
            unsigned text_len;
            unsigned fsize, fadv, fpre, fw, max_cols;

            fsize = HEAD_SIZE(base_size, hashes);
            fpre  = HEAD_PRE(hashes);
            fadv  = HEAD_ADV(base_size, hashes);
            fw    = (fsize * 6u) / 10u;

            /* available width in chars at this heading font size */
            {
                unsigned usable_w = PAGE_W_PT - left_pt - right_pt;
                max_cols = usable_w / fw;
            }
            text_len = (unsigned)strlen(text);
            if (text_len > max_cols) text_len = max_cols; /* truncate — no wrap for headings */

            row_adv = heading_line_units(fadv + fpre);
            if (cur_line + row_adv > lines_per_page) {
                /* heading doesn't fit on this page — push back and end page */
                lseek(in_fd, -(off_t)(remaining + 1u), SEEK_CUR);
                break;
            }

            if (mo) sz += measure_heading_op(text, text_len, fsize, fadv, fpre);
            else    emit_heading_op(text, text_len, fsize, fadv, fpre);

            cur_line += row_adv;
        } else {
            /* normal or italic line — wrap at chars_per_line */
            while (remaining > chars_per_line) {
                if (italic) {
                    if (mo) sz += measure_italic_op(lbuf + offset, chars_per_line);
                    else    emit_italic_op(lbuf + offset, chars_per_line);
                } else {
                    if (mo) sz += measure_text_op(lbuf + offset, chars_per_line);
                    else    emit_text_op(lbuf + offset, chars_per_line);
                }
                offset    += chars_per_line;
                remaining -= chars_per_line;
                cur_line++;
                if (cur_line >= lines_per_page) goto page_full;
            }

            /* last (or only) segment */
            if (italic) {
                if (mo) sz += measure_italic_op(lbuf + offset, remaining);
                else    emit_italic_op(lbuf + offset, remaining);
            } else {
                if (mo) sz += measure_text_op(lbuf + offset, remaining);
                else    emit_text_op(lbuf + offset, remaining);
            }
            cur_line++;
        }
    }

page_full:
    EMIT_S("ET\n");

#undef EMIT_S
#undef EMIT_I

    return sz;
}

/* ---- page counting ------------------------------------------------------- */

static unsigned count_pages(int fd) {
    char     lbuf[LINE_BUF_LEN];
    int      n;
    unsigned cur_line = 0u;
    unsigned pages    = 1u;

    toc_count = 0u;
    lseek(fd, file_body_start, SEEK_SET);

    while ((n = read_line(fd, lbuf, LINE_BUF_LEN - 1u)) >= 0) {
        unsigned rows = 1u;
        unsigned h    = 0u;

        if ((fmt_mode == FMT_MD || want_toc) && n > 0) {
            uint8_t fnp = 0u;
            h = count_hashes(lbuf, &fnp);
            if (h > 0u) {
                if (fmt_mode == FMT_MD) {
                    unsigned fadv = HEAD_ADV(base_size, h);
                    unsigned fpre = HEAD_PRE(h);
                    rows = heading_line_units(fadv + fpre);
                }
                /* force new page: flush remaining rows to next page */
                if (fnp && cur_line > 0u) {
                    pages++;
                    cur_line = 0u;
                }
            }
        }

        /* wrap: each wrapped segment is one additional row */
        if (h == 0u) {
            unsigned col = (unsigned)n;
            rows = 0u;
            while (col > chars_per_line) { rows++; col -= chars_per_line; }
            rows++; /* last segment */
        }

        /* record TOC entry */
        if (h >= 1u && want_toc && h <= (unsigned)toc_depth && toc_count < TOC_MAX) {
            toc_page[toc_count]   = (uint8_t)pages;
            toc_hashes[toc_count] = (uint8_t)h;
            toc_count++;
        }

        cur_line += rows;
        while (cur_line >= lines_per_page) {
            pages++;
            cur_line -= lines_per_page;
        }
    }
    return pages;
}

/* ---- page number footer --------------------------------------------------- */

/*
 * Measure / emit a page-number footer BT block.
 * Positioned at bottom_pt - MM_TO_PT(5), centred horizontally.
 * PGNUM_FULL : "page N of T"
 * PGNUM_SHORT: "- N -"
 * Uses base_size font (/F1).
 */
static unsigned long measure_pgnum_str(unsigned pgn, unsigned total) {
    /* count decimal digits */
    unsigned long sz = 0UL;
    unsigned v;
    if (page_num_mode == PGNUM_FULL) {
        /* "strona " + digits(N) + " z " + digits(T) */
        sz = 7UL + 3UL; /* "page "(7) + " of "(3) */
        v = pgn;  do { sz++; v /= 10u; } while (v);
        v = total; do { sz++; v /= 10u; } while (v);
    } else {
        /* "- " + digits(N) + " -" */
        sz = 4UL; /* "- "(2) + " -"(2) */
        v = pgn; do { sz++; v /= 10u; } while (v);
    }
    return sz;
}

static void emit_pgnum_str(unsigned pgn, unsigned total) {
    if (page_num_mode == PGNUM_FULL) {
        pdf_puts("strona ");
        pdf_puti((unsigned long)pgn);
        pdf_puts(" z ");
        pdf_puti((unsigned long)total);
    } else {
        pdf_puts("- ");
        pdf_puti((unsigned long)pgn);
        pdf_puts(" -");
    }
}

/*
 * Measure bytes of the page-number BT block appended to a content stream.
 * "BT\n/F1 BS Tf\nX Y Td\n(text) Tj\nET\n"
 * X is computed to centre the string; it varies with pgn — we use the
 * widest possible string for measurement so measure and emit always agree.
 * Strategy: measure uses same pgn/total as emit (called twice from
 * write_stream_obj with the same args), so they always match.
 */
static unsigned long measure_pgnum_block(unsigned pgn, unsigned total) {
    unsigned long str_len  = measure_pgnum_str(pgn, total);
    unsigned long x_digits;
    unsigned x_val;
    unsigned long sz;

    /* X = (PAGE_W_PT - str_len * font_w_pt) / 2 */
    x_val = (PAGE_W_PT - (unsigned)(str_len * (unsigned long)font_w_pt)) / 2u;

    /* count digits in x_val */
    x_digits = 0UL;
    { unsigned v = x_val; do { x_digits++; v /= 10u; } while (v); }

    /* "BT\n/F1 "(7) + digits(base_size) + " Tf\n"(4) = 11+d(bs) */
    sz  = 11UL;
    if (base_size >= 10u) sz++;
    if (base_size >= 100u) sz++;

    /* X digits + " "(1) + Y digits + " Td\n"(4) */
    { unsigned y_val = (bottom_pt > MM_TO_PT(2u)) ? bottom_pt - MM_TO_PT(2u) : 0u;
      unsigned long yd = 0UL;
      if (y_val == 0u) yd = 1UL; else { unsigned v = y_val; do { yd++; v /= 10u; } while (v); }
      sz += x_digits + 1UL + yd + 4UL; }

    /* "(text) Tj\n" = 1 + str_len + escapes + 5 */
    sz += 6UL + str_len; /* no special chars in page number string */

    /* "ET\n" */
    sz += 3UL;

    return sz;
}

static void emit_pgnum_block(unsigned pgn, unsigned total) {
    unsigned long str_len = measure_pgnum_str(pgn, total);
    unsigned x_val = (PAGE_W_PT - (unsigned)(str_len * (unsigned long)font_w_pt)) / 2u;

    pdf_puts("BT\n/F1 ");
    pdf_puti((unsigned long)base_size);
    pdf_puts(" Tf\n");
    pdf_puti((unsigned long)x_val);
    pdf_putc(' ');
    pdf_puti((unsigned long)((bottom_pt > MM_TO_PT(2u)) ? bottom_pt - MM_TO_PT(2u) : 0u));
    pdf_puts(" Td\n(");
    emit_pgnum_str(pgn, total);
    pdf_puts(") Tj\nET\n");
}

/* ---- TOC stream ----------------------------------------------------------- */

/*
 * Emit the TOC content stream (measure or write).
 * Reads the input file again, collects h==1 heading lines in order,
 * prints each as: "heading text ......... N" where N = page number.
 * Uses base_size font, normal margins.
 */
static unsigned long toc_block(int in_fd, uint8_t mo, unsigned content_pages) {
    char     lbuf[LINE_BUF_LEN];
    int      n;
    unsigned cur_line = 0u;
    unsigned toc_idx  = 0u;
    unsigned long sz  = 0UL;
    unsigned usable_w = PAGE_W_PT - left_pt - right_pt;

#define EMIT_S(s)   do { if (mo) { const char *_p=(s); unsigned _l=0u; while(_p[_l])_l++; sz+=(unsigned long)_l; } else pdf_puts(s); } while(0)
#define EMIT_I(v)   do { unsigned long _v=(unsigned long)(v); char _t[12]; int _i=12; \
                         if(!_v){if(mo)sz++;else pdf_putc('0');}           \
                         else{while(_v&&_i){_t[--_i]=(char)('0'+(_v%10UL));_v/=10UL;} \
                              if(mo)sz+=(unsigned long)(12-_i);else pdf_write_chunk(_t+_i,(unsigned)(12-_i));} \
                    } while(0)

    (void)content_pages;

    EMIT_S("BT\n/F1 ");
    EMIT_I(base_size);
    EMIT_S(" Tf\n");
    EMIT_I(left_pt);
    EMIT_S(" ");
    EMIT_I(PAGE_H_PT - top_pt - font_h_pt);
    EMIT_S(" Td\n0 -");
    EMIT_I(font_h_pt);
    EMIT_S(" TD\n");

    /* emit title line if present — rendered as H1 heading style */
    if (toc_title[0]) {
        unsigned tlen = (unsigned)strlen(toc_title);
        unsigned fsize = HEAD_SIZE(base_size, 1u);
        unsigned fadv  = HEAD_ADV(base_size, 1u);
        unsigned fpre  = HEAD_PRE(1u);
        unsigned max_cols;
        { unsigned usable = PAGE_W_PT - left_pt - right_pt;
          unsigned fw = (fsize * 6u) / 10u;
          max_cols = fw ? usable / fw : usable; }
        if (tlen > max_cols) tlen = max_cols;
        if (mo) sz += measure_heading_op(toc_title, tlen, fsize, fadv, fpre);
        else    emit_heading_op(toc_title, tlen, fsize, fadv, fpre);
        cur_line += heading_line_units(fadv + fpre);
    }

    lseek(in_fd, (off_t)file_body_start, SEEK_SET);

    while (toc_idx < toc_count && cur_line < lines_per_page) {
        unsigned pgn = 0u, text_start, text_len = 0u, dots = 0u, num_w, i;

        /* pre-compute num_w from stored page number (adjusted for TOC position) */
        { unsigned tpgn = (unsigned)toc_page[toc_idx] + (toc_at_start ? 1u : 0u);
          num_w = 0u;
          { unsigned v = tpgn; do { num_w++; v /= 10u; } while (v); }
        }

        /* scan for next heading matching toc_hashes[toc_idx] level */
        { unsigned want_h = (unsigned)toc_hashes[toc_idx];
          uint8_t  fnp2   = 0u;
          while ((n = read_line(in_fd, lbuf, LINE_BUF_LEN - 1u)) >= 0) {
              if (n > 0 && count_hashes(lbuf, &fnp2) == want_h) break;
          }
        }
        if (n < 0) break;

        { uint8_t fnp2 = 0u;
          unsigned hh  = count_hashes(lbuf, &fnp2);
          unsigned gt  = fnp2 ? 1u : 0u;
          text_start   = hh + gt + (lbuf[hh + gt] == ' ' ? 1u : 0u);
          pgn          = (unsigned)toc_page[toc_idx];
          /* indent by (h-1)*2 spaces for hierarchy */
          { unsigned indent = ((unsigned)toc_hashes[toc_idx] - 1u) * 2u;
            toc_idx++;
            text_len = (unsigned)strlen(lbuf + text_start);

            /* dots fill accounting for indent */
            { unsigned total_chars = usable_w / font_w_pt;
              unsigned used = indent + text_len + 1u + num_w;
              dots = (used < total_chars) ? (total_chars - used) : 0u;
              if (indent + text_len + 1u + num_w > total_chars) {
                  if (text_len > total_chars - indent - 1u - num_w)
                      text_len = total_chars - indent - 1u - num_w;
                  dots = 0u;
              }
            }

            /* emit: "(" + indent spaces + text */
            if (mo) {
                sz += 1UL + (unsigned long)indent;
                sz += (unsigned long)text_len;
                for (i = 0u; i < text_len; i++)
                    if (lbuf[text_start + i] == '(' || lbuf[text_start + i] == ')' || lbuf[text_start + i] == '\\') sz++;
            } else {
                unsigned ii;
                char esc[2];
                esc[1] = 0;
                pdf_putc('(');
                for (ii = 0u; ii < indent; ii++) pdf_putc(' ');
                for (ii = 0u; ii < text_len; ii++) {
                    char ch = lbuf[text_start + ii];
                    if (ch=='('||ch==')'||ch=='\\') { esc[0]='\\'; pdf_write_chunk(esc,1u); }
                    esc[0] = ch; pdf_write_chunk(esc, 1u);
                }
            }
            /* dots */
            if (mo) sz += (unsigned long)dots;
            else { unsigned ii; for (ii=0u;ii<dots;ii++) pdf_putc('.'); }
            /* space + page number */
            { unsigned tpgn = pgn + (toc_at_start ? 1u : 0u);
              if (mo) {
                  sz += 1UL;
                  { unsigned v = tpgn; do { sz++; v /= 10u; } while (v); }
              } else {
                  pdf_putc(' ');
                  { unsigned v = tpgn; int pi = 6; char pb[6];
                    do { pb[--pi] = (char)('0' + v % 10u); v /= 10u; } while (v);
                    pdf_write_chunk(pb + pi, (unsigned)(6 - pi)); }
              }
            }
            /* ") Tj\nT*\n" = 8 bytes; '(' already counted above */
            if (mo) sz += 8UL;
            else pdf_puts(") Tj\nT*\n");
          }
        }

        cur_line++;
    }

    EMIT_S("ET\n");

#undef EMIT_S
#undef EMIT_I

    return sz;
}

/* ---- title page stream ---------------------------------------------------- */

/*
 * Emit the title page content stream (measure or write).
 * Title   at 1/3 of usable height, centred, 3*base_size pt.
 * Author  at 2/3 of usable height, centred, base_size pt.
 * Positions are absolute Td from page origin (PDF Y=0 at bottom).
 */
static unsigned long title_page_block(uint8_t mo) {
    unsigned long sz = 0UL;
    unsigned title_fsize = base_size * 3u;
    unsigned title_fw    = (title_fsize * 6u) / 10u;
    unsigned usable_h    = PAGE_H_PT - top_pt - bottom_pt;
    unsigned usable_w    = PAGE_W_PT - left_pt - right_pt;

    unsigned tlen = (unsigned)strlen(doc_title);
    unsigned alen = (unsigned)strlen(doc_author);
    unsigned max_title_cols = title_fw ? usable_w / title_fw : usable_w;
    unsigned max_base_cols  = font_w_pt ? usable_w / font_w_pt : usable_w;
    unsigned tx, ty, ax, ay;

    if (tlen > max_title_cols) tlen = max_title_cols;
    if (alen > max_base_cols)  alen = max_base_cols;

    /* Y positions: PDF origin at bottom-left */
    ty = bottom_pt + (usable_h * 2u) / 3u;  /* 1/3 from top = 2/3 from bottom */
    ay = bottom_pt + usable_h / 3u;          /* 2/3 from top = 1/3 from bottom */

    /* X: centre each string */
    tx = left_pt + (usable_w > tlen * title_fw ? (usable_w - tlen * title_fw) / 2u : 0u);
    ax = left_pt + (usable_w > alen * font_w_pt ? (usable_w - alen * font_w_pt) / 2u : 0u);

#define EMIT_S(s)   do { if (mo) { const char *_p=(s); unsigned _l=0u; while(_p[_l])_l++; sz+=(unsigned long)_l; } else pdf_puts(s); } while(0)
#define EMIT_I(v)   do { unsigned long _v=(unsigned long)(v); char _t[12]; int _i=12; \
                         if(!_v){if(mo)sz++;else pdf_putc('0');}           \
                         else{while(_v&&_i){_t[--_i]=(char)('0'+(_v%10UL));_v/=10UL;} \
                              if(mo)sz+=(unsigned long)(12-_i);else pdf_write_chunk(_t+_i,(unsigned)(12-_i));} \
                    } while(0)

    /* title: BT /F1 3*base Tf  TX TY Td (text) Tj ET */
    EMIT_S("BT\n/F1 ");
    EMIT_I(title_fsize);
    EMIT_S(" Tf\n");
    EMIT_I(tx);
    EMIT_S(" ");
    EMIT_I(ty);
    EMIT_S(" Td\n(");
    { unsigned i;
      for (i = 0u; i < tlen; i++) {
          char ch = doc_title[i];
          if (mo) { sz++; if (ch=='('||ch==')'||ch=='\\') sz++; }
          else {
              char esc[2]; esc[1]=0;
              if (ch=='('||ch==')'||ch=='\\') { esc[0]='\\'; pdf_write_chunk(esc,1u); }
              esc[0]=ch; pdf_write_chunk(esc,1u);
          }
      }
    }
    EMIT_S(") Tj\n");

    /* author: /F1 base Tf  1 0 0 1 AX AY Tm (text) Tj — absolute position */
    EMIT_S("/F1 ");
    EMIT_I(base_size);
    EMIT_S(" Tf\n1 0 0 1 ");
    EMIT_I(ax);
    EMIT_S(" ");
    EMIT_I(ay);
    EMIT_S(" Tm\n(");
    { unsigned i;
      for (i = 0u; i < alen; i++) {
          char ch = doc_author[i];
          if (mo) { sz++; if (ch=='('||ch==')'||ch=='\\') sz++; }
          else {
              char esc[2]; esc[1]=0;
              if (ch=='('||ch==')'||ch=='\\') { esc[0]='\\'; pdf_write_chunk(esc,1u); }
              esc[0]=ch; pdf_write_chunk(esc,1u);
          }
      }
    }
    EMIT_S(") Tj\nET\n");

#undef EMIT_S
#undef EMIT_I

    return sz;
}

static void write_title_page_obj(unsigned page_obj, unsigned stream_obj) {
    unsigned long stream_len;

    /* page object */
    begin_obj(page_obj);
    pdf_puts("<< /Type /Page\n"
             "   /Parent ");
    pdf_puti((unsigned long)OBJ_PAGES);
    pdf_puts(" 0 R\n"
             "   /MediaBox [0 0 ");
    pdf_puti((unsigned long)PAGE_W_PT);
    pdf_putc(' ');
    pdf_puti((unsigned long)PAGE_H_PT);
    pdf_puts("]\n"
             "   /Resources << /Font << /F1 ");
    pdf_puti((unsigned long)OBJ_FONT_NORMAL);
    pdf_puts(" 0 R >> >>\n"
             "   /Contents ");
    pdf_puti((unsigned long)stream_obj);
    pdf_puts(" 0 R\n>>\n");
    end_obj();

    /* stream object */
    stream_len = title_page_block(1u);
    begin_obj(stream_obj);
    pdf_puts("<< /Length ");
    pdf_puti(stream_len);
    pdf_puts(" >>\nstream\n");
    title_page_block(0u);
    pdf_puts("endstream\n");
    end_obj();
}

/* ---- PDF structure writers ----------------------------------------------- */

static void write_header(void) {
    pdf_puts("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
}

static void write_catalog(void) {
    begin_obj(OBJ_CATALOG);
    pdf_puts("<< /Type /Catalog /Pages ");
    pdf_puti((unsigned long)OBJ_PAGES);
    pdf_puts(" 0 R >>\n");
    end_obj();
}

static void write_pages_dict(unsigned content_pages) {
    unsigned i;
    /* T: object offset for title page (2 objects: page+stream) */
    unsigned T = has_title_page ? 2u : 0u;
    unsigned total_pages = content_pages + (want_toc ? 1u : 0u) + (has_title_page ? 1u : 0u);
    /* content page objects start at OBJ_FIRST_PAGE + T */
    unsigned first_content = OBJ_FIRST_PAGE + T;
    /* TOC page object: after all content page objects */
    unsigned toc_pobj = first_content + content_pages;

    begin_obj(OBJ_PAGES);
    pdf_puts("<< /Type /Pages /Count ");
    pdf_puti((unsigned long)total_pages);
    pdf_puts("\n   /Kids [");

    /* title page always first */
    if (has_title_page) {
        pdf_puti((unsigned long)OBJ_FIRST_PAGE);
        pdf_puts(" 0 R ");
    }

    if (want_toc && toc_at_start) {
        pdf_puti((unsigned long)toc_pobj);
        pdf_puts(" 0 R");
        if (content_pages > 0u) pdf_putc(' ');
    }
    for (i = 0u; i < content_pages; i++) {
        pdf_puti((unsigned long)(first_content + i));
        pdf_puts(" 0 R");
        if (i + 1u < content_pages || (want_toc && !toc_at_start)) pdf_putc(' ');
    }
    if (want_toc && !toc_at_start) {
        pdf_puti((unsigned long)toc_pobj);
        pdf_puts(" 0 R");
    }
    pdf_puts("]\n>>\n");
    end_obj();
}

static void write_fonts(void) {
    begin_obj(OBJ_FONT_NORMAL);
    pdf_puts("<< /Type /Font /Subtype /Type1\n"
             "   /BaseFont /Courier\n"
             ">>\n");
    end_obj();

    begin_obj(OBJ_FONT_ITALIC);
    pdf_puts("<< /Type /Font /Subtype /Type1\n"
             "   /BaseFont /Courier-Oblique\n"
             ">>\n");
    end_obj();
}

static void write_page_obj(unsigned page_obj, unsigned stream_obj) {
    begin_obj(page_obj);
    pdf_puts("<< /Type /Page\n"
             "   /Parent ");
    pdf_puti((unsigned long)OBJ_PAGES);
    pdf_puts(" 0 R\n"
             "   /MediaBox [0 0 ");
    pdf_puti((unsigned long)PAGE_W_PT);
    pdf_putc(' ');
    pdf_puti((unsigned long)PAGE_H_PT);
    pdf_puts("]\n"
             "   /Resources << /Font << /F1 ");
    pdf_puti((unsigned long)OBJ_FONT_NORMAL);
    pdf_puts(" 0 R /F2 ");
    pdf_puti((unsigned long)OBJ_FONT_ITALIC);
    pdf_puts(" 0 R >> >>\n"
             "   /Contents ");
    pdf_puti((unsigned long)stream_obj);
    pdf_puts(" 0 R\n>>\n");
    end_obj();
}

static void write_stream_obj(int in_fd, unsigned stream_obj,
                             unsigned pgn, unsigned total_pages) {
    unsigned long stream_len;
    unsigned long before_read;

    before_read = (unsigned long)lseek(in_fd, 0, SEEK_CUR);

    stream_len = content_block(in_fd, 1u);
    if (page_num_mode != PGNUM_NONE)
        stream_len += measure_pgnum_block(pgn, total_pages);

    lseek(in_fd, (off_t)before_read, SEEK_SET);

    begin_obj(stream_obj);
    pdf_puts("<< /Length ");
    pdf_puti(stream_len);
    pdf_puts(" >>\nstream\n");
    content_block(in_fd, 0u);
    if (page_num_mode != PGNUM_NONE)
        emit_pgnum_block(pgn, total_pages);
    pdf_puts("endstream\n");
    end_obj();
}

static void write_toc_stream_obj(int in_fd, unsigned stream_obj,
                                 unsigned content_pages) {
    unsigned long stream_len;
    unsigned long before_read;

    before_read = (unsigned long)lseek(in_fd, 0, SEEK_CUR);
    stream_len = toc_block(in_fd, 1u, content_pages);
    if (page_num_mode != PGNUM_NONE)
        stream_len += measure_pgnum_block(content_pages + 1u, content_pages + 1u);

    lseek(in_fd, (off_t)before_read, SEEK_SET);

    begin_obj(stream_obj);
    pdf_puts("<< /Length ");
    pdf_puti(stream_len);
    pdf_puts(" >>\nstream\n");
    toc_block(in_fd, 0u, content_pages);
    if (page_num_mode != PGNUM_NONE)
        emit_pgnum_block(content_pages + 1u, content_pages + 1u);
    pdf_puts("endstream\n");
    end_obj();
}

static void write_xref_and_trailer(unsigned total_objs) {
    unsigned long xref_pos = pdf_pos;
    unsigned      i, j;
    char          entry[21];

    pdf_puts("xref\n0 ");
    pdf_puti((unsigned long)(total_objs + 1u));
    pdf_puts("\n");

    pdf_puts("0000000000 65535 f \n");

    for (i = 1u; i <= total_objs; i++) {
        unsigned long off = obj_off[i];
        for (j = 10u; j > 0u; j--) {
            entry[j - 1u] = (char)('0' + (off % 10UL));
            off /= 10UL;
        }
        entry[10] = ' '; entry[11] = '0'; entry[12] = '0';
        entry[13] = '0'; entry[14] = '0'; entry[15] = '0';
        entry[16] = ' '; entry[17] = 'n'; entry[18] = ' ';
        entry[19] = '\n';
        pdf_write_chunk(entry, 20u);
    }

    pdf_puts("trailer\n<< /Size ");
    pdf_puti((unsigned long)(total_objs + 1u));
    pdf_puts("\n   /Root ");
    pdf_puti((unsigned long)OBJ_CATALOG);
    pdf_puts(" 0 R\n>>\nstartxref\n");
    pdf_puti(xref_pos);
    pdf_puts("\n%%EOF\n");
}

/* ---- main ----------------------------------------------------------------- */

/* case-insensitive strcmp for short strings */
static uint8_t ext_eq(const char *name, const char *ext) {
    unsigned slen, elen, i;
    slen = (unsigned)strlen(name);
    elen = (unsigned)strlen(ext);
    if (slen < elen) return 0u;
    name = name + slen - elen;
    for (i = 0u; i < elen; i++) {
        char a = name[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0u;
    }
    return 1u;
}

int main(int argc, char **argv) {
    unsigned left_mm  = DEF_MARGIN;
    unsigned right_mm = DEF_MARGIN;
    unsigned top_mm   = DEF_MARGIN;
    unsigned bot_mm   = DEF_MARGIN;
    unsigned usable_w, usable_h;
    unsigned page_count, total_objs;
    unsigned i;
    unsigned slen;
    int      in_fd;
    unsigned arg_off = 0u;  /* argv index offset past parsed flags */

    if (argc < 1 || !argv[0][0]) {
        printf(NEWLINE
               "Usage: txt2pdf <file.txt> [/s N] [/m L R T B] [/n d|p]"
               NEWLINE
               "  TOC: start file with '@NL' (N=depth 1-9, L=< start or > end)"
               NEWLINE);
        return 1;
    }

    strncpy(inname, argv[0], FNAMELEN - 1u);
    inname[FNAMELEN - 1u] = 0;

    /* formatting mode set automatically from extension */
    if      (ext_eq(inname, ".asm"))                           fmt_mode = FMT_ASM;
    else if (ext_eq(inname, ".txt") || ext_eq(inname, ".md")) fmt_mode = FMT_MD;
    else                                                       fmt_mode = FMT_NONE;

    /* parse optional flags (order independent) */
    base_size     = DEF_FONT_SIZE;
    page_num_mode = PGNUM_NONE;
    want_toc      = 0u;
    arg_off       = 0u;
    while (argc > 1 + arg_off && argv[1 + arg_off][0] == '/') {
        const char *a = argv[1 + arg_off];
        char flag = a[1];
        if ((flag == 's' || flag == 'S') && a[2] == 0) {
            arg_off++;
            if (argc > 1 + arg_off) {
                unsigned sz = (unsigned)atoi(argv[1 + arg_off]);
                if (sz >= 4u && sz <= 72u) base_size = sz;
                arg_off++;
            }
        } else if ((flag == 'm' || flag == 'M') && a[2] == 0) {
            arg_off++;
            if (argc > 1 + arg_off) { left_mm  = (unsigned)atoi(argv[1 + arg_off]); arg_off++; }
            if (argc > 1 + arg_off) { right_mm = (unsigned)atoi(argv[1 + arg_off]); arg_off++; }
            if (argc > 1 + arg_off) { top_mm   = (unsigned)atoi(argv[1 + arg_off]); arg_off++; }
            if (argc > 1 + arg_off) { bot_mm   = (unsigned)atoi(argv[1 + arg_off]); arg_off++; }
        } else if ((flag == 'n' || flag == 'N') && a[2] == 0) {
            arg_off++;
            if (argc > 1 + arg_off) {
                char sub = argv[1 + arg_off][0];
                if (sub == 'd' || sub == 'D') page_num_mode = PGNUM_FULL;
                else if (sub == 'p' || sub == 'P') page_num_mode = PGNUM_SHORT;
                arg_off++;
            }
        } else {
            break;
        }
    }

    /* Courier fixed-pitch metrics: char width = size * 0.6, line height = size * 1.2 */
    font_w_pt = (base_size * 6u) / 10u;
    font_h_pt = (base_size * 12u) / 10u;
    if (font_w_pt < 1u) font_w_pt = 1u;
    if (font_h_pt < 1u) font_h_pt = 1u;

    left_pt   = MM_TO_PT(left_mm);
    right_pt  = MM_TO_PT(right_mm);
    top_pt    = MM_TO_PT(top_mm);
    bottom_pt = MM_TO_PT(bot_mm);

    usable_w = PAGE_W_PT - left_pt - right_pt;
    usable_h = PAGE_H_PT - top_pt  - bottom_pt;

    if (usable_w < font_w_pt || usable_h < font_h_pt) {
        printf(NEWLINE EXCLAMATION "margins too large" NEWLINE);
        return 1;
    }

    chars_per_line = usable_w / font_w_pt;
    lines_per_page = usable_h / font_h_pt;

    /* build output filename: strip extension, append .pdf */
    strncpy(outname, inname, FNAMELEN - 5u);
    outname[FNAMELEN - 5u] = 0;
    slen = (unsigned)strlen(outname);
    /* find last dot */
    {
        int dot = -1;
        unsigned k;
        for (k = 0u; k < slen; k++) if (outname[k] == '.') dot = (int)k;
        if (dot >= 0) outname[dot] = 0;
    }
    strcat(outname, ".pdf");

    printf(NEWLINE "%s -> %s" NEWLINE, inname, outname);
    printf("Font: %upt, margins L=%u R=%u T=%u B=%u mm" NEWLINE,
           base_size, left_mm, right_mm, top_mm, bot_mm);
    printf("Layout: %u cols, %u rows/page" NEWLINE,
           chars_per_line, lines_per_page);
    want_toc      = 0u;
    toc_depth     = 1u;
    toc_at_start  = 0u;
    file_body_start = 0;

    in_fd = open(inname, O_RDONLY);
    if (in_fd < 0) {
        printf(EXCLAMATION "cannot open %s" NEWLINE, inname);
        return -1;
    }

    /* detect @NX and ^ header lines using read_line for correct CRLF handling */
    toc_title[0]   = 0;
    has_title_page = 0u;
    doc_title[0]   = 0;
    doc_author[0]  = 0;
    {
        char lbuf[LINE_BUF_LEN];
        int  n;

        /* peek at first line */
        file_body_start = 0;
        n = read_line(in_fd, lbuf, LINE_BUF_LEN - 1u);
        if (n >= 3 && lbuf[0] == '@' &&
            lbuf[1] >= '1' && lbuf[1] <= '9' &&
            (lbuf[2] == '<' || lbuf[2] == '>') &&
            (lbuf[3] == ' ' || lbuf[3] == 0)) {
            /* @NX line found */
            want_toc     = 1u;
            toc_depth    = (uint8_t)(lbuf[1] - '0');
            toc_at_start = (lbuf[2] == '<') ? 1u : 0u;
            if (lbuf[3] == ' ') {
                strncpy(toc_title, lbuf + 4, LINE_BUF_LEN - 1u);
                toc_title[LINE_BUF_LEN - 1u] = 0;
            }
            file_body_start = lseek(in_fd, 0, SEEK_CUR);

            /* peek at next line for ^ title page */
            n = read_line(in_fd, lbuf, LINE_BUF_LEN - 1u);
            if (n >= 1 && lbuf[0] == '^') {
                strncpy(doc_title, lbuf + 1, LINE_BUF_LEN - 1u);
                doc_title[LINE_BUF_LEN - 1u] = 0;
                /* author is the next line */
                n = read_line(in_fd, lbuf, LINE_BUF_LEN - 1u);
                if (n >= 0) {
                    strncpy(doc_author, lbuf, LINE_BUF_LEN - 1u);
                    doc_author[LINE_BUF_LEN - 1u] = 0;
                }
                has_title_page = 1u;
                file_body_start = lseek(in_fd, 0, SEEK_CUR);
            } else {
                /* no ^ line — rewind to after @NX */
                lseek(in_fd, (off_t)file_body_start, SEEK_SET);
            }
        } else {
            /* no @NX — rewind to start */
            lseek(in_fd, 0, SEEK_SET);
            file_body_start = 0;

            /* still check for ^ at file start */
            if (n >= 1 && lbuf[0] == '^') {
                strncpy(doc_title, lbuf + 1, LINE_BUF_LEN - 1u);
                doc_title[LINE_BUF_LEN - 1u] = 0;
                n = read_line(in_fd, lbuf, LINE_BUF_LEN - 1u);
                if (n >= 0) {
                    strncpy(doc_author, lbuf, LINE_BUF_LEN - 1u);
                    doc_author[LINE_BUF_LEN - 1u] = 0;
                }
                has_title_page = 1u;
                file_body_start = lseek(in_fd, 0, SEEK_CUR);
            }
        }
    }

    page_count = count_pages(in_fd);
    if (page_count == 0u) {
        printf(EXCLAMATION "read error or empty file" NEWLINE);
        close(in_fd);
        return -1;
    }
    if (page_count > MAX_PAGES) {
        printf(EXCLAMATION "too many pages (max %u)" NEWLINE, MAX_PAGES);
        close(in_fd);
        return -1;
    }

    printf("Pages: %u" NEWLINE, page_count);

    pdf_fd = open(outname, O_WRONLY | O_CREAT | O_TRUNC);
    if (pdf_fd < 0) {
        printf(EXCLAMATION "cannot create %s" NEWLINE, outname);
        close(in_fd);
        return -1;
    }

    pdf_pos    = 0UL;
    /*
     * Object layout (T = 2 if title page, else 0):
     *   1..4              : Catalog, Pages, F1, F2
     *   5..4+T            : Title page obj + stream   (if has_title_page, T=2)
     *   5+T..5+T+N-1      : Content page objects
     *   5+T+N..4+T+2N     : Content streams
     *   5+T+2N            : TOC page object            (if want_toc)
     *   6+T+2N            : TOC stream                 (if want_toc)
     */
    { unsigned T = has_title_page ? 2u : 0u;
      unsigned extra = T + (want_toc ? 2u : 0u);
      total_objs = 4u + page_count * 2u + extra;
    }
    memset(obj_off, 0, sizeof(obj_off));

    lseek(in_fd, (off_t)file_body_start, SEEK_SET);

    write_header();
    write_catalog();
    write_pages_dict(page_count);
    write_fonts();

    /* title page (always obj 5 and 6 when present) */
    if (has_title_page) {
        write_title_page_obj(OBJ_FIRST_PAGE, OBJ_FIRST_PAGE + 1u);
    }

    { unsigned T  = has_title_page ? 2u : 0u;
      unsigned Wt = want_toc ? 1u : 0u;
      /* content page obj i = OBJ_FIRST_PAGE + T + i
         content stream i   = OBJ_FIRST_PAGE + T + page_count + Wt + i */
      for (i = 0u; i < page_count; i++) {
          write_page_obj(OBJ_FIRST_PAGE + T + i,
                         OBJ_FIRST_PAGE + T + page_count + Wt + i);
          write_stream_obj(in_fd,
                           OBJ_FIRST_PAGE + T + page_count + Wt + i,
                           i + 1u,
                           page_count);
      }

      if (want_toc) {
          unsigned toc_page_obj   = OBJ_FIRST_PAGE + T + page_count;
          unsigned toc_stream_obj = OBJ_FIRST_PAGE + T + page_count + 1u + page_count;
          write_page_obj(toc_page_obj, toc_stream_obj);
          write_toc_stream_obj(in_fd, toc_stream_obj, page_count);
      }
    }

    write_xref_and_trailer(total_objs);

    close(in_fd);
    close(pdf_fd);

    printf("Done." NEWLINE);
    return 0;
}
