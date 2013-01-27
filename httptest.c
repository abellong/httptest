#include <ncurses.h>
#include <panel.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
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

#define INFO_MODE 1
#define EDIT_MODE 0
#define NLINES 22
#define NCOLS 76

/* max_conns: maximum number of conn at any point */
/* cur_conns: number of conn kept currently */
/* max_para: maximum number of conns in fact  */
static int rate;
static FILE *log;
static connection* connections; /* need free */
static int max_conns, cur_conns, max_para;
static int total_conns_start, total_conns_start_old;
static int failed_conns, succeeded_conns;
static url* urls;               /* need free */
static long start_interval;     /* connect frequency */
static int maxfd;               /* highest-numbered file descriptor */
static WINDOW *info_win, *edit_win;
static PANEL  *info_panel, *edit_panel;
static int mode;

static void sig_handler(int sig);
static void usage();
static void read_url_file(char* url_file);
static void init_wins();
static void win_show(WINDOW *win, char *label);
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
    int opt;
    char *url_file;
    log = fopen("httptest.log", "w");
    if (argc != 5)
        usage();
    while ( (opt = getopt(argc, argv, "r:u:")) != -1) {
        switch (opt) {
        case 'r':
            rate = atoi(optarg);
            if (rate > 30000) {
                fprintf(log, "Error: rate must be at most 30000\n");
                exit(1);
            }
            if (rate < 1) {
                fprintf(log, "Error: rate must be at least 1\n");
                exit(1);
            }
            break;
        case 'u':
            url_file = optarg;
            break;
        default:
            break;
        }
    }

    int cnum, flags;
    Timer *t;
    struct timeval now, timeout;
    int r;
    fd_set rfdset, wfdset;
#ifdef RLIMIT_NOFILE
    struct rlimit limits;
#endif /* RLIMIT_NOFILE */
    int ch;

    /* Initialize curses */
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    /* Initialize all the colors */
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_BLUE, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);

    init_wins();
    
    read_url_file( url_file );

    /* Attach a panel to each window */
    info_panel = new_panel(info_win);
    edit_panel = new_panel(edit_win);

    /* Set up the user pointers to the next panel */
    set_panel_userptr(info_panel, edit_panel);
    set_panel_userptr(edit_panel, info_panel);

    /* Update the stacking order. */
    update_panels();

    /* Show it on the screen */
    attron(COLOR_PAIR(4));

    /* Initialize the connections table */
    max_conns = 1024 - RESERVED_FDS;
#ifdef RLIMIT_NOFILE
    /* Try and increase the limit on # of files to the maximum. */
    if ( getrlimit( RLIMIT_NOFILE, &limits ) == 0 )
	{
            if ( limits.rlim_cur != limits.rlim_max )
                {
                    if ( limits.rlim_max == RLIM_INFINITY )
                        limits.rlim_cur = 8192;		/* arbitrary */
                    else if ( limits.rlim_max > limits.rlim_cur )
                        limits.rlim_cur = limits.rlim_max;
                    (void) setrlimit( RLIMIT_NOFILE, &limits );
                }
            max_conns = limits.rlim_cur - RESERVED_FDS;
	}
