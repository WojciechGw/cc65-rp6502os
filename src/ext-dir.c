#include <rp6502.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define NEWLINE "\r\n"
#define FNAMELEN 64
#define DIR_LIST_MAX 40

typedef struct {
    char name[FNAMELEN];
    unsigned long fsize;
    unsigned char fattrib;
    unsigned fdate;
    unsigned ftime;
} dir_list_entry_t;

static char dir_cwd[FNAMELEN];
static f_stat_t dir_ent;
static char dir_dt_buf[20];

#ifndef AM_DIR
#define AM_DIR 0x10
#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_VOL 0x08
#define AM_ARC 0x20
#endif

#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define TX_READY_SPIN while(!TX_READY)

inline void tx_char(char c) {
    TX_READY_SPIN;
    RIA.tx = c;
    return;
}

void tx_chars(const char *buf, int ct) {
    for(; ct; ct--, buf++) tx_char(*buf);
    return;
}

void tx_string(const char *buf) {
    while(*buf) tx_char(*buf++);
    return;
}

// Simple wildcard match supporting '*' (0+ chars) and '?' (1 char).
bool match_mask(const char *name, const char *mask) {
    const char *star = 0;
    const char *match = 0;
    while(*name) {
        if(*mask == '?' || *mask == *name) {
            mask++;
            name++;
            continue;
        }
        if(*mask == '*') {
            star = mask++;
            match = name;
            continue;
        }
        if(star) {
            mask = star + 1;
            name = ++match;
            continue;
        }
        return false;
    }
    while(*mask == '*') mask++;
    return *mask == 0;
}

// Print an unsigned long in decimal.
void tx_dec32(unsigned long val) {
    char out[10];
    int i = 10;
    if(val == 0) {
        tx_char('0');
        return;
    }
    while(val && i) {
        out[--i] = '0' + (val % 10);
        val /= 10;
    }
    tx_chars(&out[i], 10 - i);
}

// Format FAT date/time into YYYY-MM-DD hh:mm:ss
const char *format_fat_datetime(unsigned fdate, unsigned ftime) {
    unsigned year = 1980 + (fdate >> 9);
    unsigned month = (fdate >> 5) & 0xF;
    unsigned day = fdate & 0x1F;
    unsigned hour = ftime >> 11;
    unsigned min = (ftime >> 5) & 0x3F;
    unsigned sec = (ftime & 0x1F) * 2;

    dir_dt_buf[0]  = '0' + (year / 1000);
    dir_dt_buf[1]  = '0' + ((year / 100) % 10);
    dir_dt_buf[2]  = '0' + ((year / 10) % 10);
    dir_dt_buf[3]  = '0' + (year % 10);
    dir_dt_buf[4]  = '-';
    dir_dt_buf[5]  = '0' + (month / 10);
    dir_dt_buf[6]  = '0' + (month % 10);
    dir_dt_buf[7]  = '-';
    dir_dt_buf[8]  = '0' + (day / 10);
    dir_dt_buf[9]  = '0' + (day % 10);
    dir_dt_buf[10] = ' ';
    dir_dt_buf[11] = '0' + (hour / 10);
    dir_dt_buf[12] = '0' + (hour % 10);
    dir_dt_buf[13] = ':';
    dir_dt_buf[14] = '0' + (min / 10);
    dir_dt_buf[15] = '0' + (min % 10);
    dir_dt_buf[16] = ':';
    dir_dt_buf[17] = '0' + (sec / 10);
    dir_dt_buf[18] = '0' + (sec % 10);
    dir_dt_buf[19] = 0;
    return dir_dt_buf;
}

