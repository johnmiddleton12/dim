/*** included files ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// gross sys includes 🤨
#include <sys/ioctl.h>
#include <sys/types.h>

/*** defined things ***/

#define DIM_VERSION "0.0.1"
#define DIM_TAB_STOP 8

// ANDs input with 0b00011111, similar to how term handles Ctrl+(key)
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data objs ***/

// typedef allows to define a new type, use as erow instead of struct erow
// erow is editor row
typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

struct editorConfig {
    // cursor pos
    int cx, cy;
    // rx
    int rx;
    // row & col offset (scroll)
    int rowoff;
    int coloff;
    // size of terminal
    int screenrows;
    int screencols;
    // amount of rows in file
    int numrows;
    // array of erows for file
    erow *row;
    // term settings
    struct termios orig_termios;
};

struct editorConfig E;

/*** term funcs ***/

void die(const char* s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode ()
{
    // set orig term attributes back
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode ()
{
    // read terminal attributes
    // tcgetattr is from "termios.h"
    // STDIN_FILENO is from "unistd.h"
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

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
    struct termios raw = E.orig_termios;

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

// waits for a keypress, then returns that char 
// this is in term function section because it uses read, 
// a low level function for input
int editorReadKey()
{
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    
    // if c is an escape sequence, handle it
    // \x1b is Esc + [
    if (c == '\x1b')
    {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        
        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }
            else
            {
            // arrow keys send bytes '\x1b[' followed by 'A', 'B', 'C', or 'D'
                switch (seq[1])
                {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }

    if (c == 'h') return ARROW_LEFT;
    if (c == 'j') return ARROW_DOWN;
    if (c == 'k') return ARROW_UP;
    if (c == 'l') return ARROW_RIGHT;

    return c;
}

int getCursorPosition(int *rows, int *cols)
{
    // n command, which follows the escape sequence, queries the term
    // for status info
    // 6 arg asks for cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    char buf[32];
    unsigned int i = 0;

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }

    // set final char of the string to '\0', which is how term
    // expects strings to end
    buf[i] = '\0';
    
    // if string doesn't start w escape sequence, error
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    // if error in reading rows & cols, error
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    // call ioctl which gets size of terminal, comes from "sys/ioctl.h"
    // if it succeeds, it'll put size in winsize struct
    // if it fails, it'll send two escape sequences that put the cursor in
    // the bottom right of the screen, \x1b[999C
    // then get the cursor's position
    // this line was to test the possibility that ioctl failed, and use the backup
    /*if (1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)*/
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** input ***/

void editorMoveCursor(int key)
{
    // check if cursor is on an actua line, if it is get the row
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    // create a switch that handles the keys to move the cursor
    // up, down, left, and right
    switch(key)
    {
        case ARROW_UP:
            if (E.cy != 0) E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) E.cy++;
            break;
        case ARROW_LEFT:
            if (E.cx != 0) 
            {
                E.cx--;
            }
            else if (E.cy > 0)
            {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            // only move right on a row with text if the cursor is not at the end + 1
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
    }
    // have to get row again, since E.cy may have changed
    // if the cursor is on an actual line, get the row
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    // get row size if it exists
    int rowSize = row ? row->size : 0;
    // snap horizontal cursor pos to row length if it's greater than row length
    if (E.cx > rowSize) E.cx = rowSize;
}

void editorProcessKeypress()
{
    // wait for keypress
    int c = editorReadKey();

    // keypress logic
    switch(c)
    {
        // if q, quit
        // CTRL_KEY gets Ctrl+q instead of just q
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            if (E.cy < E.numrows)
            {
                E.cx = E.row[E.cy].size;
            }
            break;

        // TODO:handling page up after multiple presses causes weird behavior
        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP)
                {
                    E.cy = E.rowoff;
                }
                else if (c == PAGE_DOWN)
                {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy > E.numrows) E.cy = E.numrows;
                }

                int times = E.screenrows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
        case ARROW_DOWN:
            editorMoveCursor(c);
            break;
    }
}

/*** row operations ***/

int editorRowCxToRx(erow *row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++)
    {
        if (row->chars[j] == '\t')
        {
            rx += (DIM_TAB_STOP - 1) - (rx % DIM_TAB_STOP);
        }
        rx++;
    }
    return rx;
}

// takes the chars string and puts it into the render string 
void editorUpdateRow(erow *row)
{
    int j;

    // count tabs in string to know how much mem to alloc
    int tabs = 0;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(DIM_TAB_STOP - 1) + 1);

    int i = 0;
    for (j = 0; j < row->size; j++)
    {
        if (row->chars[j] == '\t')
        {
            row->render[i++] = ' ';
            while (i % DIM_TAB_STOP != 0) row->render[i++] = ' ';
        }
        else
        {
            row->render[i++] = row->chars[j];
        }
    }
    // how terminal expects end of strings to look like
    row->render[i] = '\0';
    row->rsize = i;
}

