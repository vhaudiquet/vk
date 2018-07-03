#ifndef TERMIOS_HEAD
#define TERMIOS_HEAD

typedef unsigned int tcflag_t;
typedef unsigned int speed_t;
typedef unsigned char cc_t;

#define NCCS 32
struct termios
{
    tcflag_t c_iflag; //input modes
    tcflag_t c_oflag; //output modes
    tcflag_t c_cflag; //control modes
    tcflag_t c_lflag; //local modes
    cc_t c_cc[NCCS]; //control chars
};

/* control chars */
#define VEOF 1 /* end of file (^D) */
#define VEOL 2 /* end of line */
#define VERASE 3 /* backspace (^H) */
#define VINTR 4 /* interrupt (^C) */
#define VKILL 5 /* erase input buffer (^U) */
#define VMIN 6 /* minimum character numver */
#define VQUIT 7 /* sigquit */
#define VSTART 8 /* restart stopped input (^Q) */
#define VSTOP 9 /* stop input (^S) */
#define VSUSP 10 /* suspend (sigstp) (^Z) */
#define VTIME 11 /* timeout */

/* input modes flags */
#define BRKINT 1
#define ICRNL 2
#define IGNBRK 4
#define IGNCR 8
#define IGNPAR 16
#define INLCR 32
#define INPCK 64
#define ISTRIP 128
#define IUCLC 256
#define IXANY 512
#define IXOFF 1024
#define IXON 2048
#define PARMRK 4096

/* output modes flags */
#define OPOST  0000001
#define OLCUC  0000002
#define ONLCR  0000004
#define OCRNL  0000010
#define ONOCR  0000020
#define ONLRET 0000040
#define OFILL  0000100
#define OFDEL  0000200
#define NLDLY  0000400
#define  NL0   0000000
#define  NL1   0000400
#define CRDLY  0003000
#define  CR0   0000000
#define  CR1   0001000
#define  CR2   0002000
#define  CR3   0003000
#define TABDLY 0014000
#define  TAB0  0000000
#define  TAB1  0004000
#define  TAB2  0010000
#define  TAB3  0014000
#define BSDLY  0020000
#define  BS0   0000000
#define  BS1   0020000
#define FFDLY  0100000
#define  FF0   0000000
#define  FF1   0100000
#define VTDLY  0040000
#define VT0    0000000
#define VT1    0040000

/* baud rates */
#define B0      0
#define B50     1
#define B75     2
#define B110    3
#define B134    4
#define B150    5
#define B200    6
#define B300    7
#define B600    8
#define B1200   9
#define B1800  10
#define B2400  11
#define B4800  12
#define B9600  13
#define B19200 14
#define B38400 15

/* control modes */
#define CSIZE   0000060
#define  CS5    0000000
#define  CS6    0000020
#define  CS7    0000040
#define  CS8    0000060
#define CSTOPB  0000100
#define CREAD   0000200
#define PARENB  0000400
#define PARODD  0001000
#define HUPCL   0002000
#define CLOCAL  0004000

/* local modes */
#define ISIG    0000001
#define ICANON  0000002
#define XCASE   0000004
#define ECHO    0000010
#define ECHOE   0000020
#define ECHOK   0000040
#define ECHONL  0000100
#define NOFLSH  0000200
#define TOSTOP  0000400
#define IEXTEN  0001000

/* attributes */
#define TCSANOW   0x0001
#define TCSADRAIN 0x0002
#define TCSAFLUSH 0x0004

#define TCIFLUSH  0x0001
#define TCIOFLUSH 0x0003
#define TCOFLUSH  0x0002

#define TCIOFF    0x0001
#define TCION     0x0002
#define TCOOFF    0x0004
#define TCOON     0x0008

speed_t cfgetispeed(const struct termios*);
speed_t cfgetospeed(const struct termios*);
int cfsetispeed(const struct termios*, speed_t);
int cfsetospeed(const struct termios*, speed_t);
int tcdrain(int);
int tcflow(int, int);
int tcflush(int, int);
int tcgetattr(int, struct termios *);
u32 tcgetsid(int);
int tcsendbreak(int, int);
int tcsetattr(int, int, struct termios *);

#endif