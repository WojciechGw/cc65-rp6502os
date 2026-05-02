// razemOS .COM
// Tree — directory tree viewer
// command: tree [path]
//

#include "commons.h"

#define APPVER "20260502.1138"

#define MAX_DEPTH    6      /* max depth below root */
#define MAX_STACK    96     /* pending work frames   */
#define PATH_LEN     64     /* full path buffer      */
#define NAME_LEN     64     /* per-entry name buffer */
#define MAX_DIR_ENT  48     /* entries read per dir  */

#ifndef AM_DIR
#define AM_DIR 0x10
#endif

#define PAGE_LINES 27   /* output lines before "--- more ---" prompt */

/* ------------------------------------------------------------------ */

typedef struct {
    char    path[PATH_LEN]; /* full path to entry              */
    uint8_t depth;          /* 0=root (expand only), 1+ = real */
    bool    is_last;        /* last sibling in parent          */
    bool    is_dir;
    uint8_t pmask;          /* bit k → ancestor at depth k+1 was last */
} tree_frame_t;

static tree_frame_t stk[MAX_STACK];
static int          stk_top;

/* temp: one directory's entries */
static char     tmp_names[MAX_DIR_ENT][NAME_LEN];
static bool     tmp_isdir[MAX_DIR_ENT];
static f_stat_t tree_ent;

static unsigned long total_dirs;
static unsigned long total_files;

/* paging ------------------------------------------------------------ */

static int  pg_row;   /* lines printed on current page          */
static bool pg_quit;  /* set when user presses 'q'              */

/* Print NEWLINE, handle page break. Returns false → caller should stop. */
static bool pg_nl(void)
{
    char c;
    printf(NEWLINE);
    if (pg_quit) return false;
    if (++pg_row < PAGE_LINES) return true;
    pg_row = 0;
    while (RX_READY) c = RIA.rx;               /* flush pending input */
    printf(ANSI_DARK_GRAY "--- more --- [q] quit" ANSI_RESET);
    while (!RX_READY) {}
    c = RIA.rx;
    printf("\r" CSI "2K");                     /* erase prompt */
    if (c == 'q' || c == 'Q' || c == 0x1B) { pg_quit = true; return false; }
    return true;
}

/* ------------------------------------------------------------------ */

static void push_frame(const char *path, uint8_t depth,
                       bool is_last, bool is_dir, uint8_t pmask)
{
    tree_frame_t *f;
    if (stk_top >= MAX_STACK) return;
    f = &stk[stk_top++];
    strncpy(f->path, path, PATH_LEN - 1);
    f->path[PATH_LEN - 1] = 0;
    f->depth   = depth;
    f->is_last = is_last;
    f->is_dir  = is_dir;
    f->pmask   = pmask;
}

static void build_path(char *buf, const char *base, const char *name)
{
    int i = 0, blen = (int)strlen(base), j;
    for (j = 0; j < blen && i < PATH_LEN - 2; j++)
        buf[i++] = base[j];
    if (i > 0 && buf[i-1] != '/' && buf[i-1] != ':')
        buf[i++] = '/';
    for (j = 0; name[j] && i < PATH_LEN - 1; j++)
        buf[i++] = name[j];
    buf[i] = 0;
}

static const char *basename_of(const char *p)
{
    const char *last = p;
    while (*p) {
        if (*p == '/' || *p == '\\') last = p + 1;
        p++;
    }
    return last;
}

/* Draw the connector prefix for a node at given depth.
   CP437: 0xB3=│  0xC0=└  0xC3=├  0xC4=─  */
static void print_prefix(uint8_t depth, bool is_last, uint8_t pmask)
{
    uint8_t k;
    for (k = 1; k < depth; k++) {
        if (pmask & (uint8_t)(1u << (k - 1)))
            printf("    ");
        else
            printf("\xB3   ");
    }
    printf(is_last ? "\xC0\xC4\xC4 " : "\xC3\xC4\xC4 ");
}

/* Collect and sort entries of dir `path`.
   Dirs first (alpha), then files (alpha).
   Returns count, or -1 on error. */
