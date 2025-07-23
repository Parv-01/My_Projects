// Includes
#define _DEFAULT_SOURCE
#define _BSED_SOURCE
#define _GNU_SOURCE // These 3 maros are specifically defined for getline() ,may or maynot be used but good habbit to add them here
#include <ctype.h>  // For iscntrl function
#include <errno.h>  // For error handling
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>    // For exit function
#include <stdarg.h>
#include <sys/ioctl.h> // For terminal control IOCTL->ip/op ctrl to get window size
#include <termios.h>   // For terminal control
#include <unistd.h>
#include <string.h>    // For string manipulation functions
#include <sys/types.h> //For memory functions
#include <time.h>

// Defines
#define delulu_VERSION "0.0.1"   // Version of the text editor
#define DELULU_TAB_STOP 8
#define DELULU_QUIT_TIMES 3
#define CTRL_KEY(k) ((k) & 0x1f) // Macro to convert a control key to its ASCII value

enum editor_key
{
    // This enum defines the keys used in the editor
    BACKSPACE=127,     //
    ARROW_LEFT = 1000, // Left arrow key
    ARROW_RIGHT,       // Right arrow key
    ARROW_UP,          // Up arrow key
    ARROW_DOWN,        // Down arrow key
    DEL_KEY,           // Delete key <esc>[3~ in VT100
    HOME_KEY,          // Home key <esc>[1~ ,[7~,[H,OH in VT100>
    END_KEY,           // End key <esc>[4~ ,[8~,[F,OF in VT100>
    PAGE_UP,           // Page up key <esc>[5~ in VT100>
    PAGE_DOWN,         // Page down key <esc>[6~ in VT100>
};
// data

typedef struct erow
{
    int size,rsize;
    char *chars,*render;
} erow;

//  struct termios original_termios; // To store original terminal attributes
struct editor_config
{
    // This struct holds the editor configuration
    int cx, cy;                      // Cursor position (x, y)
    int rx;
    int rowoff;                     // Row offset for scrolling
    int coloff;                     // Column offset for scrolling
    int screenrows;                  // Number of rows in the terminal
    int screencols;                  // Number of columns in the terminal
    struct termios original_termios; // To store original terminal attributes
    int numrows;
    // erow row;
    //To store multiple lines we make erow array of erow structs 
    erow *row; // Array of rows in the editor
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
}E; // Global variable to hold editor configuration

/*prototypes*/
void editor_setstatus_Message(const char *fmt,...);
void editor_refressh_screen();
char *editorPrompt(char *prompt);

// terminal functions
void die(const char *s)
{
    // to clr the screen and move cursor to top-left corner before printing error message
    write(STDOUT_FILENO, "\x1b[2J", 4); // Clear the screen
    write(STDOUT_FILENO, "\x1b[H", 3);  // Move cursor to the home position (top-left corner)
    perror(s);                          // Print error message
    exit(1);                            // Exit with error code
}

void reset_terminal_mode()
{
    // tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios); // Restore original terminal attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original_termios) == -1)
    {
        die("tcsetattr"); // Restore original terminal attributes on exit
    }
}

