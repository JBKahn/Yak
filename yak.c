#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include <netdb.h>
#include "peer.h"
#include "parsemessage.h"
#include "util.h"

#define DEFPORT 1234  /* (default both for connecting and for listening) */

unsigned long ipaddr = 0;  /* 0 means not known yet */
int myport = DEFPORT;
char myportstr[5];
int relaymax = 10;
int verbose = 0;
int maxfd = 0; 
fd_set fdlist;


int main(int argc, char **argv)
{
	int c;
	extern void doconnect(unsigned long ipaddr, int port);
	extern unsigned long hostlookup(char *host);
	extern int setup();
	extern int newconnection(int fd);
	extern int read_and_process(struct peer *p);
	extern void takeInput();

	while ((c = getopt(argc, argv, "p:c:v")) != EOF) {
		switch (c) {
			case 'p':
			if ((myport = atoi(optarg)) == 0) {
				fprintf(stderr, "%s: non-numeric port number\n", argv[0]);
				return(1);
			}
			break;
			case 'c':
			relaymax = atoi(optarg);
			break;
			case 'v':
			verbose = 1;
			break;
			default:
			fprintf(stderr, "usage: %s [-p port] [-c relaymax] [-v] [host [port]]\n", argv[0]);
			return(1);
		}
	}

	/* Save the port to a string, for use in the program to avoid conversion 
	every time. */
	sprintf(myportstr,"%d",myport);
	
	// Setup the listening socket.
	int fd = setup();


	struct peer *p;
	FD_ZERO(&fdlist);
	FD_SET(fd, &fdlist);
	FD_SET(0, &fdlist);

	if (optind < argc) {
		optind++;
		doconnect(hostlookup(argv[optind - 1]), 
			(optind < argc) ? atoi(argv[optind]) : DEFPORT);
	}

	if (listen(fd, 5)) {
		perror("listen");
		exit(1);
	}

	int error;
	fd_set rset;
	for (;;) {
		/* Using rset allows the program to avoid reseting the fdlist every 
		time it calls select. */
		rset = fdlist;
		if (select(maxfd + 1, &rset, NULL, NULL, NULL) < 0) {
			perror("select");
		} else {
			// The listening port has acitivty, we have a new peer.
			if (FD_ISSET(fd, &rset)) {
				if(( error = newconnection(fd)) == 1) {
					perror("new connection");
					return(1);
				}
				continue;
			}
			/* The stdin has acitivty.
			If the stdin is 0, we should print a list of peers, else we 
			should send the message off. */
			if (FD_ISSET(0, &rset)) {
				takeInput();
			}
			/* Cycle through the peers to see who has data for me, then I will 
			process it. */
			for (p = top_peer; p; p = p->next) {
				if (FD_ISSET(p->fd, &rset)) {
					if ((error = read_and_process(p)) == 1) {
						perror("read from peer");
						return(1);
					}
				}
			}
		}
	}
	return(0);
}

// Deals with the stdin given by the user.
void takeInput() {
	char typed[1024];
	char newline[1024];
	struct peer *p;

	fgets(typed, sizeof typed, stdin);
	// If stdin was just the return key, print the list of peers.
	if (typed[0] == '\n') {
		printf("%d peers:\n",count_peers());
		for (p = top_peer; p; p = p->next)
			printf("peer on fd %d is on port %d of %s\n",
				p->fd,p->port,format_ipaddr(p->ipaddr));
		printf("end of peer list\n");
		return;
	}
	/* Otherwise, we should put it in the form ipaddr,port;;message and sent it
	to a random peer. */
	memset(&newline[0],0,sizeof(newline));
	strcpy(newline,format_ipaddr(ipaddr));
	strcat(newline,",");
	strcat(newline,myportstr);
	strcat(newline, ";;");
	strcat(newline, typed);
	newline[strlen(newline) -1] = '\r';
	newline[strlen(newline)] = '\n';
	if (count_peers() > 0) {
		p = random_peer();
		write(p->fd,newline, strlen(newline));
	} else
		printf("No one to send to!\n");
	return;

}

// reads the data from the socket and processes it.
int read_and_process(struct peer *p)
{
	char line[1024];
	extern int analyze_banner(char *s, struct peer *p);
	extern int process(struct peer *p);
	extern char *myreadline(struct peer *p);
	int i;

	/* Get a pointer to the data if it's ready, otherwise points to null and 
	leaves data in the buffer. */
	char *s = myreadline(p);
	if (!s || strlen(s) <= 0)
		return(0);
	//printf("myreadline gave back_%s_\n", s);

	/* Checks if the line is a YAK line, if so then it sends to to analyze 
	banner without the YAK in front. */
	if (p->YAKflag==0 && strncmp(p->buf,"YAK",3) == 0) {
		p->YAKflag = 1;
		for(i=4;i<1023;i++)
			line[i-4] = p->buf[i];
		if (analyze_banner(line, p) == 1)
			perror("banner issues");
		return(0);
	}
	return process(p);
}

