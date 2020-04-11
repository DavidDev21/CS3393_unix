/*
    Name: David Zheng
    CS 3393 - Unix Systems Programming
    HW6: Chatroom
    Due Date: April 23, 2020
*/

// CLIENT
#define _GNU_SOURCE
#define _POSIX_C_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>      // strlen
#include <sys/socket.h>     // socket, AF_INET, SOCK_STREAM
#include <arpa/inet.h>      // inet_pton
#include <netinet/in.h>     // servaddr
#include <sys/select.h>  // select, FD_ZERO, FD_SET, FD_ISSET
#include <errno.h>
#include <fcntl.h>

#define MSG_BUFF_SIZE 1024
#define MAX_USERNAME_SIZE 1025

static in_port_t SERVER_PORT=8920; // Port that the server is listening on
static const char* SERVER_ADDR = "127.0.0.1";
static char USERNAME[MAX_USERNAME_SIZE];

void checkNull(void* arrayPtr, const char* errorMsg)
{
    if(arrayPtr == NULL)
    {
        perror(errorMsg);
        exit(EXIT_FAILURE);
    }
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
    if(argc < 2 || argc > 6)
    {
        // The order of the options dont matter
        fprintf(stderr, "Usage: server <username> [-p for port, "
                                                "-s for server addr]\n");
        exit(EXIT_FAILURE);
    }

    if(strlen(argv[1]) >= MAX_USERNAME_SIZE)
    {
        fprintf(stderr, "Username is too long. Max length is: %d\n", MAX_USERNAME_SIZE - 1);
        exit(EXIT_FAILURE);
    }

    memset(USERNAME, '\0', MAX_USERNAME_SIZE);
    strcat(USERNAME, argv[1]);

    for(int i = 2; i < argc; i++)
    {
        if(i+1 < argc)
        {
            if(strcmp(argv[i], "-p") == 0)
            {
                char* endptr = NULL;
                SERVER_PORT = strtoul(argv[i+1], &endptr, 10);

                // Check if we had a valid conversion
                if(SERVER_PORT == 0 && argv[i+1] == endptr)
                {
                    fprintf(stderr, "Not a valid port number\n");
                    exit(EXIT_FAILURE);
                }
            }
            else if(strcmp(argv[i], "-s") == 0)
            {
                SERVER_ADDR = argv[i+1];
            }

            i++;
        }
    }
}

// Writes a message to a dest file descriptor (that can be socket)
// size of the message doesn't matter
size_t writeMessage(const char* msg, int dest)
{
    int numWritten = 0;
    size_t msgLen = strlen(msg);

    char* messageBuffer = (char*) malloc((msgLen+1) * sizeof(char));
    checkNull(messageBuffer, "Failed malloc()");

    memset(messageBuffer, '\0', (msgLen+1));
    
    strcat(messageBuffer, msg);

    // Write it to dest
    if((numWritten = write(dest, messageBuffer, msgLen+1)) !=  msgLen+1)
    {
        perror("writeMessage(): write failed: ");
        exit(EXIT_FAILURE);
    }

    free(messageBuffer);

    return numWritten;
}

// Reads from src and writes the content from src fd to dest fd
// Note: it reads as much as it can from the src
// Once there is no data to read, it's done
// Control the chunks through MSG_BUFF_SIZE
int forwardMessage(int src, int dest)
{
    char messageBuffer[MSG_BUFF_SIZE];
    int numRead = 0;
    memset(messageBuffer, '\0', MSG_BUFF_SIZE);

    // make the reads nonblocking
    int FLAGS = fcntl(src, F_GETFL);
    fcntl(src, F_SETFL, FLAGS | O_NONBLOCK);

    errno = 0;
    // Keep reading until we get an EOF or there is just nothing to read
    while((numRead = read(src, messageBuffer, MSG_BUFF_SIZE)) > 0)
    {
        // Keep writing things from the buffer until we stop
        if(write(dest, messageBuffer, numRead) != numRead)
        {
            fprintf(stderr,"Write to fd=%d failed\n", dest);
            exit(EXIT_FAILURE);
        }

        memset(messageBuffer, '\0', MSG_BUFF_SIZE);
    }

    // EFAULT is when server closed connection
    if(errno != 0 && errno != EFAULT && (errno != EAGAIN || errno != EWOULDBLOCK))
    {
        fprintf(stderr, "Failed to read from fd=%d\n", src);
        exit(EXIT_FAILURE);
    }
 
    // Reset back
    fcntl(src, F_SETFL, FLAGS);
    
    return numRead;
}

int main(int argc, char* argv[])
{
    // Goes through the arguments and sets any changes to defaults
    parseInput(argc, argv);

    printf("USERNAME: %s\n", argv[1]);
    printf("PORT: %d\n", SERVER_PORT);
    printf("SERVER_ADDR: %s\n", SERVER_ADDR);

    // Setup the client
    int clientSocket;
    struct sockaddr_in serverAddr = {0}; // sockaddr for connecting to server

    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    errorCheck(clientSocket, "Failed to create socket:");

    serverAddr.sin_family = AF_INET; // connecting using an IPv4 Address
    serverAddr.sin_port = htons(SERVER_PORT);
    
    // Attempt to convert the string to a network equal
    // SERVER_ADDR should be of the expected string format that is valid IPv4
    int inetErro;
    if((inetErro = inet_pton(AF_INET, SERVER_ADDR, &serverAddr.sin_addr)) < 0 )
    {
        perror("inet_pton failed");
        exit(EXIT_FAILURE);
    }
    else if(inetErro == 0)
    {
        fprintf(stderr, "%s is not a valid IPv4 Address\n", SERVER_ADDR);
        exit(EXIT_FAILURE);
    }

    // Attempt to connect
    errorCheck(connect(clientSocket, (struct sockaddr*) &serverAddr, 
                                        sizeof(serverAddr)), "Connect Failed");

    // State your name to the server (just for initial message)
    // from the client
    writeMessage(USERNAME, clientSocket);

    // fd set for select()
    fd_set readSet;
    int numReadyFD;
    int maxFD = clientSocket + 1;
    char* input = NULL;
    size_t inputSize = 0;

    while(1)
    {
        FD_ZERO(&readSet);

        // Initialize variables for select()
        FD_SET(STDIN_FILENO, &readSet);
        FD_SET(clientSocket, &readSet);

        printf("\n>> ");
        fflush(stdout);

        numReadyFD = select(maxFD, &readSet, NULL, NULL, NULL);
        
        // If stdin was ready
        if(FD_ISSET(STDIN_FILENO, &readSet))
        {
            if(getline(&input, &inputSize, stdin) != -1)
            {
                writeMessage(input, clientSocket);

                if(strcmp(input, "QUIT\n") == 0)
                {
                    printf("Exiting Client\n");
                    free(input);
                    exit(0);
                }
            }
        }

        // If there is something to read from server
        if(FD_ISSET(clientSocket, &readSet))
        {
            // send message from clientSocket to console
            // Reset the cursor to the beginning so we can write the output
            // from server. 
            // (We would be overwrite the >> that was there before select)

            printf("\r");
            fflush(stdout);

            if(forwardMessage(clientSocket, STDOUT_FILENO) == 0)
            {
                printf("Lost connection to server\n");
                exit(EXIT_FAILURE);
            }
        }
        if(numReadyFD < 0)
        {
            perror("select() Failed");
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}