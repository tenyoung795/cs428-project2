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
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include <inttypes.h>
int chunk_no;
int is_last_frame;
uint64_t filesize;
char * filename;
char start_packet[CS428_START_PACKET_SIZE];
char content_packet[CS428_MAX_PACKET_SIZE];
int64_t last_ack;
fd_set set;
struct timeval timev;
char *create_packet(FILE *fp,uint64_t seq_no){
    if(seq_no==0)
    { 
        uint64_t onseq=cs428_hton64(seq_no);
      memcpy(start_packet,&onseq,sizeof(seq_no));
      chunk_no=0;
      start_packet[8]=CS428_METADATA;
      strcpy(&start_packet[8+1], filename);
      uint64_t fs=cs428_hton64(filesize);
      memcpy((void*)&start_packet[8+1+CS428_FILENAME_MAX],(void *)&fs,sizeof(fs));
      return start_packet;
    }
    else{
        uint64_t onseq=cs428_hton64(seq_no);
        memcpy(content_packet,&onseq,sizeof(onseq));
        content_packet[8]=CS428_CONTENT;
        if(fseek(fp, CS428_MAX_CONTENT_SIZE*(seq_no - 1),SEEK_SET)!=0){
            perror("fseek failed");
            return 0;
        }

        int chunk_read=fread(&content_packet[8+1],1, CS428_MAX_CONTENT_SIZE,fp);
        if (chunk_read==CS428_MAX_CONTENT_SIZE)
        {   
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
int cs428_connect(const char* ip, const char *port, FILE *fp)
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
        printf("ip%s,port:%s\n",ip,port);
        if((status=getaddrinfo(ip,port,&hints, &servaddr))!=0){
            fprintf(stderr, "getaddrinfo error:%s\n", gai_strerror(status));
                    exit(1);
            }
        if(connect(cs428_socket,servaddr->ai_addr,servaddr->ai_addrlen)<0)
        { 
            perror("connect failed");
            return 0;
        }
        time_t timeout_list[CS428_WINDOW_SIZE];
        memset(timeout_list,0,sizeof(timeout_list));
printf("timelist:%ld\n",timeout_list[0]);

        while(!is_last_frame)
        {
        char *packet;
        time_t earliest_deadline = LONG_MAX;
        for(int i = 0;i<CS428_WINDOW_SIZE;i++)
        {
            
            uint64_t frame = last_ack + 1 + i;
            printf("frame:%"PRIu64"\n",frame);
            size_t j = frame % CS428_WINDOW_SIZE;
            if(timeout_list[j]<=time(NULL))
            {
                            int packet_size;
             packet=create_packet(fp,frame);
         printf("frame:%"PRIu64"\n",frame);
         if(frame==0)
            packet_size=CS428_START_PACKET_SIZE;
         else if(frame==1+filesize/CS428_MAX_CONTENT_SIZE)
            packet_size=8+1+filesize%CS428_MAX_CONTENT_SIZE;
        else
            packet_size=CS428_MAX_PACKET_SIZE;

        if(sendto(cs428_socket,packet,packet_size,0,servaddr->ai_addr,servaddr->ai_addrlen)<0)
             {
            perror("sendto faild");
                       return 0;
            
            }
            timeout_list[j]= time(NULL) + 2;

            }
            if (timeout_list[j] < earliest_deadline) {
                earliest_deadline = timeout_list[j];
            }
            if(frame==1+filesize/CS428_MAX_CONTENT_SIZE)
                break;
            }

        time_t current_time = time(NULL);
        if (earliest_deadline > current_time) {
            FD_ZERO(&set);
            FD_SET(cs428_socket,&set);
            timev.tv_sec = earliest_deadline - current_time;
            timev.tv_usec = 0;
            if(select(cs428_socket+1,&set,NULL,NULL,&timev)>0)
            {
             char msg_recv[8];
           if(recv(cs428_socket,msg_recv,8,0)<0){
               perror("receive error");
               return 0;
           }
            
           uint64_t ack_seq=cs428_ntoh64(msg_recv);
           printf("ack_seq:%"PRIu64"\n",ack_seq);


           if((int64_t)ack_seq > last_ack){
            for (uint64_t frame = last_ack + 1; frame <= ack_seq; ++frame) {
                timeout_list[frame % CS428_WINDOW_SIZE]=0;
            }
            last_ack=ack_seq;
                                        }
                }
        }
        }
        char packet[9];
        uint64_t final_seq_no = cs428_hton64(last_ack + 1);
        memcpy(packet, &final_seq_no, 8);
        packet[8] = CS428_CONTENT;
        if(sendto(cs428_socket,packet,sizeof(packet),0,servaddr->ai_addr,servaddr->ai_addrlen)<0)
             {
            perror("sendto faild");
                       return 0;
            
            }
             return 0;
    }
 
        
int main(int argc, char* argv[])
    {
        last_ack=-1;
        if(argc<5){
            fprintf(stderr, "Usage:%s Hostname Port Filename Destination\n", argv[0]);
            return EXIT_FAILURE;
            }
        FILE *fp;
        chunk_no=0;
        const char *ipaddr=argv[1];
        const char *port=argv[2];
        struct stat st;
        stat(argv[3],&st);
        filesize=st.st_size;
        filename=argv[4];
        if((fp=fopen(argv[3],"rb"))!=0)
        {  
            printf("reading%s  filename:%s filesize:%"PRIu64"\n",argv[3],filename,filesize);
            }
    
       cs428_connect(ipaddr,port,fp);
        fclose(fp);
        return 0;
    }
      
