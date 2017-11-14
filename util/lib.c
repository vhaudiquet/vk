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

#include "util.h"
#include "../video/video.h"
#include "memory/mem.h"
#include "filesystem/fs.h"

/*
* Contains basic utils functions, that would be provided by the libc if there was one
*/

/*------------------------------- MEMORY -----------------------------*/
void * memcpy(void * restrict dest, const void * restrict src, size_t n) 
{
	asm volatile("cld; rep movsb"
	            : "=c"((int){0})
	            : "D"(dest), "S"(src), "c"(n)
	            : "flags", "memory");
	return dest;
}

void * memset(void * dest, int c, size_t n) 
{
	asm volatile("cld; rep stosb"
	             : "=c"((int){0})
	             : "D"(dest), "a"(c), "c"(n)
	             : "flags", "memory");
	return dest;
}
/*--------------------------------------------------------------------*/

/*------------------------------- STRINGS ----------------------------*/
char* strcat(char* dest, const char* src)
{
    strcpy(dest + strlen(dest), src);
    return dest;
}

char* strcpy(char* dest, const char* src)
{
    char* save = dest;
    while ((*dest++ = *src++));
    return save;
}

char* strncpy(char* dest, const char* src, u32 n)
{
    char* save = dest; u32 i = 0;
    while ((i < n) && (*dest++ = *src++)) i++;
    *(dest++) = 0;
    return save;
}

char* strncat(char* dest, const char* src, u32 n)
{
    char* pos = dest;
    while(*pos) pos++;
    u32 i = 0;
    for(i = 0; i < n; i++)
    {
        if(!(*pos++ = *src++)) break;
    }
    return dest;
}

char* strchr(char* s, char c)
{
    while(*s) 
    {
        //kprintf("= %s (%c) =? %c\n", s, *s, c);
        if(*s == c) {return s;}
        s++;
    }
    return 0;
}

char* strrchr(char* s, char c)
{
    char* o = s;
    s = s + strlen(s) - 1;
    while(s != o)
    {
        if(*s == c) {return s;}
        s--;
    }
    return 0;
}

size_t strlen(const char* str)
{
    size_t retval = 0;
    for (; *str != '\0'; ++str)
        ++retval;
    return retval;
}

// Compare two strings. Returns -1 if str1 < str2, 0 if they are equal or 1 otherwise.
u32 strcmp(const char* s1, const char* s2)
{
    while ((*s1) && (*s1 == *s2))
    {
        ++s1;
        ++s2;
    }
    return (u32) (*s1 - *s2);
}

u32 strcmpnc(const char* s1, const char* s2)
{
    while((*s1) && ((*s1 == *s2) || (invCase((u8) *s1) == *s2)))
    {
        ++s1; ++s2;
    }
    return (u32) (*s1 - *s2);
}

//NOTE : FULL UNSAFE AND UNOPTIMIZED, TEMP
char** strsplit(char* str, char regex, u32* osize)
{
    u32 slen = strlen(str);
    u32 i = 0; char* off = str; u32 size = 0;

    //getting return size
    while((off = strchr(off, regex)) != 0) {size++; off++;}
    size++;

    #ifdef MEMLEAK_DBG
    char** tr = kmalloc(sizeof(char*)*size, "strsplit() return pointer");
    #else
    char** tr = kmalloc(sizeof(char*)*size);
    #endif

    off = str;
    do
    {   
        if(*off == regex) off++;

        u32 len;
        char* temp = strchr(off, regex);
        if(!temp) len = (u32) (str+slen-off);
        else len = (u32) (temp-off);

        #ifdef MEMLEAK_DBG
        tr[i] = kmalloc(len+1, "strsplit() in-string");
        #else
        tr[i] = kmalloc(len+1);
        #endif
        strncpy(tr[i], off, len);
        tr[i][len] = 0;

        i++;
    } while((off = strchr(off, regex)) != 0);

    *osize = size;
    return tr;
}

u16 strcfirst(char* str0, char* str1)
{
    u16 tr = 0;
    while((*str0 == *str1) && (*str0 != 0)) {tr++;str0++;str1++;}
    return tr;
}

char* strtrim(char* str)
{
    u32 len = strlen(str) - 1;
    while(*(str+len) == ' ')
    {
        *(str+len) = 0;
        len--;
    }
    return str;
}

