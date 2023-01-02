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
#define ESTABLISHED   1
#define DOWNLOADING   2
#define CHANGE_STATION  3
#define WAIT_APPROVAL   4
#define BUFFER_SIZE 256

#define WELCOME_REPLY 0
#define ANNOUNCE_REPLY 1
#define PERMIT_REPLY 2
#define INVALID_REPLY 3
#define NEWSTATIONS_REPLY 4


typedef struct Station
{
    char* Filepath;                                     //path to the song on the pc
    char* Multicast_Ip;                                 //multicast ip for the
} Station;
typedef struct Client
{
    pthread_t client_thread;                                     //path to the song on the pc
    int client_sock;                                 //multicast ip for the
} Client;
//-------------------------Global Variables------------------------------------
Client clients[100]={0}; // 100 control thread for max of 100 clients
int tcp_welcome_socket;
int udp_client_socket;
int num_stations;
int num_clients=0;
char buffer[BUFFER_SIZE];
char* multicastip;
int server_tcp_port;
int server_udp_port;

//--------------------------Functions declarations--------------------------------------------------
void* stream_song(void* Station);
struct in_addr increaseip(struct in_addr initialmulticast,int increment);
void free_resources(Station* Stations,pthread_t* data_threads,Client* clients);
void* control_user(void* client_index);
int main(int argc,char* argv[]){
    num_stations=argc-2;
    server_tcp_port=atoi(argv[1]);
    char* initial_multicastip=argv[2];
    fd_set fdset;
    server_udp_port=atoi(argv[3]);

    Station* Stations=(Station*)malloc(sizeof(Station)*num_stations);
    pthread_t* data_threads=(pthread_t*)malloc(sizeof(pthread_t)*num_stations);
    for(int i=0;i<num_stations;i++)
    {
        Stations[i].Filepath=argv[i+4];
        struct in_addr newaddr;
        newaddr.s_addr=inet_addr(initial_multicastip);
        newaddr=increaseip(newaddr,i);
        Stations[i].Multicast_Ip=inet_ntoa(newaddr); // need to check how to make new address same address+1
    }

    struct sockaddr_in server;
    server.sin_family=AF_INET;
    server.sin_port=htons(server_tcp_port);
    server.sin_addr.s_addr=htonl(INADDR_ANY);
    memset(server.sin_zero,'\0',sizeof server.sin_zero);
    struct sockaddr_in client;
    fd_set readfdset;



    printf("Creating welcome socket.\n");
    tcp_welcome_socket = socket(AF_INET,SOCK_STREAM,0);
    if(tcp_welcome_socket<0)
    {
        perror("Failed creating socket\n");
    }
    FD_ZERO(&readfdset);
    FD_SET(tcp_welcome_socket,&readfdset);
    FD_SET(STDIN_FILENO,&readfdset);                //add the keyboard to the read fd set to check if server click Q
    int serversize=sizeof(server);
    printf("Attempting to bind.\n");
    int bindres=bind(tcp_welcome_socket,(struct sockaddr*)&server,(socklen_t)serversize);
    if(bindres==-1)
    {
        perror("Failed to bind welcome socket.\n");
        free_resources(Stations,data_threads,clients);
        return -1;
    }
    printf("Starting to listen.\n");
    int listenres=listen(tcp_welcome_socket,SOMAXCONN);
    if(listenres==-1)
    {
        perror("Failed to listen to welcome socket.\n");
        free_resources(Stations,data_threads,clients);
        return -1;
    }
    printf("Before While1\n");
    while(1)
    {
        printf("Before select in while1\n");
        int activity=select(FD_SETSIZE,&readfdset,NULL,NULL,NULL);
        if(activity==-1)
        {
            perror("Select failed.\n");
            free_resources(Stations,data_threads,clients);

        }
        printf("After select,activity=%d\n",activity);
        if(activity>0) //there was a change in one of the things
        {
            if(FD_ISSET(tcp_welcome_socket,&readfdset))
            {
                printf("There is a new connection.\n");
                int clientsize=sizeof(client);
                int newsocket=accept(tcp_welcome_socket,(struct sockaddr*)&client,(socklen_t*)&clientsize);
                if(newsocket<0)
                {
                    perror("Cant accept client to welcome socket.\n");
                    return -1;
                }
                printf("New client accepted.\n");
                int newclientidx=0;
                for(int q=0;q<100;q++)
                {
                    if(clients[q].client_sock==0)
                    {
                        newclientidx=q;
                        break;
                    }
                }
                num_clients++;
                pthread_create(&(clients[newclientidx].client_thread),NULL,control_user,&newclientidx);
                clients[newclientidx].client_sock=newsocket;

            }
            if(FD_ISSET(STDIN_FILENO,&readfdset)) //server pressed a KEY
            {
                char buff[1];
                fgets(buff,sizeof(buff),stdin);
                if(buff[0]=='q'||buff[0]=='Q')
                {
                    free_resources(Stations,data_threads,clients);                                       //server clicked Q
                    printf("Quitting the server.\n");
                    return 0;
                }

            }


        }
        FD_SET(STDIN_FILENO,&readfdset);
        FD_SET(tcp_welcome_socket,&readfdset);
    }
    return 0;
}
struct in_addr increaseip(struct in_addr initialmulticast,int increment)
{
    struct in_addr newaddr;
    newaddr.s_addr  = htonl(ntohl(initialmulticast.s_addr) + increment);
    return newaddr;

}
void free_resources(Station* Stations,pthread_t* data_threads,Client* clients)
{
    free(Stations);
    free(data_threads);
    close(tcp_welcome_socket);
    for(int i=0;i<100;i++)
    {
        close(clients[i].client_sock);
    }
    exit(EXIT_SUCCESS);                                                 //added to deny infinite LOOP
}
void* stream_song(void* Station_Pointer)
{
    Station* ourstation;
    ourstation=(Station*)Station_Pointer;
    char* filepath=ourstation->Filepath;
    char* dest_multicastip=ourstation->Multicast_Ip;

}
void* control_user(void* client_index)
{
    // this function controls everything about the control
    int index=*((int*)client_index);                  //the index of our client in the Clients array
    int state =OFFLINE;
    int freshconnection=1;
    int currstation;

    while(1)
    {
        switch(state)
        {
            case OFFLINE:
            {
                if(freshconnection==1)                                          // the connection is new-only first time
                {
                                                                                // we will send a welcome message
                    freshconnection=0;
                    uint8_t welcomebuffer[9];
                    welcomebuffer[0]=WELCOME_REPLY;
                    welcomebuffer[1]=num_stations;
                    welcomebuffer[3]=multicastip;
                    welcomebuffer[7]=server_udp_port;
                    send(clients[index].client_sock,welcomebuffer,sizeof(welcomebuffer),0);

                }
                else
                {
                    //freeresources;
                }
                break;
            }
            case CHANGE_STATION:
            {
                break;
            }
            case ESTABLISHED:
            {
                break;
            }
            case DOWNLOADING:
            {
                break;
            }

        }
    }

}


