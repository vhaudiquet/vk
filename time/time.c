/*  
    This file is part of VK.
    Copyright (C) 2017 Valentin Haudiquet

    VK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    VK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VK.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "time.h"
#include "memory/mem.h"

typedef struct CMOS_TIME
{
    u8 seconds;
    u8 minutes;
    u8 hours;
    u8 weekday;
    u8 monthday;
    u8 month;
    u8 year;
    u8 century; // maybe register is not present
} cmos_time_t;

#define CMOS_FLAG_SET 1
#define CMOS_FLAG_BCD 2
#define CMOS_FLAG_12 4
u8 cmos_flags = 0;

cmos_time_t* get_cmos_time()
{
    if(!(cmos_flags & CMOS_FLAG_SET))
    {
        outb (0x70, (0x80 << 7) | (0xB));
        u8 register_b = inb(0x71);
        if(!(register_b & 2)) cmos_flags |= CMOS_FLAG_12;
        if(!(register_b & 4)) cmos_flags |= CMOS_FLAG_BCD;
    }

    cmos_time_t* ct = kmalloc(sizeof(cmos_time_t));

    outb (0x70, (0x80 << 7) | (0x0));
    ct->seconds = inb(0x71);
    outb (0x70, (0x80 << 7) | (0x2));
    ct->minutes = inb(0x71);
    outb (0x70, (0x80 << 7) | (0x4));
    ct->hours = inb(0x71);
    outb (0x70, (0x80 << 7) | (0x6));
    ct->weekday = inb(0x71);
    outb (0x70, (0x80 << 7) | (0x7));
    ct->monthday = inb(0x71);
    outb (0x70, (0x80 << 7) | (0x8));
    ct->month = inb(0x71);
    outb (0x70, (0x80 << 7) | (0x9));
    ct->year = inb(0x71);
    //this part is not sure
    outb (0x70, (0x80 << 7) | (0x32));
    ct->century = inb(0x71);

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wconversion"
    if(cmos_flags & CMOS_FLAG_BCD)
    {
        ct->seconds = ( (ct->seconds & 0xF0) >> 1) + ( (ct->seconds & 0xF0) >> 3) + (ct->seconds & 0xf);
        ct->minutes = ( (ct->minutes & 0xF0) >> 1) + ( (ct->minutes & 0xF0) >> 3) + (ct->minutes & 0xf);
        ct->hours = ( (ct->hours & 0xF0) >> 1) + ( (ct->hours & 0xF0) >> 3) + (ct->hours & 0xf);
        ct->weekday = ( (ct->weekday & 0xF0) >> 1) + ( (ct->weekday & 0xF0) >> 3) + (ct->weekday & 0xf);
        ct->monthday = ( (ct->monthday & 0xF0) >> 1) + ( (ct->monthday & 0xF0) >> 3) + (ct->monthday & 0xf);
        ct->month = ( (ct->month & 0xF0) >> 1) + ( (ct->month & 0xF0) >> 3) + (ct->month & 0xf);
        ct->year = ( (ct->year & 0xF0) >> 1) + ( (ct->year & 0xF0) >> 3) + (ct->year & 0xf);
        ct->century = ( (ct->century & 0xF0) >> 1) + ( (ct->century & 0xF0) >> 3) + (ct->century & 0xf);
    }
    if(cmos_flags & CMOS_FLAG_12)
    {
        if(ct->hours & 0x80) {ct->hours &= ~0x80; if(ct->hours != 12) ct->hours+=12; else ct->hours = 0;} 
    }
    #pragma GCC diagnostic pop

    return ct;
}

time_t convert_to_std_time(u8 seconds, u8 minutes, u8 hours, u8 day, u8 month, u8 year)
{
    time_t tr = seconds + 60*minutes + 3600*hours + 86400*(day-1);
    int32_t years = 2000 + year - 1; // assuming we are in the 21st century
    month--;

    switch(month) 
    {
		case 11: tr += 30*86400;
		case 10: tr += 31*86400;
		case 9: tr += 30*86400;
		case 8: tr += 31*86400;
		case 7: tr += 31*86400;
		case 6: tr += 30*86400;
		case 5: tr += 31*86400;
		case 4: tr += 30*86400;
		case 3: tr += 31*86400;
		case 2: tr += 28*86400; if ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0))) tr += 86400;
		case 1: tr += 31*86400;
		default: break;
	}

    while (years > 1969)
    {
		tr += 365*86400;
        if (years % 4 == 0) 
        {
            if (years % 100 == 0) 
            {
                if (years % 400 == 0) tr += 86400;
            } 
            else 
            {
				tr += 86400;
			}
		}
		years--;
    }

    return tr;
}

time_t get_current_time_utc()
{
    cmos_time_t* ct = get_cmos_time();
    return convert_to_std_time(ct->seconds, ct->minutes, ct->hours, ct->monthday, ct->month, ct->year);
}