void set_terminal_raw_mode()
{

    // tcgetattr(STDIN_FILENO, &original_termios); // Get current terminal attributes
    if (tcgetattr(STDIN_FILENO, &E.original_termios) == -1)
    {
        die("tcgetattr"); // Get current terminal attributes
    }
    atexit(reset_terminal_mode); // Ensure original attributes are restored on exit

    struct termios term = E.original_termios; // Copy original attributes to term
    // DIFFERENT FLAGS
    //  term.c_lflag &= ~(IXON); // Disable flow control
    //  //CTRL+S and CTRL+Q are used for flow control, S->XOFF pause tx,Q->XON resume tx,ctrl+S=19 byte & ctrl+Q=17 byte
    //  term.c_lflag &= ~(IEXTEN|ICANON | ECHO | ISIG); // Disable canonical mode and echo
    //  //ISIG is used to disable signals like Ctrl+C,ctrl+Z: Now ctrl+c=3 byte & ctrl+z=26 byte
    //  //IEXTEN is used to disable extended input characters,CTRL+V=22 byte & CTRL+O=15 byte
    //  term.c_lflag&=~(ICRNL|IXON); // Disable input translation and flow control
    //  //CR->carriage return,NL->newline,IXON->flow control,ctrl+M=13 byte & enter key=13 byte
    //  term.c_lflag&=~(OPOST); // Disable output processing
    //  //OPOST is used to disable output processing which means writing "\r\n" for newline
    // Misc Flags
    term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // Disable input flags
    term.c_oflag &= ~(OPOST);                                  // Disable output flags
    term.c_cflag |= (CS8);                                     // Set character size to 8 bits
    term.c_lflag &= ~(IEXTEN | ICANON | ECHO | ISIG);          // Disable canonical mode, echo, and signals
    // BRKINT when turned pn sent SIGINT signal to program like Ctrl+C
    // INPCK is used to disable parity checking
    // ISTRIP is used to disable stripping of high-order bit
    // CS8 is used to set character size to 8 bits, which is the default
    term.c_cc[VMIN] = 0;  // Minimum number of characters to read
    term.c_cc[VTIME] = 1; // Timeout for reading characters (1 decisecond)
    // c_cc is an array of control characters, which are used to control the terminal behavior
    // VMIN is the minimum number of characters to read before returning from read()
    // VTIME is the timeout for reading characters, in deciseconds (0.1 seconds)

    // tcsetattr(STDIN_FILENO, TCSAFLUSH, &term); // Set the new attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term) == -1)
    {
        die("tcsetattr"); // Set the new attributes
    }
}

int key_read_editor()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    { // Read a single character from standard input
        if (nread == -1 && errno != EAGAIN)
        {                // If read error occurs, handle it
            die("read"); // Handle read error
        }
    }
    if (c == '\x1b') // If the character is an escape character
    {
        char seq[3] = {0};                       // Buffer to hold the escape sequence
        if (read(STDIN_FILENO, &seq[0], 1) != 1) // Read the next character
        {
            return '\x1b'; // If read error occurs, return escape character
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) // Read the next character
        {
            return '\x1b'; // If read error occurs, return escape character
        }
        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9') // If the second character is a digit
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) // Read the next character
                {
                    return '\x1b'; // If read error occurs, return escape character
                }
                if (seq[2] == '~') // If the third character is '~'
                {
                    switch (seq[1]) // Check the second character of the escape sequence
                    {
                    case '1':
                        return HOME_KEY; // Home key
                    case '3':
                        return DEL_KEY; // Delete key
                    case '4':
                        return END_KEY; // End key
                    case '5':
                        return PAGE_UP; // Page up key
                    case '6':
                        return PAGE_DOWN; // Page down key
                    case '7':
                        return HOME_KEY; // Home key
                    case '8':
                        return END_KEY; // End key
                    }
                }
            }
            else
            {
                switch (seq[1]) // Check the second character of the escape sequence
                {
                case 'A':
                    return ARROW_UP; // Up arrow key
                case 'B':
                    return ARROW_DOWN; // Down arrow key
                case 'C':
                    return ARROW_RIGHT; // Right arrow key
                case 'D':
                    return ARROW_LEFT; // Left arrow key
                case 'H':
                    return HOME_KEY; // Home key
                case 'F':
                    return END_KEY; // End key
                }
            }
        }
        else if (seq[0] == 'O') // If the first character is 'O'
        {
            switch (seq[1]) // Check the second character of the escape sequence
            {
            case 'H':
                return HOME_KEY; // Home key
            case 'F':
                return END_KEY; // End key
            }
        }
        // If the escape sequence is not recognized, return the escape character
        return '\x1b';
    }
    else
    {
        return c; // If the character is not an escape character, return it
    }
}