int main(int argc, char **argv) {

    int i;
    const char *mask = "*";
    const char *path = ".";
    char *p;
    int dirdes = -1;
    int rc = 0;
    unsigned sort_mode = 0; /* 0=name,1=time desc (youngest),2=time asc,3=size asc,4=size desc */
    unsigned files_count = 0;
    unsigned dirs_count = 0;
    unsigned long total_bytes = 0;
    char dir_drive[3] = {0};
    static char dir_arg[FNAMELEN];
    static char dir_path_buf[FNAMELEN];
    static char dir_mask_buf[FNAMELEN];
    static dir_list_entry_t dir_entries[DIR_LIST_MAX];
    dir_list_entry_t dir_tmp;
    unsigned dir_entries_count = 0;
    unsigned dir_i = 0, dir_j = 0;

    if(argc == 1 && strcmp(argv[0],"/?") == 0) {
        printf( "Command : dir" NEWLINE NEWLINE "show active drive directory, wildcards allowed" NEWLINE NEWLINE
                "Usage:" NEWLINE
                "dir *.rp6502 (only .rp6502 files)" NEWLINE
                "dir /da (sorted by date ascending)" NEWLINE NEWLINE);
        return -1;
    }

    dir_drive[0] = dir_drive[1] = dir_drive[2] = 0;
    if(argc >= 2) {
        strcpy(dir_arg, argv[1]);
        // Handle drive prefix like "0:" or "A:"
        if(dir_arg[1] == ':') {
            if(dir_arg[0] < '0' || dir_arg[0] > '7') {
                tx_string("Invalid drive" NEWLINE);
                rc = -1;
                goto cleanup;
            }
            dir_drive[0] = dir_arg[0];
            dir_drive[1] = ':';
            dir_drive[2] = 0;
            if(f_chdrive(dir_drive) < 0) {
                tx_string("Invalid drive" NEWLINE);
                rc = -1;
                goto cleanup;
            }
            p = dir_arg + 2;
        } else {
            p = dir_arg;
        }
        if(*p == '/' || *p == '\\') p++;
        if(*p) {
            char *last_sep = 0;
            char *iter = p;
            while(*iter) {
                if(*iter == '/' || *iter == '\\') last_sep = iter;
                iter++;
            }
            if(last_sep) {
                *last_sep = 0;
                mask = last_sep + 1;
                path = (*p) ? p : ".";
            } else {
                mask = p;
                path = ".";
            }
        } else {
            mask = "*";
            path = ".";
        }
        if(!*mask) mask = "*";
    }
    /* Parse optional flags /da /dd /sa /sd */
    for(i = 2; i < argc; i++) {
        if(argv[i][0] == '/') {
            if(!strcmp(argv[i], "/da")) sort_mode = 1;
            else if(!strcmp(argv[i], "/dd")) sort_mode = 2;
            else if(!strcmp(argv[i], "/sa")) sort_mode = 3;
            else if(!strcmp(argv[i], "/sd")) sort_mode = 4;
        }
    }

    // Copy path/mask into static buffers for reuse.
    strcpy(dir_path_buf, path);
    strcpy(dir_mask_buf, mask);

    if(f_getcwd(dir_cwd, sizeof(dir_cwd)) < 0) {
        tx_string("getcwd failed" NEWLINE);
        rc = -1;
        goto cleanup;
    }

    tx_string(NEWLINE "Directory: ");
    tx_string(dir_cwd);
    tx_string(NEWLINE NEWLINE);

    dirdes = f_opendir(dir_path_buf[0] ? dir_path_buf : ".");
    if(dirdes < 0) {
        tx_string("opendir failed" NEWLINE);
        rc = -1;
        goto cleanup;
    }

    dir_entries_count = 0;
    while(1) {
        rc = f_readdir(&dir_ent, dirdes);
        if(rc < 0) {
            tx_string("readdir failed" NEWLINE);
            break;
        }
        if(!dir_ent.fname[0]) break; // No more entries
        if(dir_entries_count == DIR_LIST_MAX) {
            tx_string("Directory listing truncated, too many entries, use wildcards to narrow results" NEWLINE);
            break;
        }

        // Apply mask only to files; always include directories so they are visible.
        if(!(dir_ent.fattrib & AM_DIR)) {
            if(!match_mask(dir_ent.fname, dir_mask_buf)) continue;
        }

        strcpy(dir_entries[dir_entries_count].name, dir_ent.fname);
        dir_entries[dir_entries_count].fsize = dir_ent.fsize;
        dir_entries[dir_entries_count].fattrib = dir_ent.fattrib;
        dir_entries[dir_entries_count].fdate = dir_ent.fdate;
        dir_entries[dir_entries_count].ftime = dir_ent.ftime;
        dir_entries_count++;
    }

    if(f_closedir(dirdes) < 0 && rc >= 0) {
        tx_string("closedir failed" NEWLINE);
        rc = -1;
    }
    dirdes = -1;

    if(rc < 0) return -1;

    // Sort: files first, directories after, each group alphabetically.
    for(dir_i = 0; dir_i < dir_entries_count; dir_i++) {
        for(dir_j = dir_i + 1; dir_j < dir_entries_count; dir_j++) {
            unsigned a_dir = dir_entries[dir_i].fattrib & AM_DIR;
            unsigned b_dir = dir_entries[dir_j].fattrib & AM_DIR;
            int swap = 0;
            unsigned long a_val = 0;
            unsigned long b_val = 0;
            switch(sort_mode) {
                case 1: /* time desc (youngest first) */
                case 2: /* time asc */
                    a_val = ((unsigned long)dir_entries[dir_i].fdate << 16) | dir_entries[dir_i].ftime;
                    b_val = ((unsigned long)dir_entries[dir_j].fdate << 16) | dir_entries[dir_j].ftime;
                    if(sort_mode == 1) swap = a_val < b_val;
                    else swap = a_val > b_val;
                    break;
                case 3: /* size asc */
                    a_val = dir_entries[dir_i].fsize;
                    b_val = dir_entries[dir_j].fsize;
                    swap = a_val > b_val;
                    break;
                case 4: /* size desc */
                    a_val = dir_entries[dir_i].fsize;
                    b_val = dir_entries[dir_j].fsize;
                    swap = a_val < b_val;
                    break;
                default: /* name, files before dirs */
                    if(a_dir != b_dir) {
                        if(a_dir && !b_dir) swap = 1;
                    } else if(strcmp(dir_entries[dir_i].name, dir_entries[dir_j].name) > 0) {
                        swap = 1;
                    }
                    break;
            }
            if(swap) {
                dir_tmp = dir_entries[dir_i];
                dir_entries[dir_i] = dir_entries[dir_j];
                dir_entries[dir_j] = dir_tmp;
            }
        }
    }

    for(dir_i = 0; dir_i < dir_entries_count; dir_i++) {
        unsigned name_len;
        if(dir_entries[dir_i].fattrib & AM_DIR) {
            tx_char('[');
            tx_string(dir_entries[dir_i].name);
            tx_char(']');
            name_len = strlen(dir_entries[dir_i].name) + 2; // brackets
        } else {
            tx_string(dir_entries[dir_i].name);
            name_len = strlen(dir_entries[dir_i].name);
        }
        while(name_len < 32) {
            tx_char(' ');
            name_len++;
        }
        tx_char('\t');
        tx_string(format_fat_datetime(dir_entries[dir_i].fdate, dir_entries[dir_i].ftime));
        tx_char('\t');
        if(dir_entries[dir_i].fattrib & AM_DIR) {
            tx_string("<DIR>");
            dirs_count++;
        } else {
            tx_dec32(dir_entries[dir_i].fsize);
            total_bytes += dir_entries[dir_i].fsize;
            files_count++;
        }
        tx_char('\t');
        tx_char((dir_entries[dir_i].fattrib & AM_RDO) ? 'R' : '-');
        tx_char((dir_entries[dir_i].fattrib & AM_HID) ? 'H' : '-');
        tx_char((dir_entries[dir_i].fattrib & AM_SYS) ? 'S' : '-');
        tx_char((dir_entries[dir_i].fattrib & AM_VOL) ? 'V' : '-');
        tx_char((dir_entries[dir_i].fattrib & AM_DIR) ? 'D' : '-');
        tx_char((dir_entries[dir_i].fattrib & AM_ARC) ? 'A' : '-');
        tx_string(NEWLINE);
    }
    tx_string(NEWLINE);
    tx_string("Files: ");
    tx_dec32(files_count);
    tx_string("  Dirs: ");
    tx_dec32(dirs_count);
    tx_string("  Bytes: ");
    tx_dec32(total_bytes);
    tx_string(NEWLINE NEWLINE);

cleanup:
    if(dirdes >= 0) f_closedir(dirdes);

    return 0;
}

