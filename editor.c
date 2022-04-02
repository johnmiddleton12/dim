#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void die(const char*s)
{
    perror(s);
    exit(1);
}

void disableRawMode ()
{
    // set orig term attributes back
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode ()
{
    // read terminal attributes
    // tcgetattr is from "termios.h"
    // STDIN_FILENO is from "unistd.h"
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");

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
    //    IEXTEN - gets Ctrl-V signal
    //    ICRNL - translates carriage returns TODO: not working
    //    OPOST - output processing
    //    misc - BRKINT, INPCK, ISTRIP, CS8
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // c_cc are 'control characters'
    // VMIN sets max bytes before read() can return
    // VTIME sets max amount of time to wait before read() returns
    // This creates a timeout for read - so we can do other things
    // read() will return if it doesnt get any input in a period
    // it will still return after each keypress, though
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    // set new term attributes
    // TCSAFLUSH sets when to apply the change
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int main ()
{
    enableRawMode();

    while (1)  
    {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
        // iscntrl comes from ctype.h, and checks if char is control char
        // control chars - ASCII 0-31 & 127
        if (iscntrl(c))
        {
            printf("%d\r\n", c);
        }
        else
        {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') break;
    }

    return 0;
}