// function to get cursor position mn cmd used to query terminal for status we wanted to give it arg 6 to aks for cursor position
// then we can read reply from std ip
int get_cursor_position(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    {
        return -1;
    } // Write escape sequence to request cursor position
    while (i < sizeof(buf) - 1)
    { // Read response from terminal
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
        {
            break; // Break if read error occurs
        }
        if (buf[i] == 'R')
        {
            break; // Break when we reach the end of the response
        }
        i++;
    }
    buf[i] = '\0'; // Null-terminate the buffer
    // printf("\r\n&buf[1]: '%s'\r\n",&buf[1]); // Print the response for debugging
    // //We neglect 1 char which is '\x1b' (escape char) and expect sstrng to end with 0 byte so we make sure to
    // //assign '\0' to final byte of buf
    // key_read_editor(); // Wait for a key press to ensure terminal is ready
    // return -1;
    if (buf[0] != '\x1b' || buf[1] != '[')
    {
        return -1; // If response is not in expected format, return error
    }
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    {
        return -1; // If sscanf fails to parse the response, return error
    }
    return 0;
}

int get_window_size(int *rows, int *cols)
{
    struct winsize ws;
    // if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    //     return -1; // If ioctl fails or terminal size is zero, return error //easy way to get terminal size
    //}
    ////if ioctl fails then what can be the strategy to cope witht the situation/hard way
    /*removing the 1 from the below if condition after working out with the cursor pos and correct no of > */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
        {
            return get_cursor_position(rows, cols);
        } // C cmd moves cursor to right ,B cmd moves it down,we use big values so that cursor moves to bottom-right corner of terminal
        // we dont use <esc>[999;999H bcoz it may not work on all terminals,so we use C and B cmd to move cursor to bottom-right corner]
        // Note: 1 at front test fallback branch we developing
        // As we always return -1 (meaning error occured)from key_read_editor() function,so we can observe results of esc seq before prog calls die() and clr screen

        key_read_editor(); // Wait for a key press to ensure terminal is ready
        return -1;
    }
    *rows = ws.ws_row; // Get number of rows
    *cols = ws.ws_col; // Get number of columns
    return 0;
}

/* row operations*/
int editor_rowcxtorx(erow *row,int cx){
    int rx=0,j;
    for(j=0;j<cx;j++){
        if(row->chars[j]=='\t'){
            rx +=(DELULU_TAB_STOP-1)-(rx%DELULU_TAB_STOP);
        }
        rx++;
    }
    return rx;
}

void editor_UpdateRows(erow *row){
    int tabs=0;
    int j;
    for(j=0;j<row->size;j++){
        if(row->chars[j]=='\t'){
            tabs++;
        }
    }
    free(row->render);
    row->render=malloc(row->size+tabs*(DELULU_TAB_STOP-1)+1);
    int idx=0;
    for(j=0;j<row->size;j++){
        if(row->chars[j]=='\t'){
            row->render[idx++]=' ';
            while(idx % DELULU_TAB_STOP !=0){
                row->render[idx++]=' ';
            }
        }else{
            row->render[idx++]=row->chars[j];
        }
    }
    row->render[idx]='\0';
    row->rsize=idx;
}

void editor_AppendRows(int at,char *s,size_t len){
    if(at<0||at>E.numrows){
        return;
    }
    E.row=realloc(E.row,sizeof(erow)*(E.numrows+1)); // Reallocate memory for the rows array
    memmove(&E.row[at++],&E.row[at],sizeof(erow)*(E.numrows-at));
    //int at = E.numrows; // Get the current number of rows
    E.row[at].size = len; // Set the size of the new row
    E.row[at].chars = malloc(len + 1); // Allocate memory for the
    // characters in the new row
    memcpy(E.row[at].chars, s, len); // Copy the characters from the
    E.row[at].chars[len] = '\0'; // Null-terminate the string
    E.row[at].rsize=0;//Contains size of contents of render string
    E.row[at].render=NULL;
    editor_UpdateRows(&E.row[at]);
    E.numrows++; // Increment the number of rows
    E.dirty++;

}

void editorFreerow(erow *row){
    free(row->render);
    free(row->chars);
}

void editor_RowinsertChar(erow *row,int at,int c){
    if(at<0||at>row->size){
        at=row->size;
    }
    row->chars=realloc(row->chars,row->size+2);
    memmove(&row->chars[at+1],&row->chars[at],row->size-at+1); //comes from string same like mmcpy but safer 
    row->size++;
    row->chars[at]=c;
    editor_UpdateRows(row);
    E.dirty++;
}

