
// SERVER
#define _GNU_SOURCE
#define _POSIX_C_SOURCE

#include <stdio.h>     // puts, NULL
#include <netdb.h>     // getaddrinfo
#include <arpa/inet.h> // inet_ntop
#include <string.h>    // memset

int main(int argc, char* argv[]) {
    char buf[INET_ADDRSTRLEN];
    struct addrinfo* results = NULL;

    struct addrinfo hint;

    memset(&hint, 0, sizeof(hint));
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_family = AF_INET;

    // Get Addr returns 0 on sucess and positive error code otherwise
    // On error, results doesn't get touched or changed at all (doesn't go to NULL)
    int error =
    getaddrinfo(argv[1],
                NULL, // port number of the returned socket addresses will
                      // be left uninitialized
                &hint, // only TCP/IPv4
                &results);

    if(error == EAI_NONAME)
    {
        printf("error?\n");
    }

    for (struct addrinfo *rp = results; rp ; rp = rp->ai_next) {
        printf("hello\n");
        struct sockaddr_in*  p = (struct sockaddr_in*)rp->ai_addr;
        // For the IPv4 address in sin_addr.s_addr, convert to presetnation (cstring)
        // and then place it in the buffer we passed in, with the size of INET_ADDRSTRLEN
        // representing the largest possible length of an IPv4 address in string form
        // Which should be 16 bytes long? 4 bytes per portion (0-255)
        inet_ntop(AF_INET, &p->sin_addr.s_addr, buf, INET_ADDRSTRLEN);
        puts(buf);
    }
    return 0;
}
