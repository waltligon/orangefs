/* ucached_common.c */

#include "ucached.h"

int myread(int readfd, char *buffer)
{
    int count = read(readfd, buffer, BUFF_SIZE);
    return count;
}

void mywrite(int writefd, const char *src, char *buffer)
{
    strcpy(buffer, src);
    write(writefd, buffer, BUFF_SIZE);
    memset(buffer, 0, BUFF_SIZE);
}