void editor_rowdelchar(erow *row,int at){
    if(at<0||at>=row->size){
        return;
    }
    editorFreerow(&E.row[at]);
    memmove(&row->chars[at],&row->chars[at+1],row->size-at);
    row->size--;
    editor_UpdateRows(row);
    E.dirty++;
}

/*editor operations*/
void editor_insertchar(int c){
    if(E.cy==E.numrows){
        editor_AppendRows(E.numrows,"",0);
    }
    editor_RowinsertChar(&E.row[E.cy],E.cx,c);
    E.cx++;
}

void editor_insertNEwline(){
    if(E.cx==0){
        editor_AppendRows(E.cy,"",0);
    }else{
        erow *row=&E.row[E.cy];
        editor_AppendRows(E.cy+1,&row->chars[E.cx],row->size-E.cx);
        row=&E.row[E.cy];
        row->size=E.cx;
        row->chars[row->size]='\0';
        editor_UpdateRows(row);
    }
    E.cy++;
    E.cx=0;
}

void editor_delchar(){
    if(E.cy==E.numrows){
        return;
    }
    if(E.cx==0&&E.cy==0){
        return;
    }
    erow *row=&E.row[E.cy];
    if(E.cx>0){
        editor_rowdelchar(row,E.cx-1);
        E.cx--;
    }else{
        E.cx=E.row[E.cy-1].size;
        editorRowAppendString(&E.row[E.cy-1],row->chars,row->size);
        editor_rowdelchar(row,E.cy);
        E.cy--;
    }
}

void editorRowAppendString(erow *row,char *s,size_t len){
    row->chars=realloc(row->chars,row->size+len+1);
    memcpy(&row->chars[row->size],s,len);
    row->size+=len;
    row->chars[row->size]='\0';
    editor_UpdateRows(row);
    E.dirty++;
}

/*File i/o */
void *editor_rowtostring(int *buflen){
    int totlen=0;
    int j;
    for(j=0;j<E.numrows;j++){
        totlen+=E.row[j].size+1;
    }
    *buflen=totlen;
    char *buf=malloc(totlen);
    char *p=buf;
    for(j=0;j<E.numrows;j++){
        memcpy(p,E.row[j].chars,E.row[j].size);
        p += E.row[j].size;
        *p ='\n';
        p++;
    }
    return buf;
}

void editor_open(char *filename)
{ // Will open and read file from disk
    free(E.filename);
    E.filename=strdup(filename);
    FILE *fp = fopen(filename, "r");
    if (!fp)
    {
        die("fopen");
    }
    // char *line="Hello to Delulu";
    // ssize_t linelen=15;
    char *line = NULL;
    ssize_t linecap = 0;
    ssize_t linelen;
    // linelen = getline(&line, &linecap, fp);
    // if (linelen != -1)
    // {
    //     while (linelen > 0 && (line[linelen - 1] == '\n') || (line[linelen - 1] == '\r'))
    //     {
    //         linelen--;
    //     }
    //     editor_AppendRows(line,linelen); // Append the line to the editor rows
    // }
    while((linelen=getline(&line,&linecap,fp))!=-1){
        while(linelen>0 && (line[linelen-1]=='\n' || line[linelen-1]=='\r')){
            linelen--; // Remove trailing newline or carriage return
        }
        editor_AppendRows(E.numrows,line,linelen); // Append the line to the editor rows
    }
    free(line);
    fclose(fp);
    E.dirty=0;
}

void editor_save(){
    if(E.filename==NULL){
        E.filename=editorPrompt("Save as: %s (ESC to cancel)");
        if(E.filename==NULL){
            editor_setstatus_Message("Save aborted");
            return;
        }
    }
    int len;
    char *buf=editor_rowtostring(&len);
    int fd =open(E.filename,O_RDWR|O_CREAT,0644);
    if(fd!=-1){
        if(ftruncate(fd,len)!=-1){
            if(write(fd,buf,len)==len){
                close(fd);
                free(buf);
                E.dirty=0;
                editor_setstatus_Message("%d bytes written to disk",len);
                return;
            }
        }close(fd);
    }
    free(buf);
    editor_setstatus_Message("Can't save I/O error: %s",strerror(errno));
    //O_RDWR -> for read and write
    //O_CREAT -> extra arg to contain mode(the permission) the new file should have 
    //0644 -> std permissions
}

