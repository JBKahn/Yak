struct ipaddr_port {
    unsigned long ipaddr;
    int port;
};

extern void setparsemessage(char *s);
extern struct ipaddr_port *getparsemessage(); /* return next ipaddr/port pair */
extern char *getmessagecontent();  /* after getparsemessage() returns NULL */
