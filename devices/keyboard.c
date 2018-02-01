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

#include "system.h"
#include "keyboard.h"
#include "tasking/task.h"

/*
* THIS IS A TEMP KEYBOARD DRIVER ; i dont know yet if the keyboard driver has a place in the kernel,
* or if the kernel should just pass the keycode to the userland GUI or SCHELL, and this interface decides
* what to do with it
* EDIT : alright i figured that out now i'll have to make a nice layer for kbd and all
* Even if the keyboard driver takes place inside the kernel, there will be modules to support others kbd layouts
* (this is only used to test things)
*/

bool kbd_maj = false;
bool kbd_ctrl = false;
bool kbd_alt = false;

u8 azer_kbd_map[] = 
{
    0, 0, 
    '&', 130, '"', '\'', '(', '-', 138, '_', 135, 133, ')', '=',
    '\b', '\t',
    'a', 'z', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '^', '$',
    '\n', 0,
    'q', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 'm', 151, 0, 0, '*',
    'w', 'x', 'c', 'v', 'b', 'n', ',', ';', ':', '!', 0,
    0, 0, ' '
};

u8 azer_maj_kbd_map[] =
{
    0, 0,
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 167, '+',
    '\b', '\t',
    'A', 'Z', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 249, 156,
    '\n', 0,
    'Q', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'M', '%', 0, 0, 230,
    'W', 'X', 'C', 'V', 'B', 'N', '?', '.', '/', 245, 0,
    0, 0, ' '
};

u8 getchar(u8 keycode)
{
    if(keycode > sizeof(azer_kbd_map)) return 0;
    if(kbd_maj) return azer_maj_kbd_map[keycode];
    else return azer_kbd_map[keycode];
}
