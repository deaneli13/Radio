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
#include <sys/stat.h>
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
int currstation=0;
int nextstation=0;                                               //nextstation is updated every time we call asksong, then we signal the data thread to change station
uint16_t nextstationcandidate=0;
int changestationflag=1;
uint8_t uploadfilebuffer[256];

//--------------------------Functions declarations--------------------------------------------------
void Quit_Program();                                             //quit the program entirely
int Connect_to_server(const char* server_ip, int server_port);
int Wait_welcome();
int Send_hello();
void* Listen_data(void* no_args);                                // this function listens to a song, the udp thread will run it
int Change_station_control();                                    //this function removes the client from his multicast group and joins another multicast group
int Leave_Station();                                            //leave the current station[thread func] as a signal,then rerun Change_station
int Connect_station();
int Stdin_handler();
int Newstations_handler();
int Invalid_handler(int len);
int Announce_handler(int len);
int Listen_control();
int Upsong_handler();                                           //manages the Upsong state
int Approval_handler();                                         //manages the Wait approval state
int Uploading_handler();

struct in_addr increaseip(struct in_addr initialaddress,int increment)
{
    struct in_addr newaddr;
    newaddr.s_addr  = htonl(ntohl(initialaddress.s_addr) + increment);
    return newaddr;

}