static int collect_dir(const char *path)
{
    int dirdes, n, i, j;
    char tmp_n[NAME_LEN];
    bool tmp_d;

    dirdes = f_opendir(path);
    if (dirdes < 0) return -1;

    n = 0;
    while (n < MAX_DIR_ENT) {
        if (f_readdir(&tree_ent, dirdes) < 0 || !tree_ent.fname[0]) break;
        strncpy(tmp_names[n], tree_ent.fname, NAME_LEN - 1);
        tmp_names[n][NAME_LEN - 1] = 0;
        tmp_isdir[n] = (tree_ent.fattrib & AM_DIR) != 0;
        n++;
    }
    f_closedir(dirdes);

    /* bubble sort: dirs before files, alpha within each group */
    for (i = 0; i < n - 1; i++) {
        for (j = i + 1; j < n; j++) {
            int sw = 0;
            if (tmp_isdir[i] != tmp_isdir[j]) {
                if (!tmp_isdir[i] && tmp_isdir[j]) sw = 1;
            } else {
                if (strcmp(tmp_names[i], tmp_names[j]) > 0) sw = 1;
            }
            if (sw) {
                memcpy(tmp_n, tmp_names[i], NAME_LEN);
                memcpy(tmp_names[i], tmp_names[j], NAME_LEN);
                memcpy(tmp_names[j], tmp_n, NAME_LEN);
                tmp_d = tmp_isdir[i];
                tmp_isdir[i] = tmp_isdir[j];
                tmp_isdir[j] = tmp_d;
            }
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *root;
    static char cwd_buf[PATH_LEN];
    static char cpath[PATH_LEN];

    /* local copies of popped frame (avoids dangling pointer after push) */
    char    fp_path[PATH_LEN];
    uint8_t fp_depth;
    bool    fp_is_last;
    bool    fp_is_dir;
    uint8_t fp_pmask;

    int     n, i, last_push;
    uint8_t new_pmask;
    bool    dirs_only;

    if (argc == 1 && strcmp(argv[0], "/?") == 0) {
        printf(NEWLINE
               "Command : tree" NEWLINE NEWLINE
               "Display directory tree" NEWLINE NEWLINE
               "Usage   : tree [/t] [path]" NEWLINE NEWLINE
               "  /t    directories only" NEWLINE);
        return 0;
    }

    /* parse arguments: /t flag + optional path */
    dirs_only = false;
    root      = NULL;
    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '/' && (argv[i][1] == 't' || argv[i][1] == 'T'))
            dirs_only = true;
        else if (root == NULL)
            root = argv[i];
    }
    if (root == NULL) {
        /* default: current directory */
        cwd_buf[0] = 0;
        f_getcwd(cwd_buf, sizeof(cwd_buf));
        root = cwd_buf[0] ? cwd_buf : ".";
    }

    stk_top     = 0;
    total_dirs  = 0;
    total_files = 0;
    pg_row      = 0;
    pg_quit     = false;

    printf(NEWLINE ANSI_BOLD "%s" ANSI_RESET, root);
    if (!pg_nl()) return 0;

    push_frame(root, 0, true, true, 0);

    while (stk_top > 0) {
        /* pop — copy before any push may invalidate the slot */
        {
            tree_frame_t *fp = &stk[--stk_top];
            memcpy(fp_path, fp->path, PATH_LEN);
            fp_depth   = fp->depth;
            fp_is_last = fp->is_last;
            fp_is_dir  = fp->is_dir;
            fp_pmask   = fp->pmask;
        }

        /* print entry (skip depth-0 root, already printed above) */
        if (fp_depth > 0) {
            print_prefix(fp_depth, fp_is_last, fp_pmask);
            if (fp_is_dir) {
                total_dirs++;
                printf(ANSI_CYAN "[%s]" ANSI_RESET, basename_of(fp_path));
            } else {
                total_files++;
                printf("%s", basename_of(fp_path));
            }
            if (!pg_nl()) break;
        }

        /* expand directory if within depth limit */
        if (!fp_is_dir || fp_depth >= MAX_DEPTH) continue;

        n = collect_dir(fp_path);
        if (n <= 0) continue;

        /* pmask for children: inherit + mark this depth if it was last */
        new_pmask = fp_pmask;
        if (fp_depth > 0)
            new_pmask |= (uint8_t)(fp_is_last ? (1u << (fp_depth - 1)) : 0u);

        /* find last index that will actually be pushed */
        last_push = -1;
        for (i = 0; i < n; i++) {
            if (!dirs_only || tmp_isdir[i]) last_push = i;
        }

        /* push children in REVERSE so first entry is popped first */
        for (i = n - 1; i >= 0; i--) {
            if (dirs_only && !tmp_isdir[i]) continue;
            build_path(cpath, fp_path, tmp_names[i]);
            push_frame(cpath, fp_depth + 1, (i == last_push),
                       tmp_isdir[i], new_pmask);
        }
    }

    /* summary */
    {
        char d_buf[12], f_buf[12];
        unsigned long v;
        int pd, pf;

        v = total_dirs;  pd = 11; d_buf[11] = 0;
        do { d_buf[--pd] = (char)('0' + v % 10); v /= 10; } while (v);
        v = total_files; pf = 11; f_buf[11] = 0;
        do { f_buf[--pf] = (char)('0' + v % 10); v /= 10; } while (v);

        if (dirs_only)
            printf(NEWLINE "%s director%s" NEWLINE,
                   d_buf + pd, (total_dirs == 1) ? "y" : "ies");
        else
            printf(NEWLINE "%s director%s, %s file%s" NEWLINE,
                   d_buf + pd, (total_dirs  == 1) ? "y" : "ies",
                   f_buf + pf, (total_files == 1) ? "" : "s");
    }

    return 0;
}
