#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>

#include "timers.h"

typedef struct {
    int conn_fd;
    int conn_state;
    int sa_len;
    struct sockaddr_in sa;
    int did_connect, did_response;
} connection;
typedef struct {
    char *hostname;    /* need free */
    unsigned short port;
    char *filename;    /* need free */
    int sa_len, sock_family, sock_type, sock_protocol;
    struct sockaddr_in sa;
} url;

#define max(a,b) ((a)>=(b)?(a):(b))
#define min(a,b) ((a)<=(b)?(a):(b))

#define CNST_FREE 0
#define CNST_CONNECTING 1
#define CNST_READING 2
#define CNST_DONE 4
#define RESERVED_FDS 10
#define HEAD_CMD "HEAD %s HTTP/1.0\r\n\r\n"
#define LOCATE_STR "HTTP/1"

/* How often to show connection state */
#define UPDATE_SECS 1
/* How often to show connection rate  */
#define RATE_SHOW_SECS 5

/* max_conns: maximum number of conn at any point */
/* cur_conns: number of conn kept currently */
/* max_para: maximum number of conns in fact  */
static connection* connections; /* need free */
static int max_conns, cur_conns, max_para;
static int total_conns_start, total_conns_start_old;
static int failed_conns, succeeded_conns;
static url* urls;               /* need free */
static char headcmd[100];
static long start_interval;     /* connect frequency */
static int maxfd;               /* highest-numbered file descriptor */

static void sig_handler(int sig);
static void usage();
static void read_url_file(char* url_file);
static void start_connection(struct timeval *nowP);
static void start_socket(int cnum, struct timeval *nowP);
static void handle_connect(int cnum, struct timeval *nowP, int double_check);
static void handle_read(int cnum, struct timeval *nowP);
static void close_connection(int cnum);
static void print_statistics(struct timeval *nowP);
static void show_conn_rate(struct timeval *nowP);
int main( int argc, char *argv[])
{
    /* Parse args. */
    int opt, rate;
    char *url_file;
    if (argc != 5)
        usage();
    while ( (opt = getopt(argc, argv, "r:u:")) != -1) {
        switch (opt) {
        case 'r':
            rate = atoi(optarg);
            break;
        case 'u':
            url_file = optarg;
            break;
        default:
            break;
        }
    }

    int cnum;
    struct timeval now, timeout;
    int i, r, n;
    fd_set rfdset, wfdset;
    read_url_file( url_file );

    /* Initialize the connections table */
    max_conns = 512 - RESERVED_FDS;
    connections = (connection *) malloc(max_conns * sizeof(connection));
    for (cnum = 0; cnum < max_conns; cnum++)
        connections[cnum].conn_state = CNST_FREE;

    /* Initialize the statistics */
    cur_conns = max_para = 0;
    total_conns_start = total_conns_start_old = 0;
    failed_conns = succeeded_conns = 0;

    /* Set timers */
    tmr_init();
    gettimeofday(&now, NULL);
    tmr_create(&now, print_statistics, UPDATE_SECS * 1000000L, 1);
    tmr_create(&now, show_conn_rate, RATE_SHOW_SECS * 1000000L, 1);

    start_interval = 1000000L / rate;
    tmr_create(&now, start_connection, start_interval, 1);
    
    /* Main loop */
    while(1) {
        /* Build the fdsets. */
        FD_ZERO(&rfdset);
        FD_ZERO(&wfdset);
        maxfd = 1;
        for(cnum = 0; cnum < max_conns; ++cnum)
            switch (connections[cnum].conn_state) {
            case CNST_CONNECTING:
                FD_SET(connections[cnum].conn_fd, &wfdset);
                if (connections[cnum].conn_fd > maxfd)
                    maxfd = connections[cnum].conn_fd;
                break;
            case CNST_READING:
                FD_SET(connections[cnum].conn_fd, &rfdset);
                if (connections[cnum].conn_fd > maxfd)
                    maxfd = connections[cnum].conn_fd;
                break;
            }
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;
        r = select(maxfd, &rfdset, &wfdset, NULL, &timeout);
        if(r<0) {
            perror("select");
            exit(1);
        }
        gettimeofday(&now, NULL);

        /* Service them. */
        for(cnum = 0; cnum < max_conns; ++cnum)
            switch (connections[cnum].conn_state) {
            case CNST_CONNECTING:
                if( FD_ISSET(connections[cnum].conn_fd, &wfdset) )
                    handle_connect(cnum, &now, 1);
                break;
            case CNST_READING:
                if( FD_ISSET(connections[cnum].conn_fd, &rfdset) )
                    handle_read(cnum, &now);
                break;
            }
        /* Run the timers */
        tmr_run(&now);
    }
}

