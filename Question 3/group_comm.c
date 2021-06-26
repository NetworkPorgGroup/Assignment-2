#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <pthread.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#define SA struct sockaddr
#define MAX_LEN 1024
#define IP_LEN 100
#define GNAME_LEN 100
#define Broadcast_port 6001
#define MAX_Group 10
#define Unicast_port 6002
#define MAX_files 20
#define files_name_size 30
#define IP_SIZE 20
#define Fileack_port 6003
#define FILE_PORT 6005
const int port = 6000;

const char baseIP[] = "238.101";
int allow_loopback = 0;
int sockfd3 = -1;
int sockfd1 = -1;
int sockfd_fileack = -1;
int cpid; //variable to store child process id

/*
 * Create a socket, set various properties and return fd
 */
typedef struct
{
    char gip[IP_LEN];
    int gport;
    char gname[GNAME_LEN];
} groupinfo;
groupinfo ginfo[MAX_Group];

typedef struct
{
    int type; //0 normal message  1 file_search_req 4 file_found
    char file_name[files_name_size];
    char req_ip[IP_SIZE];
    char having_ip[IP_SIZE];
    char msg[MAX_LEN];
} msg_to_grp;
char My_Ip_addr[IP_SIZE];
char files_list[MAX_files][files_name_size];
int total_files = 0;
//groupinfo allginfo[MAX_Group];

void die(char *s)
{
    printf("%s", s);
    exit(0);
}
int createSocket()
{

    struct sockaddr_in addr;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    int val = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0)
    {
        perror("Reusing ADDR failed");
        exit(1);
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        die("[ERROR]: Unable to bind.");
        return -1;
    }

    // if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&allow_loopback, sizeof(allow_loopback)) == -1)
    // {
    //     perror("[ERROR]: Unable to disable loopback");
    // }

    int mc_all = 0;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_ALL, (void *)&mc_all, sizeof(mc_all));

    return sock;
}

void displayMenu()
{
    printf("\n********************************************************************");
    printf("\n1.  Create a Group");
    printf("\n2.  Search a Group");
    printf("\n3.  Join a Group");
    printf("\n4.  Send Message");
    printf("\n5 Search a file");
     printf("\n6 See all joined groups");
      printf("\n7 Download a file");
      printf("\n8 Leave a group \n");


    //printf("3. Leave a course\n");
    //printf("4.  Create a Group");
    //printf("4. Exit\n: ");
}
bool joingroup(char *gip)
{

    struct ip_mreq mreq;
    struct sockaddr_in gstr;

    if ((gstr.sin_addr.s_addr = inet_addr(gip)) == -1)
    {

        return false;
    }
    mreq.imr_multiaddr = gstr.sin_addr;
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
   // printf("$$$value of sockfd1 is %d", sockfd1);
    if (setsockopt(sockfd1, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) == -1)
    {
        die("[ERROR]: Unable to join group.");
        //free(gip);
        return false;
    }

    return true;
}
void creategroup(int sockfd)
{
    char gname[GNAME_LEN];
    char gip[IP_LEN];
    int gport;
    printf("Enter group name");
    scanf("%s", gname);
    printf("Enter group ip");
    scanf("%s", gip);
    //printf("Enter group port");
    //scaanf("%d", &gport);
    if (joingroup(gip))
    {
        printf("Group created successfully");
    }
    else
    {
        die("Error in creating group");
    }
    for (int i = 0; i < 10; i++)
    {
        if (ginfo[i].gport == 0)
        {
            ginfo[i].gport = port;
            strcpy(ginfo[i].gip, gip);
            strcpy(ginfo[i].gname, gname);
            //printf("\n group data added to\n");
            printf("Name %s ip %s port %d", ginfo[i].gname, ginfo[i].gip, ginfo[i].gport);
            break;
        }
    }
}
bool displaylistofgrp()
{
    int flag = 0;
    for (int i = 0; i < 10; i++)
    {
        if (ginfo[i].gport != 0)
        {
            flag = 1;
            printf("\n ID:%d Name: %s IP: %s Port:%d \n", i, ginfo[i].gname, ginfo[i].gip, ginfo[i].gport);
        }
    }
    if (flag == 0)
    {
        return false;
    }
    else
    {
        return true;
    }
}

