#ifndef KEYBOARD_HEAD
#define KEYBOARD_HEAD

extern bool kbd_requested;
extern volatile u8 kbd_keycode;
extern bool kbd_maj;

u8 kbd_getkeychar();
u8 kbd_getkeycode();

#endif
