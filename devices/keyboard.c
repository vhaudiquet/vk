#include "system.h"
#include "keyboard.h"
#include "tasking/task.h"

/*
* THIS IS A TEMP KEYBOARD DRIVER ; i dont know yet if the keyboard driver has a place in the kernel,
* or if the kernel should just pass the keycode to the userland GUI or SCHELL, and this interface decides
* what to do with it
* Even if the keyboard driver takes place inside the kernel, there will be modules to support others kbd layouts
* (this is only used to test thingsS)
*/

bool kbd_requested = false;
volatile u8 kbd_keycode = 0;

bool kbd_maj = false;

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

u8 kbd_getkeychar()
{
    u8 keycode = kbd_getkeycode();
    u8 size = sizeof(azer_kbd_map);
    u8 tr = keycode > size ? 0 : (kbd_maj ? azer_maj_kbd_map[keycode] : azer_kbd_map[keycode]);
    return tr;
}

u8 kbd_getkeycode()
{
    //kbd_keycode = 0;
    kbd_requested = true;
    //while(!kbd_keycode);
    scheduler_wait_process(current_process, SLEEP_WAIT_IRQ, 1);
    u8 buff = kbd_keycode;
    kbd_keycode = 0;
    return buff;
}
