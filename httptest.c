#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>

char *build_get_query(char *host, char *page);
int readn(int fd, void *vptr, int n);
void handle_result();

#define PORT 80
#define USERAGENT "HTTPTEST 1.0"
#define PAGE "index.html"
#define HOST "mcdoing"
#define HEADSIZE 200

static int http_status_counts[1000];	/* room for all three-digit statuses */
int main(int argc, char **argv)
{
    struct sockaddr_in *remote;
    int sock;
    int tmpres;
    char *ip;
    char *get;
    char buf[BUFSIZ+1];
    char *host = HOST;
    char *page;

    page = PAGE;


    ip = "127.0.0.1";
    remote = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in *));
    remote->sin_family = AF_INET;
    remote->sin_port = htons(PORT);
    get = build_get_query(host, page);

    /* Initialize the HTTP status-code histogram. */
    for (int i = 0; i < 1000; ++i )
        http_status_counts[i] = 0;

    for (int i=0; i<10000; i++) {
        tmpres = inet_pton(AF_INET, ip, (void *)(&(remote->sin_addr.s_addr)));
        if( tmpres < 0)  {
            perror("Can't set remote->sin_addr.s_addr");
            exit(1);
        } else if(tmpres == 0) {
            fprintf(stderr, "%s is not a valid IP address\n", ip);
            exit(1);
        }

        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
            perror("Can't create TCP socket");
            exit(1);
        }
        if(connect(sock, (struct sockaddr *)remote, sizeof(struct sockaddr)) < 0){
            perror("Could not connect");
            exit(1);
        }

        int sent = 0;
        //Send the query to the server
        sent = 0;
        while(sent < strlen(get)) {
            tmpres = send(sock, get+sent, strlen(get)-sent, 0);
            if(tmpres == -1){
                perror("Can't send query");
                exit(1);
            }
            sent += tmpres;
        }
    
        //now it is time to receive the page
        memset(buf, 0, sizeof(buf));
        if((tmpres = readn(sock, buf, HEADSIZE)) > 0) {
            http_status_counts[atoi(&buf[9])]++;
            memset(buf, 0, tmpres);
        }
        close(sock);
    }

    handle_result();
    free(get);
    free(remote);
    return;
}

char *build_get_query(char *host, char *page)
{
    char *query;
    char *getpage = page;
    char *tpl = "HEAD /%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n";
    if(getpage[0] == '/'){
        getpage = getpage + 1;
        fprintf(stderr,"Removing leading \"/\", converting %s to %s\n", page, getpage);
    }
    // -5 is to consider the %s %s %s in tpl and the ending \0
    query = (char *)malloc(strlen(host)+strlen(getpage)+strlen(USERAGENT)+strlen(tpl)-5);
    sprintf(query, tpl, getpage, host, USERAGENT);
    printf("string query length: %d\n", strlen(query));
    printf("%s",query);
    return query;
}

int readn(int fd, void *vptr, int n)
{
	int	nleft;
	int	nread;
	char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nread = read(fd, ptr, nleft)) < 0) {
			if (errno == EINTR)
				nread = 0;		/* and call read() again */
			else
				return(-1);
		} else if (nread == 0)
			break;				/* EOF */

		nleft -= nread;
		ptr   += nread;
	}
	return(n - nleft);		/* return >= 0 */
}

void handle_result()
{
    printf("HTTP response codes:\n");
    for (int i = 0; i < 1000; ++i )
	if ( http_status_counts[i] > 0 )
	    (void) printf( "  code %03d -- %d\n", i, http_status_counts[i] );
}
