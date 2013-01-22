/*
 * Parse the message data.  Note: This does not use strok() because strtok()
 * destroys the input string, and the caller needs to be able to retransmit it.
 */


#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "parsemessage.h"

static char *p;  /* where we are in the message */
static char *content;  /* message content, after parsemessage() returns NULL */

static unsigned long mygetnum();
static void myinsist(int c);


void setparsemessage(char *s)
{
    p = s;
    content = NULL;
}


struct ipaddr_port *getparsemessage()  /* return next ipaddr/port pair */
{
    static struct ipaddr_port r;

    if (*p == ';') {
	content = p + 1;
	return(NULL);
    }

    r.ipaddr = mygetnum() << 24;
    myinsist('.');
    r.ipaddr |= mygetnum() << 16;
    myinsist('.');
    r.ipaddr |= mygetnum() << 8;
    myinsist('.');
    r.ipaddr |= mygetnum();
    myinsist(',');
    r.port = mygetnum();
    myinsist(';');

    return(&r);
}


char *getmessagecontent()
{
    return(content);
}


static unsigned long mygetnum()
{
    if (!isdigit(*p)) {
	fprintf(stderr, "malformed message in getparsemessage() (expecting a number)\n");
	exit(1);
    }
    return(strtoul(p, &p, 10));
}


static void myinsist(int c)
{
    if (*p != c) {
	fprintf(stderr, "malformed message in getparsemessage() (expecting '%c')\n", c);
	exit(1);
    }
    p++;  /* skip that character c */
}
