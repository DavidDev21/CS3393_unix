/*
    Name: David Zheng
    CS 3393 - Unix Systems Programming
    HW5: Two way chat
    Due Date: April 1st, 2020
*/

// SERVER

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>      // strlen
#include <sys/socket.h>     // socket, AF_INET, SOCK_STREAM
#include <arpa/inet.h>      // inet_pton (converts a string to network)
#include <netinet/in.h>     // servaddr
#include <sys/select.h>  // select, FD_ZERO, FD_SET, FD_ISSET
#include <errno.h>

// marcos
#define LISTEN_BUFF 10 // for the queue size for listen()
#define MSG_BUFF_SIZE 4096
#define MAX_USERNAME_SIZE 1024


// Usage: server <username> <optional port>

// As equal to the type of the field in struct sockaddr_in
// Most likely good idea to make these easy to access for functions
// since these won't change really.
static in_port_t SERVER_PORT=8920;
static char USERNAME[MAX_USERNAME_SIZE];

void errorCheck(int errorCode, char* message)
{
    if(errorCode < 0)
    {
        perror(message);

        exit(EXIT_FAILURE);
    }
}

// Parse Input???
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

    // + 3 for the " : ", + 1 for the Null terminate
    memset(USERNAME, '\0', MAX_USERNAME_SIZE + 4);
    strcat(USERNAME, argv[1]);
    strcat(USERNAME, " : ");


    for(int i = 2; i < argc; i++)
    {
        if(strcmp(argv[i], "-p") == 0 && i+1 < argc)
        {
            char* endptr = NULL;
            SERVER_PORT = strtoul(argv[i+1], &endptr, 10);

            // Check if we had a valid conversion
            if(SERVER_PORT == 0 && argv[i+1] == endptr)
            {
                fprintf(stderr, "Not a valid port number\n");
                exit(EXIT_FAILURE);
            }

            i++;
        }
    }
}

void addFDSet(int fd, fd_set* set)
{
    FD_SET(fd, set);
}

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
    
    // Write it to dest
    if((numWritten = write(dest, messageBuffer, strlen(messageBuffer))) !=  strlen(messageBuffer))
    {
        perror("writeMessage(): write failed: ");
        exit(EXIT_FAILURE);
    }

    //printf("Num written: %ld\n", numWritten);

    return numWritten;
}

// Reads from src and writes the content from src fd to dest fd
size_t sendMessage(int src, int dest, int writeName, int echo)
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
            exit(0);
        }
        if(echo > 0)
        {
            printf("%s", messageBuffer);
        }

        // printf("Message Buffer: %s\n", messageBuffer);
        // printf("Message length: %ld VS %ld\n", strlen(messageBuffer), numRead);
        // printf("Message at: %c\n", messageBuffer[numRead-2]);
        // printf("True: %d\n", messageBuffer[numRead-2] == '\n');
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
    parseInput(argc, argv);

    printf("USERNAME: %s\n", USERNAME);
    printf("PORT: %d\n", SERVER_PORT);

    // Setup the socket for server
    // Variables
    // FD for serversocker and the client connection
    int serverSocket, clientConn;
    struct sockaddr_in serverAddr; // init to 0

    // Creates an TCP socket using IPv4 addressing
    // 0 is for the "protcol", which is usually 0 since most communication semantics only have one protocol?
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
    serverAddr.sin_port = htons(SERVER_PORT); // short = 16 bits, 2 bytes (uint16)
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); // long = 32 bits, 4 bytes (uint32)

    // bind the socket
    // bind(fd, pointer to a struct sockaddr, then the size of that struct we are pointing to)
    errorCheck(bind(serverSocket, 
                (struct sockaddr *) &serverAddr, sizeof(serverAddr)), 
                "Failed Bind(): ");

    // Listen or become passive socket
    listen(serverSocket, LISTEN_BUFF);

    // Enter infinite loop
    while(1)
    {
        struct sockaddr_in clientInfo = {0};
        socklen_t clientSize = sizeof(clientInfo);
        int connected = 0;

        printf("Waiting for client\n");

        clientConn = accept(serverSocket, (struct sockaddr*) &clientInfo, &clientSize);

        errorCheck(clientConn, "Failed Accept: ");

        printf("CLIENT CONNECTED\n\n");

        printf("Client Addr: %d\n", clientInfo.sin_addr.s_addr);

        connected = 1;

        writeMessage("Welcome User To My Sex Dungeon\n", clientConn, 1);
        // printf("INIT STRLEN: %ld\n", strlen("Welcome User To My Sex Dungeon\n"));

        while(connected)
        {
            // fd set for select()
            fd_set readSet, writeSet;
            FD_ZERO(&readSet);
            int numReadyFD;
            int maxFD = clientConn + 1;

            // Initialize variables for select()
            addFDSet(STDIN_FILENO, &readSet);
            addFDSet(clientConn, &readSet);
            //addFDSet(clientSocket, &writeSet);

            numReadyFD = select(maxFD, &readSet, NULL, NULL, NULL);

            // If stdin was ready
            if(FD_ISSET(STDIN_FILENO, &readSet))
            {
                // printf("Can read from stdin\n");

                // printf("===================================\n%s : ", USERNAME);
                printf("\n\n");
                sendMessage(STDIN_FILENO, clientConn, 1, 1);
            }
            // If there is something to read from server
            if(FD_ISSET(clientConn, &readSet))
            {
                //printf("Can read from clientSocket\n");
                // send message from clientSocket to console
                // EOF, client has exited in some fashion
                if(sendMessage(clientConn, STDOUT_FILENO, 0, 0) == 0)
                {
                    close(clientConn);
                    connected = 0;
                }
            }
            if(numReadyFD < 0)
            {
                perror("select() Failed");
                exit(EXIT_FAILURE);
            }

            //close(clientConn);
        }
    }

    return 0;
}