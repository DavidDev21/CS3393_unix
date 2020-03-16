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
static char USERNAME[MAX_USERNAME_SIZE+3];

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
        fprintf(stderr, "Usage: server <username> [-p for port, \
                                                -s for server addr]\n");
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

    // State your name
    writeMessage("has connected\n", clientSocket, 1);

    // fd set for select()
    fd_set readSet;
    int numReadyFD;
    int maxFD = clientSocket + 1;

    while(1)
    {
        FD_ZERO(&readSet);

        // Initialize variables for select()
        addFDSet(STDIN_FILENO, &readSet);
        addFDSet(clientSocket, &readSet);

        printf("\n>> ");
        fflush(stdout);

        numReadyFD = select(maxFD, &readSet, NULL, NULL, NULL);
        
        // If stdin was ready
        if(FD_ISSET(STDIN_FILENO, &readSet))
        {
            sendMessage(STDIN_FILENO, clientSocket, 1, 1);
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