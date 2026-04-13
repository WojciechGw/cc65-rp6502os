#include "commons.h"

#define APPVER "20260413.1835"

// #define DEBUG

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
void print_calendar_year(int year, int today_month, int today_day);
void print_calendar_quarter(int year, int start_month, int today_month, int today_day);
void print_calendar_neighbours(int year, int month, int today_day);
int getFirstDayOfMonth(int year, int month);
const char* getMonthName(int month);
int daysInMonth(int year, int month);

#define SHOW_CURRENTMONTH     0
#define SHOW_YEAR             1
#define SHOW_QUARTER          2
#define SHOW_NEIGHBOURS       3
#define SHOW_PARTICULAR       4
#define SHOW_DATETIME         5
#define SHOW_DATETIMECALENDAR 6
#define SET_DATETIME          99

int main(int argc, char **argv) {

    int year, month, day, hour, minute, second, mday, wday, yday; //, isdst
    int action = SHOW_CURRENTMONTH;
    int quarter_start;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    year = tm->tm_year + 1900;
    month = tm->tm_mon + 1;
    day = tm->tm_mday;
    hour = tm->tm_hour;
    minute = tm->tm_min;
    second = tm->tm_sec; 
    yday = tm->tm_yday;
    mday = tm->tm_mday;
    wday = tm->tm_wday;
    // tm->tm_isdst;

    #ifdef DEBUG
    {
        int i;
        printf(NEWLINE "--------------" NEWLINE "argc=%d" NEWLINE, argc);
        for(i = 0; i < argc; i++) {
            printf("argv[%d]=\"%s\"" NEWLINE, i, argv[i]);
        }
    }
    #endif

    if(year == 1970){
        printf(NEWLINE ANSI_RED EXCLAMATION "Real Time Clock is not set." ANSI_RESET NEWLINE);
        return 0;
    } 

    if(argc == 0) {
        action = SHOW_DATETIME;
    } else {
        if (argc >= 2 && !strcmp(argv[0], "/c") && !strcmp(argv[1], "/y")) {
            action = SHOW_YEAR;
        } else if (argc >= 2 && !strcmp(argv[0], "/c") && !strcmp(argv[1], "/q")) {
            action = SHOW_QUARTER;
        } else if (argc >= 2 && !strcmp(argv[0], "/c") && !strcmp(argv[1], "/n")) {
            action = SHOW_NEIGHBOURS;
        } else if (argc >= 2 && !strcmp(argv[0], "/c") && !strcmp(argv[1], "/p")) {
            action = SHOW_PARTICULAR;
        } else if (!strcmp(argv[0], "/?")) {
            printf(NEWLINE "Command: Date" NEWLINE NEWLINE
                    "shows current time and/or calendar in various ways" NEWLINE);
            return 0;
        } else if (!strcmp(argv[0], "/a")) {
            action = SHOW_DATETIMECALENDAR;
        } else if (!strcmp(argv[0], "/c")) {
            action = SHOW_CURRENTMONTH;
        } else if (!strcmp(argv[0], "/s")) {
            action = SET_DATETIME;
        }
    }
    
    switch(action){
        case SHOW_DATETIMECALENDAR:
            printf(NEWLINE 
                   "Date & time information" NEWLINE
                   "-----------------------" NEWLINE
                   NEWLINE 
                   " %04d-%02d-%02d  %02d:%02d:%02d" NEWLINE,
                     year, month, day, hour, minute, second);
            printf(NEWLINE "    day of: year  %d" NEWLINE
                           "            month %d" NEWLINE
                           "            week  %d" NEWLINE,
                           yday, mday, wday );
            print_calendar(year, month, day);
            break;
        case SET_DATETIME:
            printf("Setting date & time procedure isn't implemented. yet ...");
            break;
        case SHOW_YEAR:
            if (argc > 2){
                year = atoi(argv[2]);
                month = 0;
                day = 0;
            }
            print_calendar_year(year, month, day);
            break;
        case SHOW_QUARTER:
            if (argc > 2){
                year = atoi(argv[2]);
                month = atoi(argv[3]);
                day = 0;
            }
            quarter_start = ((month - 1) / 3) * 3 + 1;
            print_calendar_quarter(year, quarter_start, month, day);
            break;
        case SHOW_NEIGHBOURS:
            if (argc > 2){
                year = atoi(argv[2]);
                month = atoi(argv[3]);
                day = 0;
            }
            print_calendar_neighbours(year, month, day);
            break;
        case SHOW_PARTICULAR:
            year = atoi(argv[2]);
            month = atoi(argv[3]);
            print_calendar(year, month, 0);
            break;
        case SHOW_CURRENTMONTH:
            print_calendar(year, month, day);
            break;
        default:
            printf(NEWLINE "%04d-%02d-%02d   %02d:%02d:%02d" NEWLINE,
                   year, month, day, hour, minute, second);
            print_calendar(year, month, day);
    }
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

    printf("\r\n %d%16s" NEWLINE, year, getMonthName(month));

    printf("\r\n Mo Tu We Th Fr Sa  S\r\n -- -- -- -- -- -- --\r\n ");
    for (i = 0; i < (day_in_week-1); i++) {
        printf("   ");
    }

    while (day <= days_in_month) {

        if(day == today) {
            printf(ANSI_BOLD ANSI_GREEN "%2d " ANSI_RESET, day);
            // printf("\033[47m\033[30m%2d \033[0m", day);
        } else {
            printf("%2d ",day);
        } 

        if ((start_day + 7) % 7 == 0) {
            printf("\r\n ");
        }
        start_day++;
        day++;
    }
    printf(NEWLINE);

}

