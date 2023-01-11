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
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
//--------------------------------Defines-------------------------------------------
#define OFFLINE     0
#define LISTENING   1
#define WAIT_WELCOME   2
#define WAIT_SONGINFO   3
#define WAIT_APPROVAL   4
#define UPLOADING   5
#define BUFFER_SIZE 1024



#define WELCOME_REPLY 0
#define ANNOUNCE_REPLY 1
#define PERMIT_REPLY 2
#define INVALID_REPLY 3
#define NEWSTATIONS_REPLY 4
#define HELLO_TYPE 0
#define ASK_SONG_TYPE 1
#define UPSONG_TYPE 2
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
int change_flag = 0;                                            //flag that rises when we want to change song




//--------------------------Functions declarations--------------------------------------------------
void Quit_Program();                                             //quit the program entirely
int Connect_to_server(const char* server_ip, int server_port);
int Wait_welcome();
int Send_hello();
void* Listen_data(void* no_args);                               // this function listens to a song, the udp thread will run it
void* Change_station(void*);                                    //this function removes the client from his multicast group and joins another multicast group
int Leave_Station();                                            //leave the current station[thread func] as a signal,then rerun Change_station
int Connect_station();
int Stdin_handler();
int Listen_control();
struct in_addr increaseip(struct in_addr initialaddress,int increment);       //increase the ip of an ip address by



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
    //variables
    int server_port = atoi(argv[2]);
    char* server_ip = argv[1];
    printf("argv1:%s\n",argv[1]);
    printf("argv2:%s\n",argv[2]);
    tcp_client_socket = socket(AF_INET,SOCK_STREAM,0);
    udp_client_socket=socket(AF_INET,SOCK_DGRAM,0);
    if(tcp_client_socket<0)
    {
        perror("Failed creating socket\n");
    }
    while(1)
    {
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
                Listen_control();

                break;
            }
            case WAIT_WELCOME:
            {
                Wait_welcome();
                break;
            }
            case WAIT_SONGINFO:
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
    struct ip_mreq mreq;
    addr.sin_family=AF_INET;
    addr.sin_port=htons(udp_portnumber);
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    bind(udp_client_socket,(struct sockaddr*)&addr,sizeof(addr));
    mreq.imr_interface.s_addr= htonl(INADDR_ANY);
    mreq.imr_multiaddr.s_addr= increaseip(initialmulticast,currstation).s_addr;
    struct sockaddr_in multicast_addr;
    multicast_addr.sin_family=AF_INET;
    multicast_addr.sin_port=htons(udp_portnumber);
   // multicast_addr.sin_addr.s_addr=increaseip(initialmulticast,currstation<<24).s_addr;
    multicast_addr.sin_addr.s_addr=initialmulticast.s_addr;
    setsockopt(udp_client_socket,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq));
    FILE* fp;
    fp= popen("play -t mp3 -> /dev/null 2>&1","w");          //open a command that plays mp3
    int rec_size;
    size_t addrlen=sizeof(addr);
    int multicastaddrlen=sizeof(multicast_addr);
    while(1)
    {
        memset(data_buffer,'\0',sizeof(data_buffer));
        rec_size=recvfrom(udp_client_socket,data_buffer,BUFFER_SIZE,0,(struct sockaddr*)&multicast_addr,&addrlen);
        //printf("Recsise is: %d\n",rec_size);
        rec_size=fwrite(data_buffer,1,rec_size,fp);
        //printf("Recsise write is: %d\n",rec_size);

    }
}
void Quit_Program(int reason)                    // reason is EXIT-FAILURE or EXIT-SUCCESS
{
    struct ip_mreq mreq;
    close(tcp_client_socket);
    pthread_cancel(datathread);
    mreq.imr_interface.s_addr= htonl(INADDR_ANY);
    mreq.imr_multiaddr.s_addr=multicastip;
    setsockopt(udp_client_socket,IPPROTO_IP,IP_DROP_MEMBERSHIP,&mreq,sizeof(mreq));         // leave the multicast group
    close(udp_client_socket);
    exit(reason);


}
int Listen_control()
{
    FD_SET(STDIN_FILENO,&fdset);
    FD_SET(tcp_client_socket,&fdset);
    int selectres=select(FD_SETSIZE,&fdset,NULL,NULL,NULL);
    if(selectres<0)
    {
        perror("Failed to select on Listen Control.\n");
    }
    else                                                    //there is a change in one of the fds(STDIN or TCP)
    {
        if(FD_ISSET(STDIN_FILENO,&fdset))                    // the change was in stdin
        {
            Stdin_handler();
        }
        else                                                //there is TCP input
        {

        }
    }
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

int Send_hello()
{
    uint8_t buffer[3];
    buffer[0] = HELLO_TYPE;
    buffer[1] = 0;
    buffer[2] = 0;
    int sendres = send(tcp_client_socket,buffer,3*sizeof(uint8_t),0);
    return sendres;
}
int Wait_welcome()
{
    int hellores = Send_hello();
    if(hellores<0){
        perror("Failed to send Hello message\n");
        state = OFFLINE;
        return -1;
    }
    struct timeval tv;
    FD_ZERO(&fdset);
    FD_SET(tcp_client_socket,&fdset);
    tv.tv_sec=0;
    tv.tv_usec=300*1000; // 300 MS timeout for select
    int select_res=select(tcp_client_socket + 1, &fdset, NULL, NULL, &tv);
    printf("Select res in wait welcome: %d\n",select_res);

    if (select_res > 0)
    {
//        if(FD_ISSET(STDIN_FILENO,&fdset))                       //client pressed a KEY
//        {
//            Stdin_handler();
//        }

        {

        }
        //success- we received a message
        int recvres=recv(tcp_client_socket,control_buffer,9,0);
        if(recvres!= -1)
        {
            // success

            printf("We received 9 bytes of welcome message");
            if(control_buffer[0]!=WELCOME_REPLY)
            {
                printf("the reply is not of type WELCOME.\n");
                Quit_Program(EXIT_FAILURE);                         //quit the program

            }
            for(int i=0;i<9;i++)
                printf("WELCOME=%d\n",control_buffer[i]);
            numstations= pack_uint8_t_to_uint16_t(control_buffer[1],control_buffer[2]);
            multicastip=pack_uint8_t_to_uint32_t(control_buffer[3],control_buffer[4],control_buffer[5],control_buffer[6]);
            initialmulticast.s_addr=multicastip;
            udp_portnumber=pack_uint8_t_to_uint16_t(control_buffer[7],control_buffer[8]);
            //memset(control_buffer,'\0',BUFFER_SIZE);
            struct in_addr temp;
            temp.s_addr=multicastip;
            printf("There are %d stations\n the ip of the first station is: %s \n the udp port is:%d.\n",(int)numstations,inet_ntoa(temp),(int)udp_portnumber);
            state=LISTENING;
            currstation=0;
            pthread_create(&datathread,NULL,Listen_data,NULL);

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

int Stdin_handler()                                     //assume the change was in STDIN andw e need to either change station or quit
{
    char buff[100];
    printf("buffer in select:%s.\n");            //printf the buffer
    fgets(buff,sizeof(buff),stdin);
    //fseek(stdin,0,SEEK_END);
    fflush(stdin);
    if((buff[0]=='q'||buff[0]=='Q') &&strlen(buff)==1)                      //client pressed Q and wants to exit
    {
        printf("Quitting the program.\n");
        Quit_Program(EXIT_SUCCESS);
    }
    else if((buff[0]=='s'||buff[0]=='S') &&strlen(buff)==1)                      //client pressed Q and wants to exit
    {
        printf("Insert song name.\n");
        //"UPLOAD SONG"
    }
    else                                                                        //client pressed neither S nor Q
    {
        int alldigits=1;
        int inputnum;
        for(int i=0;i<strlen(buff);i++)
        {
            if(buff[i]>'9'||buff[i]<'0')
            {
                alldigits = 0;
                break;
            }
        }
        inputnum=atoi(buff);
        if (alldigits==1 &&inputnum<numstations)                            //user can change the station to whatever he picked
        {
            Change_station(NULL);
            nextstation=inputnum;
            state=WAIT_SONGINFO;
            //user either pressed a number of a station or he pressed a wrong key
        }
        else
        {
            printf("The command is invalid.Please try again.\n");
        }

    }




}
