#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
//-----------------------------Includes--------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include<arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/select.h>
#include <sys/time.h>
#include "errno.h"
#include "libgen.h"
//--------------------------------Defines-------------------------------------------
#define OFFLINE     0
#define ESTABLISHED   1
#define DOWNLOADING   2
#define CHANGE_STATION  3
#define WAIT_APPROVAL   4
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 100
#define EMPTY_SOCKET -5

#define WELCOME_REPLY 0
#define ANNOUNCE_REPLY 1
#define PERMIT_REPLY 2
#define INVALID_REPLY 3
#define NEWSTATIONS_REPLY 4
#define HELLO_TYPE 0
#define ASK_SONG_TYPE 1
#define UPSONG_TYPE 2

#define MAX_FILE_SIZE 10*1048576
#define MIN_FILE_SIZE 2000

typedef struct Station
{
    FILE* fp;
    char* filename;                                     //path to the song on the pc
    uint32_t multicastip;                                 //multicast ip for the
} Station;
typedef struct Client
{
    pthread_t client_thread;                                     //path to the song on the pc
    int client_sock;                                 //multicast ip for the
    fd_set clientfdset;                                     //fd set for each socket to listen
} Client;
//-------------------------Global Variables------------------------------------
Client clients[MAX_CLIENTS];                    // 100 control thread for max of 100 clients
Station* Stations;
uint8_t data_buffer[BUFFER_SIZE];
int tcp_welcome_socket;                             //the tcp welcome socket which we will connect new clients with
int udp_server_socket;                              //the udp client socket, which we use to stream songs
uint16_t num_stations;
int num_clients=0;                                  //num of current clients connected to the radio
uint32_t multicastGroup;                            //holds the ip of the initial multicast group in 4 bytes
int tcp_server_port;
int udp_server_port;
pthread_t data_thread;
uint8_t permitsong=1;
fd_set fdset;
pthread_mutex_t numclients_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t permitsong_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stationsmutex;
pthread_mutexattr_t  attr;
uint32_t expected_songsize;
uint8_t songname[256];
//--------------------------Functions declarations--------------------------------------------------
void* stream_song(void* Stations);
struct in_addr increaseip(struct in_addr initialmulticast,int increment);
void Quit_program(Station* Station);
void* control_user(void* client_index);
int timeout_client(int client_index,const char* replystr);
int Stdin_handler();
int send_invalid(int client_index,const char* reply_string,uint8_t reply_string_len);
int send_permit(int client_index);                                             //send permission 1/0 to client
int Welcome_handler(int client_index);                                         // sends a welcome message to the client that just connected
int Announce_handler(int client_index);  // sends the client the song name at the station he asked for
int send_newstations();                                                     // sends to everyone a message about the new station in the radio,done after upload
int Established_handler(int index);                                      // waits for any input while client is listening



uint32_t pack_uint8_t_to_uint32_t(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)c << 8 | (uint32_t)d;
}

void unpack_uint32_t_to_uint8_t(uint32_t in, uint8_t out[4])
{
    out[0] = (in >> 24) & 0xff;
    out[1] = (in >> 16) & 0xff;
    out[2] = (in >> 8) & 0xff;
    out[3] = in & 0xff;
}

uint16_t pack_uint8_t_to_uint16_t(uint8_t a, uint8_t b)
{
    return  (uint16_t)a << 8 | (uint16_t)b;
}

