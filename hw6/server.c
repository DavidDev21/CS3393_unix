/*
    Name: David Zheng
    CS 3393 - Unix Systems Programming
    HW6: Chatroom
    Due Date: April 23, 2020
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
#include <pthread.h>

// marcos
#define LISTEN_BUFF 10 // for the queue size for listen()
#define MSG_BUFF_SIZE 4096
#define MAX_USERNAME_SIZE 1024
#define MAX_CLIENT_NUM 1025
#define MSG_QUEUE_SIZE 1024
#define GREETING "Welcome User! Hopefully I can keep you sane. :)\n"

// Usage: server <username> <optional port>

// As equal to the type of the field in struct sockaddr_in
// Most likely good idea to make these easy to access for functions
// since these won't change really.
static in_port_t SERVER_PORT=8920;
static char USERNAME[MAX_USERNAME_SIZE+3];
static volatile sig_atomic_t SIG_CAUGHT = 0;
static volatile sig_atomic_t jmpActive = 0;

// Struct representing a client's information
typedef struct{
    char username[MAX_USERNAME_SIZE];
    int sockfd; // the socket associated with this client
    int id; // representing its location in our client list
} client_info;

typedef struct {
    client_info** array[MAX_CLIENT_NUM];
    size_t length;
    pthread_mutex_t lock;
    pthread_cond_t cond; // for if the chatroom is full
} client_list;

typedef struct{
    char** queue[MSG_QUEUE_SIZE];
    size_t length;
    pthread_mutex_t lock;
    pthread_cond_t cond; // for if the queue is full
} message_queue;

// Should be global for easy accessibility between threads
client_list userList;
message_queue outMessages;

// Intended to be used right after a malloc / realloc
// for error checking
// Could also be used for anything that returns a pointer I guess
void checkNull(void* arrayPtr, const char* errorMsg)
{
    if(arrayPtr == NULL)
    {
        perror(errorMsg);
        exit(EXIT_FAILURE);
    }
}

// Checks any given errorCode
// and if it is an error, it will print the error and jump back to the start
// of the shell. reprompting the user for the new command.
// We assume something went wrong and just have the user reissue their command
void errorCheck(int errorCode, char* message, int restart)
{
    if(errorCode < 0)
    {
       fprintf(stderr, "shell: %s: %s\n", message, strerror(errno));

       if(restart > 0)
       {
            siglongjmp(resetPrompt, errno);
            return;
       } 
       else
       {
           exit(EXIT_FAILURE);
       }
    }
}

// Functions for managing message queue
// Adds given message to the queue
char* enqueueMessage(char* message, client_info* sender)
{
    if(strlen(message) + strlen(sender->username) + 3 > MSG_BUFF_SIZE)
    {
        fprintf(stderr, "enqueueMessage(): message too long\n");
        return NULL;
    }

    // Attempt to acquire the lock on the msg queue
    pthread_mutex_lock(&(outMessages.lock));

    // If the queue is full, we wait until it gets cleared up
    while(outMessages.length + 1 > MSG_QUEUE_SIZE)
    {
        pthread_cond_wait(&(outMessages.cond), &(outMessages.lock));
    }

    // Allocate space on heap for message
    char* messageBuffer = (char*)malloc(MSG_BUFF_SIZE+1);
    checkNull(messageBuffer, "Failed malloc()");
    memset(messageBuffer, '\0', MSG_BUFF_SIZE+1);
    
    // Add username to the message
    strcat(messageBuffer, sender->username);
    strcat(messageBuffer, ": ");
    strcat(messageBuffer, message);

    // Add to queue
    outMessages.queue[outMessages.length] = messageBuffer;
    outMessages.length++;

    // Signal the consumer that there is a new message
    pthread_cond_signal(&(outMessages.cond));
    pthread_mutex_unlock(&(outMessages.lock));

    return messageBuffer;
}

// Remove message from queue
// Yes, this isn't optimal
// This is to be used in conjunction with a function
// that already has acquired the lock for the message queue
int removeMessage()
{
    // Free up the message in the head of queue
    free(outMessages.queue[0]);

    // Shift items over to the left, writing over the item at index
    for(size_t i = 0; outMessages.queue[i] != NULL; i++)
    {
        outMessages.queue[i] = outMessages.queue[i+1];
    }

    // decrease length by 1
    outMessages.length--;
    return 0;
}

// Thread for reading from message queue and broadcast whenever there is a message
void* broadcastThread(void* args)
{
    // Mark the thread as detached so we dont have to wait for it
    // Auto cleanup after thread exits (if it ever does)
    pthread_detach(pthread_self());

    while(1)
    {
        pthread_mutex_lock(&(outMessages.lock));

        // While there is no messages, unlock and block until there is one
        while(outMessages.length == 0)
        {
            pthread_cond_wait(&(outMessages.cond), &(outMessages.lock));
        }

        // Now that we have a message to send out
        char* outboundMessage = outMessages.queue[0];

        // Lock for the list of clients
        pthread_mutex_lock(&(userList.lock));

        // Send the message out per client
        // userList[0] shall be reserved for server
        for(size_t i = 1; i < userList.length; i++)
        {
            // Note: we are also sending the message back to its sender
            // This acts as an echo for the user who wrote the message
            // and forces an ordering of the messages that is based on 
            // how the server received them
            sendMessage(outboundMessage, userList.array[i].sockfd);
        }

        // Free up the message we just sent from the queue
        // MUST HAVE THE LOCK to message queue
        // We should remove it then signal that the queue has more space
        // to the producer threads
        removeMessage();

        pthread_cond_signal(&(outMessages.cond));
        pthread_mutex_unlock(&(userList.lock));
        pthread_mutex_unlock(&(outMessages.lock));

    }
}

// Functions for managing user list
int addUser(client_info* newUser)
{
    if(newUser == NULL)
    {
        printf("addUser(): user is NULL\n");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&(userList.lock));

    if(userList.length + 1 > MAX_CLIENT_NUM)
    {
        printf(stderr, "addUser(): No more room for new clients\n");
        pthread_mutex_unlock(&(userList.lock));
        return -1;
    }
    
    for(size_t i = 1; i < MAX_CLIENT_NUM; i++)
    {
        // Empty slot
        if(userList.array[i] == NULL)
        {
            userList.array[i]  = newUser;
            userList.length++;
            newUser->id = i; // each position is a unique user
            break;
        }
    }

    // Announce who joined to the room
    char* serverMsg = (char*) malloc(MSG_BUFF_SIZE);
    checkNull(serverMsg, "Failed malloc()");

    memset(serverMsg, '\0', MSG_BUFF_SIZE);
    strcat(serverMsg, newUser->username);
    strcat(serverMsg, "has joined the room\n");

    // Note: array[0] is reserved for the server
    enqueueMessage(serverMsg, userList.array[0]);

    pthread_mutex_unlock(&(userList.lock));
    return 0;
}

int removeUser()

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
    if(argc > 3)
    {
        fprintf(stderr, "Usage: server [-p for port]\n");
        exit(EXIT_FAILURE);
    }

    for(int i = 1; i < argc; i++)
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
size_t sendMessage(const char* msg, int dest)
{
    char messageBuffer[MSG_BUFF_SIZE+1];
    size_t numWritten = 0;
    size_t msgLen = strlen(msg);

    memset(messageBuffer, '\0', MSG_BUFF_SIZE+1);

    if(msgLen > MSG_BUFF_SIZE)
    {
        fprintf(stderr, "writeMessage(): message size is too large\n");
        return numWritten;
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