void generateMessage(char *buf, char *msg, int tid)
{
    strcpy(buf, "\n\n- - - - - - - - - - - - - - - - - - - -\n");
    char headr[150];
    if (tid == -1)
    {
        sprintf(headr, "MESSAGE FROM -1");
    }
    else
    {
        sprintf(headr, "MESSAGE FROM %s", ginfo[tid].gname);
    }

    strcat(buf, headr);
    strcat(buf, "\n- - - - - - - - - - - - - - - - - - - -\n");
    int mlen = strlen(msg);
    if (mlen > 0 && msg[mlen - 1] == '\n')
        msg[mlen - 1] = '\0';
    strcat(buf, msg);
    strcat(buf, "\n- - - - - - - - - - - - - - - - - - - -\n");
}

bool sendMessage(int sockfd, int tid)
{
    struct sockaddr_in addr;
    char buf[MAX_LEN], msg[MAX_LEN];
    char destIp[1024];
    strcpy(destIp, ginfo[tid].gip);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(destIp);

    printf("Type the message to be sent \n");
    if (fgets(msg, MAX_LEN - 250, stdin) == NULL)
    {
        printf("[ERROR]: Unable to read stdin\n");
        //free(destIp);
        return false;
    }
    generateMessage(buf, msg, tid);
    int len = strlen(buf);
    msg_to_grp msg_send;
    msg_send.type = 0;
    strcpy(msg_send.msg, buf);
    if (sendto(sockfd, &msg_send, sizeof(msg_to_grp), 0, (SA *)&addr, sizeof(addr)) < 0)
    {
        die("[ERROR]: Unable to send.");
        //free(destIp);
        return false;
    }
    //free(destIp);
    return true;
}

//void getMessage(int sockfd){
bool lookforfile(char *str)
{
    for (int i = 0; i < total_files; i++)
    {
        if (strcmp(files_list[i], str) == 0)
        {
           // printf("\nfile found %s ", files_list[i]);
            return true;
        }
    }
    return false;
}
void *socketThread(void *arg)
{
    int sockfd = *((int *)arg);
    int nr = 0;
    char recvBuf[MAX_LEN] = {0};
    msg_to_grp msg_recv;
    for (;;)
    {
        fflush(stdout);
        nr = recv(sockfd, &msg_recv, sizeof(msg_to_grp), 0);
        if (nr < 0)
        {
            die("[ERROR]: Difficulty in reading from the multicast socket.");
        }
        if (msg_recv.type == 0)
        {

            printf("%s", msg_recv.msg);
        }
        else if (msg_recv.type == 1)
        {
            printf("\n ****************\n Special msg : File req of %s", msg_recv.file_name);
            if (lookforfile(msg_recv.file_name))
            {
                //printf("\n sending reply to requerter\n");
                msg_recv.type = 4;
                strcpy(msg_recv.having_ip, My_Ip_addr);
                struct sockaddr_in addr;

                char destIp[1024];
                strcpy(destIp, msg_recv.req_ip);

                addr.sin_family = AF_INET;
                addr.sin_port = htons(Fileack_port);
                addr.sin_addr.s_addr = inet_addr(destIp);
                if (sendto(sockfd_fileack, &msg_recv, sizeof(msg_to_grp), 0, (SA *)&addr, sizeof(addr)) < 0)
                {
                    die("[ERROR]: Unable to send.");
                    //free(destIp);
                    //return false;
                }
            }
            else
            {
                printf("Required file not present in list\n ");
            }
        }
    }
}

