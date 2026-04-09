#include "commons.h"

#define APPVER "20260410.0007"

static char dev_label[16];
static char saved_cwd[128];

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

int main(int argc, char **argv) {

    int rc;
    char drv[3] = "0:";
    unsigned i;
    char saved_drive = '0';
    const char *saved_path = "/";
    unsigned long free_blks = 0;
    unsigned long total_blks = 0;
    unsigned long pct = 0;

    if(argc == 1 && strcmp(argv[0],"/?") == 0) {
        printf( "Command : drives" NEWLINE NEWLINE "show a list of drives" NEWLINE NEWLINE
                "Usage:" NEWLINE
                "drives" NEWLINE);
        return -1;
    }

    if(f_getcwd(saved_cwd, sizeof(saved_cwd)) < 0) {
        tx_string("getcwd failed" NEWLINE);
        return -1;
    }
    if(saved_cwd[1] == ':') {
        saved_drive = saved_cwd[0];
        saved_path = saved_cwd + 2;
        if(!*saved_path) saved_path = "/";
    }

    tx_string(NEWLINE "Drive(s)" NEWLINE NEWLINE "drive" TAB "label      " TAB "[MB]" TAB "free" NEWLINE
              "-----" TAB "-----------" TAB "------" TAB "----" NEWLINE);
    for(i = 0; i < 8; i++) {
        drv[0] = '0' + i;
        rc = f_chdrive(drv);
        if(rc == 0) {
            if(f_getfree(drv, &free_blks, &total_blks) != 0 || !total_blks) {
                continue; // Skip drives without size info
            }
            tx_string("MSC");
            tx_string(drv);
            tx_string(TAB);
            if(f_getlabel(drv, dev_label) >= 0) {
                unsigned len = strlen(dev_label);
                tx_string(dev_label);
                while(len < 11) { tx_char(' '); len++; }
            } else {
                tx_string("(no label)       ");
            }
            tx_string(TAB);
            {
                unsigned long mb = total_blks / 2048; // 512-byte blocks -> MB
                pct = (free_blks * 100UL) / total_blks;
                tx_dec32(mb);
                tx_string(TAB);
                tx_dec32(pct);
                tx_char('%');
            }
            tx_string(NEWLINE);
        }
    }
    tx_string(NEWLINE);

    drv[0] = saved_drive;
    drv[1] = ':';
    drv[2] = 0;
    if(f_chdrive(drv) == 0) {
        chdir(saved_path);
        // current_drive = drv[0];
    }
    return 0;

}