void unpack_uint16_t_to_uint8_t(uint16_t in, uint8_t out[2])
{
    out[0] = (in >> 8) & 0xff;
    out[1] = in & 0xff;
}
int main(int argc,char* argv[])
{
    for(int i=0;i<MAX_CLIENTS;i++)
    {
        clients[i].client_sock=EMPTY_SOCKET;

    }
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&stationsmutex,&attr);
    num_stations=argc-4;
    tcp_server_port=atoi(argv[1]);
    char* initial_multicastip=argv[2];
    multicastGroup=(uint32_t)inet_addr(initial_multicastip);               // changes the multicast group from a string to uint32
    udp_server_port=atoi(argv[3]);
    Stations=(Station*)malloc(sizeof(Station)*num_stations);
    for(int i=0;i<num_stations;i++)
    {
        Stations[i].fp=fopen(argv[i+4],"r");
        if(Stations[i].fp==NULL)            // checks if mp3 file opened successfully
        {
            printf("Cannot open file.\n");
        }
        int templen = strlen(argv[i+4]);
        Stations[i].filename=(char*)malloc(sizeof(char)*templen);
        strcpy(Stations[i].filename,argv[i+4]);
        struct in_addr newaddr;
        newaddr.s_addr=inet_addr(initial_multicastip);
        newaddr=increaseip(newaddr,i);
        Stations[i].multicastip=newaddr.s_addr;
    }

    struct sockaddr_in server;
    server.sin_family=AF_INET;
    server.sin_port=htons(tcp_server_port);
    server.sin_addr.s_addr=htonl(INADDR_ANY);
    memset(server.sin_zero,'\0',sizeof server.sin_zero);
    struct sockaddr_in client;
    tcp_welcome_socket = socket(AF_INET,SOCK_STREAM,0);
    udp_server_socket=socket(AF_INET,SOCK_DGRAM,0);
    pthread_create(&data_thread,NULL,stream_song,Stations);
    if(tcp_welcome_socket==-1)
    {
        perror("Failed to open tcp server socket.\n");
    }
    if(udp_server_socket==-1)
    {
        perror("Failed to open udp server socket.\n");
    }
    FD_ZERO(&fdset);
    FD_SET(tcp_welcome_socket,&fdset);
    FD_SET(STDIN_FILENO,&fdset);                //add the keyboard to the read fd set to check if server click Q
    int serversize=sizeof(server);
    int bindres=bind(tcp_welcome_socket,(struct sockaddr*)&server,(socklen_t)serversize);
    if(bindres==-1)
    {
        perror("Failed to bind welcome socket.\n");
        Quit_program(Stations);
        return -1;
    }
    int listenres=listen(tcp_welcome_socket,SOMAXCONN);
    if(listenres==-1)
    {
        perror("Failed to listen to welcome socket.\n");
        Quit_program(Stations);
        return -1;
    }

    while(1)
    {
        printf("This is the Radio sever!\nPress 'p' or 'P' to view the database\nPress 'q' or 'Q' to close the server\n");
        int activity=select(FD_SETSIZE,&fdset,NULL,NULL,NULL);
        if(activity==-1)
        {
            perror("Select failed.\n");
            Quit_program(Stations);

        }
        if(activity>0)                                                  //there was a change in either STDIN or welcome socket
        {
            if(FD_ISSET(tcp_welcome_socket,&fdset))                 //the change is in the welcome socket
            {
                printf("There is a new connection.\n");
                int clientsize=sizeof(client);
                int newsocket=accept(tcp_welcome_socket,(struct sockaddr*)&client,(socklen_t*)&clientsize);
                if(newsocket<0)
                {
                    perror("Cant accept client to welcome socket.\n");
                    return -1;
                }
                int newclientidx=0;
                for(int q=0;q<MAX_CLIENTS;q++)
                {
                    if(clients[q].client_sock==EMPTY_SOCKET)
                    {
                        newclientidx=q;
                        break;
                    }
                }
                pthread_mutex_lock(&numclients_mutex);
                num_clients++;
                pthread_mutex_unlock(&numclients_mutex);
                pthread_create(&(clients[newclientidx].client_thread),NULL,control_user,&newclientidx);
                clients[newclientidx].client_sock=newsocket;
            }
            if(FD_ISSET(STDIN_FILENO,&fdset)) //server pressed a KEY
            {
                Stdin_handler(Stations);
            }


        }
        if(num_clients<MAX_CLIENTS)
        {
            FD_SET(tcp_welcome_socket,&fdset);                                      //insert the welcome socket into the FD set again,only if we have room
        }
        FD_SET(STDIN_FILENO,&fdset);                                                //insert STDIN into the FD set again

    }
    return 0;
}
struct in_addr increaseip(struct in_addr initialmulticast,int increment)
{
    struct in_addr newaddr;
    newaddr.s_addr  = htonl(ntohl(initialmulticast.s_addr) + increment);
    return newaddr;

}
void Quit_program(Station* Stations)
{
    /*
    this function clears all the resources of the server, such as threads, sockets, and then exits the program.
     done when the server presses Q.
     */
    for(int i=0;i<MAX_CLIENTS;i++)                          //close control sockets and the control threads
    {
        if(clients[i].client_sock!=EMPTY_SOCKET)
        {
            pthread_cancel(clients[i].client_thread);
            close(clients[i].client_sock);
        }
    }
    for(int i=0;i<num_stations;i++)
    {
        free(Stations[i].filename);
    }
    free(Stations);
    close(tcp_welcome_socket);
    close(udp_server_socket);
    pthread_cancel(data_thread);
    exit(EXIT_SUCCESS);                                                 //added to deny infinite LOOP
}
void* stream_song(void* Station_Pointer)
{
    int ttl=64;
    socklen_t len = sizeof(ttl);
    setsockopt(udp_server_socket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, len);
    Station* stations=(Station*)Station_Pointer;
    /*
     * stream songs to all stations, one chunk at a time
     */
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_port=htons(udp_server_port);
    uint8_t multicastbuffer[BUFFER_SIZE];
    while(1)
    {

        for(int i=0;i<num_stations;i++)
        {
            addr.sin_addr.s_addr=stations[i].multicastip;
            printf("the ip is :%s\n", inet_ntoa(addr.sin_addr));
            int a=fread(multicastbuffer,1,BUFFER_SIZE,Stations[i].fp);
            if(a==0)
                rewind(Stations[i].fp);
            sendto(udp_server_socket,multicastbuffer,a,0,(struct sockaddr*)&addr,sizeof(addr));
            struct timespec tim, tim2;
            tim.tv_sec = 0;
            tim.tv_nsec = (62500*1000)/num_stations;///num_stations
            nanosleep(&tim,&tim2);
        }
    }


}
int timeout_client(int client_index,const char* replystr)                                //when we need to timeout a client, what to do
{
    send_invalid(client_index,replystr,strlen(replystr));
    close(clients[client_index].client_sock);
    clients[client_index].client_sock=EMPTY_SOCKET;
    clients[client_index].client_thread=EMPTY_SOCKET;
    pthread_mutex_lock(&numclients_mutex);
    num_clients--;
    pthread_mutex_unlock(&numclients_mutex);
    pthread_exit(NULL);
}
int Stdin_handler(Station* stationsarr)                                     //assume the change was in STDIN andw e need to either change station or quit
{
    char buff[100]={0};
    fgets(buff,99,stdin);
    if (strlen(buff)>0)
        buff[strlen(buff)-1]='\0';
    fseek(stdin,0,SEEK_END);
    fflush(stdin);
    int sockfd; // file descriptor of the socket
    struct sockaddr_in addr; // to hold address information
    socklen_t addrlen = sizeof(addr);
    if((buff[0]=='q'||buff[0]=='Q') &&strlen(buff)==1)                      //server pressed Q and wants to exit
    {
        printf("Quitting the program.\n");
        Quit_program(stationsarr);
    }
    else if((buff[0]=='p'||buff[0]=='P') &&strlen(buff)==1)                      //server pressed P and wants to print all the clients
    {
        printf("The clients connected to the radio are:\n");
        for(int i=0;i<MAX_CLIENTS;i++)
        {
            if(clients[i].client_sock!=EMPTY_SOCKET)
            {
                int ret = getpeername(clients[i].client_sock, (struct sockaddr *) &addr, &addrlen);
                if (ret == 0)
                {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(addr.sin_addr), ip, INET_ADDRSTRLEN);
                    printf("The IP address of the client is: %s\n", ip);
                }

            }
        }
        printf("There are %d stations.\n",num_stations);
        for(int i=0;i<num_stations;i++)
        {
            printf("Station %d plays: %s\n",i,stationsarr[i].filename);
        }
    }
    else        //client pressed neither P nor Q
    {
        printf("Wrong input. Please Enter 'p' or 'q'\n");
    }
    FD_CLR(STDIN_FILENO,&fdset);
}
int Welcome_handler(int client_index)
{
    //waits for an Hello message
    FD_ZERO(&clients[client_index].clientfdset);
    FD_SET(clients[client_index].client_sock,&clients[client_index].clientfdset);
    struct timeval tv;
    tv.tv_sec=0;
    tv.tv_usec=300*1000; // 300 MS timeout for select
    int selectres=select(FD_SETSIZE,&clients[client_index].clientfdset,NULL,NULL,&tv);
    if(selectres<0)
    {
        perror("Failed to select on Listen Control.\n");
    }
    else                                                    //there is a change in one of the fds(STDIN or TCP)
    {
        int count=0;
        uint8_t hellobuffer[3];
        recv(clients[client_index].client_sock,hellobuffer,3,MSG_DONTWAIT);
        switch(hellobuffer[0])
        {
            case HELLO_TYPE:
            {
                if(hellobuffer[1]!=0 || hellobuffer[2]!=0)
                {
                    timeout_client(client_index, "Corrupted Hello message");
                    return OFFLINE;
                }
                break;
            }
            default:
            {
                timeout_client(client_index, "Not Hello message");
                return OFFLINE;
            }
        }
        if(recv(clients[client_index].client_sock,hellobuffer,1,MSG_DONTWAIT)>0)
        {
            timeout_client(client_index, "Too long or duplicate Hello message");
            return ESTABLISHED;
        }
    }
    // sends a welcome message to the client that just connected
    uint8_t welcomebuffer[9];
    welcomebuffer[0]=WELCOME_REPLY;
    uint8_t temp1[2];
    unpack_uint16_t_to_uint8_t(num_stations,temp1);
    welcomebuffer[1] = temp1[0];
    welcomebuffer[2] = temp1[1];
    uint8_t temp2[4];
    unpack_uint32_t_to_uint8_t(multicastGroup,temp2);
    welcomebuffer[3] = temp2[0];
    welcomebuffer[4] = temp2[1];
    welcomebuffer[5] = temp2[2];
    welcomebuffer[6] = temp2[3];
    unpack_uint16_t_to_uint8_t(udp_server_port,temp1);
    welcomebuffer[7] = temp1[0];
    welcomebuffer[8] = temp1[1];
    int bytes_sent=send(clients[client_index].client_sock,welcomebuffer,9*sizeof(uint8_t),0);
    if (bytes_sent < 0)
    {
        // Some other error occurred
        perror("Failed to send on send_welcome.\n");

    }
    return ESTABLISHED;
}
int Announce_handler(int client_index)
{
    uint8_t announce_buffer[2];
    recv(clients[client_index].client_sock,announce_buffer,2,0);
    uint16_t newclientstation= pack_uint8_t_to_uint16_t(announce_buffer[0],announce_buffer[1]);
    // sends the client the song name at the station he asked for
    if(newclientstation>=num_stations||newclientstation<0)
    {
        timeout_client(client_index,"The station does not exist.\n");
        return OFFLINE;
    }
    uint8_t len=strlen(Stations[newclientstation].filename);
    uint8_t announcebuffer[BUFFER_SIZE];// size of song name,another byte for announce reply and another byte for length
    announcebuffer[0]=ANNOUNCE_REPLY;
    announcebuffer[1]=len;
    strcpy(announcebuffer+2,Stations[newclientstation].filename);
    int bytes_sent=send(clients[client_index].client_sock,announcebuffer,len+2,0);
    if(bytes_sent==-1)
    {
        perror("Announce handler;");
    }
    return ESTABLISHED;

}
int Established_handler(int index)
{
    int count=0;
    FD_SET(clients[index].client_sock,&clients[index].clientfdset);
    uint8_t message_type;
    int selectres=select(FD_SETSIZE,&clients[index].clientfdset,NULL,NULL,NULL);
    if(selectres<0)
    {
        perror("Select function failed.\n");
    }
    else                                                    //there is a change in one of the fds(STDIN or TCP)
    {
        uint8_t message_type;
        int count=0;
        while((count=recv(clients[index].client_sock,&message_type,1,MSG_DONTWAIT))>=0)
        {
            if(count>0)
            {
                switch(message_type)
                {
                    case ASK_SONG_TYPE:
                    {
                        return CHANGE_STATION;
                        break;

                    }
                    case UPSONG_TYPE:
                    {
                        return send_permit(index);;
                        break;

                    }

                    default:
                    {
                        perror("Incompatible message received at Established handler,terminating.\n");
                        timeout_client(index,"invalid message type\n");
                    }
                }
            }
            else     //count =0, meaning we received nothing and the connection dropped
            {
                printf("A user disconnected from the radio\n");
                timeout_client(index,"");

            }

        }
        if(count<0)
        {
            perror("error on receive\n");
            timeout_client(index,"");
        }
    }

}