static void usage()
{
    printf("example: httptest -r 32 -u url_file\n");
}

static void read_url_file(char *url_file)
{
    FILE *fp;
    char line[500],hostname[500];
    char portstr[10];
    char *http = "http://";
    int http_len = strlen(http);
    int host_len;
    int n;
    char *cp;
    struct addrinfo hint, *ai;

    fp = fopen(url_file, "r");
    if (fp == (FILE*) 0) {
        perror(url_file);
        exit(1);
    }
    if ( (fgets (line, sizeof(line), fp)) == (char*) 0 ) {
        perror("file is empty");
        exit(1);
    }
    
    if ( line[strlen( line ) - 1] == '\n' )
        line[strlen( line ) - 1] = '\0';

    urls = (url*) malloc(sizeof(url));

    /* Parse it. */
    for (cp = line + http_len;
         *cp != '\0' && *cp != ':' && *cp != '/'; ++cp)
        ;
    host_len = cp -line;
    host_len -= http_len;
    strncpy(hostname, line + http_len, host_len);
    hostname[host_len] = '\0';
    urls->hostname = strdup(hostname);
    if (*cp == ':') {
        urls->port = (unsigned short) atoi(++cp);
        while (*cp != '\0' && *cp != '/')
            ++cp;
    }
    else
        urls->port = 80;
    if (*cp == '\0')
        urls->filename = strdup("/");
    else
        urls->filename = strdup(cp);

    bzero(&hint, sizeof(struct addrinfo));
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    (void) snprintf( portstr, sizeof(portstr), "%d", (int) urls->port );
    if ( (n = getaddrinfo(hostname, portstr, &hint, &ai)) != 0) {
        (void) fprintf(
                       stderr, "httptest: getaddrinfo %s - %s\n", hostname,
                       gai_strerror( n ) );
	exit( 1 );
    }

    while (ai == NULL || ai->ai_family != AF_INET) 
        ai = ai->ai_next;
    
    urls->sock_family = ai->ai_family;
    urls->sock_type = ai->ai_socktype;
    urls->sock_protocol = ai->ai_protocol;
    urls->sa_len = ai->ai_addrlen;
    memmove (&urls->sa, ai->ai_addr, ai->ai_addrlen);

    freeaddrinfo(ai);
    return;
}

static void sig_handler(int sig)
{
    if(sig == SIGINT){
        free(urls->filename);
        free(urls->hostname);
        free(urls);
        free(connections);
        tmr_destroy();
        exit(0);
    }
}

static void start_connection(struct timeval *nowP)
{
    int cnum, url_num;

    for(cnum = 0; cnum < max_conns; ++cnum)
        if (connections[cnum].conn_state == CNST_FREE) {
            start_socket(cnum, nowP);
            if (connections[cnum].conn_state != CNST_FREE) {
                ++cur_conns;
                if(cur_conns > max_para)
                    max_para = cur_conns;
            }
            total_conns_start++;
            return;
        }
    /* No slots left */
    fprintf(stderr, "httptest: ran out of connection slots\n");
    exit(0);
}

