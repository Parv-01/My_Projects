// Includes
#include <ctype.h> // For iscntrl function
#include <errno.h> // For error handling
#include <stdio.h>
#include <stdlib.h>    // For exit function
#include <sys/ioctl.h> // For terminal control IOCTL->ip/op ctrl to get window size
#include <termios.h>   // For terminal control
#include <unistd.h>

// Defines
#define CTRL_KEY(k) ((k) & 0x1f) // Macro to convert a control key to its ASCII value

// data
//  struct termios original_termios; // To store original terminal attributes
struct editor_config
{
    int screenrows;                  // Number of rows in the terminal
    int screencols;                  // Number of columns in the terminal
    struct termios original_termios; // To store original terminal attributes
} E;                                 // Global variable to hold editor configuration

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

char key_read_editor()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    { // Read a single character from standard input
        if (nread == -1 && errno != EAGAIN)
        {                // If read error occurs, handle it
            die("read"); // Handle read error
        }
    };
}

// function to get cursor position mn cmd used to query terminal for status we wanted to give it arg 6 to aks for cursor position
// then we can read reply from std ip
int get_cursor_position(int *rows, int *cols)
{
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    {
        return -1;
    } // Write escape sequence to request cursor position
    printf("\r\n");
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1)
    {
        if (iscntrl(c))
        {
            printf("%d\r\n", c); // Print control character as its ASCII value
        }
        else
        {
            printf("%d ('%c')\r\n", c, c); // Print regular character with its ASCII value
        }
    }
    key_read_editor(); // Wait for a key press to ensure terminal is ready
    return -1;
}

int get_window_size(int *rows, int *cols)
{
    struct winsize ws;
    // if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    //     return -1; // If ioctl fails or terminal size is zero, return error //easy way to get terminal size
    //}
    ////if ioctl fails then what can be the strategy to cope witht the situation/hard way
    if (1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
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

// output functions
void editor_draw_rows()
{
    // This function would draw the rows of the editor
    int y;
    for (y = 0; y < E.screenrows; y++)
    {                                     // Assuming 24 rows for simplicity
        write(STDOUT_FILENO, ">\r\n", 3); // Draw the first row with a tilde
        // 3 bytes long >\r\n -> greater then char,carriage return and newline
        // \r is carriage return (ASCII 13) and \n is newline (ASCII
    }
}

void editor_refressh_screen()
{
    write(STDOUT_FILENO, "\x1b[2J", 4); // 4 means we write 4 byt out to terminal,1 byte \x1b ->esc char or 27 in decimal,3 bytes [2J
    // esc seq strt with esc char 27 followed by [ char
    // J cmd to clr the screen,arg 2 says clr entrire screen
    //<esc>[1] clr screen upto where cursor is
    //<esc>[0] clr screen from cursor to end of screen
    // 0 default arg for J,so <esc>[J clr screen from cursor to end of screen
    // We using VT100 esc seq
    //<esc>[2J cmd left cursor at bottom of screen,so we need to move it to top-left corner to draw editor from top to bottom
    write(STDOUT_FILENO, "\x1b[H", 3); // Move cursor to the home position (top-left corner)
    // 3 bytes long H ->position cursor ,takes 2 arg-row no and col no at which cursor should be placed
    // example :- <esc>[5;10H moves cursor to 5th row and 10th column
    // default is 1,1 which is top-left corner of screen
    // rows,col no starts from 1 not 0,so <esc>[H is same as <esc>[1;1H
    editor_draw_rows();                // Draw the rows of the editor
    write(STDOUT_FILENO, "\x1b[H", 3); // Move cursor back to the home position (top-left corner)
}

// input functions
void editor_process_keypress()
{
    char c = key_read_editor(); // Read a single character from standard input
    switch (c)                  // Process the character
    {
    case CTRL_KEY('q'):                     // If Ctrl+Q is pressed
        write(STDOUT_FILENO, "\x1b[2J", 4); // Clear the screen
        write(STDOUT_FILENO, "\x1b[H", 3);  // Move cursor to the home position (top-left corner)
        exit(0);                            // Exit the program
        break;
    }
}

// init

void editor_init()
{
    // Initialize the editor configuration
    if (get_window_size(&E.screenrows, &E.screencols) == -1)
    {
        die("get_window_size");
    }
}

int main()
{
    set_terminal_raw_mode(); // Set terminal to raw mode
    editor_init();           // Its job is to initialize the editor configuration, including getting the terminal size

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

// This program sets the terminal to raw mode, reads characters from standard input,
// and prints their ASCII values. It handles control characters differently by printing their ASCII values.
/*
 *tcsetattr(), read(),tcgetattr() are system calls used to control terminal behavior ;if return -1, it indicates an error occurred, and we handle it using the die() function which prints the error message and exits the program.
 */

// CTRL_KEY macro bitwise-ANDs a char with val 00011111,in binary, which effectively converts a control key to its ASCII value. For example, CTRL_KEY('a') would yield 1 (0x01), CTRL_KEY('b') would yield 2 (0x02), and so on. This allows us to easily check for control key combinations in the input handling loop.
// ctrl key in terminal strips 5 & 6 bits from whatever key is pressed and send that
//  key_read_editor job is to wait for 1 keypress and return it, later we expand func to handle escape seq,involving multiple bytes representing single keypresses like arrow keys, function keys, etc.

// The editor_process_keypress waits for keypress & then handles it,later it'll map various Ctrl combinations & other special keys to diff editor funct,& insert any alphanumeric characters and other printable keys char into text thats being edited
// editor_process_keypress belongs in /*terminal */ section bcoz it deals with low-level terminal ip,whereas editor_process_keypress is more about handling user input and processing key presses in the context of an editor. The separation allows for better organization of code, making it easier to maintain and extend functionality in the future.

// Last step done is step 31 -> not written notes for that so go again on curorser position and window size functions