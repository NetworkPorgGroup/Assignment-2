#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <strings.h>
#include <sys/select.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <pthread.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024
#define LISTENQ 30
#define TOTALMAX 1000
#define PORT 4444

void errexit(char *str)
{
    perror(str);
    exit(1);
}

struct child
{
    pid_t pid;
    int pipefd;
    int status;
    int count;
} child;

int MaxSpareServers, MinSpareServers, MaxRequestsPerChild;
const char *SOCK_PATH = "/Users/pranalisancheti/Downloads/NP_Slides/Lab/assign2/mysock";
struct child *cptr;
static pthread_mutex_t *mptr;
int total_servers, spare_servers;
int maxfd = 0;

char *sock_ntop(struct sockaddr *sa, socklen_t salen)
{
    char portstr[8];
    static char str[128];
    struct sockaddr_in *sin = (struct sockaddr_in *)sa;
    if (inet_ntop(AF_INET, &sin->sin_addr, str, sizeof(str)) == NULL)
        return NULL;
    if (ntohs(sin->sin_port) != 0)
    {
        snprintf(portstr, sizeof(portstr), " : %d", ntohs(sin->sin_port));
        strcat(str, portstr);
    }
    return str;
}

void web_child(int sockfd)
{
    int ntowrite;
    ssize_t nread;
    char buf[1024];
    recv(sockfd, buf, sizeof(buf), 0);
    sleep(1);
    send(sockfd, "HI", 2, 0);
    //printf("Handling client\n");
}

