#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <assert.h>
#include <errno.h>

#define filename "/tmp/test_odirect.786"
#define mem_pagesize 512

#define buff_size (1024*1024*2)

int main(){
  int fd;
  int ret;
  unlink(filename);
  void * buff =  malloc(buff_size+mem_pagesize);
  char * buffReal =  (unsigned char *)(((unsigned long)buff + 
      mem_pagesize - 1) & (~(mem_pagesize - 1)));
      
  memset (buff, 'A', buff_size+mem_pagesize);
  
  fd = open(filename, O_RDWR | O_CREAT | O_DIRECT,  S_IRUSR | S_IWUSR);
  
  ret = pwrite(fd, buffReal, 512, 1024);
  printf("WRITE %d\n", ret);
  ftruncate(fd, 1220);
  ret = pwrite(fd, buffReal, 512, 1024);
  ret = pread(fd, buffReal, 2048, 0 );
  /*if ( ret != 1536){
    printf("ARGH %d\n", ret);
}*/
  printf("READ %d\n", ret);
  close(fd);
  
  free(buff);
  return 0;
}