/* Taken directly from Prof Alan J Rosenthal's chat server program. 
Reads fromt he socket and checks to ensure it gets a full line before allowing
Yak to process it. */
char *myreadline(struct peer *p)
{
	int nbytes;
    /* move the leftover data to the beginning of buf */
	if (p->bytes_in_buf && p->nextpos)
		memmove(p->buf, p->nextpos, p->bytes_in_buf);

    /* If we've already got another whole line, return it without a read() */
	if ((p->nextpos = extractline(p->buf, p->bytes_in_buf))) {
		p->bytes_in_buf -= (p->nextpos - p->buf);
		return(p->buf);
	}

    /* Ok, try a read().  Note that we _never_ fill the buffer, so that there's
     * always room for a \0.*/
	nbytes = read(p->fd, p->buf + p->bytes_in_buf, 
		sizeof p->buf - p->bytes_in_buf - 1);
	
	if (nbytes <= 0) {
		if (nbytes == 0) {
			printf("Disconnecting fd %d, ipaddr %s, port %d\n", 
				p->fd, format_ipaddr(p->ipaddr), p->port);
			close(p->fd);
			FD_CLR(p->fd, &fdlist);
			delete_peer(p);
		}
	} else {

		p->bytes_in_buf += nbytes;
		/* So, _now_ do we have a whole line? */
		if ((p->nextpos = extractline(p->buf, p->bytes_in_buf))) {
			p->bytes_in_buf -= (p->nextpos - p->buf);
			return(p->buf);
		}
		

		/* If we've hit the maximum message size, we should call
	 	* it all a line.*/
		if (p->bytes_in_buf >= 1024) {
			p->buf[p->bytes_in_buf] = '\0';
			p->bytes_in_buf = 0;
			p->nextpos = NULL;
			return(p->buf);
		}
	}
    /* If we got to here, we don't have a full input line yet. */
	return(NULL);
}

// Processes the current peer's buffer.
int process(struct peer *p) {
	char line[1024];
	strcpy(line,p->buf);
	if (verbose)
		printf("Received message to evaluate: %s\n",line);
	struct ipaddr_port *s;

	// The line is set as the message.
	setparsemessage(line);
	s = getparsemessage();
	extern void doconnect(unsigned long ipaddr, int port);
	int countips = 0;
	int fromme = 0;

	/* The ip and port combinations are cycled through to see if I have seen 
	the message. */
	setparsemessage(line);
	while ((s = getparsemessage()) && 
		!(s->ipaddr == ipaddr && s->port == myport))
		;
	// If I was the first in the list, I must have been the original writer.
	if (s) {
		if (getparsemessage() == NULL) {
			fromme = 1; 
			printf("Hey, that message was from me!\n");
		}
	}
	if ((countips >= relaymax) || (fromme == 1))
		printf("The history of the message was:\n");
	setparsemessage(line);
	while ((s = getparsemessage())) {
		countips += 1;
	}
	setparsemessage(line);
	while ((s = getparsemessage())) {
		if ((fromme == 1) || (relaymax <= countips)) {
			if (s->ipaddr == ipaddr && s->port == myport) {
				printf("    you\n");
			} else {
				printf("    IP address %lu.%lu.%lu.%lu, port %d\n",
					(s->ipaddr >> 24) & 255, (s->ipaddr >> 16) & 255,
					(s->ipaddr >> 8) & 255, s->ipaddr & 255,
					s->port);
			}
		}
		if (!(find_peer(s->ipaddr, s->port)) && 
			(s->ipaddr != ipaddr || s->port != myport)) 
		 		doconnect(s->ipaddr, s->port);
	}

	if ((countips >= relaymax) && (fromme == 0)) {
		printf("Here's a message which we're not going to relay because the relaymax count has been reached: %s\n", getmessagecontent());
		return(0);
	} 
	if (fromme == 1) {
		printf("And the message was: %s\n", getmessagecontent());
		return(0);
	}
	if (countips < relaymax) {
		if (strlen(line) > 
			(1020 - strlen(format_ipaddr(ipaddr)) - strlen(myportstr))) {
			perror("line is too long");
			return(1);
		}
		// Format the reply.
		char newline[1024];
		strcpy(newline,format_ipaddr(ipaddr));
		strcat(newline,",");
		strcat(newline,myportstr);
		strcat(newline, ";");
		strcat(newline, line);
		strcat(newline, "\r");

		if (count_peers() == 0) {
			printf("No one to send to!\n");
			return(0);
		}
		p = random_peer();
		write(p->fd, newline, strlen(newline));
		if (verbose)
			printf("relaying to %s, port %d\n", 
				format_ipaddr(p->ipaddr),p->port);
	}
	return(0);
}

