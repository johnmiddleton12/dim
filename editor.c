#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode ()
{
    // set orig term attributes back
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode ()
{
    // read terminal attributes
    // tcgetattr is from "termios.h"
    // STDIN_FILENO is from "unistd.h"
    tcgetattr(STDIN_FILENO, &orig_termios);

    // atexit() is from std lib, registers function to run - at exit
    atexit(disableRawMode);

    // edit terminal attributes in struct
    // this uses bitwise NOT, then bitwise AND to only set bits
    // i.e., ECHO    = 0000001000
    //       ~(ECHO) = 1111110111
    //       then AND with flags only sets ECHO bit to 0
    // disabled settings:
    //    ECHO - shows what you're writing
    //    ICANON - reads line-by-line, instead of byte-by-byte
    //    ISIG - gets Ctrl-C & Ctrl-Z signals
    //    IXON - gets Ctrl-S & Ctrl-Q signals (data -> terminal flow control)
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IXON);

    // set new term attributes
    // TCSAFLUSH sets when to apply the change
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main ()
{
    enableRawMode();

    char c;
    // read from term until Ctrl-D or 'q'
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
    {
        // iscntrl comes from ctype.h, and checks if char is control char
        // control chars - ASCII 0-31 & 127
        if (iscntrl(c))
        {
            printf("%d\n", c);
        }
        else
        {
            printf("%d ('%c')\n", c, c);
        }
    }

    return 0;
}