unsigned char* toupper(unsigned char* s)
{
    for (size_t i = 0; s[i] != 0; i++)
    {
        s[i] = toUpper(s[i]);
    }
    return s;
}

unsigned char* tolower(unsigned char* s)
{
    for (size_t i = 0; s[i] != 0; i++)
    {
        s[i] = toLower(s[i]);
    }
    return s;
}

/*---------------------------------------------------------------------*/

/*------------------------------- STRING CONVERSION -----------------------------*/
void reverse(unsigned char* s)
{
    for (size_t i=0, j=strlen((char*) s)-1; i<j; i++, j--)
    {
        unsigned char c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

unsigned char* itoa(int n, unsigned char* s)
{
    if (n < 0)
    {
        s[0] = '-';
        utoa(((u32) (-n)), s + 1);
        return s;
    }
    return utoa(((u32) (n)), s);
}

unsigned char* utoa(u32 n, unsigned char* s)
{
    u32 i = 0;
    do // generate digits in reverse order
    {
        s[i++] = (unsigned char) (n % 10 + '0'); // get next digit
    }
    while ((n /= 10) > 0);     // delete it
    s[i] = '\0';
    reverse(s);
    return (s);
}

void i2hex(u32 val, unsigned char* dest)
{
    u32 len = 8;
    if(val < U8_MAX) len = 2;
    else if(val < U16_MAX) len = 4;

    unsigned char* cp = &dest[len];
    while (cp > dest)
    {
        unsigned char x = val & 0xF;
        val >>= 4;
        *--cp = (unsigned char) (x + ((x > 9) ? 'A' - 10 : '0'));
    }
    dest[len]='\0';
}

int atoi(const unsigned char* s)
{
    int num = 0;
    bool sign = false;
    for (size_t i=0; s[i] != 0; i++)
    {
        if (s[i] >= '0' && s[i] <= '9')
        {
            num = num * 10 + s[i] -'0';
        }
        else if (s[0] == '-' && i==0)
        {
            sign = true;
        }
        else
        {
            break;
        }
    }
    if (sign)
    {
        num *= -1;
    }
    return num;
}
/*------------------------------------------------------------------------------*/

/*------------------------------- I/O -----------------------------*/
static void vkprintf(const unsigned char* args, va_list ap)
{
	unsigned char buffer[32];
    u8 color = 0b00000111;
    u8 level = 0;

	//vga_text_puts((unsigned char*)"[KERNEL] ", 0b00001111);
	while(*args)
	{
		if(*args == '%')
		{
			switch(*(++args))
			{
				case 'u':
                    utoa(va_arg(ap, u32), buffer);
                    vga_text_puts(buffer, color);
                    break;
                case 'i': case 'd':
                    itoa(va_arg(ap, int32_t), buffer);
                    vga_text_puts(buffer, color);
                    break;
                case 'X':
                    i2hex(va_arg(ap, u32), buffer);
                    toupper(buffer);
                    vga_text_puts(buffer, color);
                    break;
                case 'x':
                    i2hex(va_arg(ap, u32), buffer);
                    tolower(buffer);
                    vga_text_puts(buffer, color);
                    break;
				case 's':
                    {
                        char* temp = va_arg(ap, char*);
                        vga_text_puts((unsigned char*)temp, color);
                        break;
                    }
                case 'c':
                    vga_text_putc((uint8_t)va_arg(ap, int32_t), color);
                    break;
                case 'v':
                    color = (u8) va_arg(ap, u32);
                    break;
                case 'l':
                    level = (u8) va_arg(ap, u32);
                    color = (level == 0 ? 0b00000111 : level == 1 ? 0b00000010 : level == 2 ? 0b00000100 : 0b00001001);
                    break;
				default:
					vga_text_putc(*args, color);
                    break;
			}
		}
		else
		{
			vga_text_putc(*args, color);
		}

		args++;
	}
	//vga_text_putc('\n', 0b00000111);
}

void kprintf(const char* args, ...)
{
    va_list ap;
    va_start(ap, (u8*) args);
    vkprintf((u8*) args, ap);
    va_end(ap);
}
/*------------------------------------------------------------------*/
