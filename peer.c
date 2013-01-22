/* maintain a list of peers in a hash table */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "peer.h"

struct peer *top_peer = NULL;

static int npeers = 0;


struct peer *add_peer(unsigned long ipaddr, int port)
{
    struct peer *p;

    if ((p = malloc(sizeof(struct peer))) == NULL) {
	fprintf(stderr, "out of memory!\n");
	exit(1);
    }
    p->ipaddr = ipaddr;
    p->port = port;
    p->next = top_peer;
    top_peer = p;

    npeers++;
    return(p);
}


void delete_peer(struct peer *p)
{
    struct peer **pp;
    for (pp = &top_peer; *pp && *pp != p; pp = &(*pp)->next)
	;
    if (*pp) {
	*pp = p->next;
	free(p);
	npeers--;
    } else {
	fprintf(stderr, "can't delete non-existent peer %ld,%d\n", p->ipaddr, p->port);
    }
}


struct peer *find_peer(unsigned long ipaddr, int port)
{
    struct peer *p;
    for (p = top_peer;
	    p && (p->ipaddr != ipaddr || p->port != port);
	    p = p->next)
	;
    return(p);
}


int count_peers()
{
    return(npeers);
}


struct peer *nth_peer(int n)
{
    struct peer *p;

    for (p = top_peer; p && n > 0; p = p->next)
	n--;
    return(p);
}


struct peer *random_peer()
{
    static int urandomfd = -1;
    unsigned int n;

    if (npeers == 0)
	return(NULL);
    if (urandomfd < 0) {
	if ((urandomfd = open("/dev/urandom", O_RDONLY)) < 0) {
	    perror("/dev/urandom");
	    exit(1);
	}
    }
    if (read(urandomfd, &n, sizeof n) != sizeof n) {
	perror("/dev/urandom");
	exit(1);
    }
    return(nth_peer(n % npeers));
}