void *socketThread2(void *arg)
{
    int sockfd = *((int *)arg);
    //printf("\n broad cast thread fd %d ", sockfd);
    int nr = 0;
    char recvBuf[MAX_LEN] = {0};
    struct sockaddr_in si_other;
    int slen = sizeof(si_other);
    for (;;)
    {
        fflush(stdout);
        nr = recvfrom(sockfd, recvBuf, MAX_LEN, 0, (struct sockaddr *)&si_other, &slen);
        if (nr < 0)
        {
            die("[ERROR]: Difficulty in reading from the multicast socket.");
        }
        //printf("\n......printing recived data\n");
        //write(1, recvBuf, nr);

       // printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
        //printf("string: %s \t length:%ld\n", recvBuf, strlen(recvBuf));
        for (int i = 0; i < MAX_Group; i++)
        {
            // printf("%d name is %s \n", i, ginfo[i].gname);
            if (ginfo[i].gport != 0)
            {
                // printf("%d", strcmp(ginfo[i].gname, recvBuf));

                if (strcmp(ginfo[i].gname, recvBuf) == 0)
                {

                    //printf("Group found..sending data \n");
                    //printf("sockfd3 is %d and 2 is %d \n", sockfd3, sockfd);
                    struct sockaddr_in addr;
                    memset((char *)&addr, 0, sizeof(addr));
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(6002);
                    char temp[50];

                    strcpy(temp, inet_ntoa(si_other.sin_addr));
                    //printf("%s send to port of 6002 \n", temp);
                    addr.sin_addr.s_addr = inet_addr(temp);
                    //si_other.sin_addr.s_addr = inet_addr("127.0.0.1");
                    int addrl = sizeof(addr);
                    if (sendto(sockfd3, &ginfo[i], sizeof(ginfo[i]), 0, (struct sockaddr *)&addr, addrl) == -1)
                    {
                        die("sendto()");
                    }
                    //printf("Name:%s\t IP:%s \t Port:%d\n", ginfo[i].gname, ginfo[i].gip, ginfo[i].gport);
                    sleep(1);
                }
            }
        }
    }
}
void *socketThread3(void *arg)
{

    int listenfd = 0;
    int connfd = 0;
    struct sockaddr_in serv_addr;
    char sendBuff[1025];
    int numrv;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
   // printf("Socket retrieve success\n");

    memset(&serv_addr, '0', sizeof(serv_addr));
    memset(sendBuff, '0', sizeof(sendBuff));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(FILE_PORT);

    bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (listen(listenfd, 10) == -1)
    {
        die("Failed to listen\n");
        
        //return -1;
    }
    while (1)
    {

        int offset=0;

        ;
        connfd = accept(listenfd, (struct sockaddr *)NULL, NULL);
        //printf("Waiting for client to send the command (Full File (0) Partial File (1)\n");

        msg_to_grp msg_recv;
        read(connfd, &msg_recv, sizeof(msg_recv));
        char file_name[files_name_size];
        strcpy(file_name, msg_recv.file_name);
        //printf("Sending file @@@@%s", file_name);

        FILE *fp = fopen(file_name, "rb");
        if (fp == NULL)
        {
            printf("File opern error");
            continue;
        }

        while (1)
        {
            /* First read file in chunks of 256 bytes */
            unsigned char buff[256] = {0};
            int nread = fread(buff, 1, 256, fp);
            //printf("Bytes read %d \n", nread);
            /* If read was success, send data. */
            if (nread > 0)
            {
                //printf("Sending \n");
                write(connfd, buff, nread);
            }
            /*
* There is something tricky going on with read ..
* Either there was error, or we reached end of file.
*/

            if (nread < 256)
            {
                if (feof(fp)){

                }
                   // printf("End of file\n");
                if (ferror(fp))
                {

                }
                    //printf("Error reading\n");
                break;
            }
        }
        close(connfd);
    }
}
int createSocket2()
{

    struct sockaddr_in addr;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;
    bzero(&addr, sizeof(addr));

    addr.sin_family = AF_INET;
    // char tempip[20];
    //strcpy(tempip,"255.255.255.255");
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(Broadcast_port);

    int val = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0)
    {
        die("Reusing ADDR failed");
        exit(1);
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        die("[ERROR]: Unable to bind.");
        return -1;
    }

    // if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&allow_loopback, sizeof(allow_loopback)) == -1)
    // {
    //     perror("[ERROR]: Unable to disable loopback");
    // }

    int on = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

    return sock;
}

int createSocket3(int portnum)
{

    struct sockaddr_in addr;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;
    bzero(&addr, sizeof(addr));

    addr.sin_family = AF_INET;
    // char tempip[20];
    //strcpy(tempip,"255.255.255.255");
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(portnum);

    int val = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0)
    {
        die("Reusing ADDR failed");
        exit(1);
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        die("[ERROR]: Unable to bind.");
        return -1;
    }

    // if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    // {
    //     perror("Error");
    // }

    // if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&allow_loopback, sizeof(allow_loopback)) == -1)
    // {
    //     perror("[ERROR]: Unable to disable loopback");
    // }

    // int on = 1;
    // setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

    return sock;
}

