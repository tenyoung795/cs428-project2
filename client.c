#define _POSIX_C_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h> /* for fprintf */ 
#include <string.h> /* for memcpy */ 
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char* argv[])
    {
        if(argc<2){
            fprintf(stderr, "Usage:%s IP_Adress Filename\n", argv[0]);
            return EXIT_FAILURE;
            }
        FILE *fp;
        if((fp=fopen(argv[1],"rb"))>0)
        {   
            printf("reading%s\n",argv[1]);
            }
        fprintf(fp,"testing..\n");
        fclose(fp);
        return 0;
    }
int connect(char* ip)
    {
      /* udp socket creation */  
       int socket;
       if((socket=socket(AF_INET, SOCK_DGRAM, 0))<0{
           perror("fail to create socket");
           return -1;
       }

       return 0;
    }
       
