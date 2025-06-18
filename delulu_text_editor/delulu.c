//Includes
#include<unistd.h>
#include<stdio.h>
#include<termios.h> // For terminal control
#include<stdlib.h> // For exit function
#include<ctype.h> // For iscntrl function
#include<errno.h> // For error handling

//data
struct termios original_termios; // To store original terminal attributes

//terminal functions
void die(const char *s) {
    perror(s); // Print error message
    exit(1); // Exit with error code
}

void reset_terminal_mode() {
    // tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios); // Restore original terminal attributes
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios)==-1){
        die("tcsetattr"); // Restore original terminal attributes on exit
    } 
}

void set_terminal_raw_mode() {
    
    // tcgetattr(STDIN_FILENO, &original_termios); // Get current terminal attributes
    if(tcgetattr(STDIN_FILENO, &original_termios)==-1){
        die("tcgetattr"); // Get current terminal attributes
    }
    atexit(reset_terminal_mode); // Ensure original attributes are restored on exit

    struct termios term=original_termios; // Copy original attributes to term
    //DIFFERENT FLAGS
    // term.c_lflag &= ~(IXON); // Disable flow control
    // //CTRL+S and CTRL+Q are used for flow control, S->XOFF pause tx,Q->XON resume tx,ctrl+S=19 byte & ctrl+Q=17 byte
    // term.c_lflag &= ~(IEXTEN|ICANON | ECHO | ISIG); // Disable canonical mode and echo
    // //ISIG is used to disable signals like Ctrl+C,ctrl+Z: Now ctrl+c=3 byte & ctrl+z=26 byte
    // //IEXTEN is used to disable extended input characters,CTRL+V=22 byte & CTRL+O=15 byte
    // term.c_lflag&=~(ICRNL|IXON); // Disable input translation and flow control
    // //CR->carriage return,NL->newline,IXON->flow control,ctrl+M=13 byte & enter key=13 byte
    // term.c_lflag&=~(OPOST); // Disable output processing
    // //OPOST is used to disable output processing which means writing "\r\n" for newline
    //Misc Flags 
    term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // Disable input flags
    term.c_oflag &= ~(OPOST); // Disable output flags
    term.c_cflag |= (CS8); // Set character size to 8 bits
    term.c_lflag &= ~(IEXTEN | ICANON | ECHO | ISIG); // Disable canonical mode, echo, and signals
    //BRKINT when turned pn sent SIGINT signal to program like Ctrl+C
    //INPCK is used to disable parity checking
    //ISTRIP is used to disable stripping of high-order bit
    //CS8 is used to set character size to 8 bits, which is the default
    term.c_cc[VMIN] = 0; // Minimum number of characters to read
    term.c_cc[VTIME] = 1; // Timeout for reading characters (1 decisecond)
    //c_cc is an array of control characters, which are used to control the terminal behavior
    //VMIN is the minimum number of characters to read before returning from read()
    //VTIME is the timeout for reading characters, in deciseconds (0.1 seconds)

    // tcsetattr(STDIN_FILENO, TCSAFLUSH, &term); // Set the new attributes
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &term)==-1){
        die("tcsetattr"); // Set the new attributes
    }
}



//init
int main(){
    set_terminal_raw_mode(); // Set terminal to raw mode
    
    // Read characters from standard input until 'p' is pressed
    //char c;
    // while(read(STDIN_FILENO,&c,1)==1 && (c!='p')){ //read file until p is not triggered
    //     // printf("%s ",&c); to check the input
    //     if(iscntrl(c)){
    //         printf("%d\r\n", c); // Print control character as its ASCII value
    //     }else{
    //         printf("%d ('%c')\r\n",c, c); // Print regular character
    //     }
    // }

    while(1){
        char c='\0';

        // read(STDIN_FILENO, &c, 1); // Read a single character from standard input
        if(read(STDIN_FILENO, &c, 1)==-1 && errno!=EAGAIN){ // Read a single character from standard input
            die("read"); // Handle read error
        }

        if(iscntrl(c)){ // Check if the character is a control character
            printf("%d\r\n", c); // Print control character as its ASCII value
        } else {
            printf("%d ('%c')\r\n", c, c); // Print regular character with its ASCII value
        }if(c=='p'){ // If 'p' is pressed, exit the loop
            break; // Exit the loop
        }
    }
    return 0;
}

// This program sets the terminal to raw mode, reads characters from standard input,
// and prints their ASCII values. It handles control characters differently by printing their ASCII values.
/*
 *tcsetattr(), read(),tcgetattr() are system calls used to control terminal behavior ;if return -1, it indicates an error occurred, and we handle it using the die() function which prints the error message and exits the program.
 */