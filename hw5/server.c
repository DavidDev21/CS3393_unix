/*
    Name: David Zheng
    CS 3393 - Unix Systems Programming
    HW5: Two way chat
    Due Date: March 31, 2020
*/

// SERVER
#define _GNU_SOURCE
// Needed for sigaction
#define _POSIX_C_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>      // strlen
#include <sys/socket.h>     // socket, AF_INET, SOCK_STREAM
#include <arpa/inet.h>      // inet_pton (converts a string to network)
#include <netinet/in.h>     // servaddr
#include <sys/select.h>  // select, FD_ZERO, FD_SET, FD_ISSET
#include <errno.h>
#include <signal.h>
#include <setjmp.h>

// marcos
#define LISTEN_BUFF 10 // for the queue size for listen()
#define MSG_BUFF_SIZE 4096
#define MAX_USERNAME_SIZE 1024
#define GREETING "Welcome User! Hopefully I can keep you sane. :)\n"

// Usage: server <username> <optional port>

// As equal to the type of the field in struct sockaddr_in
// Most likely good idea to make these easy to access for functions
// since these won't change really.
static in_port_t SERVER_PORT=8920;
static char USERNAME[MAX_USERNAME_SIZE+3];
static volatile sig_atomic_t SIG_CAUGHT = 0;
static volatile sig_atomic_t jmpActive = 0;

sigjmp_buf TO_MAIN;

// Signal handing SIGINT to do some cleanup
void cleanupHandle(int signum)
{
    if(jmpActive == 0)
    {
        return;
    }
    SIG_CAUGHT = 1;
    siglongjmp(TO_MAIN, 0);
}

void errorCheck(int errorCode, char* message)
{
    if(errorCode < 0)
    {
        perror(message);

        exit(EXIT_FAILURE);
    }
}

