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
char buffer[BUFFER_SIZE];
uint16_t numstations;
uint32_t multicastgroup;
uint16_t portnumber;
int currstation;
//--------------------------Functions declarations--------------------------------------------------
void Disconnect();
int Connect_to_server(const char* server_ip, int server_port);
int Wait_welcome();

int main(int argc,char* argv[]){
    //variables
    int server_port = atoi(argv[2]);
    char* server_ip = argv[1];
    pthread_t datathread;
    tcp_client_socket = socket(AF_INET,SOCK_STREAM,0);
    udp_client_socket=socket(AF_INET,SOCK_DGRAM,0);
    if(tcp_client_socket<0){
        perror("Failed creating socket\n");
    }
    switch (state) {
        case OFFLINE:{
            Connect_to_server(server_ip, server_port);//connect
            break;
        }
        case LISTENING:{

            break;
        }
        case WAIT_WELCOME:{
            Wait_welcome();
            break;
        }
        case WAIT_SONG:{
            break;
        }
        case WAIT_APPROVAL:{
            break;
        }
        case UPLOADING:{
            break;
        }
    }
    return 0;
}

int Connect_to_server(const char* server_ip, int server_port){
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
    int select_res=select(tcp_client_socket + 1, NULL, &fdset, NULL, &tv);
    if (select_res == 1)
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
                multicastgroup=buffer[3];
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
        //TIMEOUT OCCURED
    }
}