#endif /* RLIMIT_NOFILE */
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
    t = tmr_create(&now, start_connection, start_interval, 1);

    top_panel(info_panel);
    update_panels();
    doupdate();
    signal(SIGINT,sig_handler);

    /* Set the file descriptor stdin to non-block mode. */
    flags = fcntl(0, F_GETFL, 0);
    if (flags == -1) {
        fprintf( log, "Set stdin to non-block error: %s\n", strerror(errno) );
        /* perror("Set stdin to non-block error"); */
        return 1;
    }
    if (fcntl(0, F_SETFL, flags | O_NONBLOCK) < 0) {
        fprintf(log, "Set stdin to non-block error: %s\n", strerror(errno));
        /* perror("Set stdin to non-block error"); */
        return 1;
    }
    mode = INFO_MODE;
    /* Main loop */
    while(1) {
        if (mode == INFO_MODE) {
            cbreak();
            if ((ch = getchar()) == 'c') {
                mode = EDIT_MODE;
                mvwprintw(edit_win, 4, 4, "Change the desired connect rate");
                mvwprintw(edit_win, 7, 6, "Please input an integer more than 1 and less than 30000.");
                mvwprintw(edit_win, 8, 6, "Note that the number you input will not be echoed");
                mvwprintw(edit_win, 10, 6, "rate: ");
                top_panel(edit_panel);
                update_panels();
                doupdate();
            }
        }
        if (mode == EDIT_MODE) {
            /* if ((ch = getchar()) == 'c') { */
            nocbreak();
            if (scanf("%d", &rate) == 1) {
                if (rate > 30000) {
                    fprintf(log, "Error: rate must be at most 30000\n");
                    exit(1);
                }
                if (rate < 1) {
                    fprintf(log, "Error: rate must be at least 1\n");
                    exit(1);
                }
                start_interval = 1000000L / rate;
                t->msecs = start_interval;
                gettimeofday(&now, NULL);
                tmr_reset(&now, t);
                mode = INFO_MODE;
                mvwprintw(info_win, 14, 8, "Desired connect rate: %5d/s", rate);
                refresh();
                top_panel(info_panel);
                update_panels();
                doupdate();
            }
        }
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
            fprintf(log, "select error: %s\n", strerror(errno));
            /* perror("select here"); */
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
    printf("usage: httptest -r N -u URL_FILE\n\
for example: ./httptest -r 2000 -u urls\n");
    exit(0);
}

static void read_url_file(char *url_file)
{
    FILE *fp;
    char line[500],hostname[500];
    char portstr[10];
    char *http = "http://";
    int http_len = strlen(http);
    int host_len;
    int n,tmp;
    char *cp;
    struct addrinfo hint, *ai;

    fp = fopen(url_file, "r");
    if (fp == (FILE*) 0) {
        fprintf(log, "%s: %s\n", url_file, strerror(errno));
        /* perror(url_file); */
        exit(1);
    }
    if ( (fgets (line, sizeof(line), fp)) == (char*) 0 ) {
        fprintf(log, "file is empty: %s\n", strerror(errno));
        /* perror("file is empty"); */
        exit(1);
    }
    
    if ( line[strlen( line ) - 1] == '\n' )
        line[strlen( line ) - 1] = '\0';
    if ( (tmp = COLS-strlen(line)-15) > 1 ) 
    mvwprintw(info_win, 3, tmp, "url: %s", line);
    else
        mvwprintw(info_win, 3, COLS - strlen("......") - 15, "url: %s", "......");
    refresh();

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
                       log, "httptest: getaddrinfo %s - %s\n", hostname,
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
        endwin();
        fclose(log);
        printf("maximum connections in parallel: %5d\n", max_para);
        exit(0);
    }
}

static void init_wins()
{
    char label[] = "httptest";
    int x, y;
    if (LINES <= NLINES) {
        fprintf(log, "Terminal too small\n");
        exit(1);
    }
    y = (LINES - NLINES) / 2;
    x = (COLS - NCOLS) / 2;
    info_win = newwin(NLINES, NCOLS, y, x);
    win_show(info_win, label);
    edit_win = newwin(NLINES, NCOLS, y, x);
    win_show(edit_win, label);
}