/*Append Buffer*/
// This section is for handling the append buffer, which is used to efficiently build strings for output
struct abuf
{
    char *b; // Pointer to the buffer
    int len; // Length of the buffer
};
#define ABUF_INIT {NULL, 0} // Initialize the append buffer
// append buff consist of ptr to our buff mem and length ,we defined
// abuf_init const representing empty buffer which acts as constructor
void ab_append(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len); // Reallocate memory for the buffer
    if (new == NULL)
    {
        return; // If memory allocation fails, do nothing
    }
    memcpy(&new[ab->len], s, len); // Copy the new string into the buffer
    ab->b = new;                   // Update the buffer pointer
    ab->len += len;                // Update the length of the buffer
}

void ab_free(struct abuf *ab)
{
    free(ab->b);  // Free the memory allocated for the buffer
    ab->b = NULL; // Set the pointer to NULL to avoid dangling pointer
    ab->len = 0;  // Reset the length to 0
}

// output functions
void editor_scroll(){
    E.rx=0;
    if(E.cy<E.numrows){
        E.rx=editor_rowcxtorx(&E.row[E.cy],E.cx);
    }
    if(E.cy < E.rowoff) {
        E.rowoff = E.cy; // If the cursor is above the visible area, adjust the row offset
    }
    if(E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1; // If the cursor is below the visible area, adjust the row offset
    }
    if(E.rx<E.coloff){
        E.coloff=E.rx;
    }
    if(E.rx>=E.coloff + E.screencols){
        E.coloff = E.rx - E.screencols +1;
    }
}

// This function draws the rows of the editor to the append buffer
void editor_draw_rows(struct abuf *ab)
{
    // This function would draw the rows of the editor
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        int filerow = y + E.rowoff; // Calculate the row to be drawn based on the offset
        if (filerow >= E.numrows)
        {
            if (E.numrows == 0 && y == E.screenrows / 3)
            {
                char welcome[80];
                int welcome_note = snprintf(welcome, sizeof(welcome), "Delulu Editor - Version %s", delulu_VERSION);
                int welcomelen = strlen(welcome); // Get the length of the welcome message
                if (welcomelen > E.screencols)
                {
                    welcomelen = E.screencols; // Limit the length to the number of columns
                }
                int padding = (E.screencols - welcomelen) / 2; // Calculate padding for centering
                if (padding)
                {
                    ab_append(ab, "~", 1); // Draw the first row with a tilde
                    // 1 byte long ~ -> tilde char
                    padding--; // Decrease padding after adding tilde
                }
                while (padding--)
                {
                    ab_append(ab, " ", 1); // Add spaces for padding
                    // 1 byte long space -> space char
                }
                ab_append(ab, welcome, welcomelen); // Append the welcome message to the buffer
                // 11 bytes long delulu_VERSION -> version of the text editor
            }
            else
            {
                ab_append(ab, "~", 1); // Draw the first row with a tilde
                // 3 bytes long >\r\n -> ~ char,carriage return and newline
                // \r is carriage return (ASCII 13) and \n is newline (ASCII
            }
        }
        else
        {
            int len = E.row[filerow].rsize - E.coloff; // Get the length of the row
            if(len < 0)
            {
                len = 0; // If the length is negative, set it to 0
            }
            if (len > E.screencols)
            {
                len = E.screencols;
            }
            ab_append(ab,&E.row[filerow].render[E.coloff], len);
        }
        ab_append(ab, "\x1b[K", 3); // Clear the line
        // 3 bytes long \x1b[K -> escape sequence to clear the line
        // for the last line in text editor, we don't want to print a >
        // if (y < E.screenrows - 1)
        // {
        ab_append(ab, "\r\n", 2); // Move to the next line
            // 2 bytes long \r\n -> carriage return and newline
        //}
    }
}

