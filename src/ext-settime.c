#include "commons.h"

#define TX_READY (RIA.ready & RIA_READY_TX_BIT)
#define RX_READY (RIA.ready & RIA_READY_RX_BIT)
#define TX_READY_SPIN while(!TX_READY)
#define RX_READY_SPIN while(!RX_READY)

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

static int read_line_editor(char *buf, int maxlen) { // Read a single line from the console; returns length or -1 if cancelled with ESC.
    int len = 0;
    char c = 0;
    while(1) {
        RX_READY_SPIN;
        c = (char)RIA.rx;
        if(c == CHAR_CR || c == CHAR_LF) {
            tx_string(NEWLINE);
            buf[len] = 0;
            return len;
        }
        if(c == CHAR_BS || c == KEY_DEL) {
            if(len) {
                len--;
                tx_string("\b \b");
            } else {
                tx_char(CHAR_BELL);
            }
            continue;
        }
        if(c == CHAR_ESC) {
            buf[0] = 0;
            return -1;
        }
        if(((unsigned char)c >= 32) && (c != 127)) {
            if(len < maxlen - 1) {
                buf[len++] = c;
                tx_char(c);
            } else {
                tx_char(CHAR_BELL);
            }
        }
    }
}

int main(void) { // Interactive setter for RTC when it is unset (year 1970).
    char line[32];
    int year, mon, day, hour, min, sec;
    struct tm tmset;
    struct timespec ts;
    time_t epoch;
    int len;

    tx_string(ANSI_CLS "[OS Shell INFO] RTC is not set." NEWLINE
              "Enter current date & time [YYYY-MM-DD HH:MM:SS]" NEWLINE
              "or press ESC to cancel procedure." NEWLINE "> " CSI_CURSOR_SHOW);
    len = read_line_editor(line, sizeof(line));
    if(len <= 0) {
        tx_string(NEWLINE "[OS Shell INFO] Cancel." NEWLINE);
        return -1;
    }
    if(sscanf(line, "%4d-%2d-%2d %2d:%2d:%2d", &year, &mon, &day, &hour, &min, &sec) != 6) {
        tx_string(NEWLINE "[OS Shell INFO] Wrong date & time format." NEWLINE);
        return -1;
    }
    if(year < 1970 || mon < 1 || mon > 12 || day < 1 || day > 31 ||
       hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        tx_string(NEWLINE "[OS Shell INFO] Wrong date & time values." NEWLINE);
        return -1;
    }

    tmset.tm_year = year - 1900;
    tmset.tm_mon  = mon - 1;
    tmset.tm_mday = day;
    tmset.tm_hour = hour;
    tmset.tm_min  = min;
    tmset.tm_sec  = sec;
    tmset.tm_isdst = -1;

    epoch = mktime(&tmset);
    if(epoch == (time_t)-1) {
        tx_string(NEWLINE "[OS Shell INFO] date & time setting failed." NEWLINE);
        return -1;
    }
    ts.tv_sec = epoch;
    ts.tv_nsec = 0;
    if(clock_settime(CLOCK_REALTIME, &ts) != 0) {
        tx_string(NEWLINE "[OS Shell INFO] RTC setting failed." NEWLINE);
        return -1;
    }
    tx_string(NEWLINE "[OS Shell INFO] RTC set." NEWLINE);
    return 0;
}
