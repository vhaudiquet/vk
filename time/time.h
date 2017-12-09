#ifndef TIME_HEAD
#define TIME_HEAD
#include "system.h"

//time_t is UNIX/POSIX time : 0 = 1st of January 1970, 0h (UTC), unit: seconds
typedef int32_t time_t;
time_t convert_to_std_time(u8 seconds, u8 minutes, u8 hours, u8 day, u8 month, u8 year);
void convert_to_readable_time(time_t time, u8* seconds, u8* minutes, u8* hour, u8* day, u8* month, u8* year);
time_t get_current_time_utc();

#endif