static void start_socket(int cnum, struct timeval *nowP)
{
    int flags;

    connections[cnum].conn_fd = socket(urls->sock_family,
                                       urls->sock_type,
                                       urls->sock_protocol);
    if(connections[cnum].conn_fd < 0) {
        perror("url: create socket error");
        return;
    }

    /* Set the file descriptor to non-block mode. */
    flags = fcntl(connections[cnum].conn_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("Set fd to non-block error");
        close(connections[cnum].conn_fd);
        return;
    }
    if (fcntl(connections[cnum].conn_fd,
              F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("Set fd to non-block error");
        close (connections[cnum].conn_fd);
        return;
    }

    /* Connect to the host. */
    connections[cnum].sa_len = urls->sa_len;
    memmove((void *)&connections[cnum].sa,
            (void *)&urls->sa, urls->sa_len);
    if ( connect(connections[cnum].conn_fd,
                 (struct sockaddr*) &connections[cnum].sa,
                 connections[cnum].sa_len) ) {
        if (errno = EINPROGRESS) {
            connections[cnum].conn_state = CNST_CONNECTING;
            return;
        }
        else {
            perror ("connect error");
            close(connections[cnum].conn_fd);
            failed_conns++;
            return;
        }
    }
    /* Connect succeeded instantly, so handle it now. */
    gettimeofday(nowP, NULL);
    handle_connect(cnum, nowP, 0);
}

static void handle_connect(int cnum, struct timeval *nowP, int double_check)
{
    char buf[600];
    int bytes, r;
    if (double_check) {
        /* Check to make sure the non-blocking connect succeeded. */
        int err, errlen;

        if ( connect(connections[cnum].conn_fd,
                     (struct sockaddr*) &connections[cnum].sa,
                     connections[cnum].sa_len) < 0 ) {
            switch (errno) {
            case EISCONN:       /* OK */
                break;
            case EINVAL:
                errlen = sizeof(err);
                if ( getsockopt( connections[cnum].conn_fd,
                                 SOL_SOCKET, SO_ERROR,
                                 (void*) &err, &errlen ) < 0 )
                    fprintf(stderr, "connect error\n");
                else
                    fprintf(stderr, "connect error: %s\n", strerror(err));
                close_connection(cnum);
                failed_conns++;
                return;
            }
        }
    }
    connections[cnum].did_connect = 1;

    /* Format the request */
    bytes = snprintf(buf, sizeof(buf), HEAD_CMD, urls->filename);
    /* Send the request */
    r = write(connections[cnum].conn_fd, buf, bytes);
    if (r < 0) {
        perror("Send the HEAD request error");
        close_connection(cnum);
        failed_conns++;
        return;
    }
    connections[cnum].conn_state = CNST_READING;
}

static void handle_read(int cnum, struct timeval* nowP)
{
    char buf[20], *p;
    int bytes_read;
    if (!connections[cnum].did_response)
        connections[cnum].did_response = 1;

    bytes_read = read(connections[cnum].conn_fd, buf, 20);
    if (bytes_read <= 0) {
        close_connection(cnum);
        failed_conns++;
        return;
    }

    /* Handle read content */
    if ( (p = strstr(buf, LOCATE_STR)) != NULL ) {
        if(atoi(&p[9]) == 200)
            succeeded_conns++;
        else
            failed_conns++;
        close_connection(cnum);
    }
}

static void close_connection(int cnum)
{
    close(connections[cnum].conn_fd);
    connections[cnum].conn_state = CNST_FREE;
    --cur_conns;
}

static void print_statistics(struct timeval *nowP)
{
    printf("Current connections: %d\n", cur_conns);
    printf("Total connections:%d\n", total_conns_start);
    printf("Succeeded connections: %d\n", succeeded_conns);
    printf("Failed connections: %d\n\n", failed_conns);
}

static void show_conn_rate(struct timeval *nowP)
{
    printf( "\nFor the past 5s, connection rate is %d/s\n\n",
           (total_conns_start - total_conns_start_old) / 5 );
    total_conns_start_old = total_conns_start;
}