void editor_draw_StatusBar(struct abuf *ab){
ab_append(ab,"\x1b[7m]",4);
char status[80],rstatus[80];
int len=snprintf(status,sizeof(status),"%.20s - %d lines %s",E.filename ? E.filename :"[No Name]",E.numrows,E.dirty ?"(modified)":"");
int rlen=snprintf(rstatus,sizeof(rstatus),"%d/%d",E.cy+1,E.numrows);
if(len>E.screencols) len = E.screencols;
ab_append(ab,status,len);
while(len<E.screencols){
    if(E.screencols-len == rlen){
        ab_append(ab,rstatus,rlen);
        break;
    }else{
        ab_append(ab," ",1);
        len++;
    }
}
ab_append(ab,"x1b[m",3);
ab_append(ab,"\r\n",2);
}

void editor_draw_MessageBar(struct abuf *ab){
    ab_append(ab,"\x1b[K",3);
    int msglen=strlen(E.statusmsg);
    if(msglen>E.screencols){
        msglen=E.screencols;
    }
    if(msglen&&time(NULL)-E.statusmsg_time<5){
        ab_append(ab,E.statusmsg,msglen);
    }
}

void editor_refressh_screen()
{
    editor_scroll(); // Scroll the editor if necessary
    struct abuf ab = ABUF_INIT;     // Initialize the append buffer
    ab_append(&ab, "\x1b[?25l", 6); // Hide the cursor
    // 6 bytes long \x1b[?25l -> escape sequence to hide the cursor
    // ab_append(&ab,"\x1b[2J", 4); // 4 means we write 4 byt out to terminal,1 byte \x1b ->esc char or 27 in decimal,3 bytes [2J
    // esc seq strt with esc char 27 followed by [ char
    // J cmd to clr the screen,arg 2 says clr entrire screen
    //<esc>[1] clr screen upto where cursor is
    //<esc>[0] clr screen from cursor to end of screen
    // 0 default arg for J,so <esc>[J clr screen from cursor to end of screen
    // We using VT100 esc seq
    //<esc>[2J cmd left cursor at bottom of screen,so we need to move it to top-left corner to draw editor from top to bottom
    ab_append(&ab, "\x1b[H", 3); // Move cursor to the home position (top-left corner)
    // 3 bytes long H ->position cursor ,takes 2 arg-row no and col no at which cursor should be placed
    // example :- <esc>[5;10H moves cursor to 5th row and 10th column
    // default is 1,1 which is top-left corner of screen
    // rows,col no starts from 1 not 0,so <esc>[H is same as <esc>[1;1H
    editor_draw_rows(&ab); // Draw the rows of the editor
    editor_draw_StatusBar(&ab);
    editor_draw_MessageBar(&ab);
    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",(E.cy-E.rowoff)+1,(E.rx - E.coloff)+1);
    // 32 bytes long buf -> buffer to hold the cursor position escape sequence
    // snprintf is used to format the string with cursor position
    // E.cy and E.cx are the current cursor position in the editor
    // E.cy is the row number and E.cx is the column number
    // We add 1 to both E.cy and E.cx because the escape sequence uses 1-based indexing
    // 1 byte \x1b -> escape char,2 bytes [d;dH -> row no and col no at which cursor should be placed
    ab_append(&ab,buf,strlen(buf)); // Append the cursor position escape sequence to the buffer
    // strlen(buf) returns the length of the formatted string in buf
    // 1 byte \x1b -> escape char,2 bytes [d;dH -> row no and col no at which cursor should be placed
    // write(STDOUT_FILENO, ab.b, ab.len); // Write the buffer to standard output
    // 1 byte \x1b -> escape char,2 bytes [d;dH -> row no and col no at which cursor should be placed
    // ab.b is the pointer to the buffer and ab.len is the length of the buffer
    ab_append(&ab,"\x1b[?25l",6);
    write(STDOUT_FILENO,ab.b,ab.len); // Move cursor back to the home position (top-left corner)
    ab_free(&ab);                       // Free the append buffer memory
}