void printProgress(long totalBytesSent, long totalBytes)
{
    int progress = (int) (totalBytesSent * 100.0 / totalBytes);
    printf("\rSending progress: [%d%%]", progress);
}

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
    tcp_client_socket = socket(AF_INET,SOCK_STREAM,0);
    udp_client_socket=socket(AF_INET,SOCK_DGRAM,0);
    if(tcp_client_socket<0)
    {
        perror("Failed creating TCP socket\n");
    }
    if(udp_client_socket<0)
    {
        perror("Failed creating UDP socket\n");
    }
    while(1)
    {
        switch (state)
        {
            case OFFLINE:
            {
                Connect_to_server(server_ip, server_port);//connect
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
                Change_station_control(nextstationcandidate);           //send asksong, get responce and change enxtstation
                break;
            }
            case WAIT_APPROVAL:
            {
                Approval_handler();
                break;
            }
            case UPLOADING:
            {
                Uploading_handler();
                break;
            }
        }
    }

}
void* Listen_data(void* no_args)
{
    FILE* fp;
    fp= popen("play -t mp3 -> /dev/null 2>&1","w");          //open a command that plays mp3
    struct sockaddr_in addr;
    socklen_t addrlen=sizeof(addr);
    struct ip_mreq mreq2;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(udp_portnumber);
    bind(udp_client_socket, (struct sockaddr*) &addr, sizeof(addr));
    mreq2.imr_multiaddr.s_addr = increaseip(initialmulticast,currstation).s_addr;
    mreq2.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(udp_client_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq2, sizeof(mreq2));
    while(1)
    {
        while(1)
        {
            int rec_size;
            rec_size=recvfrom(udp_client_socket, data_buffer, BUFFER_SIZE, 0, (struct sockaddr*) &addr, &addrlen);
            rec_size = fwrite(data_buffer, 1, rec_size, fp);
            if(currstation!=nextstation)
                break;
        }
        setsockopt(udp_client_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq2, sizeof(mreq2));
        mreq2.imr_multiaddr.s_addr = increaseip(initialmulticast,nextstation).s_addr;
        setsockopt(udp_client_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq2, sizeof(mreq2));
        currstation=nextstation;
    }

}
void Quit_Program(int reason)                    // reason is EXIT-FAILURE or EXIT-SUCCESS
{
    printf("QUITTING THE PROGRAM\n");
    sleep(1);
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
    FD_ZERO(&fdset);
    FD_SET(STDIN_FILENO,&fdset);
    FD_SET(tcp_client_socket,&fdset);
    printf("Welcome to the Radio!\nPress 's' or 'S' to upload a song\nPress any number between 0 to %d to change station\nPress 'q' or 'Q' to leave\n",numstations);
    int selectres=select(FD_SETSIZE,&fdset,NULL,NULL,NULL);
    if(selectres<0)
    {
        perror("Failed to select on Listen Control.\n");
    }
    else                                                    //there is a change in one of the fds(STDIN or TCP)
    {
        if(FD_ISSET(tcp_client_socket,&fdset))                                             //there is TCP input
        {
            uint8_t message_type;
            int count=0;
            while((count=recv(tcp_client_socket,&message_type,1,MSG_DONTWAIT))>0)
            {
                switch(message_type)
                {
                    case NEWSTATIONS_REPLY:
                    {
                        recv(tcp_client_socket,control_buffer,2,0);
                        Newstations_handler();
                        state=LISTENING;
                        break;

                    }
                    case INVALID_REPLY:
                    {
                        recv(tcp_client_socket,control_buffer,1,0);
                        int lentoread=(int)control_buffer[0];
                        recv(tcp_client_socket,control_buffer,lentoread,0);
                        Invalid_handler(lentoread);
                        break;
                    }
                    default:
                    {
                        perror("Incompatible message received at listen_control,terminating.\n");
                        Quit_Program(EXIT_FAILURE);
                    }
                }
            }
            if(count == 0 )                 //  tcp connection lost
            {
                perror("The Server crashed\n");
                Quit_Program(EXIT_FAILURE);
            }

        }
        if(FD_ISSET(STDIN_FILENO,&fdset))                    // the change was in stdin
        {
            Stdin_handler();
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
    if (select_res > 0)
    {

        //success- we received a message
        int recvres=recv(tcp_client_socket,control_buffer,9,0);
        if(recvres!= -1)
        {
            // success
            if(control_buffer[0]!=WELCOME_REPLY)
            {
                perror("the reply is not of type WELCOME.\n");
                Quit_Program(EXIT_FAILURE);                         //quit the program

            }
            numstations= pack_uint8_t_to_uint16_t(control_buffer[1],control_buffer[2]);
            multicastip=pack_uint8_t_to_uint32_t(control_buffer[3],control_buffer[4],control_buffer[5],control_buffer[6]);
            initialmulticast.s_addr=multicastip;
            udp_portnumber=pack_uint8_t_to_uint16_t(control_buffer[7],control_buffer[8]);
            //memset(control_buffer,'\0',BUFFER_SIZE);
            struct in_addr temp;
            temp.s_addr=multicastip;
            printf("There are %d stations\n the ip of the first station is: %s \n the udp port is:%d.\n",(int)numstations,inet_ntoa(temp),(int)udp_portnumber);
            state=LISTENING;
            uint8_t message_type[1];
            int count=0;
            while((count=recv(tcp_client_socket,message_type,1,MSG_DONTWAIT))>0)
            {
                switch(message_type[0])
                {
                    case NEWSTATIONS_REPLY:
                    {
                        recv(tcp_client_socket,control_buffer,2,0);
                        Newstations_handler();
                        state=LISTENING;
                        break;

                    }
                    case INVALID_REPLY:
                    {
                        recv(tcp_client_socket,control_buffer,1,0);
                        int lentoread=(int)control_buffer[0];
                        recv(tcp_client_socket,control_buffer,lentoread,0);
                        Invalid_handler(lentoread);
                        break;

                    }
                    default:
                    {
                        perror("Incompatible message received at wait_welcome,terminating.\n");
                        Quit_Program(EXIT_FAILURE);
                        break;
                    }
                }
            }
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
        perror("Timeout on wait welcome.\n");
        Quit_Program(EXIT_FAILURE);
    }
    else if (select_res==-1)
    {
        perror("ERROR IN SELECT");
        Quit_Program(EXIT_FAILURE);
    }
}
int Change_station_control()
{
    uint8_t Asksongbuffer[3];
    Asksongbuffer[0]=ASK_SONG_TYPE;
    uint8_t temp1[2];
    unpack_uint16_t_to_uint8_t(nextstationcandidate,temp1);
    Asksongbuffer[1]=temp1[0];
    Asksongbuffer[2]=temp1[1];
    if(send(tcp_client_socket,Asksongbuffer,3*sizeof(uint8_t),0)<0)
        perror("Send in Change station control failed.\n");
    FD_ZERO(&fdset);
    FD_SET(tcp_client_socket,&fdset);
    struct timeval tv;
    tv.tv_sec=0;
    tv.tv_usec=300*1000; // 300 MS timeout for select
    while(changestationflag)
    {
        int select_res = select(tcp_client_socket + 1, &fdset, NULL, NULL, &tv);
        if (select_res > 0)
        {
            //success- we received a message
            uint8_t message_type;
            while (recv(tcp_client_socket, &message_type, 1, MSG_DONTWAIT)>0)
            {
                switch (message_type)
                {
                    case NEWSTATIONS_REPLY:
                    {
                        recv(tcp_client_socket, control_buffer, 2, 0);
                        Newstations_handler();
                        break;
                    }
                    case INVALID_REPLY:
                    {
                        recv(tcp_client_socket, control_buffer, 1, 0);
                        int lentoread = (int) control_buffer[0];
                        recv(tcp_client_socket, control_buffer, lentoread, 0);
                        Invalid_handler(lentoread);
                        state = OFFLINE;
                        break;

                    }
                    case ANNOUNCE_REPLY:
                    {
                        recv(tcp_client_socket, control_buffer, 1, 0);
                        int lentoread = (int) control_buffer[0];
                        recv(tcp_client_socket, control_buffer, lentoread, 0);
                        Announce_handler(lentoread);
                        state = LISTENING;
                        changestationflag=0;
                        nextstation=nextstationcandidate;
                        break;
                    }
                    default:
                    {
                        perror("Incompatible message received at listen_control,terminating.\n");
                        Quit_Program(EXIT_FAILURE);
                        break;
                    }
                }
            }
        }
        else if (select_res==0)
        {
            perror("Timeout on Change station.\n");
            Quit_Program(EXIT_FAILURE);
        }
        else if (select_res==-1)
        {
            perror("ERROR IN SELECT in change station\n");
            Quit_Program(EXIT_FAILURE);
        }
    }

}

int Stdin_handler()                                     //assume the change was in STDIN andw e need to either change station or quit
{
    char buff[100]={0};
    fgets(buff,99,stdin);
    if (strlen(buff)>0)
        buff[strlen(buff)-1]='\0';
    fseek(stdin,0,SEEK_END);
    fflush(stdin);

    if((buff[0]=='q'||buff[0]=='Q') &&strlen(buff)==1)                      //client pressed Q and wants to exit
    {
        Quit_Program(EXIT_SUCCESS);
    }
    else if((buff[0]=='s'||buff[0]=='S') &&strlen(buff)==1)                      //client pressed Q and wants to exit
    {
        state = WAIT_APPROVAL;
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
        if (alldigits==1 &&inputnum<numstations  &&strlen(buff)>0)    //user can change the station to whatever he picked
        {
            nextstationcandidate=(uint16_t)inputnum;
            changestationflag=1;
            state=WAIT_SONGINFO;

        }
        else                            //user either pressed a number of a station or he pressed a wrong key
        {
            printf("The command is invalid.Please try again.\n");
        }
        FD_CLR(STDIN_FILENO,&fdset);
    }
}
int Newstations_handler()               // read the control buffer and assume its a newstations announcement,print it and increase numstations
{
    uint16_t temp;
    temp= pack_uint8_t_to_uint16_t(control_buffer[0],control_buffer[1]);
    numstations=temp;
    printf("NEW STATIONS!!!!\n There are %d stations .\n",(int)numstations);
    return 1;
}
int Invalid_handler(int len)               // read the control buffer and assume its an invalid command, read it and print it
{
    for(uint8_t i=0;i<len;i++)
    {
        printf("%c",control_buffer[i]);
    }
    Quit_Program(EXIT_FAILURE);
    return 1;
}
int Announce_handler(int len)               // read the control buffer and assume its an invalid command, read it and print it
{
    printf("The song name in station %d : ",nextstation);
    for(uint8_t i=0;i<len;i++)
    {
        putchar(control_buffer[i]);
    }
    putchar('\n');

    return 1;
}
int Approval_handler()
{
    printf("Enter song name: ");
    fgets(uploadfilebuffer,256,stdin);
    if (strlen(uploadfilebuffer)>0)
        uploadfilebuffer[strlen(uploadfilebuffer)-1]='\0';
    if (access(uploadfilebuffer, F_OK) == -1)
    {
        printf("File does not exist in current directory\n");
        state=LISTENING;

    }
    else                // file name is valid and is inside our directory
    {

        FILE* file = fopen(uploadfilebuffer, "r");
        if (file != NULL)
        {
            struct stat buffer;
            int filefd= fileno(file);
            fstat(filefd, &buffer);
            fclose(file);
            uint8_t* sendbuffer=(uint8_t*)malloc(sizeof(uint8_t)*(strlen(uploadfilebuffer)+6));
            sendbuffer[0]=UPSONG_TYPE;
            uint8_t temp[4];
            unpack_uint32_t_to_uint8_t(buffer.st_size,temp);
            for(int i=0;i<4;i++)
                sendbuffer[1+i]=temp[i];
            sendbuffer[5]=strlen(uploadfilebuffer);
            strcpy(sendbuffer+6,uploadfilebuffer);
            int sendres=send(tcp_client_socket,sendbuffer,strlen(uploadfilebuffer)+6,0);
            if(sendres<0)
            {
                perror("Failed to send file in approval handler\n");

            }
            struct timeval tv;
            tv.tv_sec=0;
            tv.tv_usec=300*1000; // 300 MS timeout for select
            int upsongflag=1;
            FD_SET(tcp_client_socket,&fdset);
            while(upsongflag)
            {
                int select_res = select(tcp_client_socket + 1, &fdset, NULL, NULL, &tv);
                if (select_res > 0)
                {
                    //success- we received a message
                    uint8_t message_type;
                    while (recv(tcp_client_socket, &message_type, 1, MSG_DONTWAIT) > 0)
                    {
                        switch (message_type)
                        {
                            case NEWSTATIONS_REPLY:
                            {
                                recv(tcp_client_socket, control_buffer, 2, 0);
                                Newstations_handler();
                                break;
                            }
                            case INVALID_REPLY:
                            {
                                recv(tcp_client_socket, control_buffer, 1, 0);
                                int lentoread = (int) control_buffer[0];
                                recv(tcp_client_socket, control_buffer, lentoread, 0);
                                Invalid_handler(lentoread);
                                state = OFFLINE;
                                break;

                            }
                            case PERMIT_REPLY:
                            {
                                upsongflag = 0;
                                recv(tcp_client_socket, control_buffer, 2, 0);
                                if(control_buffer[0]==1)
                                    state=UPLOADING;
                                else if(control_buffer[0]==0)
                                    state=LISTENING;
                                else
                                {
                                    perror("Received invalid permit message.\n");
                                    Quit_Program(EXIT_FAILURE);
                                }
                                break;
                            }
                            default:
                            {
                                perror("Incompatible message received at UPSONG REQUEST,terminating.\n");
                                Quit_Program(EXIT_FAILURE);
                                break;
                            }
                        }
                    }
                }
                else if (select_res == 0)               //TIMEOUT
                {
                    perror("Timeout on UPSONG REQUEST.\n");
                    Quit_Program(EXIT_FAILURE);
                }
                else if (select_res == -1)
                {
                    perror("ERROR IN SELECT in UPSONG REQUEST\n");
                    Quit_Program(EXIT_FAILURE);
                }
            }

        }

        else                                //upload file cannot be opened
        {
            perror("Error getting file information\n");
        }
    }
}
int Uploading_handler()
{
    FILE* file = fopen(uploadfilebuffer, "r");
    if (file == NULL)
    {
        perror("Failed opening the file in uploading handler\n");
    }
    /* Get the size of the file to be sent */
    fseek(file, 0, SEEK_END);
    uint32_t filelen = ftell(file);
    rewind(file);
    uint8_t sending_buffer[BUFFER_SIZE];
    int bytesRead=0,totalBytesSent=0;
    struct timespec tim, tim2;
    tim.tv_sec = 0;
    tim.tv_nsec = (8000*1000);
    while (1)
    {
        /* Read from the file */
        bytesRead = fread(sending_buffer, 1, sizeof(sending_buffer), file);
        if (bytesRead == 0)         //finished reading from file
        {
            fclose(file);
            break;
        }
        /* Send data to the server */
        int q;
        if ((q=write(tcp_client_socket, sending_buffer, bytesRead)) < 0)
        {      //failed writing to socket
            perror("ERROR writing to socket");
            state=OFFLINE;
            fclose(file);
            Quit_Program(EXIT_FAILURE);
            break;
        }
        tim.tv_sec = 0;
        tim.tv_nsec = (8000*1000);
        totalBytesSent += bytesRead;
        printProgress(totalBytesSent, filelen);
        nanosleep(&tim,&tim2);
    }
    if(totalBytesSent==filelen)
    {
        printf("\n");
        FD_ZERO(&fdset);
        FD_SET(tcp_client_socket,&fdset);
        struct timeval tv;
        tv.tv_sec=2;
        tv.tv_usec=0; // 300 MS timeout for select
        int select_res=select(tcp_client_socket + 1, &fdset, NULL, NULL, &tv);
        if(select_res==0)       //we got a timeout
        {
            perror("Uploading proccess failed. terminating select_res==0\n");
            Quit_Program(EXIT_FAILURE);
        }
        else if(select_res==-1)
        {
            perror("Uploading proccess failed. terminating select_res==-1\n");
            Quit_Program(EXIT_FAILURE);
        }
        else
        {
            uint8_t messagetype;
            int recvres=recv(tcp_client_socket,&messagetype,1,0);
            if(recvres==0)
            {
                perror("lost connection to server\n");
                Quit_Program(EXIT_FAILURE);
            }
            else if(recvres==-1)
            {
                perror("failed receiving response in Upload handler\n");
                Quit_Program(EXIT_FAILURE);
            }
            else
            {
                switch(messagetype)
                {
                    case NEWSTATIONS_REPLY:
                    {
                        recv(tcp_client_socket,control_buffer,2,0);
                        Newstations_handler();
                        state=LISTENING;
                        break;
                    }
                    case INVALID_REPLY:
                    {
                        recv(tcp_client_socket,control_buffer,1,0);
                        int lentoread=(int)control_buffer[0];
                        recv(tcp_client_socket,control_buffer,lentoread,0);
                        Invalid_handler(lentoread);
                        break;
                    }
                    default:
                    {
                        printf("Incompatible message received at listen_control,terminating.\n");
                        Quit_Program(EXIT_FAILURE);
                        break;
                    }
                }
            }

        }

    }
    else        // did not send the file fully
    {
        perror("Failed to sent the file fully");
        Quit_Program(EXIT_FAILURE);
    }
}

