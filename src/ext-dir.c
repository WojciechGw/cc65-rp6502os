#include <rp6502.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

static char dir_cwd[FNAMELEN];
static f_stat_t dir_ent;
#ifndef AM_DIR
#define AM_DIR 0x10
#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_VOL 0x08
#define AM_ARC 0x20
#endif

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
    char *dir_arg = malloc(FNAMELEN);
    char *dir_path_buf = malloc(FNAMELEN);
    char *dir_mask_buf = malloc(FNAMELEN);
    dir_list_entry_t *dir_entries = malloc(sizeof(dir_list_entry_t) * DIR_LIST_MAX);
    dir_list_entry_t dir_tmp;
    unsigned dir_entries_count = 0;
    unsigned dir_i = 0, dir_j = 0;

    if(argc < 1) {
        printf("Usage: label <new_label>\r\n");
        return -1;
    }

    printf("\r\n--------------\r\nargc=%d\r\n", argc);
    for(i = 0; i < argc; i++) {
        printf("argv[%d]=\"%s\"\r\n", i, argv[i]);
    }

// --------------------------


    if(!dir_arg || !dir_path_buf || !dir_mask_buf || !dir_entries) {
        tx_string("dir: OOM" NEWLINE);
        free(dir_arg); free(dir_path_buf); free(dir_mask_buf); free(dir_entries);
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
            current_drive = dir_drive[0];
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
    free(dir_arg);
    free(dir_path_buf);
    free(dir_mask_buf);
    free(dir_entries);

// --------------------------

    return 0;
}

