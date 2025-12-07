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
int getFirstDayOfMonth(int year, int month, int day);
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

int getFirstDayOfMonth(int year, int month, int day) {

    int year_offset, month_offset;
    year_offset = (year + (year / 4) - (year / 100) + (year / 400)) % 7;
    if(month < 3) {
        year--;
    }

    month_offset = daysInMonth(year, month) - day + (day > 7 ? 6 : 0);

    return (((year_offset + month_offset) % 7 - 1) % 7) - 1;
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
    day_in_week = getFirstDayOfMonth(year, month, 1);
    days_in_month = daysInMonth(year, month);
    day = 1;
    start_day = day_in_week;

    printf("\r\n %d%16s", year, getMonthName(month));

    printf("\r\n Mo Tu We Th Fr Sa  S\r\n ");
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
