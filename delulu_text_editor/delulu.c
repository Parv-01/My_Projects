#include<unistd.h>
#include<stdio.h>
int main(){
    char c;
    while(read(STDIN_FILENO,&c,1)==1 && (c!='p'))
        printf("%s ",&c);
    return 0;
}