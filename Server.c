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
#include "errno.h"
//--------------------------------Defines-------------------------------------------
#define OFFLINE     0
#define ESTABLISHED   1
#define DOWNLOADING   2
#define CHANGE_STATION  3
#define WAIT_APPROVAL   4
#define BUFFER_SIZE 256
#define MAX_CLIENTS 100

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
Client clients[MAX_CLIENTS]={0};                            // 100 control thread for max of 100 clients
int tcp_welcome_socket;                             //the tcp welcome socket which we will connect new clients with
int udp_server_socket;                              //the udp client socket, which we use to stream songs
uint16_t num_stations;
int num_clients=0;                                  //num of current clients connected to the radio
uint32_t multicastGroup;                            //holds the ip of the initial multicast group in 4 bytes
int server_tcp_port;
int server_udp_port;

//--------------------------Functions declarations--------------------------------------------------
void* stream_song(void* Station);
struct in_addr increaseip(struct in_addr initialmulticast,int increment);
void free_resources(Station* Stations,pthread_t* data_threads);
void* control_user(void* client_index);
int timeout_client(int client_index);
int send_invalid(int client_index,char* reply_string,uint8_t reply_string_len);
int send_welcome(int client_index);                                         // sends a welcome message to the client that just connected
int send_announce(int client_index,char* song_name,uint8_t song_name_len);  // sends the client the song name at the station he asked for
int send_permitsong(int client_index,uint8_t permission);                   // sends the client 1 or 0 if he can upload his song currently or not
int send_newstations();                                                     // sends to everyone a message about the new station in the radio,done after uplod
int main(int argc,char* argv[]){
    num_stations=argc-2;
    server_tcp_port=atoi(argv[1]);
    char* initial_multicastip=argv[2];
    multicastGroup=(uint32_t)inet_addr(initial_multicastip);               // changes the multicast group from a string to uint32 t
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
        free_resources(Stations,data_threads);
        return -1;
    }
    printf("Starting to listen.\n");
    int listenres=listen(tcp_welcome_socket,SOMAXCONN);
    if(listenres==-1)
    {
        perror("Failed to listen to welcome socket.\n");
        free_resources(Stations,data_threads);
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
            free_resources(Stations,data_threads);

        }
        printf("After select,activity=%d\n",activity);
        if(activity>0)                                                  //there was a change in either STDIN or welcomesocket
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
                for(int q=0;q<MAX_CLIENTS;q++)
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
                if(buff[0]=='q'||buff[0]=='Q')                                                              //server clicked Q
                {
                    free_resources(Stations,data_threads);
                    printf("Quitting the server.\n");
                    return 0;
                }

            }


        }
        FD_SET(STDIN_FILENO,&readfdset);                                                //insert STDIN into the FD set again
        FD_SET(tcp_welcome_socket,&readfdset);                                          //insert the welcome socket into the FD set again
    }
    return 0;
}
struct in_addr increaseip(struct in_addr initialmulticast,int increment)
{
    struct in_addr newaddr;
    newaddr.s_addr  = htonl(ntohl(initialmulticast.s_addr) + increment);
    return newaddr;

}
void free_resources(Station* Stations,pthread_t* data_threads)
{
    /*
    this function clears all the resources of the server, such as threads, sockets, and then exits the program.
     done when the server presses Q.
     */
    for(int i=0;i<MAX_CLIENTS;i++)                          //close control sockets and the control threads
    {
        pthread_cancel(clients[i].client_thread);
        close(clients[i].client_sock);
    }
    for(int i=0;i<num_stations;i++)                         //cancel the data threads
    {
        pthread_cancel(data_threads[i]);
    }
    free(Stations);
    free(data_threads);
    close(tcp_welcome_socket);
    close(udp_server_socket);

    exit(EXIT_SUCCESS);                                                 //added to deny infinite LOOP
}
void* stream_song(void* Station_Pointer)
{
    /*
     * this function streams a song at station i to all the clients that are listening to that station
     * it sends the station to the multicast address of the station by chunks
     */
    Station* ourstation;
    ourstation=(Station*)Station_Pointer;
    char* filepath=ourstation->Filepath;
    char* dest_multicastip=ourstation->Multicast_Ip;

}
int timeout_client(int client_index)                                //when we need to timeout a client, what to do
{
    clients[client_index].client_sock=-1;
    pthread_cancel(clients[client_index].client_thread);
    num_clients--;

}
int send_welcome(int client_index)
{
    // sends a welcome message to the client that just connected
    uint8_t* welcomebuffer=(uint8_t*)malloc(sizeof(uint8_t)*9);
    if(welcomebuffer==NULL)
    {
        perror("Failed to allocate memory for welcome message.\n");
    }
    welcomebuffer[0]=WELCOME_REPLY;
    welcomebuffer[1]=num_stations;
    welcomebuffer[3]=multicastGroup;
    welcomebuffer[7]=server_udp_port;

    int bytes_sent=send(clients[client_index].client_sock,welcomebuffer,sizeof(welcomebuffer),0);
    if (bytes_sent < 0)
    {
        // Some other error occurred
        perror("Failed to send on send_welcome.\n");

    }
    free(welcomebuffer);
    return bytes_sent;
    // should return the number of bytes sent if res is successful, -1 if error.

}
int send_announce(int client_index,char* song_name,uint8_t song_name_len)
{
    // sends the client the song name at the station he asked for
    uint8_t len=song_name_len;
    uint8_t* announcebuffer=(uint8_t*)malloc(sizeof(uint8_t)*(len+2)); // size of song name,another byte for announce reply and another byte for length
    if(announcebuffer==NULL)
    {
        perror("Failed to allocate memory for announce message.\n");
    }
    announcebuffer[0]=ANNOUNCE_REPLY;
    announcebuffer[1]=len;
    for(uint8_t i=2;i<2+len;i++)
    {
        announcebuffer[i]=*(song_name+i);
    }

    int bytes_sent=send(clients[client_index].client_sock,announcebuffer,sizeof(announcebuffer),0);
    if(bytes_sent==-1)
    {
        char* errormsg=(char*)malloc(sizeof(char)*(70+len+1));
        sprintf(errormsg,"Failed sending the announce message to client %d about song %s\n.",client_index,song_name);
        // perror("Failed sending the announce message to client %d about song %s\n.",client_index,song_name);
        perror(errormsg);
        free(errormsg);
    }
    free(announcebuffer);
    return bytes_sent;

}
int send_permitsong(int client_index,uint8_t permission)
{
    // sends the client 1 or 0 if he can upload his song currently or not
    uint8_t * permitbuffer=(uint8_t*)malloc(sizeof(uint8_t)*2);
    if(permitbuffer==NULL)
    {
        perror("Failed to allocate memory for permit message.\n");
    }
    permitbuffer[0]=PERMIT_REPLY;
    permitbuffer[1]=permission;
    int bytes_sent=send(clients[client_index].client_sock,permitbuffer,sizeof(permitbuffer),0);
    if (bytes_sent < 0)
    {

        // Some other error occurred
        perror("Failed to send on send_welcome.\n");

    }
    free(permitbuffer);
    return bytes_sent;
}
int send_invalid(int client_index,char* reply_string,uint8_t reply_string_len)
{
    // sends an error message of invalid command to client who typed something stupid
    uint8_t * invalidbuffer=(uint8_t*)malloc(sizeof(uint8_t)*(strlen(reply_string)+2));
    if(invalidbuffer==NULL)
    {
        perror("Failed to allocate memory for invalid message.\n");
    }
    invalidbuffer[0]=INVALID_REPLY;
    invalidbuffer[1]=(uint8_t)strlen(reply_string);
    for(uint8_t i=2;i<2+reply_string_len;i++)
    {
        invalidbuffer[i]=*(reply_string+i);
    }
    int bytes_sent=send(clients[client_index].client_sock,invalidbuffer,sizeof(invalidbuffer),0);
    if (bytes_sent < 0)
    {
        // Some other error occurred
        perror("Failed to send on send_invalid.\n");

    }
    free(invalidbuffer);
    return bytes_sent;

}
int send_newstations()
{
    // updates all the clients about the new number of stations available(done after new station was added)
    // sends to everyone a message about the new station in the radio,done after uplod
    uint8_t * newstationsbuffer=(uint8_t*)malloc(sizeof(uint8_t)*3);
    if(newstationsbuffer==NULL)
    {
        perror("Failed to allocate memory for newstations message.\n");
    }
    newstationsbuffer[0]=NEWSTATIONS_REPLY;
    newstationsbuffer[1]=num_stations;
    int failed_send=0;
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        if(clients[i].client_sock>0)                                                            //client has an open socket
        {
            int bytes_sent=send(clients[i].client_sock,newstationsbuffer,sizeof(newstationsbuffer),0);
            if (bytes_sent < 0)
            {
                // Some other error occurred
                perror("Failed to send on send_welcome.\n");
                failed_send=1;

            }
        }

    }

    free(newstationsbuffer);
    return failed_send;


}
void* control_user(void* client_index)
{
    // this function hanldes the control plane(the TCP sockets) with each of the server's clients.
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
                    send_welcome(index);


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