void my_lock_init()
{
    pthread_mutexattr_t mattr;
    mptr = mmap(0, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(mptr, &mattr);
}

void my_lock_wait()
{
    pthread_mutex_lock(mptr);
}

void my_lock_release()
{
    pthread_mutex_unlock(mptr);
}

void sig_int(int signo)
{
    int x = total_servers - spare_servers;
    if (x < 0)
        x = 0;
    printf("Number of currently active children : %d\n", x);
    for (int i = 0; i < TOTALMAX; i++)
    {
        if (cptr[i].status != -1)
        {
            printf("Number of clients handled for child %d : %d\n", cptr[i].pid, cptr[i].count);
        }
    }
}

void handle_servers(int listenfd, int addrlen, fd_set *masterset, struct child *cptr)
{
    void kill_child(int num, struct child *cptr);
    void create_child(int num, int listenfd, int addrlen, fd_set *masterset);
    if (spare_servers < MinSpareServers)
    {
        printf("Clients being handled : %d , Total servers : %d - Creating %d children\n", total_servers - spare_servers, total_servers, MinSpareServers - spare_servers);
        create_child(MinSpareServers - spare_servers, listenfd, addrlen, masterset);
        printf("After creation - Clients being handled : %d , Total servers : %d\n", total_servers - spare_servers, total_servers);
        return;
    }
    if (spare_servers > MaxSpareServers)
    {
        printf("Clients being handled : %d , Total servers : %d - Killing %d children\n", total_servers - spare_servers, total_servers, spare_servers - MaxSpareServers);
        kill_child(spare_servers - MaxSpareServers, cptr);
        printf("After killing - Clients being handled : %d , Total servers : %d\n", total_servers - spare_servers, total_servers);
        return;
    }
}

void child_main(int i, int listenfd, int addrlen, int fd)
{
    int connfd;
    socklen_t clilen;
    struct sockaddr *cliaddr;
    cliaddr = (struct sockaddr *)malloc(addrlen);
    printf("Child %ld starting\n", (long)getpid());
    clilen = addrlen;
    for (;;)
    {
        int m;
        my_lock_wait();
        connfd = accept(listenfd, cliaddr, &clilen);
        char *client_info = sock_ntop(cliaddr, clilen);
        printf("Child %ld accepted client %s\n", (long)getpid(), client_info);
        m = 1;
        if (write(fd, &m, sizeof(int)) < 0)
            perror("writing stream message after accept");
        my_lock_release();
        web_child(connfd);
        close(connfd);
        m = 0;
        printf("Child %d is free now\n", getpid());
        if (write(fd, &m, sizeof(int)) < 0)
            perror("writing stream message closing");
    }
}

pid_t child_make(int i, int listenfd, int addrlen, fd_set *masterset)
{
    pid_t pid;
    int p[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, p);
    if ((pid = fork()) > 0)
    {
        signal(SIGINT, sig_int);
        close(p[1]);
        cptr[i].pipefd = p[0];
        cptr[i].pid = pid;
        cptr[i].count = 0;
        cptr[i].status = 0;
        FD_SET(p[0], masterset);
        maxfd = maxfd > p[0] ? maxfd : p[0];
        spare_servers++;
        total_servers++;
        return (pid);
    }
    signal(SIGINT, SIG_IGN);
    close(p[0]);
    child_main(i, listenfd, addrlen, p[1]);
}

void create_child(int num, int listenfd, int addrlen, fd_set *masterset)
{
    int k, d;
    int x = num;
    int ptr = 0;
    for (k = 1; x != 0;)
    {
        printf("Generation rate : %d\n", k);
        d = x > k ? k : x;
        x -= d;
        for (int j = ptr;; j++)
        {
            if (cptr[j].status == -1)
            {
                child_make(j, listenfd, addrlen, masterset);
                d--;
            }
            if (j == TOTALMAX - 1)
                j = -1;
            if (d == 0)
            {
                ptr = j + 1;
                break;
            }
        }
        sleep(1);
        if (x == 0)
            break;
        if (k == 32)
            ;
        else
            k *= 2;
    }
}

void kill_child(int num, struct child *cptr)
{
    int x = num;
    int y = 0;
    for (int j = 0; j < TOTALMAX; j++)
    {
        if (cptr[j].status == 0)
        {
            (total_servers) -= 1;
            (spare_servers) -= 1;
            cptr[j].status = -1;
            close(cptr[j].pipefd);
            kill(cptr[j].pid, SIGTERM);
        }
        x--;
        if (x == 0)
            break;
    }
}

int tcp_listen(socklen_t *addrlenp)
{
    int listenfd;
    struct sockaddr_in servaddr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

    listen(listenfd, LISTENQ);
    return (listenfd);
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("Please enter 3 command line arguments 1)Max_Spare Servers\n 2)Min_Spare_Servers\n3) Max_Requests_per_child\n");
        exit(1);
    }
    MaxSpareServers = atoi(argv[1]);
    MinSpareServers = atoi(argv[2]);
    MaxRequestsPerChild = atoi(argv[3]);
    if (MinSpareServers > MaxSpareServers)
    {
        printf("Min_Spare_Servers are greater than Max_Spare_Servers\n");
        exit(0);
    }
    cptr = (struct child *)calloc(TOTALMAX, sizeof(struct child));
    total_servers = 0;
    spare_servers = 0;
    int listenfd, i;
    socklen_t addrlen;
    fd_set masterset, rset;
    listenfd = tcp_listen(&addrlen);
    FD_ZERO(&masterset);
    my_lock_init();
    for (i = 0; i < TOTALMAX; i++)
    {
        cptr[i].status = -1;
    }
    create_child(MaxSpareServers, listenfd, addrlen, &masterset);
    int nsel;
    for (;;)
    {
        int m;
        rset = masterset;
        nsel = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nsel == 0)
            continue;
        for (int i = 0; i < TOTALMAX; i++)
        {
            if (FD_ISSET(cptr[i].pipefd, &rset))
            {
                if (read(cptr[i].pipefd, &m, sizeof(m)) < 0)
                    nsel--;
                if (m == 0)
                {
                    cptr[i].status = 0;
                    spare_servers++;
                    if (cptr[i].count == MaxRequestsPerChild)
                    {
                        printf("Recycling child %d\n", cptr[i].pid);
                        cptr[i].count = 0;
                    }
                }
                if (m == 1)
                {
                    cptr[i].status = 1;
                    cptr[i].count++;
                    spare_servers--;
                }
                nsel--;
            }
            if (nsel == 0)
                break;
        }
        //printf("Spare server : %d , Total servers : %d\n", spare_servers, total_servers);
        handle_servers(listenfd, addrlen, &masterset, cptr);
    }
    return 0;
}