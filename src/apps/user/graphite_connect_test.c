#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>


int main(){

  char addr[] = "130.127.148.159\0";
  char basemessage[] = "system.graphite_test 1000";
  char sendmessage[100];
  char timemessage[30];
  time_t timer;
  int fd;
  int ret;
  char c;
  printf("starting\n");
  while(1){
    fd = graphite_connect(addr);
    time(&timer);
    sprintf(sendmessage, "%s %u\n", basemessage, timer);
    printf("%s\n", sendmessage);
    ret = write(fd, sendmessage, strlen(sendmessage)+1);
    printf("ret: %d\n",ret);
    close(fd);
    sleep(20);
  }
  return 0;
}

int graphite_connect(char *graphite_addr){
    int sockfd,  n;
		int portno = 2003;
		struct sockaddr_in serv_addr;
		struct hostent *server;
                struct in_addr ipv4addr;
	  sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if(sockfd < 0){
                        printf("Failed to open sock\n");
			return -1;
		}
                inet_pton(AF_INET, graphite_addr, &ipv4addr);
    server = gethostbyname(graphite_addr);
		//server = gethostbyaddr(&ipv4addr, sizeof(ipv4addr), AF_INET);
    if(!server){
                        printf("Failed to resolve host\n");
			return -2;
		}
                printf("h_name: %s\nh_addr: %s\n", server->h_name,server->h_addr);
		serv_addr.sin_family = AF_INET;
		bcopy(server->h_addr, (char *)&serv_addr.sin_addr.s_addr,
				server->h_length);
		serv_addr.sin_port = htons(portno);
		if(connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(struct sockaddr)) < 0){
                        printf("failed to connec to socket\n");
			return -3;
                }

    return sockfd;
}
