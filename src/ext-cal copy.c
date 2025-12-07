#include <rp6502.h>
#include <stdio.h>
#include <time.h>

struct Month {
    const char* name;
    int days;
};

static const struct Month months[12] = {
    {"January",   31},
    {"February",  28},
    {"March",     31},
    {"April",     30},
    {"May",       31},
    {"June",      30},
    {"July",      31},
    {"August",    31},
    {"September", 30},
    {"October",   31},
    {"November",  30},
    {"December",  31}
};

void print_calendar(int year, int month, int day);
int getFirstDayOfMonth(int year, int month);
const char* getMonthName(int month);
int daysInMonth(int year, int month);

int main() {

    int year, month, day;
    // char buf[100];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    struct tm *tminfo;
    // UTC requires no effort
    tminfo = gmtime(&t);
    ria_tzset(t);
    tminfo = localtime(&t);
    tminfo->tm_isdst = _tz.daylight;
    year = tm->tm_year + 1900;
    month = tm->tm_mon + 1;
    day = tm->tm_mday;
    print_calendar(year, month, day);
    printf("\r\n\r\n");
    return 0;
}

int getFirstDayOfMonth(int year, int month) {
    struct tm t;
    int w;

    t.tm_sec = 0;
    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_mday = 1;
    t.tm_mon = month - 1;
    t.tm_year = year - 1900;
    t.tm_wday = 0;
    t.tm_yday = 0;
    t.tm_isdst = -1;
    if (mktime(&t) == (time_t)-1) {
        return 1;
    }
    w = t.tm_wday;
    return w == 0 ? 7 : w;
}

const char* getMonthName(int month) {
    if (month < 1 || month > 12) return "Invalid";
    return months[month - 1].name;
}

int daysInMonth(int year, int month) {
    if (month < 1 || month > 12) return -1;

    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
        return 29;
    }

    return months[month - 1].days;
}

void print_calendar(int year, int month, int today) {

    int i, day, start_day, day_in_week, days_in_month;
    day_in_week = getFirstDayOfMonth(year, month);
    days_in_month = daysInMonth(year, month);
    day = 1;
    start_day = day_in_week;

    printf("\r\n %d%16s\r\n", year, getMonthName(month));

    printf("\r\n Mo Tu We Th Fr Sa  S\r\n -- -- -- -- -- -- --\r\n ");
    for (i = 0; i < (day_in_week-1); i++) {
        printf("   ");
    }

    while (day <= days_in_month) {

        if(day == today) { 
            printf("\033[47m\033[30m%2d \033[0m", day);
        } else {
            printf("%2d ",day);
        } 

        if ((start_day + 7) % 7 == 0) {
            printf("\r\n ");
        }
        start_day++;
        day++;
    }
}
