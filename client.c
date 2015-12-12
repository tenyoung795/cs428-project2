#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h> /* for fprintf */ 
#include <string.h> /* for memcpy */ 
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include "common.h"
#include <sys/stat.h>
#include <stdbool.h>
uint64_t seq_no;
int chunk_no;
int is_last_frame;
uint64_t filesize;
char * filename;
char start_packet[CS428_START_PACKET_SIZE];
char content_packet[CS428_MAX_PACKET_SIZE];
char *create_packet(FILE *fp){
    if(seq_no==0)
    { 
        uint64_t onseq=cs428_hton64(seq_no);
      memcpy(start_packet,&onseq,sizeof(seq_no));
      chunk_no=0;
      start_packet[8]=CS428_METADATA;
      strcpy(&start_packet[8+1], filename);
      uint64_t fs=cs428_hton64(filesize);
      memcpy((void*)&start_packet[8+1+CS428_FILENAME_MAX],(void *)&fs,sizeof(fs));
      seq_no++;
      return start_packet;
    }
    else{
        uint64_t onseq=cs428_hton64(seq_no);
        memcpy(content_packet,&onseq,sizeof(onseq));
        content_packet[8]=CS428_CONTENT;
        if(fseek(fp, 512*chunk_no,SEEK_SET)!=0){
            perror("fseek failed");
            return 0;
        }
        seq_no++;

        int chunk_read=fread(&content_packet[8+1],1, CS428_MAX_CONTENT_SIZE,fp);
        if (chunk_read==CS428_MAX_CONTENT_SIZE)
        {   
            chunk_no++;
            return content_packet;
        }
        else if(chunk_read<CS428_MAX_CONTENT_SIZE)
        {
            is_last_frame=1;
            return content_packet;
        }
       } 
    return 0;
}
int cs428_connect(char* ip, FILE *fp)
    {
      /* udp socket creation */  
       int cs428_socket;
       if((cs428_socket=socket(AF_INET, SOCK_DGRAM, 0))<0){
           perror("fail to create socket");
           return 0;
       }
       printf("entering\n");
        struct addrinfo hints;
        struct addrinfo *servaddr;
        int status;
        memset(&hints,0,sizeof(hints));
        hints.ai_family=AF_INET;
        hints.ai_socktype=SOCK_DGRAM;
        char *port=malloc(sizeof(int));
        sprintf(port,"%d",CS428_SERVER_PORT);
        printf("ip%s,port:%s\n",ip,port);
        if((status=getaddrinfo(ip,port,&hints, &servaddr))!=0){
            fprintf(stderr, "getaddrinfo error:%s\n", gai_strerror(status));
                    exit(1);
            }
        char *test="testing";
        if(connect(cs428_socket,servaddr->ai_addr,servaddr->ai_addrlen)<0)
        { 
            perror("connect failed");
            return 0;
        }
        
        while(!is_last_frame)
        {
       char *packet;
        int packet_size;
               packet=create_packet(fp);
        printf("seq_no:%d\n",seq_no);
         if(seq_no==1)
            packet_size=CS428_START_PACKET_SIZE;
        else if(seq_no==1+filesize/CS428_MAX_CONTENT_SIZE+1)
            packet_size=8+1+filesize%CS428_MAX_CONTENT_SIZE;
        else
            packet_size=CS428_MAX_PACKET_SIZE;
       
        if(sendto(cs428_socket,packet,packet_size,0,servaddr->ai_addr,servaddr->ai_addrlen)<0)
        {
            perror("sendto faild");
            return 0;
            }
         char msg_recv[8];
       if(recv(cs428_socket,msg_recv,8,0)<0){
           perror("receive error");
           return 0;
       }
       int ack_seq=cs428_ntoh64(msg_recv);
       printf("ack_seq%d",ack_seq);


        }
             return 0;
    }
 
        
int main(int argc, char* argv[])
    {
        if(argc<3){
            fprintf(stderr, "Usage:%s IP_Adress Filename\n", argv[0]);
            return EXIT_FAILURE;
            }
        FILE *fp;
        seq_no=0;
        char *ipaddr=argv[2];
        struct stat st;
        stat(argv[1],&st);
        filesize=st.st_size;
        filename=strdup(argv[1]);
        if((fp=fopen(argv[1],"rb"))>0)
        {  
            basename(filename);
            printf("reading%s  filename:%s filesize:%d\n",argv[1],filename,filesize);
            }
    
       cs428_connect(ipaddr,fp);
        fclose(fp);
        return 0;
    }
      
