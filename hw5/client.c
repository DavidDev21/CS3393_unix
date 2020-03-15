/*
    Name: David Zheng
    CS 3393 - Unix Systems Programming
    HW5: Two way chat
    Due Date: April 1st, 2020
*/

// CLIENT

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>      // strlen
#include <sys/socket.h>     // socket, AF_INET, SOCK_STREAM
#include <arpa/inet.h>      // inet_pton
#include <netinet/in.h>     // servaddr
#include <sys/select.h>  // select, FD_ZERO, FD_SET, FD_ISSET
#include <errno.h>

#define MSG_BUFF_SIZE 4096
#define MAX_USERNAME_SIZE 1024

static in_port_t SERVER_PORT=8920; // Port that the server is listening on
static const char* SERVER_ADDR = "127.0.0.1";
static char USERNAME[MAX_USERNAME_SIZE];

void errorCheck(int errorCode, char* message)
{
    if(errorCode < 0)
    {
        perror(message);

        exit(EXIT_FAILURE);
    }
}

// Parses through the command line and assigns the values corresponding to the expected input
void parseInput(int argc, char** argv)
{
    if(argc < 2 || argc > 6)
    {
        // The order of the options dont matter
        fprintf(stderr, "Usage: server <username> [-p for port, -s for server addr]\n");
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

size_t sendMessage(int src, int dest, int writeName, int echo)
{
    char messageBuffer[MSG_BUFF_SIZE+1];
    size_t numRead = 0;

    memset(messageBuffer, '\0', MSG_BUFF_SIZE+1);

    if(echo > 0)
    {
        //printf("ECHO TRUE\n");
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

    printf("USERNAME: %s\n", argv[2]);
    printf("PORT: %d\n", SERVER_PORT);
    printf("SERVER_ADDR: %s\n", SERVER_ADDR);

    // Setup the client
    int clientSocket;
    struct sockaddr_in serverAddr = {0}; // sockaddr for connecting to server

    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    errorCheck(clientSocket, "Failed to create socket:");

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    
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

    errorCheck(connect(clientSocket, (struct sockaddr*) &serverAddr, sizeof(serverAddr)), "Connect Failed");

    // State your name
    writeMessage("has connected\n", clientSocket, 1);

    // fd set for select()
    fd_set readSet, writeSet;
    int numReadyFD;
    int maxFD = clientSocket + 1;
    int loopCount = 0;

    while(1)
    {
        // if(loopCount >5)
        // {
        //     exit(0);
        // }

        // loopCount++;
        FD_ZERO(&readSet);

        // Initialize variables for select()
        addFDSet(STDIN_FILENO, &readSet);
        addFDSet(clientSocket, &readSet);
        //addFDSet(clientSocket, &writeSet);

        printf("\n>> ");
        fflush(stdout);

        numReadyFD = select(maxFD, &readSet, NULL, NULL, NULL);
        
        // printf("numReadyFD: %d\n", numReadyFD);
        // printf("I am unblocked\n");
        // If stdin was ready
        if(FD_ISSET(STDIN_FILENO, &readSet))
        {
            //printf("Can read from stdin\n");
            sendMessage(STDIN_FILENO, clientSocket, 1, 1);
        }
        // If there is something to read from server
        if(FD_ISSET(clientSocket, &readSet))
        {
            //printf("Can read from clientSocket\n");
            // send message from clientSocket to console

            // Reset the line to the beginning so we can write the output
            printf("\r");
            fflush(stdout);

            if(sendMessage(clientSocket, STDOUT_FILENO, 0, 0) == 0)
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