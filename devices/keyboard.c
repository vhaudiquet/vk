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

bool kbd_requested = false;
volatile u8 kbd_keycode = 0;

bool kbd_maj = false;

u8 azer_kbd_map[] = 
{
    '?', '?', 
    '&', 130, '"', '\'', '(', '-', 138, '_', 135, 133, ')', '=',
    '\b', '\t',
    'a', 'z', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '^', '$',
    '\n', '?',
    'q', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 'm', 151, '?', '?', '*',
    'w', 'x', 'c', 'v', 'b', 'n', ',', ';', ':', '!', '?',
    '?', '?', ' '
};

u8 azer_maj_kbd_map[] =
{
    '?', '?',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 167, '+',
    '\b', '\t',
    'A', 'Z', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 249, 156,
    '\n', '?',
    'Q', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'M', '%', '?', '?', 230,
    'W', 'X', 'C', 'V', 'B', 'N', '?', '.', '/', 245, '?',
    '?', '?', ' '
};

u8 kbd_getkeychar()
{
    u8 keycode = kbd_getkeycode();
    u8 size = sizeof(azer_kbd_map);
    u8 tr = keycode > size ? kbd_getkeychar() : (kbd_maj ? azer_maj_kbd_map[keycode] : azer_kbd_map[keycode]);
    return tr;
}

u8 kbd_getkeycode()
{
    //kbd_keycode = 0;
    kbd_requested = true;
    //while(!kbd_keycode);
    scheduler_wait_process(current_process, SLEEP_WAIT_IRQ, 1, 0);
    u8 buff = kbd_keycode;
    kbd_keycode = 0;
    return buff;
}