int Downloading_handler(int index)
{
    printf("Downloading a new song called: %s\n",(char *)songname);
    printf("The song size is: %d\n",(uint32_t)expected_songsize);
    struct timeval tv;
    tv.tv_sec=3;
    tv.tv_usec=0;
    uint32_t totalbytesreceived=0;
    uint8_t buffer[BUFFER_SIZE];
    FILE* file=fopen("tempfile","w");
    FD_SET(clients[index].client_sock,&clients[index].clientfdset);
    if(file==NULL)
    {
        perror("Failed to create file in downloading handler\n");
    }

    while(totalbytesreceived<expected_songsize)
    {
        //FD_ZERO(&clients[index].clientfdset);
        //FD_SET(clients[index].client_sock,&clients[index].clientfdset);
        int selectres=select(clients[index].client_sock+1,&clients[index].clientfdset,NULL,NULL,NULL);
        if(selectres==0)        //timeout
        {
            fclose(file);
            remove((char *)songname);
            timeout_client(index,"select timeout.\n");
            return OFFLINE;
        }
        if(selectres==-1)        //error
        {
            fclose(file);
            //remove((char *)songname);
            timeout_client(index,"select =-1.\n");
            return OFFLINE;
        }
        else                //select success
        {
            int recvres=recv(clients[index].client_sock,buffer,BUFFER_SIZE,MSG_DONTWAIT); //success
            struct timespec tim, tim2;
            if(recvres>0)
            {
                totalbytesreceived+=recvres;
                fwrite(buffer,sizeof(uint8_t),recvres,file);
            }
            else        //either the client dc or there was an error
            {
                fclose(file);
                timeout_client(index,"Failed to download the song.\n");
                return OFFLINE;
            }

        }
        tv.tv_sec=3;
        tv.tv_usec=0;
    }
    if(totalbytesreceived==expected_songsize)
    {
        pthread_mutex_lock(&stationsmutex);
        //assume we have the complete file
        fclose(file);
        if(rename("tempfile",(char *)songname)==-1)
            perror("Failed renaming the file.\n");
        Stations = (Station *) realloc(Stations, sizeof(Station) * (num_stations+1));
        Stations[num_stations].filename = (char *) malloc(sizeof(char) * strlen((char *) songname));
        strcpy(Stations[num_stations].filename, (char *) songname);
        Stations[num_stations].fp=fopen((char*)songname,"r");
        struct in_addr newaddr;
        newaddr.s_addr = multicastGroup;
        newaddr = increaseip(newaddr, num_stations);
        Stations[num_stations].multicastip = newaddr.s_addr;
        num_stations++;
        send_newstations();
        pthread_mutex_unlock(&stationsmutex);
        pthread_mutex_unlock(&permitsong_mutex);
        return ESTABLISHED;
    }




}