void editor_setstatus_Message(const char*fmt,...){
    va_list ap;
    va_start(ap,fmt);
    vsnprintf(E.statusmsg,sizeof(E.statusmsg),fmt,ap);
    va_end(ap);
    E.statusmsg_time=time(NULL);
}
// input functions
char *editorPrompt(char *prompt){
    size_t bufsize=128;
    char *buf=malloc(bufsize);
    size_t buflen=0;
    buf[0]='\0';
    while(1){
        editor_setstatus_Message(prompt,buf);
        editor_refressh_screen();
        int c =key_read_editor();
        if(c==DEL_KEY||c==CTRL_KEY('h')||c==BACKSPACE){
            if(buflen!=0){
                buf[--buflen]='\0';
            }
        }
        else if(c=='\x1b'){
            editor_setstatus_Message("");
            free(buf);
            return NULL;
        }else if(c=='\r'){
            if(buflen!=0){
                editor_setstatus_Message("");
                return buf;
            }
        }else if(!iscntrl(c)&&c<128){
            if(buflen==bufsize-1){
                bufsize*=2;
                buf=realloc(buf,bufsize);
            }
            buf[buflen++]=c;
            buf[buflen]='\0';
        }
    }
}

void editor_move_cursor(int key)
{
    erow *row =(E.cy>=E.numrows) ? NULL :&E.row[E.cy];
    switch (key) // This function moves the cursor based on the key pressed
    {
    case ARROW_LEFT:
        if (E.cx != 0)
        {
            E.cx--; // Move cursor left
        }else if(E.cy>0){
            E.cy--;
            E.cx=E.row[E.cy].size;//Allowing user to press <- at begining of line to move to end of previous line
        }
        break;
    case ARROW_RIGHT:
        // if (E.cx < E.screencols - 1)
        // {
        //     // E.cx < E.screencols - 1 to prevent cursor from moving beyond
        //     E.cx++; // Move cursor right
        // }
        if(row && E.cx <row->size){
        E.cx++;}else if(row&&E.cx==row->size){
            E.cy++;
            E.cx=0; //Allowing user to to press -> at end of line 
        }
        break;
    case ARROW_UP:
        if (E.cy != 0)
        {
            // E.cy!=0 to prevent cursor from moving beyond the top of the screen
            E.cy--; // Move cursor up
        }
        break;
    case ARROW_DOWN:
        if (E.cy < E.numrows)
        {
            // E.cy < E.screenrows - 1 to prevent cursor from moving beyond the bottom
            E.cy++; // Move cursor down
        }
        break;
    }
    row=(E.cy>=E.numrows) ?NULL:&E.row[E.cy];
    int rowlen=row?row->size:0;
    if(E.cx>rowlen){
        E.cx=rowlen;
    }
}

void editor_process_keypress()
{
    static int quit_times=DELULU_QUIT_TIMES;
    int c = key_read_editor(); // Read a single character from standard input
    switch (c)                 // Process the character
    {
    case '\r':
        /*TODO*/
        editor_insertNEwline();
        break;
    case CTRL_KEY('q'):                     // If Ctrl+Q is pressed
        if(E.dirty && quit_times>0){
            editor_setstatus_Message("WARNING!!! File has unsaved changes."
            "Press Ctrl+q %d more times to quit.",quit_times);
            quit_times--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4); // Clear the screen
        write(STDOUT_FILENO, "\x1b[H", 3);  // Move cursor to the home position (top-left corner)
        exit(0);                            // Exit the program
        break;
    case CTRL_KEY('s'):
        editor_save();
        break;
    case HOME_KEY: // If Home key is pressed
        E.cx = 0;  // Move cursor to the beginning of the line
        break;
    case END_KEY:                // If End key is pressed
        if(E.cy<E.numrows){
            E.cx = E.row[E.cy].size; // Move cursor to the end
        }
        break;
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
        if(c==DEL_KEY){
            editor_move_cursor(ARROW_RIGHT);
        }
        editor_delchar();
        break;
    case PAGE_UP:
    case PAGE_DOWN:
    {
        if(c==PAGE_UP){
            E.cy=E.rowoff;
        }else if(c==PAGE_DOWN){
            E.cy=E.rowoff+E.screenrows-1;
            if(E.cy>E.numrows){
                E.cy=E.numrows;
            }
        }
        int times = E.screenrows; // Number of rows to move the cursor
        while (times--)
        {
            editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN); // Move the cursor up or down based on the key pressed
        }
    }
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editor_move_cursor(c); // Move the cursor based on the key pressed
        break;
    case CTRL_KEY('l'):
    case '\x1b':
        break;
    default:
        editor_insertchar(c);
        break;
    }
    quit_times=DELULU_QUIT_TIMES;
}