static void win_show(WINDOW *win, char *label)
{
    int x, height, width, length;
    float temp;

    getmaxyx(win, height, width);

    box(win, 0, 0);
    /* mvwaddch(win, 2, 0, ACS_LTEE); */
    /* mvwhline(win, 2, 1, ACS_HLINE, width - 2); */
    /* mvwaddch(win, 2, width - 1, ACS_RTEE); */

    length = strlen(label);
    temp = (width - length)/ 2;
    x = (int)temp;
    wattron(win, COLOR_PAIR(2));
    mvwprintw(win, 1, x, "%s", label);
    wattroff(win, COLOR_PAIR(1));
    if (win == info_win) {
        mvwprintw(info_win, 4, 4, "Connect status");
        mvwprintw(info_win, 6, 8,
                  "Current connections: %5d", cur_conns);
        mvwprintw(info_win, 7, 8,
                  "Total connections: %5d", total_conns_start);
        mvwprintw(info_win, 8, 8,
                  "Succeeded connections: %5d", succeeded_conns);
        mvwprintw(info_win, 9, 8,
                  "Failed connections: %5d", failed_conns);
        mvwprintw(info_win, 11, 8,
                  "Success rate:   0%%");

        struct tm *local;
        time_t t;
        t=time(NULL);
        local=localtime(&t);
        mvwprintw(info_win, 13, 8,
                  "For the past 5s, connection rate is %5d/s   (%02d:%02d:%02d)",
                  (total_conns_start - total_conns_start_old) / 5,
                  local->tm_hour,
                  local->tm_min,
                  local->tm_sec);
        mvwprintw(info_win, 14, 8, "Desired connect rate: %5d/s", rate);

        mvwprintw(info_win, 16, 4, "NOTE");
        mvwprintw(info_win, 16, 10, "1. Please refer to the man page for the precise definitions");
        mvwprintw(info_win, 17, 13, "of the above terms");
        mvwprintw(info_win, 18, 10, "2. To change the desired connect rate, press 'c'");
        refresh();
    }
}

static void print_statistics(struct timeval *nowP)
{
    if (mode == INFO_MODE) {
        mvwprintw(info_win, 6, 29, "%5d", cur_conns);
        mvwprintw(info_win, 7, 27, "%5d", total_conns_start);
        mvwprintw(info_win, 8, 31, "%5d", succeeded_conns);
        mvwprintw(info_win, 9, 28, "%5d", failed_conns);
        if (succeeded_conns||failed_conns)
            mvwprintw(info_win, 11, 22, "%5d%%",
                      100*succeeded_conns/(succeeded_conns+failed_conns));

        refresh();
        update_panels();
        doupdate();
    }
}

static void show_conn_rate(struct timeval *nowP)
{
    if (mode == INFO_MODE) {
        struct tm *local;
        time_t t;
        t=time(NULL);
        local=localtime(&t);
        mvwprintw(info_win, 13, 8,
                  "For the past 5s, connection rate is %5d/s   (%02d:%02d:%02d)",
                  (total_conns_start - total_conns_start_old) / 5,
                  local->tm_hour,
                  local->tm_min,
                  local->tm_sec);
        mvwprintw(info_win, 14, 8, "Desired connect rate: %5d/s", rate);        
        refresh();
        update_panels();
        doupdate();
        total_conns_start_old = total_conns_start;
    }
}

static void start_connection(struct timeval *nowP)
{
    int cnum;

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
    fprintf(log, "httptest: ran out of connection slots\n");
    exit(1);
}

static void start_socket(int cnum, struct timeval *nowP)
{
    int flags;

    connections[cnum].conn_fd = socket(urls->sock_family,
                                       urls->sock_type,
                                       urls->sock_protocol);
    if(connections[cnum].conn_fd < 0) {
        fprintf(log, "url: create socket error: %s\n", strerror(errno));
        /* perror("url: create socket error"); */
        return;
    }

    /* Set the file descriptor to non-block mode. */
    flags = fcntl(connections[cnum].conn_fd, F_GETFL, 0);
    if (flags == -1) {
        fprintf(log, "Set fd to non-block error: %s\n", strerror(errno));
        /* perror("Set fd to non-block error"); */
        close(connections[cnum].conn_fd);
        return;
    }
    if (fcntl(connections[cnum].conn_fd,
              F_SETFL, flags | O_NONBLOCK) < 0) {
        fprintf(log, "Set fd to non-block error: %s\n", strerror(errno));
        /* perror("Set fd to non-block error"); */
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
            fprintf(log, "connect error: %s\n", strerror(errno));
            /* perror ("connect error"); */
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
                    fprintf(log, "connect error\n");
                else
                    fprintf(log, "connect error: %s\n", strerror(err));
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
        fprintf(log, "Send HEAD request error: %s\n", strerror(errno));
        /* perror("Send the HEAD request error"); */
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