int send_invalid(int client_index,const char* reply_string,uint8_t reply_string_len)
{
    // sends an error message of invalid command to client who typed something stupid
    uint8_t invalidbuffer[BUFFER_SIZE];
    invalidbuffer[0]=INVALID_REPLY;
    invalidbuffer[1]=(uint8_t)strlen(reply_string);
    for(uint8_t i=2;i<2+reply_string_len;i++)
    {
        invalidbuffer[i]=*(reply_string+i);
    }
    int bytes_sent=send(clients[client_index].client_sock,invalidbuffer,(sizeof(uint8_t)*reply_string_len+2),0);
    if (bytes_sent < 0)
    {
        // Some other error occurred
        perror("Failed to send on send_invalid.\n");

    }
    return bytes_sent;
}

int send_permit(int client_index)
{
    uint8_t allow=1;
    uint8_t permitsendbuffer[2];
    uint8_t recvbuffer[5];
    recv(clients[client_index].client_sock,recvbuffer,sizeof(uint8_t)*5,0);
    uint32_t songsize= pack_uint8_t_to_uint32_t(recvbuffer[0],recvbuffer[01],recvbuffer[2],recvbuffer[3]);
    expected_songsize=songsize;                     //global variable

    if(songsize>MAX_FILE_SIZE||songsize<MIN_FILE_SIZE)
        allow=0;
    bzero(songname,256);
    recv(clients[client_index].client_sock,songname,recvbuffer[4],0);       //songname is a global variable that holds the name
    for(int i=0;i<num_stations;i++)
    {
        if(strcmp((char*)songname,Stations[i].filename)==0)
        {
            allow=0;
            break;
        }
    }
    if(permitsong==0)
        allow=0;
    permitsendbuffer[0]=PERMIT_REPLY;
    permitsendbuffer[1]=allow;
    int sendres=send(clients[client_index].client_sock,permitsendbuffer,2,0);
    if(sendres<0)
    {
        perror("Failed to send on permit res\n");
    }
    if(allow)
    {
        pthread_mutex_lock(&permitsong_mutex);
        permitsong=0;
        return DOWNLOADING;
    }
    return ESTABLISHED;


}
int send_newstations()
{
    // updates all the clients about the new number of stations available(done after new station was added)
    // sends to everyone a message about the new station in the radio,done after uplod
    uint8_t newstationsbuffer[3];
    newstationsbuffer[0]=NEWSTATIONS_REPLY;
    uint8_t temp[2];
    unpack_uint16_t_to_uint8_t(num_stations,temp);
    newstationsbuffer[1]=temp[0];
    newstationsbuffer[2]=temp[1];
    int failed_send=0;
    for(int i=0;i<MAX_CLIENTS;i++)
    {

        if(clients[i].client_sock!=EMPTY_SOCKET)                                                            //client has an open socket
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
    return failed_send;
}



void* control_user(void* client_index)
{
    // this function hanldes the control plane(the TCP sockets) with each of the server's clients.
    int index=*((int*)client_index);                  //the index of our client in the Clients array
    int state =OFFLINE;

    while(1)
    {
        switch(state)
        {
            case OFFLINE:                                                       // the client has just connected for the first time
            {
                state=Welcome_handler(index);                              //established
                break;
            }
            case CHANGE_STATION:                                                    //the client asked to switch a station
            {
                state=Announce_handler(index);
                break;
            }
            case ESTABLISHED:                                                       // the client is listening to some songs
            {
                state=Established_handler(index);
                break;
            }
            case DOWNLOADING:                                                       // the client is uploading a song to us
            {
                state=Downloading_handler(index);
                break;
            }

        }
    }
}