bool searchGroup(int sockfd2, int opt)
{
    struct sockaddr_in addr;
    char buf[MAX_LEN], msg[MAX_LEN];
    char destIp[1024];
    //strcpy(destIp,ginfo[tid].gip);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(Broadcast_port);
    char tempip[20];
    strcpy(tempip, "255.255.255.255");
    addr.sin_addr.s_addr = inet_addr(tempip);
    if (opt == 1)
    {
        printf("Enter groupname to join \n");
    }
    else
    {
        printf("Enter groupname to search \n");
    }

    scanf("%s", msg);
    // if (fgets(msg, MAX_LEN - 250, stdin) == NULL)
    // {
    //     die("[ERROR]: Unable to read stdin\n");
    //     //free(destIp);
    //     //return false;
    // }
    printf("Searching for group : %s .......", msg);
    //generateMessage(buf,msg,-1);
    strcpy(buf, msg);
    int len = strlen(buf);
    if (sendto(sockfd2, buf, len, 0, (SA *)&addr, sizeof(addr)) < 0)
        die("[ERROR]: Unable to send.");
    //free(destIp);
    //return false;

    ////recving unicast msgs
    fd_set rset;
    FD_ZERO(&rset);
    int maxfdp = sockfd3 + 1;
    int nready;
    int helper = 0;
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    for (;;)
    {
        FD_SET(sockfd3, &rset);
        nready = select(maxfdp, &rset, NULL, NULL, &tv);
        if (FD_ISSET(sockfd3, &rset))
        {
            groupinfo tg;
            strcpy(tg.gip, "");
            strcpy(tg.gname, "");
            tg.gport = 0;
            struct sockaddr_in si_other;
            int slen = sizeof(si_other);
            int flag = 0;
            //printf("sockfd3 is %d and 2 is %d",sockfd3,sockfd2);
            if (recvfrom(sockfd3, &tg, sizeof(tg), 0,
                         (struct sockaddr *)&si_other, &slen) >= 0)
            {
                //timeout reached

                if (flag == 0)
                {
                    //printf("\n @@@@@@@@@@Data ,name ip reached \n");
                    printf("Group name: %s \t Ip: %s port :%d\n", tg.gname, tg.gip, tg.gport);
                    if (strcmp(tg.gname, msg) == 0)
                    {
                        helper = 1;
                    }
                    if (opt == 1)
                    {
                        if (strcmp(tg.gname, msg) == 0)
                        {
                            flag = 1;
                            char temp1[50];
                            strcpy(temp1, tg.gip);
                            //printf("calling join grp with %s", temp1);
                            joingroup(temp1);
                            for (int i = 0; i < 10; i++)
                            {
                                if (ginfo[i].gport == 0)
                                {
                                    ginfo[i].gport = tg.gport;
                                    strcpy(ginfo[i].gip, tg.gip);
                                    strcpy(ginfo[i].gname, tg.gname);
                                   // printf("\n group data added to\n");
                                    printf("Name %s ip %s port %d", ginfo[i].gname, ginfo[i].gip, ginfo[i].gport);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            if (helper == 1)
            {
                return true;
            }
            printf("No such grp found");
            return false;
        }
    }

    return true;
}

bool myIP()
{

    //printf("\n enter file name to search \n");
    // char file_tosearch[30];
    // scanf("%s", file_tosearch);
    struct ifaddrs *addrs;
    struct ifaddrs *tmp;
    getifaddrs(&addrs);
    tmp = addrs;

    while (tmp)
    {
        if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in *pAddr = (struct sockaddr_in *)tmp->ifa_addr;
            //printf("%s: %s\n", tmp->ifa_name, inet_ntoa(pAddr->sin_addr));
            if (strcmp("127.0.0.1", inet_ntoa(pAddr->sin_addr)) != 0)
            {
                strcpy(My_Ip_addr, inet_ntoa(pAddr->sin_addr));
                freeifaddrs(addrs);
                return true;
            }
        }

        tmp = tmp->ifa_next;
    }

    freeifaddrs(addrs);
    return false;
}
bool searchfile()
{
    printf("\nEnter file name to search\n");
    char file_name[files_name_size];
    scanf("%s", file_name);

    msg_to_grp msg_send;
    msg_send.type = 1;
    strcpy(msg_send.file_name, file_name);
    strcpy(msg_send.req_ip, My_Ip_addr);

    struct sockaddr_in addr;
    //char buf[MAX_LEN], msg[MAX_LEN];
    for (int i = 0; i < MAX_Group; i++)
    {
        if (ginfo[i].gport != 0)
        {
            char destIp[1024];
            strcpy(destIp, ginfo[i].gip);
           // printf("Searching msg send in group : %s", ginfo[i].gname);
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = inet_addr(destIp);
            if (sendto(sockfd1, &msg_send, sizeof(msg_to_grp), 0, (SA *)&addr, sizeof(addr)) < 0)
            {
                die("[ERROR]: Unable to send.");
                //free(destIp);
                //return false;
            }
        }
    }
   // printf("Search request send");

    //free(destIp);
    //return true;

    fd_set rset;
    FD_ZERO(&rset);
    int maxfdp = sockfd_fileack + 1;
    int nready;
    int helper = 0;
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    printf("\n***********************************\n");
    printf("\nFile found at following ip\n");
    for (;;)
    {
        FD_SET(sockfd_fileack, &rset);
        nready = select(maxfdp, &rset, NULL, NULL, &tv);
        if (FD_ISSET(sockfd_fileack, &rset))
        {
            msg_to_grp recv_msg;
            struct sockaddr_in si_other;
            int slen = sizeof(si_other);
            int flag = 0;
            //printf("sockfd3 is %d and 2 is %d",sockfd3,sockfd2);
            if (recvfrom(sockfd_fileack, &recv_msg, sizeof(msg_to_grp), 0,
                         (struct sockaddr *)&si_other, &slen) >= 0)
            {
                if (recv_msg.type == 4 && strcmp(recv_msg.file_name, file_name) == 0)
                {
                    printf("IP addr: %s\n", recv_msg.having_ip);
                    helper = 1;
                }
            }
        }
        else
        {
            if (helper == 1)
            {
                return true;
            }
            printf("No File found");
            return false;
        }
    }
}
void download_file()
{

    printf("Enter file name\n");
    char file_name[files_name_size];
    scanf("%s", file_name);
    printf("enter ip");
    char file_ip[50];
    scanf("%s", file_ip);

    printf("Downloading %s from %s", file_name, file_ip);
    msg_to_grp msg_send;
    msg_send.type = 2;
    strcpy(msg_send.file_name, file_name);

    int bytesReceived = 0;
    char recvBuff[256];
    struct sockaddr_in serv_addr;
    /* Create a socket first */
    int sockfd_file;
    if ((sockfd_file = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        die("\n Error : Could not create socket \n");
        
    }
    /* Initialize sockaddr_in data structure */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(FILE_PORT); // port
    serv_addr.sin_addr.s_addr = inet_addr(file_ip);

    if (connect(sockfd_file, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\n Error : Connect Failed \n");
        return;
    }
    FILE *fp;
    fp = fopen(file_name, "ab");
    if (NULL == fp)
    {
        die("Error opening file");
        //return 1;
    }
    if (write(sockfd_file, &msg_send, sizeof(msg_send)) < 0)
    {
        printf("error in sending file name\n");
    }
    while ((bytesReceived = read(sockfd_file, recvBuff, 256)) > 0)
    {
        printf("Bytes received %d\n", bytesReceived);
        // recvBuff[n] = 0;
        fwrite(recvBuff, 1, bytesReceived, fp);
        // printf("%s \n", recvBuff);
    }
    if (bytesReceived < 0)
    {
        printf("\n Read Error \n");
    }
    fclose(fp);
    printf("\n\t\t***FILE DOWNLOADED SUCCESSFULLY***\n");
}
void leaveGroup(){
    int grpid;
    displaylistofgrp();
    printf("Enter group ID you want to leave");
    scanf("%d",&grpid);
    char gip[50];
    strcpy(gip,ginfo[grpid].gip);
    struct ip_mreq  mreq;
    struct sockaddr_in gstr;
    if((gstr.sin_addr.s_addr = inet_addr(gip)) == -1) {
        //free(gip);
        //return false;
    }
    mreq.imr_multiaddr = gstr.sin_addr;
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if(setsockopt(sockfd1,IPPROTO_IP,IP_DROP_MEMBERSHIP,(char *)&mreq,sizeof(mreq)) == -1){
        die("setsockopt(): IP_DROP_MEMBERSHIP:");
        //ps("[ERROR]: Unable to leave group.");
        //free(gip);
       // return false;
    }
    //free(gip);
    ginfo[grpid].gport=0;
    strcpy(ginfo[grpid].gip,"");
    strcpy(ginfo[grpid].gname,"");
    //return true;
}
void startChatApp()
{

    pthread_t tid, tid2, tid3;
    sockfd1 = createSocket();
    if (sockfd1 == -1)
        exit(-1);
   // printf("$$$main value of sockfd1 is %d", sockfd1);
    int sockfd2 = createSocket2();
    if (sockfd2 == -1)
        exit(-1);

    sockfd3 = createSocket3(Unicast_port);
    if (sockfd3 == -1)
        exit(-1);

    sockfd_fileack = createSocket3(Fileack_port);
    if (sockfd_fileack == -1)
        exit(-1);

    if (pthread_create(&tid, NULL, socketThread, &sockfd1) != 0)
        printf("Failed to create thread\n");

    //printf("socket 1 created");
    if (pthread_create(&tid2, NULL, socketThread2, &sockfd2) != 0)
        printf("Failed to create thread\n");

    //printf("socket 2 created");

    if (pthread_create(&tid3, NULL, socketThread3, NULL) != 0)
        printf("Failed to create thread\n");
    // if ((cpid = fork()) == 0)
    // {
    //     getMessage(sockfd);
    //  }
    //  else
    // {

    for (;;)
    {
        displayMenu();
        int ch;
        scanf("%d", &ch);
        if (ch == 1)
        {
            //create a group
            creategroup(sockfd1);
        }
        else if (ch == 2)
        {
            char t;
            scanf("%c", &t);

            if (searchGroup(sockfd2, 0) == false)
            {
                printf("No group found");
            }
        }
        else if (ch == 3)
        {

            // join a group
            char t;
            scanf("%c", &t);

            if (searchGroup(sockfd2, 1) == false)
            {
                printf("No group found");
            }
            //   printf("Enter gip to join \n");
            //     char temp[IP_LEN];
            //     scanf("%s",temp);
            //     printf("group Ip to join is %s ",temp);
            //     joingroup(sockfd1,temp);
            //     printf("group joined");
        }
        else if (ch == 4)
        {
            //send msg

            if (!displaylistofgrp())
            {
                printf("You are not joined in any group");
                continue;
            }
            int tid;
            printf("Enter group id to send msg\n");
            scanf("%d", &tid);
            char t;
            scanf("%c", &t);
            if (!sendMessage(sockfd1, tid))
            {
                die("error is sending msg");
            }
            else
            {
                printf("msg send successfully");
            }
        }
        else if (ch == 5)
        {
            char t;
            scanf("%c", &t);
            searchfile();
        }
        else if (ch == 6)
        {
            displaylistofgrp();
        }
        else if (ch == 7)
        {
            char t;
            scanf("%c", &t);
            download_file();
        }else if(ch ==8){
            leaveGroup();
        }
    }
    //  }
}
void initialsize_ginfo()
{
    for (int i = 0; i < 10; i++)
    {
        ginfo[i].gport = 0;
        strcpy(ginfo[i].gname, "");
        strcpy(ginfo[i].gip, "");
    }
}
void load_files_name()
{
    DIR *d;
    struct dirent *dir;
    d = opendir(".");
    if (d)
    {

        while ((dir = readdir(d)) != NULL)
        {
            if (dir->d_type == DT_REG)
            {
                //printf("%s\n", dir->d_name);
                strcpy(files_list[total_files], dir->d_name);
                total_files++;
            }
        }
        closedir(d);
    }
}
int main(int argc, char *argv[])
{
    initialsize_ginfo();
    load_files_name();
    if (!myIP())
    {
        printf("Cant find your eth0 IP addr..Exiting");
        exit(0);
    }
    printf("YOUR IP %s", My_Ip_addr);
    startChatApp();
}
//238.101.110.192