void print_calendar_quarter(int year, int start_month, int today_month, int today_day) {

    int idx, week, wday, m;
    int start_day[3];
    int days_in_month[3];
    int current_day[3];
    char title[24];

    for (idx = 0; idx < 3; idx++) {
        m = start_month + idx;
        start_day[idx] = getFirstDayOfMonth(year, m);
        days_in_month[idx] = daysInMonth(year, m);
        current_day[idx] = 1;
    }
    
    printf(NEWLINE);

    for (idx = 0; idx < 3; idx++) {
        m = start_month + idx;
        sprintf(title, " %4d%16s", year,getMonthName(m));
        printf("%-23s", title);
    }

    printf(NEWLINE NEWLINE);

    for (idx = 0; idx < 3; idx++) {
        printf(" Mo Tu We Th Fr Sa  S  ");
    }
    printf(NEWLINE);

    for (idx = 0; idx < 3; idx++) {
        printf(" -- -- -- -- -- -- --  ");
    }
    printf(NEWLINE);

    /* Linie kalendarza (max 6 tygodni) */
    for (week = 0; week < 6; week++) {
        printf(" ");
        for (idx = 0; idx < 3; idx++) {
            for (wday = 1; wday <= 7; wday++) {
                if (week == 0 && wday < start_day[idx]) {
                    printf("   ");
                } else if (current_day[idx] <= days_in_month[idx]) {
                    if ((start_month + idx) == today_month && current_day[idx] == today_day) {
                        printf(ANSI_BOLD ANSI_GREEN "%2d " ANSI_RESET, current_day[idx]);
                        // printf("\033[47m\033[30m%2d \033[0m", current_day[idx]);
                    } else {
                        printf("%2d ", current_day[idx]);
                    }
                    current_day[idx]++;
                } else {
                    printf("   ");
                }
            }
            printf("  ");
        }
        printf(NEWLINE);
    }
}

