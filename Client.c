#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
//-----------------------------Includes--------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include<arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/time.h>


//--------------------------------Defines-------------------------------------------
#define OFFLINE     0
#define LISTENING   1
#define WAIT_WELCOME   2
#define WAIT_SONG   3
#define WAIT_APPROVAL   4
#define UPLOADING   5
#define BUFFER_SIZE 256



#define WELCOME_REPLY 0
#define ANNOUNCE_REPLY 1
#define PERMIT_REPLY 2
#define INVALID_REPLY 3
#define NEWSTATIONS_REPLY 4
//-------------------------Global Variables------------------------------------
int state = OFFLINE;
fd_set fdset;
int tcp_client_socket;
int udp_client_socket;
pthread_t datathread;
char buffer[BUFFER_SIZE];
uint16_t numstations;
uint32_t multicastip;
uint16_t portnumber;
int currstation;
//--------------------------Functions declarations--------------------------------------------------
void quit();
int Connect_to_server(const char* server_ip, int server_port);
int Wait_welcome();

int main(int argc,char* argv[]){
    //variables
    int server_port = atoi(argv[2]);
    char* server_ip = argv[1];
    tcp_client_socket = socket(AF_INET,SOCK_STREAM,0);
    udp_client_socket=socket(AF_INET,SOCK_DGRAM,0);
    if(tcp_client_socket<0)
    {
        perror("Failed creating socket\n");
    }
    while(1)
    {
        int select_res=select(FD_SETSIZE,&fdset,NULL,NULL,NULL);
        if(select_res>0)
        {
            if(FD_ISSET(STDIN_FILENO,&fdset)) //server pressed a KEY
            {
                char buff[BUFFER_SIZE];
                fgets(buff,sizeof(buff),stdin);
                printf("buffer in select: %s .\n",buff);            //printf the buffer
                if(buff[0]=='q'||buff[0]=='Q')                      //client pressed Q and wants to exit
                {
                    printf("Quitting the program.\n");
                    quit();
                }
                else
                {

                }
                FD_SET(STDIN_FILENO,&fdset);                        //add the STDIN back into FDSET so we can read it again next iter


            }
        }


        switch (state)
        {
            case OFFLINE:
            {
                Connect_to_server(server_ip, server_port);//connect
                state = WAIT_WELCOME;
                break;
            }
            case LISTENING:
            {

                break;
            }
            case WAIT_WELCOME:
            {
                Wait_welcome();
                break;
            }
            case WAIT_SONG:
            {
                break;
            }
            case WAIT_APPROVAL:
            {
                break;
            }
            case UPLOADING:
            {
                break;
            }
        }
    }

}
void quit(int reason)                    // reason is EXIT-FAILURE or EXIT SUCCESS
{
    close(tcp_client_socket);
    pthread_cancel(datathread);
    struct ip_mreq mreq;
    mreq.imr_interface.s_addr= htonl(INADDR_ANY);
    mreq.imr_multiaddr.s_addr=multicastip;
    setsockopt(udp_client_socket,IPPROTO_IP,IP_DROP_MEMBERSHIP,&mreq,sizeof(mreq));         // leave the multicast group
    close(udp_client_socket);
    exit(reason);


}

int Connect_to_server(const char* server_ip, int server_port)
{
    //creating struct
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    struct timeval tv;
//    tv.tv_sec = 0;
//    tv.tv_usec = 0;
    memset(server_addr.sin_zero,'\0', sizeof(server_addr.sin_zero));
    int connection = connect(tcp_client_socket,(struct sockaddr*) &server_addr, sizeof(server_addr));
    if(connection<0)
    {
        perror("Failed connect to server\n");
    }
    else
    {
        //connection successful
        printf("Connected to server successfully->waiting for welcome message");
        state=WAIT_WELCOME;
    }


}

int Wait_welcome()
{
    struct timeval tv;
    FD_ZERO(&fdset);
    FD_SET(tcp_client_socket,&fdset);
    tv.tv_sec=0;
    tv.tv_usec=300*1000; // 300 MS timeout for select
    int select_res=select(tcp_client_socket + 1, &fdset, NULL, NULL, &tv);
    if (select_res > 0)
    {
        uint8_t welcomebuffer[9];
        //success- we received a welcome message
        int recvres=recv(tcp_client_socket,welcomebuffer,9,0);
        if(recvres!= -1)
        {
            // success
            if(welcomebuffer[0]==0 &&welcomebuffer[1]==0 && welcomebuffer[2]==0)
            {
                printf("We received 9 bytes of 0s=welcome message");
                if(welcomebuffer[0]!=WELCOME_REPLY)
                {
                    printf("the reply is not of type WELCOME");
                    //ERROR

                }
                numstations=buffer[1];
                multicastip=buffer[3];
                portnumber=buffer[7];
                state=LISTENING;
                currstation=0;
            }

        }
        else
        {
            perror("Failed to receive welcome message.\n");

        }

    }
    else if (select_res==0)
    {
        printf("Timeout on wait welcome.\n");
        quit(EXIT_FAILURE);
    }
    else if (select_res==-1)
    {
        printf("ERROR IN SELECT");
        quit(EXIT_FAILURE);
    }

}