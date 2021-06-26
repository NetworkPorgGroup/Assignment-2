#include <sys/types.h>  /* basic system data types */
#include <sys/socket.h> /* basic socket definitions */
#include <sys/time.h>   /* timeval{} for select() */
#include <time.h>       /* timespec{} for pselect() */
#include <netinet/in.h> /* sockaddr_in{} and other Internet defns */
#include <arpa/inet.h>  /* inet(3) functions */
#include <errno.h>
#include <fcntl.h> /* for nonblocking */
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* for S_xxx file mode constants */
#include <sys/uio.h>  /* for iovec{} and readv/writev */
#include <unistd.h>
#include <sys/wait.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <ctype.h>
#include <sys/epoll.h>

#define PORT 60000
#define MAX_BUF 1000 /* Maximum bytes fetched by a single read() */
#define MAX_EVENTS 5 /* Maximum number of events to be returned from */
#define BUFSIZE 1500

/* globals */
char sendbuf[BUFSIZE];
int MAX_TIME;
int verbose;
struct timeval tval;
time_t start, end;
int activeNodes;
//////////////for epoll
int epfd, ready;
struct epoll_event ev;
struct epoll_event evlist[MAX_EVENTS];
////////////
void init_v6(int);
void proc_v4(char *a, ssize_t b, struct msghdr *c, struct timeval *d, int);
void proc_v6(char *a, ssize_t b, struct msghdr *c, struct timeval *d, int);
void send_v4(int);
void send_v6(int);
void readloop(int);
void sig_alrm(int);
//void sig_int(int);
void tv_sub(struct timeval *, struct timeval *);
uint16_t in_cksum(uint16_t *addr, int len);

struct proto
{
    void (*fproc)(char *, ssize_t, struct msghdr *, struct timeval *, int);
    void (*fsend)(int);
    void (*finit)(int);
    struct sockaddr *sasend; /* sockaddr{} for send, from getaddrinfo */
    struct sockaddr *sarecv; /* sockaddr{} for receiving */
    socklen_t salen;         /* length of sockaddr {}s */
    int icmpproto;           /* IPPROTO_xxx value for ICMP */
} * pr;

typedef struct node /// id of each node is the array index
{
    int isSet;
    int sockfd;
    char hostIP[40];
    struct addrinfo *ai;
    int ping_no;
    double rtt[3];
    struct proto *pr;
    int recNo;
} NODE;

int datalen = 56; /* data that goes with ICMP echo request */

void initialise_epoll()
{
    epfd = epoll_create(100);
    if (epfd == -1)
    {
        printf("epoll_create");
        exit(1);
    }
}

char *sock_ntop_host(const struct sockaddr *sa, socklen_t salen)
{
    static char str[128]; /* Unix domain is largest */

    switch (sa->sa_family)
    {
    case AF_INET:
    {
        struct sockaddr_in *sin = (struct sockaddr_in *)sa;

        if (inet_ntop(AF_INET, &sin->sin_addr, str, sizeof(str)) == NULL)
            return (NULL);
        return (str);
    }

    case AF_INET6:
    {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

        if (inet_ntop(AF_INET6, &sin6->sin6_addr, str, sizeof(str)) == NULL)
            return (NULL);
        return (str);
    }
    default:
        snprintf(str, sizeof(str), "sock_ntop_host: unknown AF_xxx: %d, len %d",
                 sa->sa_family, salen);
        return (str);
    }
    return (NULL);
}

char *Sock_ntop_host(const struct sockaddr *sa, socklen_t salen)
{
    char *ptr;

    if ((ptr = sock_ntop_host(sa, salen)) == NULL)
    {
        printf("sock_ntop_host error"); /* inet_ntop() sets errno */
        exit(1);
    }

    return (ptr);
}

