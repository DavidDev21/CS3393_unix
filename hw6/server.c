/*
    Name: David Zheng
    CS 3393 - Unix Systems Programming
    HW6: Chatroom
    Due Date: April 23, 2020
*/

/*
    TODO:
        Allow server to send any variable message length
        It doesn't have to depend on MSG_BUFF_SIZE since we allocate on the heap for the queue anyways
        - the client now is able to read any length of messages from the server, So you might as well
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
#include <setjmp.h>
#include <pthread.h>

// marcos
#define LISTEN_BUFF 10 // for the queue size for listen()
#define MSG_BUFF_SIZE 4096
#define MAX_USERNAME_SIZE 1024
#define MAX_CLIENT_NUM 4
#define MSG_QUEUE_SIZE 1024
#define GREETING "Welcome User! Hopefully I can keep you sane. :)\n"

// Usage: server <username> <optional port>

// As equal to the type of the field in struct sockaddr_in
// Most likely good idea to make these easy to access for functions
// since these won't change really.
static in_port_t SERVER_PORT=8920;

// Struct representing a client's information
typedef struct{
    char username[MAX_USERNAME_SIZE];
    int sockfd; // the socket associated with this client
    int id; // representing its location in our client list
} client_info;

typedef struct {
    client_info* array[MAX_CLIENT_NUM];
    size_t length;
    pthread_mutex_t lock;
    pthread_cond_t cond; // for if the chatroom is full
} client_list;

typedef struct{
    char* queue[MSG_QUEUE_SIZE];
    size_t length;
    pthread_mutex_t lock;
    pthread_cond_t cond; // for if the queue is full
} message_queue;

// Should be global for easy accessibility between threads
client_list userList;
message_queue outMessages;

// Function prototypes
void checkNull(void* arrayPtr, const char* errorMsg);
void errorCheck(int errorCode, char* message);
void parseInput(int argc, char** argv);
void p_init_userlist(void);
void p_init_msgqueue(void);
void p_free_userlist(void);
void p_free_msgqueue(void);
void* broadcastThread(void* args);
void* clientThread(void* fd);

int enqueueMessage(char* message, client_info* sender);
int removeMessage();
int addUser(client_info* newUser);
int removeUser(client_info* user);
int serverAnnouncement(char* message, char* username);
int sendUserList(int dest);

size_t sendMessage(const char* msg, int dest);

// ====== Utils

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
// This is meant as a means for the server to sent a direct message to a user
size_t sendMessage(const char* msg, int dest)
{
    char messageBuffer[MSG_BUFF_SIZE];
    size_t numWritten = 0;
    size_t msgLen = strlen(msg);

    if(msgLen >= MSG_BUFF_SIZE)
    {
        fprintf(stderr, "sendMessage(): message size is too large\n");
        return numWritten;
    }

    memset(messageBuffer, '\0', MSG_BUFF_SIZE);

    // Copy the message into the buffer
    strncpy(messageBuffer, msg, msgLen);
    
    // Write it to dest
    if((numWritten = write(dest, messageBuffer, msgLen)) !=  msgLen)
    {
        perror("sendMessage(): write failed: ");
        exit(EXIT_FAILURE);
    }

    return numWritten;
}

// func to initialize a struct with mutex and cond
void p_init_userlist(void)
{
    if(MAX_CLIENT_NUM <= 1)
    {   
        fprintf(stderr, "Max number of clients must be greater than 1\n");
        exit(EXIT_FAILURE);
    }

    userList.length = 1;
 
    memset(userList.array, 0, MAX_CLIENT_NUM * sizeof(client_info*));

    errorCheck(pthread_mutex_init(&(userList.lock), NULL), "pthread_mutex_init()");
    errorCheck(pthread_cond_init(&(userList.cond), NULL), "pthread_cond_init()");

    // The server is the first user
    client_info* serverInfo = (client_info*) malloc(sizeof(client_info));
    checkNull(serverInfo, "Failed malloc()");
    memset(serverInfo->username, '\0', MAX_USERNAME_SIZE);
    strcat(serverInfo->username,  "SERVER");
    serverInfo->id = 0;

    userList.array[0] = serverInfo;
}

void p_init_msgqueue(void)
{   
    if(MSG_QUEUE_SIZE <= 0)
    {   
        fprintf(stderr, "Message queue size can't be 0\n");
        exit(EXIT_FAILURE);
    }

    outMessages.length = 0;
    memset(outMessages.queue, 0, MSG_QUEUE_SIZE * sizeof(char*));
    errorCheck(pthread_mutex_init(&(outMessages.lock), NULL), "pthread_mutex_init()");
    errorCheck(pthread_cond_init(&(outMessages.cond), NULL), "pthread_cond_init()");
}

void p_free_userlist(void)
{
    for(size_t i = 0; i < MAX_CLIENT_NUM; i++)
    {
        free(userList.array[i]);
    }

    errorCheck(pthread_mutex_destroy(&(userList.lock)), "pthread_mutex_destroy()");
    errorCheck(pthread_cond_destroy(&(userList.cond)), "pthread_cond_destroy()");
}

void p_free_msgqueue(void)
{

    for(size_t i = 0; i < MSG_QUEUE_SIZE; i++)
    {
        free(outMessages.queue[i]);
    }

    errorCheck(pthread_mutex_destroy(&(outMessages.lock)), "pthread_mutex_destroy()");
    errorCheck(pthread_cond_destroy(&(outMessages.cond)), "pthread_cond_destroy()");
}

// ======= Chatroom functions
// Functions for managing message queue
// Note to self: messages on the queue are meant for broadcasting
// Adds given message to the queue
int enqueueMessage(char* message, client_info* sender)
{
    if(strlen(message) + strlen(sender->username) + 3 >= MSG_BUFF_SIZE)
    {
        fprintf(stderr, "enqueueMessage(): message too long\n");
        return -1;
    }

    // Attempt to acquire the lock on the msg queue
    pthread_mutex_lock(&(outMessages.lock));

    // If the queue is full, we wait until it gets cleared up
    while(outMessages.length + 1 > MSG_QUEUE_SIZE)
    {
        pthread_cond_wait(&(outMessages.cond), &(outMessages.lock));
    }

    // Allocate space on heap for message
    char* messageBuffer = (char*)malloc(MSG_BUFF_SIZE);
    checkNull(messageBuffer, "Failed malloc()");
    memset(messageBuffer, '\0', MSG_BUFF_SIZE);
    
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

    return 0;
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

// Functions for managing user list
int addUser(client_info* newUser)
{
    printf("IN ADDUSER\n");
    if(newUser == NULL)
    {
        fprintf(stderr, "addUser(): user is NULL\n");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&(userList.lock));

    if(userList.length + 1 > MAX_CLIENT_NUM)
    {
        fprintf(stderr, "addUser(): No more room for new clients\n");
        pthread_mutex_unlock(&(userList.lock));
        return -1;
    }

    for(size_t i = 1; i < MAX_CLIENT_NUM; i++)
    {
        printf("ADDUSER(): %ld\n", i);
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
    // serverMsg will get freed up once the message is removed from queue
    // char* serverMsg = (char*) malloc(MSG_BUFF_SIZE);
    // checkNull(serverMsg, "Failed malloc()");

    // memset(serverMsg, '\0', MSG_BUFF_SIZE);
    // strcat(serverMsg, newUser->username);
    // strcat(serverMsg, "has joined the room\n");

    // // Note: array[0] is reserved for the server
    // enqueueMessage(serverMsg, userList.array[0]);

    serverAnnouncement(" has joined the room\n", newUser->username);

    // Print it for the server's display
    printf("%s has joined the room\n", newUser->username);

    pthread_mutex_unlock(&(userList.lock));
    return 0;
}

// Removes the user from the list
int removeUser(client_info* user)
{
    if(user == NULL)
    {
        fprintf(stderr, "removeUser(): user given is NULL\n");
        return -1;
    }
    // Lock the userlist
    pthread_mutex_lock(&(userList.lock));

    // (the id should be within the list range if we used addUser())
    if(user->id < 1 || user->id > MAX_CLIENT_NUM)
    {
        fprintf(stderr, "removeUser(): user id not valid\n");
        pthread_mutex_unlock(&(userList.lock));
        return -2;
    }
    else if(userList.array[user->id] == NULL)
    {
        fprintf(stderr, "removeUser(): user id %d doesn't exist\n", user->id);
        pthread_mutex_unlock(&(userList.lock));
        return -3;
    }

    // Announce the user is leaving
    serverAnnouncement(" has left the room\n", user->username);
    printf("%s has left the room\n", user->username);
    
    printf("REMOVED(): id %d\n", user->id);
    // Release the user and mark the slot as free again
    free(userList.array[user->id]);
    userList.array[user->id] = NULL;
    userList.length--;

    printf("REMOVED(): success\n");

    // Announce to other threads that there is now space in the list
    // (If anyone is possibly waiting)
    // Possibly no one if we reject new users at max capacity
    // Rather than letting them wait.
    pthread_cond_signal(&(userList.cond));
    pthread_mutex_unlock(&(userList.lock));
    return 0;
}

// Generic function for the server to use to make announcement to room
// username could be NULL if it is a message not directed towards anyone
int serverAnnouncement(char* message, char* username)
{
    if((username == NULL && strlen(message) >= MSG_BUFF_SIZE) ||
       (username != NULL && strlen(message) + strlen(username) >= MSG_BUFF_SIZE))
    {
        fprintf(stderr, "serverAnnouncement(): message too long\n");
        return -1;
    }

    char* serverMsg = (char*) malloc(MSG_BUFF_SIZE);
    checkNull(serverMsg, "Failed malloc()");
    memset(serverMsg, '\0', MSG_BUFF_SIZE);

    if(username != NULL)
    {
        strcat(serverMsg, username);
    }
    strcat(serverMsg, message);
    
    // put the message on the queue
    enqueueMessage(serverMsg, userList.array[0]);

    return 0;
}

// Sends the user list to the destination fd (the client's socket)
int sendUserList(int dest)
{
    if(dest < 0)
    {
        fprintf(stderr, "sendUserlist(): non-valid destionation\n");
        return -1;
    }

    char message[MSG_BUFF_SIZE];
    memset(message, '\0', MSG_BUFF_SIZE);

    strcat(message, "Users Online:\n");

    sendMessage(message, dest);

    pthread_mutex_lock(&(userList.lock));


    // No other user other than server
    if(userList.length == 1)
    {
        sendMessage("No other users online\n\n", dest);        
    }
    else
    {
        // Send the users one at a time
        // This ensures we would be able to give the entire list
        // without worrying about the message buffer
        for(size_t i = 1; i < MAX_CLIENT_NUM; i++)
        {
            if(userList.array[i] != NULL)
            {  
                memset(message, '\0', MSG_BUFF_SIZE);
                char* name = userList.array[i]->username;

                strcat(message, name);

                strcat(message, "\n");

                sendMessage(message, dest);
            }
        }

        memset(message, '\0', MSG_BUFF_SIZE);
        snprintf(message, MSG_BUFF_SIZE, "\nNumber of online users: %ld\n", userList.length-1);
        sendMessage(message, dest);
        sendMessage("\n", dest);
    }

    pthread_mutex_unlock(&(userList.lock));
    return 0;
}

// Functions for the threads
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

        printf("BROADCAST: START\n");

        // Now that we have a message to send out
        char* outboundMessage = outMessages.queue[0];

        // Lock for the list of clients
        pthread_mutex_lock(&(userList.lock));
        printf("BROADCAST: %ld\n", userList.length);
        // Send the message out per client
        // userList[0] shall be reserved for server
        for(size_t i = 1; i < MAX_CLIENT_NUM; i++)
        {
            // Note: we are also sending the message back to its sender
            // This acts as an echo for the user who wrote the message
            // and forces an ordering of the messages that is based on 
            // how the server received them
            if(userList.array[i] != NULL)
            {
                printf("SEND MESSAGE TO %s\n", userList.array[i]->username);
                sendMessage(outboundMessage, userList.array[i]->sockfd);
            }
        }

        // Free up the message we just sent from the queue
        // MUST HAVE THE LOCK to message queue
        // We should remove it then signal that the queue has more space
        // to the producer threads
        removeMessage();

        printf("BROADCAST: FINISHED\n");

        pthread_cond_signal(&(outMessages.cond));
        pthread_mutex_unlock(&(userList.lock));
        pthread_mutex_unlock(&(outMessages.lock));

    }
}

// fd here should be the client socket from Accept()
void* clientThread(void* fd)
{
    // First mark the thread as detached
    pthread_detach(pthread_self());

    int clientSock = *((int*)fd);
    char buffer[MSG_BUFF_SIZE];
    memset(buffer, '\0', MSG_BUFF_SIZE);

    // client's first message should be its username
    if(read(clientSock, buffer, MAX_USERNAME_SIZE) < 0)
    {
        perror("clientThread: failed to read username");
        pthread_exit((void*)-1);
    }

    client_info* user = (client_info*) malloc(sizeof(client_info));
    checkNull(user, "Failed malloc()");
    memset(user->username, '\0', MAX_USERNAME_SIZE);

    strcat(user->username, buffer);
    user->sockfd = clientSock;

    // Send the list over the newly connected client
    sendUserList(clientSock);

    // Try to add to the list
    // If fail, then we just inform the user
    // And reject their request
    if(addUser(user) < 0)
    {
        sendMessage("Room is full\n", clientSock);
        memset(buffer, '\0', MSG_BUFF_SIZE);
        snprintf(buffer, MSG_BUFF_SIZE, "Room capacity: %d\n", MAX_CLIENT_NUM-1);
        sendMessage(buffer, clientSock);
        free(user);
        close(clientSock);
        pthread_exit(EXIT_SUCCESS);
    }

    printf("NEW USER name: %s\n", user->username);
    printf("NEW USER sockfd: %d\n", user->sockfd);
    printf("NEW USER ID: %d\n", user->id);

    // send a greeting
    sendMessage(GREETING, clientSock);

    memset(buffer, '\0', MSG_BUFF_SIZE);
    // Now start to take in user messages
    while(read(clientSock, buffer, MSG_BUFF_SIZE) > 0)
    {
        // Client told us, it is exiting the chat
        // Move on to cleanup
        if(strcmp(buffer, "QUIT\n") == 0)
        {
            break;
        }

        printf("%s: %s\n", user->username, buffer);
        // Theory
        enqueueMessage(buffer, user);
    }

    // Note: by the time the server gets "QUIT\n"
    // The client on the other end, should have already exited
    // Having the server send messages to the client at this point
    // Is an error

    // Removes the user from the list and informs the room
    removeUser(user);

    printf("CLIENT_THREAD with sockfd %d: EXITING\n", clientSock);
    // free up the int we stored on the heap
    free(fd);

    pthread_exit(0);
}

// =========== start of main
int main(int argc, char* argv[])
{
    // Goes through the arguments and sets any changes to defaults
    parseInput(argc, argv);

    printf("PORT: %d\n", SERVER_PORT);

    // init the data structures for msg queue and user list
    p_init_userlist();
    p_init_msgqueue();

    // Start up the thread for broadcasting
    // Note: We dont need the thread id since we setting the thread to detach
    // No need for the main to join it.
    pthread_t broadcast_tid;
    pthread_create(&broadcast_tid, NULL, broadcastThread, NULL);

    printf("PASSED BROADCAST\n");
    // Setup the socket for server
    // Variables
    // FD for serversocker and the client connection
    int serverSocket;
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

    // Enter infinite loop
    // Main thread goes and accepts any new clients
    // Starts up a new thread for client
    while(1)
    {
        struct sockaddr_in clientInfo = {0};
        socklen_t clientSize = sizeof(clientInfo);
        // The client thread has to clean this up
        int* clientConn = malloc(sizeof(int));
        checkNull(clientConn, "Failed malloc()");
        // int connected = 0;

        printf("\nWaiting for client\n");

        *clientConn = accept(serverSocket, (struct sockaddr*) &clientInfo, 
                                                            &clientSize);

        errorCheck(*clientConn, "Failed Accept: ");

        printf("CLIENT CONNECTED\n\n");

        // char clientAddr[INET_ADDRSTRLEN];
        // if(inet_ntop(AF_INET, &clientInfo.sin_addr, clientAddr, 
        //                                         INET_ADDRSTRLEN) == NULL)
        // {
        //     perror("inet_ntop()");
        //     exit(EXIT_FAILURE);
        // }

        // printf("Client Addr: %s\n", clientAddr);

        pthread_t client_tid; // not actually used
        pthread_create(&client_tid, NULL, clientThread, (void*)clientConn);

    }
    // Never reaches
    errorCheck(close(serverSocket), "failed close()");            
    p_free_userlist();
    p_free_msgqueue();
    return 0;
}