int newconnection(int fd)  /* accept connection, update linked list */
{
	int connfd;
	struct peer *newp = malloc(sizeof(struct peer));
    if (!newp) {
		fprintf(stderr, "out of memory!\n");  /* highly unlikely to happen */
		exit(1);
    }

	struct sockaddr_in r;
	socklen_t clilen = sizeof r;

	// Accept the connection and add the peer.
	if ((connfd = accept(fd, (struct sockaddr *)&r, &clilen)) < 0) {
		perror("accept");
		return(1);
	} else {
		printf("new connection from %s, fd %d\n", inet_ntoa(r.sin_addr),connfd);
		FD_SET(connfd, &fdlist);
		fflush(stdout);
		struct peer *newpeer = add_peer(ntohl(r.sin_addr.s_addr),0);
	    newpeer->fd = connfd;
	    newpeer->YAKflag=0;
	    // Set the yak flag to 0 so that it knows it's still waiting for a YAK.
	}
    if (connfd > maxfd)
		maxfd = connfd;

	// Write a YAK to the new client.
	char message[1024];
	strcpy(message, "YAK ");
	strcat(message,inet_ntoa(r.sin_addr));
	strcat(message,"\r\n");
	int n = strlen(message);
	write(connfd, message, n);
	//printf("%s",message);
	return(0);
}


int setup()  /* setup socket without listening yet */
{
	struct sockaddr_in r;
	int fd;
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}

	r.sin_family      = AF_INET;
	r.sin_addr.s_addr = htonl(INADDR_ANY);
	r.sin_port        = htons(myport);

	if (bind(fd, (struct sockaddr *)&r, sizeof(r))) {
		perror("bind");
		exit(1);
	}
	maxfd = fd;
	return fd;
}

void doconnect(unsigned long ipaddr, int port)
{
	int newfd;
	struct sockaddr_in s;

	if ((newfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}

	memset(&s, '\0', sizeof s);
	s.sin_family = AF_INET;
	s.sin_addr.s_addr = htonl(ipaddr);
	s.sin_port = htons(port);

	if (connect(newfd, (struct sockaddr *)&s, sizeof s) < 0) {
		perror("connect");
		exit(1);
	}

	FD_SET(newfd, &fdlist);
	if (newfd > maxfd) 
		maxfd = newfd;

	struct peer *newpeer = add_peer(ipaddr,port);
    newpeer->fd = newfd;
    newpeer->YAKflag=0;

    // Write a YAK message to the server.
	char message[1024];
	strcpy(message, "YAK ");
	strcat(message,inet_ntoa(s.sin_addr));
	strcat(message," ");
	strcat(message,myportstr);
	strcat(message,"\r\n");
	write(newfd, message, strlen(message));
	if (verbose)
		printf("Connecting to %s, port %d\n",inet_ntoa(s.sin_addr),port);

}	

unsigned long hostlookup(char *host)
{
	struct hostent *hp;
	struct in_addr a;

	if ((hp = gethostbyname(host)) == NULL) {
		fprintf(stderr, "%s: no such host\n", host);
		exit(1);
	}
	if (hp->h_addr_list[0] == NULL || hp->h_addrtype != AF_INET) {
		fprintf(stderr, "%s: not an internet protocol host name\n", host);
		exit(1);
	}
	memcpy(&a, hp->h_addr_list[0], hp->h_length);
	return(ntohl(a.s_addr));
}


int analyze_banner(char *s, struct peer *p)
{
	unsigned long a, b, c, d, newipaddr;
	int numfields;
	int newport;

	numfields = sscanf(s, "%lu.%lu.%lu.%lu %d", &a, &b, &c, &d, &newport);
	if (numfields < 4) {
		fprintf(stderr, "'%s' does not begin with an IP address\n", s);
		return(-1);
	}

	newipaddr = (a << 24) | (b << 16) | (c << 8) | d;
	if (ipaddr == 0) {
		ipaddr = newipaddr;
		printf("I've learned that my IP address is %s\n",format_ipaddr(ipaddr));
	} else if (ipaddr != newipaddr) {
		fprintf(stderr, "fatal error: I thought my IP address was %s, but newcomer says it's %s\n", format_ipaddr(ipaddr), s);
		exit(1);
	}

	if (numfields > 4) {
		if (p->port == 0) {
			struct peer *q = find_peer(p->ipaddr, newport);
			if (q == NULL) {
				p->port = newport;
				printf("I've learned that the peer on fd %d's port number is %d\n",p->fd, p->port);
			} else {
				printf("fd %d's port number is %d, so it's a duplicate of fd %d, so I'm dropping it.\n",p->fd, newport, q->fd);
				delete_peer(p);
			}
		} else if (p->port != newport) {
			printf("I'm a bit concerned because I thought the peer on fd %d's port number was %d, but it says it's %d\n", p->fd, p->port, newport);
		}
	}

	return(0);
}
