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

#ifndef UTIL_HEAD
#define UTIL_HEAD

#include "types.h"

//Memory
void * memcpy(void * restrict dest, const void * restrict src, size_t n);
void * memset(void * dest, int c, size_t n);

//Strings
char* strcat(char* dest, const char* src);
char* strcpy(char* dest, const char* src);
size_t strlen(const char* str);
u32 strcmp(const char* s1, const char* s2);
char* strchr(char* s, char c);
char* strrchr(char* s, char c);
char* strncpy(char* dest, const char* src, u32 n);
char* strncat(char* dest, const char* src, u32 n);
char* strtrim(char* str);

char** strsplit(char* str, char regex, u32* osize);
u16 strcfirst(char* str0, char* str1);
u16 strcfirstnc(char* str0, char* str1);
u32 strcmpnc(const char* s1, const char* s2);

unsigned char* toupper(unsigned char* s);
unsigned char* tolower(unsigned char* s);

//Chars
#define isdigit(c) ((c) >= '0' && (c) <= '9')
#define isalpha(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define isalnum(c) (isdigit(c) || isalpha(c))
#define isupper(c) ((c) >= 'A' && (c) <= 'Z')
#define islower(c) ((c) >= 'a' && (c) <= 'z')

static inline unsigned char toLower(unsigned char c) { return isupper(c) ? (unsigned char) (('a' - 'A') + c) : c; }
static inline unsigned char toUpper(unsigned char c) { return islower(c) ? (unsigned char) (('A' - 'a') + c) : c; }
static inline unsigned char invCase(unsigned char c) {return islower(c) ? toUpper(c) : toLower(c);}

//Conversion
int atoi(const unsigned char* s);
void i2hex(u32 val, unsigned char* dest);
unsigned char* utoa(u32 n, unsigned char* s);
unsigned char* itoa(int n, unsigned char* s);
void reverse(unsigned char* s);

//align
#define alignup(i, a) while(i % a) i++
#define aligndown(i, a) while(i % a) i--

//LISTS
typedef struct list_entry
{
    void* element;
    struct list_entry* next;
} list_entry_t;
void list_free(list_entry_t* list, u32 list_size);
void list_free_eonly(list_entry_t* list, u32 list_size);

//QUEUES
typedef struct QUEUE
{
    void** front;
    void** rear;
    u32 size;
} queue_t;

queue_t* queue_init();
void queue_add(queue_t* queue, void* element);
void* queue_take(queue_t* queue);
void queue_remove(queue_t* queue, void* element);

//STACKS
typedef struct STACK
{
    void** buffer;
    u32 count;
    u32 buffer_size;
} stack_t;

stack_t* stack_init();
void stack_add(stack_t* stack, void* element);
void* stack_take(stack_t* stack);
void* stack_look(stack_t* stack, u32 position);
void stack_remove(stack_t* stack, void* element);

#endif
