struct peer {
    unsigned long ipaddr;
    int port;
    struct peer *next;
    int fd;
    int YAKflag;
    int bytes_in_buf;
    char buf[1024];
    char* nextpos;
};  
extern struct peer *add_peer(unsigned long ipaddr, int port);
extern void delete_peer(struct peer *p);
extern struct peer *find_peer(unsigned long ipaddr, int port);
extern int count_peers();
extern struct peer *nth_peer(int n), *random_peer();
extern struct peer *top_peer;
