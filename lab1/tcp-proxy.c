
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <event.h>

#define MAX_CACHED 8000

unsigned int daddr;
unsigned short dport;
const char * addr2;
int port2;

//Sets a socket descriptor to nonblocking mode
void setnonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
}

//Translates hostname to IP address
int hostname_to_ip(char *hostname , char *ip)
{
	struct hostent *he;
	struct in_addr **addr_list;
	int i;

	if((he = gethostbyname(hostname)) == NULL)
	{
		// get the host info
		herror("gethostbyname");
		return 1;
	}

	addr_list = (struct in_addr **) he->h_addr_list;

	for(i = 0; addr_list[i] != NULL; i++)
	{
		//Return the first one;
		strcpy(ip , inet_ntoa(*addr_list[i]) );
		return 0;
	}

	return 1;
}

void readcb(struct bufferevent *bev, void *ptr)
{
    struct evbuffer *input, *output;
    struct bufferevent *otherbev = (struct bufferevent *) ptr;
    input = bufferevent_get_input(bev);
    output = bufferevent_get_output(otherbev);
    int res = evbuffer_add_buffer(output, input);
    fprintf(stdout,"copying between buffers %d\n", res);
}

void eventcb(struct bufferevent *bev, short events, void *ptr)
{
    fprintf(stdout,"got event! from bev %p\n", bev);
    if (events & BEV_EVENT_CONNECTED) {
        /* We're connected to 127.0.0.1:8080.   Ordinarily we'd do
           something here, like start reading or writing. */
        fprintf(stdout,"we connected******)()()()()()(\n");
    } else if (events & BEV_EVENT_ERROR) {
        /* An error occured while connecting. */
        fprintf(stdout,"An error occured while connecting.\n");
    } else if (events & BEV_EVENT_EOF) {
        struct bufferevent * otherside = (struct bufferevent *) ptr;
        int flushres = bufferevent_flush(otherside, EV_WRITE, BEV_FINISHED);
        evutil_socket_t fd = bufferevent_getfd(otherside); // send shutdown to other side
        fprintf(stdout,"got eof flush result %d, fd %d\n", flushres, fd);
        //fprintf(stdout,"got eof %d\n", flushres);
    } else if (events & BEV_EVENT_TIMEOUT) {
        fprintf(stdout,"got timeout\n");
    } else if (events & BEV_EVENT_WRITING) {
        fprintf(stdout,"got event write\n");
    } else if (events & BEV_EVENT_READING) {
        fprintf(stdout,"got event reading\n");
    } else {
        fprintf(stdout,"some other error?\n");
    }
}

void accept_cb(evutil_socket_t fd, short what, void *base)
{
    struct event_base *b= base;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int accepted = accept(fd, (struct sockaddr*)&ss, &slen);
    fprintf(stdout,"got connection accepted %d\n", accepted);
    if (accepted < 0) {
        perror("accept");
    } else if (accepted > FD_SETSIZE) {
        fprintf(stdout,"closing!\n");
        close(accepted);
    } else {
        struct bufferevent *sourcebev, *destbev;
        struct sockaddr_in destaddr;
        int connresult;
        struct evdns_base *dns_base;

        dns_base = evdns_base_new(b, 1);// temp greg stuff


        // stuff for dest
        destbev = bufferevent_socket_new(b, -1, BEV_OPT_CLOSE_ON_FREE);
        // stuff for source
        evutil_make_socket_nonblocking(accepted);
        sourcebev = bufferevent_socket_new(b, accepted, BEV_OPT_CLOSE_ON_FREE);
        //stuff for dest2;
        bufferevent_setcb(destbev, readcb, NULL, eventcb, sourcebev);
        bufferevent_setwatermark(sourcebev, EV_READ|EV_WRITE, 0, MAX_CACHED);
        bufferevent_enable(destbev, EV_READ|EV_WRITE);
        // stuff for source2
        bufferevent_setcb(sourcebev, readcb, NULL, eventcb, destbev);
        bufferevent_setwatermark(sourcebev, EV_READ|EV_WRITE, 0, MAX_CACHED);
        bufferevent_enable(sourcebev, EV_READ|EV_WRITE);
        fprintf(stdout,"starting buffevent\n");
        // make connection to destination of proxy and add
        connresult = bufferevent_socket_connect_hostname(destbev, dns_base, AF_UNSPEC, addr2, port2);
        fprintf(stdout,"dest connect result %d\n", connresult);
    }
}

int main(int argc, char **argv)
{
	int socketlisten;
	struct sockaddr_in addresslisten;
	int reuse = 1;
    fprintf(stdout,"Welcome to Greg's super-proxy!\n");

	if(argc != 4)
	{
		printf("Usage: %s destination-host destination-port listening-port\n", argv[0]);
		return 0;
	}
    addr2 = argv[1];
    /*
	daddr = inet_addr(argv[1]);

	if(daddr == INADDR_NONE)
	{
		char temp[100];
		hostname_to_ip(argv[1], temp);
		daddr = inet_addr(temp);
	}

	dport = atoi(argv[2]);
    */
	port2 = atoi(argv[2]);

	event_init();
	struct event_base *base = event_base_new();

	//Create listening socket
	socketlisten = socket(AF_INET, SOCK_STREAM, 0);
	if (socketlisten < 0)
	{
		fprintf(stderr,"Failed to create listen socket");
		return 1;
	}

	//Init listening structure
	memset(&addresslisten, 0, sizeof(addresslisten));

	addresslisten.sin_family = AF_INET;
	addresslisten.sin_addr.s_addr = INADDR_ANY;
	addresslisten.sin_port = htons(atoi(argv[3]));

	//Binding
	if (bind(socketlisten,
			(struct sockaddr *)&addresslisten,
			sizeof(addresslisten)) < 0)
	{
		fprintf(stderr,"Failed to bind\n");
		return 1;
	}

	//Listen
	if (listen(socketlisten, 5) < 0)
	{
		fprintf(stderr,"Failed to listen to socket\n");
		return 1;
	}

	//Set some socket properties
	setsockopt(socketlisten, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	setnonblock(socketlisten);

	//Add accept callback
	struct event *accept_event = event_new(base, socketlisten, EV_READ | EV_PERSIST, accept_cb, base);
	event_add(accept_event, NULL);
	event_base_dispatch(base);

	//If dispatch returns for some reason....
	event_free(accept_event);
	close(socketlisten);
	return 0;
}