void editorAppendRow(char *s, size_t len)
{
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    
    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
}

/*** file input and output ***/

void editorOpen(char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1)
    {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
        {
            linelen--;
        }
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

/*** append buffer ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len)
{
    // allocate a new char array (string) with the added stuff
    // e.g., current string + new string (s)
    // cool function that either extends the block of memory existing
    // or does the necessary free() to find a new spot
    char *new = realloc(ab->b, ab->len + len);

    // if this new array is null, leave current alone
    if (new == NULL) return;
    
    // copies old string into new one
    memcpy(&new[ab->len], s, len);

    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/*** output ***/

void editorScroll()
{
    E.rx = E.cx;

    if (E.cy < E.numrows)
    {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    // if cursor is above screen, scroll up
    if (E.cy < E.rowoff)
    {
        E.rowoff = E.cy;
    }
    // if cursor is below screen, scroll down
    if (E.cy >= E.rowoff + E.screenrows)
    {
        E.rowoff = E.cy - E.screenrows + 1;
    }

    // if cursor is to the left of screen, scroll left
    if (E.rx < E.coloff)
    {
        E.coloff = E.rx;
    }
    // if cursor is to the right of screen, scroll right
    if (E.rx >= E.coloff + E.screencols)
    {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab)
{
    int y; 
    for (y = 0; y < E.screenrows; y++)
    {
        // check where user is scrolled to
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) 
        {
            // if we're at the middle of the terminal, print a message
            if (E.numrows == 0 && y == E.screenrows / 2)
            {
                char message[80];
                int messagelen = snprintf(message, sizeof(message),
                    "Dim editor -- version %s", DIM_VERSION);
                // truncate if message is too long for term
                if (messagelen > E.screencols) messagelen = E.screencols;

                // add padding to center message
                // E.screencols / 2 - messagelen / 2,
                // which equals:
                int padding = (E.screencols - messagelen) / 2;
                if (padding)
                {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);

                abAppend(ab, message, messagelen);
            }
            else
            {
                // print a tilda
                abAppend(ab, "~", 1);
            }
        }
        else
        {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }

        // K command erases part of cur line
        // 3 arg erases from cursor to end of line
        abAppend(ab, "\x1b[K", 3);

        // dont write new line on last line
        /*if (y < E.screenrows - 1)*/
        /*{*/
            abAppend(ab, "\r\n", 2);
        /*}*/
    }
}

void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = ABUF_INIT;

    // hide cursor before writing to screen
    abAppend(&ab, "\x1b[?25l", 6);

    // write & STDOUT_FILENO from "unistd.h"
    // 4 arg is writing 4 bytes to the terminal
    // first byte is \x1b, the escape character (27 in decimal)
    // 2-4 bytes are [2J
    // this is ESCAPE SEQUENCE - terminal instructions for text formatting
    // they start with \x1b (escape char) followed by [
    // J command clears the screen
    // arg for J is 2, which says clear the whole screen
    /*write(STDOUT_FILENO, "\x1b[2J", 4);*/

    // we want to clear a line as its being written, and this clears whole
    // screen, moved to editorDrawRows()
    /*abAppend(&ab, "\x1b[2J", 4);*/

    // H command moves cursor
    // default args for it are 1, 1
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    // put cursor at stored loc, term is 1-indexed, we are 0-indexed
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    // show cursor again
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);

    abFree(&ab);

}

/*** init ***/

void initEditor()
{
    // fields for cursor pos
    E.cx = 0;
    E.rx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 1;
}

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();

    if (argc >= 2)
    {
        editorOpen(argv[1]);
    }

    while (1)  
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