// Parses through the command line and 
// assigns the values corresponding to the expected input
void parseInput(int argc, char** argv)
{
    if(argc < 2 || argc > 4)
    {
        fprintf(stderr, "Usage: server <username> [-p for port]\n");
        exit(EXIT_FAILURE);
    }

    if(strlen(argv[1]) > MAX_USERNAME_SIZE)
    {
        fprintf(stderr, "Username is too long. Max length is: 1024\n");
        exit(EXIT_FAILURE);
    }

    // + 2 for the ": ", + 1 for the Null terminate
    // Note: USERNAME fits: MAX_USERNAME_SIZE + 3
    memset(USERNAME, '\0', sizeof(USERNAME));
    strcat(USERNAME, argv[1]);
    strcat(USERNAME, ": ");


    for(int i = 2; i < argc; i++)
    {
        if(strcmp(argv[i], "-p") == 0 && i+1 < argc)
        {
            char* endptr = NULL;
            SERVER_PORT = strtoul(argv[i+1], &endptr, 10);

            // Check if we had a valid conversion
            if(SERVER_PORT == 0 && argv[i+1] == endptr)
            {
                fprintf(stderr, "strtoul(): No valid conversion\n");
                exit(EXIT_FAILURE);
            }

            i++;
        }
        else
        {
            fprintf(stderr, "Unknown flag: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }
}

// Writes a message to a dest file descriptor (that can be socket)
// writeName flag determines if you want to include your username in the message
size_t writeMessage(const char* msg, int dest, int writeName)
{
    char messageBuffer[MSG_BUFF_SIZE+1];
    size_t numWritten = 0;
    size_t msgLen = strlen(msg);

    memset(messageBuffer, '\0', MSG_BUFF_SIZE+1);

    if(msgLen + strlen(USERNAME) > MSG_BUFF_SIZE)
    {
        fprintf(stderr, "writeMessage(): message size is too large\n");
        return numWritten;
    }

    if(writeName > 0)
    {
        // Write the name of the sender before the message
        strcat(messageBuffer, USERNAME);
    }

    // Copy the message into the buffer
    strcat(messageBuffer, msg);

    msgLen = strlen(messageBuffer);
    
    // Write it to dest
    if((numWritten = write(dest, messageBuffer, msgLen)) !=  msgLen)
    {
        perror("writeMessage(): write failed: ");
        exit(EXIT_FAILURE);
    }

    return numWritten;
}

// Reads from src and writes the content from src fd to dest fd
// writeName: to include the username or not (if src is from stdin)
// echo: whether you want the result that was sent to dest, to be echoed to stdout
size_t forwardMessage(int src, int dest, int writeName, int echo)
{
    char messageBuffer[MSG_BUFF_SIZE+1];
    size_t numRead = 0;

    memset(messageBuffer, '\0', MSG_BUFF_SIZE+1);

    if(echo > 0)
    {
        printf("%s", USERNAME);
    }

    if(writeName > 0)
    {
        // Write the name of the sender before the message
        writeMessage(USERNAME, dest, 0);
    }

    errno = 0;
    // Keep reading until we get an EOF or newline
    while((numRead = read(src, messageBuffer, MSG_BUFF_SIZE)) > 0)
    {
        // Null terminate
        messageBuffer[numRead+1] = '\0';

        // Keep writing things from the buffer until we stop
        if(write(dest, messageBuffer, numRead+1) != numRead+1)
        {
            fprintf(stderr,"Write to fd=%d failed\n", dest);
            exit(EXIT_FAILURE);
        }
        if(echo > 0)
        {
            printf("%s", messageBuffer);
        }
    
        // If we ended with a newline, we are also done reading
        if(messageBuffer[strlen(messageBuffer)-1] == '\n')
        {
            break;
        }
    }

    if(errno != 0)
    {
        fprintf(stderr, "Failed to read from fd=%d\n", src);
        exit(EXIT_FAILURE);
    }

    return numRead;
}
int main(int argc, char* argv[])
{
    struct sigaction socketCleaner = {.sa_handler = cleanupHandle, 
                                        .sa_flags = SA_RESTART};

    // Goes through the arguments and sets any changes to defaults
    parseInput(argc, argv);

    // Note: the actual user name at this point would include ": " at the end
    printf("USERNAME: %s\n", argv[1]);
    printf("PORT: %d\n", SERVER_PORT);

    // Setup the socket for server
    // Variables
    // FD for serversocker and the client connection
    int serverSocket, clientConn;
    struct sockaddr_in serverAddr;

    // Creates an TCP socket using IPv4 addressing
    // 0 is for the "protcol" which is usually 0 since most 
    // communication semantics only have one protocol
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    errorCheck(serverSocket, "Failed to create socket");

    // struct sockaddr_in {
    //     sa_family_t sin_family; // address family: AF_INET
    //     in_port_t sin_port; // port in network byte order
    //     struct in_addr sin_addr; // internet address
    // };

    // Setup the address the server will listen on
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    // short = 16 bits, 2 bytes (uint16)
    serverAddr.sin_port = htons(SERVER_PORT);
    // long = 32 bits, 4 bytes (uint32)
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind the socket
    // bind(fd, pointer to a struct sockaddr, size of that struct)
    errorCheck(bind(serverSocket, 
                (struct sockaddr *) &serverAddr, sizeof(serverAddr)), 
                "Failed Bind(): ");

    // Listen or become passive socket
    errorCheck(listen(serverSocket, LISTEN_BUFF), "listen()");

    errorCheck(sigaction(SIGINT, &socketCleaner, NULL), "sigaction");

    sigsetjmp(TO_MAIN, 1);
    jmpActive=1;

    // Enter infinite loop
    while(1)
    {
        // Cleanup if we got signal
        if(SIG_CAUGHT)
        {
            close(serverSocket);
            printf("Shutting down server. Goodbye :)\n");
            exit(EXIT_SUCCESS);
        }

        struct sockaddr_in clientInfo = {0};
        socklen_t clientSize = sizeof(clientInfo);
        int connected = 0;

        printf("\nWaiting for client\n");

        clientConn = accept(serverSocket, (struct sockaddr*) &clientInfo, 
                                                            &clientSize);

        errorCheck(clientConn, "Failed Accept: ");

        printf("CLIENT CONNECTED\n\n");

        char clientAddr[INET_ADDRSTRLEN];
        if(inet_ntop(AF_INET, &clientInfo.sin_addr, clientAddr, 
                                                INET_ADDRSTRLEN) == NULL)
        {
            perror("inet_ntop()");
            exit(EXIT_FAILURE);
        }

        printf("Client Addr: %s\n", clientAddr);

        // Write out the initial message from the client
        // The client should be sending us who they are (Their username)
        forwardMessage(clientConn, STDOUT_FILENO, 0, 0);

        connected = 1;

        // Greet the user
        writeMessage(GREETING, clientConn, 1);

        while(connected)
        {
            // fd set for select()
            fd_set readSet;
            FD_ZERO(&readSet);
            int numReadyFD;
            int maxFD = clientConn + 1;

            // Initialize variables for select()
            FD_SET(STDIN_FILENO, &readSet);
            FD_SET(clientConn, &readSet);

            printf("\n>> ");
            fflush(stdout);

            // block for any IO from readSet. no timeout.
            numReadyFD = select(maxFD, &readSet, NULL, NULL, NULL);

            // If stdin was ready
            if(FD_ISSET(STDIN_FILENO, &readSet))
            {
                // Send whatever we got from stdin through the socket
                // Also echo whatever was sent to the console.
                forwardMessage(STDIN_FILENO, clientConn, 1, 1);
            }
            // If there is something to read from server
            if(FD_ISSET(clientConn, &readSet))
            {
                // send message from clientSocket to console
                
                // reset the cursor to the beginning of the line
                // Since we would have the prompt string if
                // we were waiting for stdin.
                printf("\r");
                fflush(stdout);

                if(forwardMessage(clientConn, STDOUT_FILENO, 0, 0) == 0)
                {
                    close(clientConn);
                    connected = 0;
                    printf("Client has disconnected\n");
                    printf("===============================\n\n");
                }
            }
            // Check if select failed
            if(numReadyFD < 0)
            {
                perror("select() Failed");
                exit(EXIT_FAILURE);
            }
        }
    }
    return 0;
}