struct addrinfo *Host_serv(const char *host)
{
    int n;
    struct addrinfo *res = (struct addrinfo *)calloc(1, sizeof(struct addrinfo));
    n = strlen(host);
    for (int i = 0; i < n; i++)
    {
        if (isalnum(host[i]))
            continue;
        if (host[i] == '.')
        {
            res->ai_family = AF_INET;

            struct sockaddr_in *sin = (struct sockaddr_in *)calloc(1, sizeof(struct sockaddr_in));
            memset(sin, '0', sizeof(struct sockaddr_in));
            sin->sin_family = AF_INET;
            int s = inet_pton(AF_INET, host, &sin->sin_addr);
            if (s == 0)
            {
                printf("0 error\n");
                printf("inet_pton error for %s", host);
                exit(1);
            }
            if (s < 0)
            {
                printf("-1 error\n");
                printf("inet_pton error for %s", host);
                exit(1);
            }

            sin->sin_port = htons(PORT);
            res->ai_flags = 0;                            /* Input flags.  */
            res->ai_socktype = SOCK_RAW;                  /* Socket type.  */
            res->ai_protocol = IPPROTO_ICMP;              /* Protocol for socket.  */
            res->ai_addrlen = sizeof(struct sockaddr_in); /* Length of socket address.  */
            res->ai_addr = (struct sockaddr *)sin;        /* Socket address for socket.  */
            res->ai_canonname = "";                       /* Canonical name for service location.  */
            res->ai_next = NULL;
            break;
        }
        if (host[i] == ':')
        {
            //printf("here in ipv6\n");
            res->ai_family = AF_INET6;
            //printf("here in 18\n");

            struct sockaddr_in6 *sin = (struct sockaddr_in6 *)calloc(1, sizeof(struct sockaddr_in6));
            bzero(sin, sizeof(struct sockaddr_in6));
            //printf("here in 182\n");
            sin->sin6_family = AF_INET6;
            sin->sin6_port = htons(PORT); /* daytime server */
            //printf("here in 185\n");

            if (inet_pton(AF_INET6, host, &sin->sin6_addr) <= 0)
            {
                printf("inet_pton error for %s", host);
                exit(1);
            }
            //            printf("here in 192\n");

            res->ai_flags = 0;                             /* Input flags.  */
            res->ai_socktype = SOCK_RAW;                   /* Socket type.  */
            res->ai_protocol = IPPROTO_ICMPV6;             /* Protocol for socket.  */
            res->ai_addrlen = sizeof(struct sockaddr_in6); /* Length of socket address.  */
            res->ai_addr = (struct sockaddr *)sin;         /* Socket address for socket.  */
            res->ai_canonname = "";                        /* Canonical name for service location.  */
            res->ai_next = NULL;
            break;
        }
    }
    return res; /* return pointer to first on linked list */
}
NODE nodes[MAX_EVENTS];
int main(int argc, char **argv)
{
    int c;
    struct addrinfo *ai;
    char buf[MAX_BUF];

    ////////////file read mechanism
    if (argc != 3)
    {
        printf("Enter input file path\n");
        exit(1);
    }
    FILE *fp = fopen(argv[1], "r");
    if (!fp)
    {
        printf("file open error. EXITING\n");
        exit(1);
    }
    MAX_TIME = atoi(argv[2]);
    char *host = NULL;
    size_t len = 0;
    char temp[20];

    //signal(SIGINT, sig_int);

    memset(nodes, 0, sizeof(nodes));
    int i = 0;
    while (getline(&host, &len, fp) != -1)
    {
        //printf("%d IP read: %s\n", i, host);
        if (host[strlen(host) - 1] == '\n')
            host[strlen(host) - 1] = '\0';
        printf("%s\n", host);
        ai = Host_serv(host);
        strcpy(nodes[i].hostIP, host);
        nodes[i].ai = ai;
        nodes[i].ping_no = 0;
        nodes[i].recNo = 0;

        memset(nodes[i].rtt, 0.0, sizeof(nodes[i].rtt));
        nodes[i].isSet = 1;

        i++;
        if (i == MAX_EVENTS || feof(fp))
        {
            //printf("here\n");
            i = 0;
            pid_t child = fork();
            if (child == -1)
            {
                printf("fork error, exiting\n");
                exit(1);
            }
            if (child == 0)
            {
                // printf("inside child\n");
                time(&start);
                initialise_epoll();
                signal(SIGALRM, sig_alrm);
                activeNodes = 0;
                for (int j = 0; j < MAX_EVENTS; j++)
                {
                    if (nodes[j].isSet == 1)
                    {
                        ///////set pr
                        if (nodes[j].ai->ai_family == AF_INET)
                        {
                            nodes[j].pr = (struct proto *)calloc(1, sizeof(struct proto));
                            nodes[j].pr->fproc = proc_v4;
                            nodes[j].pr->fsend = send_v4;
                            nodes[j].pr->finit = NULL;
                            nodes[j].pr->salen = 0;
                            nodes[j].pr->sarecv = NULL;
                            nodes[j].pr->sasend = NULL;
                            nodes[j].pr->icmpproto = IPPROTO_ICMP;
                        }
                        else if (nodes[j].ai->ai_family == AF_INET6)
                        {
                            //printf("checking ipv6\n");
                            nodes[j].pr = (struct proto *)calloc(1, sizeof(struct proto));
                            nodes[j].pr->fproc = proc_v6;
                            nodes[j].pr->fsend = send_v6;
                            nodes[j].pr->finit = init_v6;
                            nodes[j].pr->salen = 0;
                            nodes[j].pr->sarecv = NULL;
                            nodes[j].pr->sasend = NULL;
                            nodes[j].pr->icmpproto = IPPROTO_ICMPV6;
                            if (IN6_IS_ADDR_V4MAPPED(&(((struct sockaddr_in6 *)
                                                            nodes[j]
                                                                .ai->ai_addr)
                                                           ->sin6_addr)))
                            {
                                printf("cannot ping IPv4-mapped IPv6 address");
                                nodes[j].isSet = 0;
                                continue;
                            }
                        }
                        else
                        {
                            printf("unknown address family %d", nodes[j].ai->ai_family);

                            nodes[j].isSet = 0;
                            continue;
                        }

                        nodes[j].pr->sasend = nodes[j].ai->ai_addr;
                        nodes[j].pr->sarecv = calloc(1, nodes[j].ai->ai_addrlen);
                        nodes[j].pr->salen = nodes[j].ai->ai_addrlen;

                        nodes[j].sockfd = socket(nodes[j].ai->ai_family, SOCK_RAW, nodes[j].ai->ai_protocol);
                        //printf("%d %s socket %d\n", j, nodes[j].hostIP,nodes[j].sockfd);

                        if (nodes[j].sockfd < 0)
                        {
                            printf("ERROR IN SOCKET CREATION\n");
                            printf("Something went wrong with socket call!  --> %d %s\n", errno, strerror(errno));
                            exit(1);
                        }

                        //printf("After socket\n");
                        if (nodes[j].pr->finit)
                            (*nodes[j].pr->finit)(j);

                        int size = 60 * 1024; /* OK if setsockopt fails */
                        // if (connect(nodes[j].sockfd, nodes[j].pr->sasend, nodes[j].pr->salen) == -1)
                        //     printf("Oh dear, something went wrong with connect call!  --> %d %s\n", errno, strerror(errno));

                        setsockopt(nodes[j].sockfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
                        ev.events = EPOLLIN;
                        ev.data.fd = nodes[j].sockfd;
                        if (epoll_ctl(epfd, EPOLL_CTL_ADD, nodes[j].sockfd, &ev) == -1)
                        {
                            printf("Error in adding socket to epoll list. Hence exiting\n");
                            exit(1);
                        }
                        //  printf("added node %d\n", nodes[j].sockfd);
                        activeNodes++;
                    }
                }
                setuid(getuid());
                sig_alrm(SIGALRM);

                while (activeNodes > 0)
                {
                    //printf("inside while");
                    ready = epoll_wait(epfd, evlist, MAX_EVENTS, -1);
                    //printf("inside epoll\n");
                    if (ready == -1)
                    { //printf("ready is -1\n");

                        if (errno == EINTR)
                        {
                            //printf("errno is eintr\n");
                            continue;
                        }

                        else
                        {
                            printf("Epoll wait error. BYE BYE\n");
                            alarm(0);
                            exit(1);
                        }
                    }
                    // printf("crosse checkls\n");

                    gettimeofday(&tval, NULL);
                    for (int k = 0; k < ready; k++)
                    {

                        if (evlist[k].events & EPOLLIN)
                        {
                            for (int l = 0; l < MAX_EVENTS; l++)
                            {
                                if (nodes[l].sockfd == evlist[k].data.fd)
                                {
                                    readloop(l);
                                    break;
                                }
                            }
                        }
                    }
                }
                //alarm(0);
                exit(0);
            }
            else
            {
                //sleep(2);
                memset(nodes, 0, sizeof(nodes));
            }
        }
    }

    //printf("ending main\n");
    //readloop();
    exit(0);
}

void readloop(int index)
{
    //printf("reading \n");
    int size;
    char recvbuf[BUFSIZE];
    char controlbuf[BUFSIZE];
    struct msghdr msg;
    struct iovec iov;
    ssize_t n;
    iov.iov_base = recvbuf;
    iov.iov_len = sizeof(recvbuf);
    msg.msg_name = nodes[index].pr->sarecv;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = controlbuf;
    msg.msg_namelen = nodes[index].pr->salen;
    msg.msg_controllen = sizeof(controlbuf);
    n = recvmsg(nodes[index].sockfd, &msg, 0);
    if (n < 0)
    {
        if (errno == EINTR)
            return;
        else
        {
            printf("recvmsg error");
            exit(1);
        }
    }

    (*nodes[index].pr->fproc)(recvbuf, n, &msg, &tval, index);
}
void tv_sub(struct timeval *out, struct timeval *in)
{
    if ((out->tv_usec -= in->tv_usec) < 0)
    { /* out -= in */
        --out->tv_sec;
        out->tv_usec += 1000000;
    }
    out->tv_sec -= in->tv_sec;
}
// ??
void proc_v4(char *ptr, ssize_t len, struct msghdr *msg, struct timeval *tvrecv, int index)
{
    int hlenl, icmplen;
    double rtt;
    struct ip *ip;
    struct icmp *icmp1;
    struct timeval *tvsend;
    ip = (struct ip *)ptr;  /* start of IP header */
    hlenl = ip->ip_hl << 2; /* length of IP header */
    if (ip->ip_p != IPPROTO_ICMP)
        return;                           /* not ICMP */
    icmp1 = (struct icmp *)(ptr + hlenl); /* start of ICMP header */
    if ((icmplen = len - hlenl) < 8)
        return; /* malformed packet */
    if (icmp1->icmp_type == ICMP_ECHOREPLY)
    {
        if (icmp1->icmp_id != getpid() & index & 0xffff)
            return; /* not a response to our ECHO_REQUEST */
        if (icmplen < 16)
            return; /* not enough data to use */
        tvsend = (struct timeval *)icmp1->icmp_data;
        tv_sub(tvrecv, tvsend);
        rtt = tvrecv->tv_sec * 1000.0 + tvrecv->tv_usec / 1000.0;
        // printf("%d bytes from %s: seq=%u, ttl=%d, rtt=%.3f ms\n",
        //        icmplen, nodes[index].hostIP,
        //        icmp1->icmp_seq, ip->ip_ttl, rtt);
        if (rtt < 0.0)
            return;
        nodes[index].rtt[nodes[index].recNo] = rtt;
        nodes[index].recNo++;
        if (nodes[index].recNo == 3)
        {
            printf("%s\t\t%.3f ms  %.3f ms  %.3f ms\n", nodes[index].hostIP, nodes[index].rtt[0], nodes[index].rtt[2], nodes[index].rtt[2]);

            nodes[index].isSet = 0;
            activeNodes--;
            free(nodes[index].ai);
            //printf("freed ai\n");
            free(nodes[index].pr);
            //printf("freed pr\n");
            close(nodes[index].sockfd);
            //printf("closed socket for %d\n", index);
        }
    }
    else if (verbose)
    {
        // printf(" %d bytes from %s: type = %d, code = %d\n",
        //        icmplen, Sock_ntop_host(nodes[index].pr->sarecv, nodes[index].pr->salen),
        //        icmp1->icmp_type, icmp1->icmp_code);
    }
}
void init_v6(int index)
{
    int on = 1;
    if (verbose == 0)
    {
        /* install a filter that only passes ICMP6_ECHO_REPLY unless verbose */
        struct icmp6_filter myfilt;
        ICMP6_FILTER_SETBLOCKALL(&myfilt);
        ICMP6_FILTER_SETPASS(ICMP6_ECHO_REPLY, &myfilt);
        setsockopt(nodes[index].sockfd, IPPROTO_IPV6, ICMP6_FILTER, &myfilt,
                   sizeof(myfilt));
        /* ignore error return; the filter is an optimization */
    }
/* ignore error returned below; we just won't receive the hop limit */
#ifdef IPV6_RECVHOPLIMIT
    /* RFC 3542 */
    setsockopt(nodes[index].sockfd, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, &on, sizeof(on));
#else
    /* RFC 2292 */
    setsockopt(sockfd, IPPROTO_IPV6, IPV6_HOPLIMIT, &on, sizeof(on));
#endif
}
void proc_v6(char *ptr, ssize_t len, struct msghdr *msg, struct timeval *tvrecv, int index)
{
    printf("in procv6");
    double rtt;
    struct icmp6_hdr *icmp6;
    struct timeval *tvsend;
    struct cmsghdr *cmsg;
    int hlim;
    icmp6 = (struct icmp6_hdr *)ptr;
    if (len < 8)
        return; /* malformed packet */
    if (icmp6->icmp6_type == ICMP6_ECHO_REPLY)
    {
        if (icmp6->icmp6_id != getpid() & index & 0xffff)
            return; /* not a response to our ECHO_REQUEST */
        if (len < 16)
            return; /* not enough data to use */
        tvsend = (struct timeval *)(icmp6 + 1);
        tv_sub(tvrecv, tvsend);
        rtt = tvrecv->tv_sec * 1000.0 + tvrecv->tv_usec / 1000.0;
        printf("rtt %f\n", rtt);
        if (rtt < 0.0)
            return;
        hlim = -1;
        for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL;
             cmsg = CMSG_NXTHDR(msg, cmsg))
        {
            if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_HOPLIMIT)
            {
                hlim = *(u_int32_t *)CMSG_DATA(cmsg);
                break;
            }
        }
        printf("%ld bytes from %s: seq=%u, hlim=",
               len, nodes[index].hostIP, icmp6->icmp6_seq);
        if (hlim == -1)
            printf("???"); /* ancillary data missing */
        else
            printf("%d", hlim);
        printf(", rtt=%.3f ms\n", rtt);
        nodes[index].rtt[nodes[index].recNo] = rtt;
        nodes[index].recNo++;
        if (nodes[index].recNo == 3)
        {
            printf("%s\t\t%.3f ms  %.3f ms  %.3f ms\n", nodes[index].hostIP, nodes[index].rtt[0], nodes[index].rtt[2], nodes[index].rtt[2]);
            nodes[index]
                .isSet = 0;
            activeNodes--;
            free(nodes[index].ai);
            free(nodes[index].pr);
            close(nodes[index].sockfd);
        }
    }
    else if (verbose)
    {
        printf(" %ld bytes from %s: type = %d, code = %d\n",
               len, Sock_ntop_host(nodes[index].pr->sarecv, nodes[index].pr->salen),
               icmp6->icmp6_type, icmp6->icmp6_code);
    }
}
void sig_alrm(int signo)
{
    for (int i = 0; i < MAX_EVENTS; i++)
    {
        if (nodes[i].isSet == 1)
            (nodes[i].pr->fsend)(i);
    }
    //printf("in sig alarm\n");
    time(&end);
    if (end - start >= MAX_TIME)
    {
        printf("time exceeded. ending !! \n");
        exit(0);
    }
    alarm(5);
    return;
}
// void sig_int(int signo)
// {
//     // for (int i = 3; i < 1024; i++)
//     //     fclose(&i);
//     fcloseall();
//     exit(0);
// }
void send_v4(int index) //////////////add pid, nsent , sockfd pr
{
    int len;
    struct icmp *icmp1;
    icmp1 = (struct icmp *)sendbuf;
    icmp1->icmp_type = ICMP_ECHO;
    icmp1->icmp_code = 0;
    icmp1->icmp_id = getpid() & index & 0xffff;
    icmp1->icmp_seq = nodes[index].ping_no++;
    memset(icmp1->icmp_data, 0xa5, datalen); /* fill with pattern */
    gettimeofday((struct timeval *)icmp1->icmp_data, NULL);
    len = 8 + datalen; /* checksum ICMP header and data */
    icmp1->icmp_cksum = 0;
    icmp1->icmp_cksum = in_cksum((short *)icmp1, len);
    sendto(nodes[index].sockfd, sendbuf, len, 0, nodes[index].pr->sasend, nodes[index].pr->salen);
    //printf("Sent message number %d to host %s on socket %d\n", nodes[index].ping_no, nodes[index].hostIP,nodes[index].sockfd);
}
uint16_t in_cksum(uint16_t *addr, int len)
{
    int nleft = len;
    uint32_t sum = 0;
    uint16_t *w = addr;
    uint16_t answer = 0;
    /*
* Our algorithm is simple, using a 32 bit accumulator (sum), we add
 * sequential 16 bit words to it, and at the end, fold back all the
 * carry bits from the top 16 bits into the lower 16 bits.
 */
    while (nleft > 1)
    {
        sum += *w++;
        nleft -= 2;
    }
    /* mop up an odd byte, if necessary */
    if (nleft == 1)
    {
        *(unsigned char *)(&answer) = *(unsigned char *)w;
        sum += answer;
    }
    /* add back carry outs from top 16 bits to low 16 bits */
    sum = (sum >> 16) + (sum & 0xffff); /* add hi 16 to low 16 */
    sum += (sum >> 16);                 /* add carry */
    answer = ~sum;                      /* truncate to 16 bits */
    return (answer);
}
void send_v6(int index)
{
    //printf("in send v6\n");
    int len;
    struct icmp6_hdr *icmp6;
    icmp6 = (struct icmp6_hdr *)sendbuf;
    icmp6->icmp6_type = ICMP6_ECHO_REQUEST;
    icmp6->icmp6_code = 0;
    icmp6->icmp6_id = getpid() & index & 0xffff;
    icmp6->icmp6_seq = nodes[index].ping_no++;
    memset((icmp6 + 1), 0xa5, datalen); /* fill with pattern */
    gettimeofday((struct timeval *)(icmp6 + 1), NULL);
    len = 8 + datalen; /* 8-byte ICMPv6 header */
    sendto(nodes[index].sockfd, sendbuf, len, 0, nodes[index].pr->sasend, nodes[index].pr->salen);
    //printf("sent v6");
    /* kernel calculates and stores checksum for us */
}