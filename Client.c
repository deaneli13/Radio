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
uint8_t control_buffer[BUFFER_SIZE];
uint8_t data_buffer[BUFFER_SIZE];
uint16_t numstations;
struct in_addr initialmulticast;
uint32_t multicastip;
uint16_t udp_portnumber;                                            //the udp port which we connect to
int currstation;
int nextstation=0;                                               //nextstation is updated every time we call asksong, then we signal the data thread to change station
struct ip_mreq mreq;
struct in_addr a;
int change_flag = 0;                                            //flag that rises when we want to change song




//--------------------------Functions declarations--------------------------------------------------
void Quit_Program();                                             //quit the program entirely
int Connect_to_server(const char* server_ip, int server_port);
int Wait_welcome();
void* Listen_song(void* no_args);                               // this function listens to a song, the udp thread will run it
void* Change_station(void*);                                    //this function removes the client from his multicast group and joins another multicast group
int Leave_Station();                                            //leave the current station[thread func] as a signal,then rerun Change_station
int Connect_station();
struct in_addr increaseip(struct in_addr initialaddress,int increment);       //increase the ip of an ip address by

int main(int argc,char* argv[])
{
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
                printf("buffer in select:%s.\n");            //printf the buffer
                fgets(buff,sizeof(buff),stdin);
                if(buff[0]=='q'||buff[0]=='Q')                      //client pressed Q and wants to exit
                {
                    printf("Quitting the program.\n");
                    Quit_Program(EXIT_SUCCESS);
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
void* Listen_song(void* no_args)
{
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_port=htons(udp_portnumber);
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    bind(udp_client_socket,(struct sockaddr*)&addr,sizeof(addr));
    mreq.imr_interface.s_addr= htonl(INADDR_ANY);
    mreq.imr_multiaddr.s_addr= increaseip(initialmulticast,currstation).s_addr;
    setsockopt(udp_client_socket,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq));
    FILE* fp;
    fp= popen("play -t mp3 ->/dev ->/null 2 >&1","w");          //open a command that plays mp3
    while(1)
    {
        int addrlen=sizeof(addr);
        recvfrom(udp_client_socket,data_buffer,sizeof(data_buffer),0,(struct sockaddr*)&addr,(socklen_t *)addrlen);
        fwrite(data_buffer,sizeof(uint8_t),sizeof(data_buffer),fp);

    }
}
void Quit_Program(int reason)                    // reason is EXIT-FAILURE or EXIT-SUCCESS
{
    close(tcp_client_socket);
    pthread_cancel(datathread);
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
        perror("Failed to connect to server.\n");
    }
    else
    {
        //connection successful
        printf("Connected to server successfully->waiting for welcome message.\n");
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

            printf("We received 9 bytes of welcome message");
            if(welcomebuffer[0]!=WELCOME_REPLY)
            {
                printf("the reply is not of type WELCOME.\n");
                Quit_Program(EXIT_FAILURE);                         //quit the program

            }
            numstations=welcomebuffer[1];
            multicastip=welcomebuffer[3];
            initialmulticast.s_addr=multicastip;
            udp_portnumber=welcomebuffer[7];
            struct in_addr temp;
            temp.s_addr=multicastip;
            printf("There are %d stations, the ip of the first station is: %s and the udp port is:%d.\n",(int)numstations,inet_ntoa(temp),(int)udp_portnumber);
            state=LISTENING;
            currstation=0;
            pthread_create(&datathread,NULL,Listen_song,NULL);


        }
        else
        {
            perror("Failed to receive welcome message.\n");

        }

    }
    else if (select_res==0)
    {
        printf("Timeout on wait welcome.\n");
        Quit_Program(EXIT_FAILURE);
    }
    else if (select_res==-1)
    {
        printf("ERROR IN SELECT");
        Quit_Program(EXIT_FAILURE);
    }
}

struct in_addr increaseip(struct in_addr initialaddress,int increment)
{
    struct in_addr newaddr;
    newaddr.s_addr  = htonl(ntohl(initialaddress.s_addr) + increment);
    return newaddr;

}