void print_calendar_neighbours(int year, int month, int today_day) {

    int idx, week, wday;
    int start_day[3];
    int days_in_month[3];
    int current_day[3];
    int month_val[3];
    int year_val[3];
    char title[24];

    month_val[0] = month - 1;
    year_val[0] = year;
    if (month_val[0] < 1) {
        month_val[0] = 12;
        year_val[0] = year - 1;
    }
    month_val[1] = month;
    year_val[1] = year;
    month_val[2] = month + 1;
    year_val[2] = year;
    if (month_val[2] > 12) {
        month_val[2] = 1;
        year_val[2] = year + 1;
    }

    for (idx = 0; idx < 3; idx++) {
        start_day[idx] = getFirstDayOfMonth(year_val[idx], month_val[idx]);
        days_in_month[idx] = daysInMonth(year_val[idx], month_val[idx]);
        current_day[idx] = 1;
    }
    
    printf(NEWLINE);

    for (idx = 0; idx < 3; idx++) {
        sprintf(title, " %4d%16s", year_val[idx],getMonthName(month_val[idx]));
        printf("%-23s", title);
    }

    printf(NEWLINE NEWLINE);

    for (idx = 0; idx < 3; idx++) {
        printf(" Mo Tu We Th Fr Sa  S  ");
    }
    printf(NEWLINE);

    for (idx = 0; idx < 3; idx++) {
        printf(" -- -- -- -- -- -- --  ");
    }
    printf(NEWLINE);

    for (week = 0; week < 6; week++) {
        printf(" ");
        for (idx = 0; idx < 3; idx++) {
            for (wday = 1; wday <= 7; wday++) {
                if (week == 0 && wday < start_day[idx]) {
                    printf("   ");
                } else if (current_day[idx] <= days_in_month[idx]) {
                    if (month_val[idx] == month && year_val[idx] == year && current_day[idx] == today_day) {
                        printf(ANSI_BOLD ANSI_GREEN "%2d " ANSI_RESET, current_day[idx]);
                        // printf("\033[47m\033[30m%2d \033[0m", current_day[idx]);
                    } else {
                        printf("%2d ", current_day[idx]);
                    }
                    current_day[idx]++;
                } else {
                    printf("   ");
                }
            }
            printf("  ");
        }
        printf(NEWLINE);
    }
}

void print_calendar_year(int year, int today_month, int today_day) {

    int quarter, idx, m1, week, wday;
    int start_day[3];
    int days_in_month[3];
    int current_day[3];
    int m;
    char title[24];

    for (quarter = 0; quarter < 4; quarter++) {
        
        m1 = quarter * 3 + 1;

        for (idx = 0; idx < 3; idx++) {
            m = m1 + idx;
            start_day[idx] = getFirstDayOfMonth(year, m);
            days_in_month[idx] = daysInMonth(year, m);
            current_day[idx] = 1;
        }

        printf("" NEWLINE);

        for (idx = 0; idx < 3; idx++) {
            m = m1 + idx;
            sprintf(title, " %4d%16s", year,getMonthName(m));
            printf("%-23s", title);
        }

        printf(NEWLINE NEWLINE);

        for (idx = 0; idx < 3; idx++) {
            printf(" Mo Tu We Th Fr Sa  S  ");
        }
        printf(NEWLINE);

        for (idx = 0; idx < 3; idx++) {
            printf(" -- -- -- -- -- -- --  ");
        }
        printf(NEWLINE);

        /* Linie kalendarza (max 6 tygodni) */
        for (week = 0; week < 6; week++) {
            printf(" ");
            for (idx = 0; idx < 3; idx++) {
                for (wday = 1; wday <= 7; wday++) {
                    if (week == 0 && wday < start_day[idx]) {
                        printf("   ");
                    } else if (current_day[idx] <= days_in_month[idx]) {
                        if ((m1 + idx) == today_month && current_day[idx] == today_day) {
                            printf(ANSI_BOLD ANSI_GREEN "%2d " ANSI_RESET, current_day[idx]);
                            // printf("\033[47m\033[30m%2d \033[0m", current_day[idx]);
                        } else {
                            printf("%2d ", current_day[idx]);
                        }
                        current_day[idx]++;
                    } else {
                        printf("   ");
                    }
                }
                printf("  ");
            }
            printf(NEWLINE);
        }
    }
}
