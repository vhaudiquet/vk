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

#ifndef TYPE_HEAD
#define TYPE_HEAD

//Common types
typedef unsigned char u8, uint8_t;
typedef unsigned short u16, uint16_t;
typedef unsigned int u32, uint32_t;
typedef unsigned long long u64, uint64_t;
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef u32 uintptr_t;

typedef unsigned long size_t;

//Booleans
typedef unsigned char boolean, bool;
#define true 1
#define false 0

//va_lists
typedef __builtin_va_list va_list;
#define va_start(ap, X)    __builtin_va_start(ap, X)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(dest, src) __builtin_va_copy(dest, src)

//Limits
#define U8_MAX (255)
#define U16_MAX (65535)
#define U32_MAX (4294967295)
#define U64_MAX (18446744073709551615)
#define S8_MAX (128)
#define S16_MAX (32768)
#define S32_MAX (2147483648)
#define S64_MAX (0x8000000000000000)

#endif