// init
void editor_init()
{
    // This function initializes the editor configuration
    E.cx = 0;      // Initialize cursor x position
    E.cy = 0;      // Initialize cursor y position
    E.rx=0;
    E.rowoff = 0;  // Initialize row offset for scrolling
    E.coloff = 0;  // Initialize column offset for scrolling
    E.numrows = 0; // Initialize num of rows
    E.row=NULL;  //Initialise rows to NULL
    E.dirty=0;
    E.filename=NULL;
    E.statusmsg[0]='\0';
    E.statusmsg_time=0;
    // Initialize the editor configuration
    if (get_window_size(&E.screenrows, &E.screencols) == -1)
    {
        die("get_window_size");
    }
    E.screenrows -=2;
}

int main(int argc, char *argv[])
{
    set_terminal_raw_mode(); // Set terminal to raw mode
    editor_init();           // Its job is to initialize the editor configuration, including getting the terminal size
    if (argc >= 2)
    {
        editor_open(argv[1]);
    }
    // Read characters from standard input until 'p' is pressed
    // char c;
    // while(read(STDIN_FILENO,&c,1)==1 && (c!='p')){ //read file until p is not triggered
    //     // printf("%s ",&c); to check the input
    //     if(iscntrl(c)){
    //         printf("%d\r\n", c); // Print control character as its ASCII value
    //     }else{
    //         printf("%d ('%c')\r\n",c, c); // Print regular character
    //     }
    // }
    editor_setstatus_Message("HELP: Ctrl+S=save | Ctrl+Q=quit");
    while (1)
    {
        // char c='\0';

        // // read(STDIN_FILENO, &c, 1); // Read a single character from standard input
        // if(read(STDIN_FILENO, &c, 1)==-1 && errno!=EAGAIN){ // Read a single character from standard input
        //     die("read"); // Handle read error
        // }

        // if(iscntrl(c)){ // Check if the character is a control character
        //     printf("%d\r\n", c); // Print control character as its ASCII value
        // } else {
        //     printf("%d ('%c')\r\n", c, c); // Print regular character with its ASCII value
        // }
        // // }if(c=='p'){ // If 'p' is pressed, exit the loop
        // //     break; // Exit the loop
        // // }
        // if(c == CTRL_KEY('q')) { // If Ctrl+Q is pressed, exit the loop
        //     break; // Exit the loop
        // }
        editor_refressh_screen();  // Refresh the screen
        editor_process_keypress(); // Process keypresses
    }
    return 0;
}

/* This program sets the terminal to raw mode, reads characters from standard input,
 and prints their ASCII values. It handles control characters differently by printing their ASCII values.
 tcsetattr(), read(),tcgetattr() are system calls used to control terminal behavior ;if return -1, it indicates an error occurred, and we handle it using the die() function which prints the error message and exits the program.
 CTRL_KEY macro bitwise-ANDs a char with val 00011111,in binary, which effectively converts a control key to its ASCII value. For example, CTRL_KEY('a') would yield 1 (0x01), CTRL_KEY('b') would yield 2 (0x02), and so on. 
 This allows us to easily check for control key combinations in the input handling loop.Ctrl key in terminal strips 5 & 6 bits from whatever key is pressed and send that
 key_read_editor job is to wait for 1 keypress and return it, later we expand func to handle escape seq,involving multiple bytes representing single keypresses like arrow keys, function keys, etc.
 The editor_process_keypress waits for keypress & then handles it,later it'll map various Ctrl combinations & other special keys to diff editor funct,& insert any alphanumeric characters and other printable keys char into text thats being edited
 editor_process_keypress belongs in {terminal} section bcoz it deals with low-level terminal ip,whereas editor_process_keypress is more about handling user input and processing key presses in the context of an editor.
 The separation allows for better organization of code,making it easier to maintain and extend functionality in the future.
 */

 //last step done is 130