/*
    Name: David Zheng
    CS 3393 - Unix Systems Programming
    HW6: Chatroom
    Due Date: April 23, 2020
*/

// SERVER
#define _GNU_SOURCE
#define _POSIX_C_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>      // strlen
#include <sys/socket.h>     // socket, AF_INET, SOCK_STREAM
#include <netinet/in.h>     // servaddr, htons, htonl
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>

// marcos
#define LISTEN_BUFF 10 // for the queue size for listen()
#define MSG_BUFF_SIZE 2048
#define MAX_USERNAME_SIZE 1024
#define MAX_CLIENT_NUM 20
#define MSG_QUEUE_SIZE 20
#define GREETING "Welcome User! Feel free to speak your mind!\n"

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
} client_list;

typedef struct{
    char* queue[MSG_QUEUE_SIZE];
    size_t length;
    pthread_mutex_t lock;
    pthread_cond_t condConsumer; // for signalling to Consumer thread
    pthread_cond_t condProducer; // for signalling to Producer thread
} message_queue;

// Struct for a string of unknown sizes (allocated on the heap)
typedef struct
{
    char* message;
    size_t length;
    size_t capacity;
} dynamic_string;


// Should be global for easy accessibility between threads
client_list userList;
message_queue outMessages;

// Function prototypes
void checkNull(void* arrayPtr, const char* errorMsg);
void errorCheck(int errorCode, char* message);
void p_errorCheck(int errorCode, char* message);
void parseInput(int argc, char** argv);
void p_init_userlist(void);
void p_init_msgqueue(void);
void p_free_userlist(void);
void p_free_msgqueue(void);
void DS_init(dynamic_string* target, size_t initCap);
void DS_free(dynamic_string* target);
void* broadcastThread(void* args);
void* clientThread(void* fd);

int DS_appendMessage(dynamic_string* dest, const char* message);
int enqueueMessage(char* message, client_info* sender);
int removeMessage(void);
int addUser(client_info* newUser);
int removeUser(client_info* user);
int serverAnnouncement(char* message, char* username);
int sendUserList(int dest);

size_t sendMessage(const char* msg, int dest);

// ====== Utils
// dest should be the address to the dynamic string struct
// This function makes sure that there is enough space in dest->message
// for strcat(), otherwise it will realloc enough space
int DS_appendMessage(dynamic_string* dest, const char* message)
{
    if(dest == NULL)
    {
        fprintf(stderr, "DS_appendMessage(): dest is NULL\n");
        return -1;
    }
    if(message == NULL)
    {
        fprintf(stderr, "DS_appendMessage(): message is NULL\n");
        return -1;
    }

    size_t msgLen = strlen(message);
    size_t newCap = 0;

    // Inital message, just make the size the same length as the message
    if(dest->message == NULL)
    {
        newCap = (msgLen + 1)* sizeof(char);        
        dest->message = (char*) malloc(newCap);
        checkNull(dest->message, "Failed malloc()");
        dest->capacity = newCap;
        memset(dest->message, '\0', newCap);
    }
    // Realloc if our buffer is not long enough
    else if(dest->length + msgLen + 1 >= dest->capacity)
    {
        newCap = 2 * (dest->length + msgLen + 1) * sizeof(char);
        char* temp = (char*) realloc(dest->message, newCap);
        checkNull(temp, "Failed realloc()");
        dest->message = temp;
        dest->capacity = newCap;
    }

    // concat message with existing string
    strcat(dest->message, message);
    dest->length = dest->length + msgLen;

    return 0;
}

// initializes the dynamic_string struct for use
// Optional initial size if wanted
// Do not init twice on same object
void DS_init(dynamic_string* target, size_t initCap)
{
    if(target == NULL)
    {
        fprintf(stderr, "DS_init(): target is null\n");
        exit(EXIT_FAILURE);
    }

    if(initCap > 0)
    {
        target->message = (char*) malloc(initCap * sizeof(char));
        checkNull(target->message, "Failed malloc()");
        memset(target->message, '\0', initCap);
        target->capacity = initCap;
    }
    else
    {
        // if message is null, DS_appendMessage() will allocate for us
        target->message = NULL;
        target->capacity = 0;
    }

    target->length = 0;

}

// Cleans up the dynamic_string struct
// mainly just frees up the memory allocated for the message
void DS_free(dynamic_string* target)
{
    if(target == NULL)
    {
        fprintf(stderr, "DS_free(): target is null\n");
        exit(EXIT_FAILURE);
    }

    free(target->message);
}

// erases existing content in dynamic_string (not free up the memory)
void DS_clear(dynamic_string* target)
{
    if(target == NULL)
    {
        fprintf(stderr, "DS_clear(): target is null\n");
        exit(EXIT_FAILURE);
    }

    if(target->message != NULL)
    {
        memset(target->message, '\0', target->capacity);
        target->length = 0;
    }
}

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

// errorCode for pthread functions
void p_errorCheck(int errorCode, char* message)
{
    if(errorCode > 0)
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
    int numWritten = 0;
    size_t msgLen = strlen(msg);

    size_t buffLen = (msgLen+1) * sizeof(char);
    char* messageBuffer = (char*) malloc(buffLen);
    checkNull(messageBuffer, "Failed malloc()");
    memset(messageBuffer, '\0', buffLen);

    // Copy the message into the buffer
    strncpy(messageBuffer, msg, msgLen);
    
    // Write it to dest
    if((numWritten = write(dest, messageBuffer, buffLen)) !=  buffLen)
    {
        perror("sendMessage(): write failed: ");
        free(messageBuffer);
        exit(EXIT_FAILURE);
    }

    free(messageBuffer);

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

    p_errorCheck(pthread_mutex_init(&(userList.lock), NULL), 
                                                "pthread_mutex_init()");

    // The server is the first and permanent user
    // The server never gets removed from the list until process dies
    client_info* serverInfo = (client_info*) malloc(sizeof(client_info));
    checkNull(serverInfo, "Failed malloc()");
    memset(serverInfo->username, '\0', MAX_USERNAME_SIZE);
    strcat(serverInfo->username,  "SERVER");
    serverInfo->id = 0;

    userList.array[0] = serverInfo;
}

// Initializes the message queue
void p_init_msgqueue(void)
{   
    if(MSG_QUEUE_SIZE <= 0)
    {   
        fprintf(stderr, "Message queue size can't be 0\n");
        exit(EXIT_FAILURE);
    }

    outMessages.length = 0;
    memset(outMessages.queue, 0, MSG_QUEUE_SIZE * sizeof(char*));
    p_errorCheck(pthread_mutex_init(&(outMessages.lock), NULL), 
                                                    "pthread_mutex_init()");
    p_errorCheck(pthread_cond_init(&(outMessages.condConsumer), NULL), 
                                                    "pthread_cond_init()");
    p_errorCheck(pthread_cond_init(&(outMessages.condProducer), NULL), 
                                                    "pthread_cond_init()");
}

// Cleans up userlist
void p_free_userlist(void)
{
    for(size_t i = 0; i < MAX_CLIENT_NUM; i++)
    {
        free(userList.array[i]);
    }

    p_errorCheck(pthread_mutex_destroy(&(userList.lock)), 
                                                    "pthread_mutex_destroy()");
}

// Cleans up msgqueue
void p_free_msgqueue(void)
{
    for(size_t i = 0; i < MSG_QUEUE_SIZE; i++)
    {
        free(outMessages.queue[i]);
    }

    p_errorCheck(pthread_mutex_destroy(&(outMessages.lock)), 
                                                    "pthread_mutex_destroy()");
    p_errorCheck(pthread_cond_destroy(&(outMessages.condConsumer)), 
                                                    "pthread_cond_destroy()");
    p_errorCheck(pthread_cond_destroy(&(outMessages.condProducer)), 
                                                    "pthread_cond_destroy()");
}

// ======= Chatroom functions
// Functions for managing message queue

// Adds given message to the queue
// Note to self: messages on the queue are meant for broadcasting
// Note 2: it makes its own copy of the message, so it doesn't matter
// if the message gets freed up after a call to enqueueMessage() by the caller
int enqueueMessage(char* message, client_info* sender)
{
    // Attempt to acquire the lock on the msg queue
    p_errorCheck(pthread_mutex_lock(&(outMessages.lock)), "mutex_lock()");

    // If the queue is full, we wait until it gets cleared up
    while(outMessages.length + 1 > MSG_QUEUE_SIZE)
    {
        p_errorCheck(pthread_cond_wait(&(outMessages.condProducer), 
                                    &(outMessages.lock)), "cond_wait()");
    }

    // Allocate space on heap for message
    size_t buffLen = (strlen(message) + strlen(sender->username) + 3) * 
                                                                sizeof(char);
    char* messageBuffer = (char*) malloc(buffLen);
    checkNull(messageBuffer, "Failed malloc()");
    memset(messageBuffer, '\0', buffLen);

    // Add username to the message
    strcat(messageBuffer, sender->username);
    strcat(messageBuffer, ": ");
    strcat(messageBuffer, message);

    // Add to queue
    outMessages.queue[outMessages.length] = messageBuffer;
    outMessages.length++;

    // Signal the consumer that there is a new message
    p_errorCheck(pthread_cond_signal(&(outMessages.condConsumer)), "cond_signal()");
    p_errorCheck(pthread_mutex_unlock(&(outMessages.lock)), "mutex_unlock()");

    return 0;
}

// Remove message from queue
// Yes, this isn't optimal
// This is to be used in conjunction with a function
// that already has acquired the lock for the message queue
// Otherwise, risk corrupting the queue
int removeMessage(void)
{
    if(outMessages.length <= 0)
    {
        fprintf(stderr, "removeMessage(): message queue empty\n");
        return -1;
    }

    // Free up the message in the head of queue
    free(outMessages.queue[0]);

    // Shift items over to the left, writing over the item at index
    for(size_t i = 0; i < MSG_QUEUE_SIZE-1; i++)
    {
        outMessages.queue[i] = outMessages.queue[i+1];
    }

    // decrease length by 1
    outMessages.length--;
    return 0;
}

// Functions for managing user list
// Adds a new user to the list
// Basically adds the pointer to the client_info struct for book keeping
int addUser(client_info* newUser)
{
    if(newUser == NULL)
    {
        fprintf(stderr, "addUser(): user is NULL\n");
        exit(EXIT_FAILURE);
    }

    p_errorCheck(pthread_mutex_lock(&(userList.lock)), "mutex_lock()");

    // If we full, then we must reject this user
    // Have client thread handle cleanup
    if(userList.length + 1 > MAX_CLIENT_NUM)
    {
        fprintf(stderr, "addUser(): No more room for new clients\n");
        p_errorCheck(pthread_mutex_unlock(&(userList.lock)), "mutex_unlock()");
        return -1;
    }

    // Go find an empty slot
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

    serverAnnouncement(" has joined the room\n", newUser->username);

    // Print it for the server's display
    printf("%s has joined the room\n", newUser->username);

    p_errorCheck(pthread_mutex_unlock(&(userList.lock)), "mutex_unlock()");
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
    p_errorCheck(pthread_mutex_lock(&(userList.lock)), "mutex_lock()");

    // (the id should be within the list range if we used addUser())
    if(user->id < 1 || user->id > MAX_CLIENT_NUM)
    {
        fprintf(stderr, "removeUser(): user id not valid\n");
        p_errorCheck(pthread_mutex_unlock(&(userList.lock)), "mutex_unlock()");
        return -2;
    }
    else if(userList.array[user->id] == NULL)
    {
        fprintf(stderr, "removeUser(): user id %d doesn't exist\n", user->id);
        p_errorCheck(pthread_mutex_unlock(&(userList.lock)), "mutex_unlock()");
        return -3;
    }

    // Announce the user is leaving
    serverAnnouncement(" has left the room\n", user->username);
    printf("%s has left the room\n", user->username);

    // Release the user and mark the slot as free again
    free(userList.array[user->id]);
    userList.array[user->id] = NULL;
    userList.length--;

    p_errorCheck(pthread_mutex_unlock(&(userList.lock)), "mutex_unlock()");
    return 0;
}

// Generic function for the server to use to make announcement to room
// username could be NULL if it is a message not directed towards anyone
int serverAnnouncement(char* message, char* username)
{
    if(message == NULL)
    {
        fprintf(stderr, "ServerAnnoucement: message is null\n");
        return -1;
    }

    size_t nameLen = 0;

    if(username != NULL)
    {
        nameLen = strlen(username);
    }

    size_t buffLen = (strlen(message) + nameLen + 1) * sizeof(char);
    char* serverMsg = (char*) malloc(buffLen);
    checkNull(serverMsg, "Failed malloc()");
    memset(serverMsg, '\0', buffLen);

    // The message has a user of interest
    // EX: "User1 has left the room"
    if(username != NULL)
    {
        strcat(serverMsg, username);
    }

    strcat(serverMsg, message);
    
    // put the message on the queue
    enqueueMessage(serverMsg, userList.array[0]);

    // Free it up after we enqueue on the queue
    // The queue has its own copy
    free(serverMsg);

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

    dynamic_string messageBuffer;
    DS_init(&messageBuffer, 0);
    DS_appendMessage(&messageBuffer, "Users Online:\n");

    p_errorCheck(pthread_mutex_lock(&(userList.lock)), "mutex_lock()");

    // No other user other than server
    if(userList.length == 1)
    {
        DS_appendMessage(&messageBuffer, "No other users online\n\n");     
    }
    else
    {
        // Add each username to our message
        for(size_t i = 1; i < MAX_CLIENT_NUM; i++)
        {
            if(userList.array[i] != NULL)
            {  
                DS_appendMessage(&messageBuffer, userList.array[i]->username);
                DS_appendMessage(&messageBuffer, "\n");
            }
        }

        // Definitely overkill in space
        char userCount[MAX_CLIENT_NUM];
        memset(userCount, '\0', MAX_CLIENT_NUM);

        DS_appendMessage(&messageBuffer, "\nNumber of online users: ");

        snprintf(userCount, MAX_CLIENT_NUM, "%ld", userList.length-1);
        DS_appendMessage(&messageBuffer, userCount);
        DS_appendMessage(&messageBuffer, "\n\n");
    }

    // Send our message over to client (properly formatted)
    sendMessage(messageBuffer.message, dest);
    // Free up our dynamic string
    DS_free(&messageBuffer);

    p_errorCheck(pthread_mutex_unlock(&(userList.lock)), "mutex_unlock()");
    return 0;
}

// Functions for the threads (thread is created in detached state)
// Thread for reading from message queue and broadcast 
// whenever there is a message 
void* broadcastThread(void* args)
{

    while(1)
    {
        p_errorCheck(pthread_mutex_lock(&(outMessages.lock)), "mutex_lock");

        // While there is no messages, unlock and block until there is one
        while(outMessages.length == 0)
        {
            p_errorCheck(pthread_cond_wait(&(outMessages.condConsumer), 
                                &(outMessages.lock)), "pthread_cond_wait()");
        }

        // Now that we have a message to send out
        char* outboundMessage = outMessages.queue[0];

        // Lock for the list of clients
        p_errorCheck(pthread_mutex_lock(&(userList.lock)), "mutex_lock");

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
                sendMessage(outboundMessage, userList.array[i]->sockfd);
            }
        }

        // Free up the message we just sent from the queue
        // MUST HAVE THE LOCK to message queue
        // We should remove it then signal that the queue has more space
        // to the producer threads
        removeMessage();

        // Signal to threads that the queue is now has space
        p_errorCheck(pthread_cond_signal(&(outMessages.condProducer)), 
                                                            "cond_signal()");
        p_errorCheck(pthread_mutex_unlock(&(userList.lock)), 
                                                            "mutex_unlock()");
        p_errorCheck(pthread_mutex_unlock(&(outMessages.lock)), 
                                                            "mutex_unlock()");

    }
}

// fd here should be the client socket from Accept()
// client function (the thread is created in detach state)
void* clientThread(void* fd)
{

    int clientSock = *((int*)fd);

    // The check for the size is done in main()
    // MSG_BUFF_SIZE >= MAX_USERNAME_SIZE
    char buffer[MSG_BUFF_SIZE + 1];
    size_t buffSize = sizeof(buffer);
    memset(buffer, '\0', buffSize);

    dynamic_string clientInput;

    // client's first message should be its username
    if(read(clientSock, buffer, MAX_USERNAME_SIZE) < 0)
    {
        perror("clientThread: failed to read username");
        pthread_exit(0);
    }

    client_info* user = (client_info*) malloc(sizeof(client_info));
    checkNull(user, "Failed malloc()");
    memset(user->username, '\0', MAX_USERNAME_SIZE);

    strcat(user->username, buffer);
    user->sockfd = clientSock;

    // Send the list over the newly connected client
    sendUserList(clientSock);

    // Try to add to the list
    // If fail, then we just inform the user and reject their request
    if(addUser(user) < 0)
    {
        // Log that we rejected a user
        printf("Room at full capacity, Rejected USER -> %s\n", user->username);

        // using DS to avoid problems with a small buffSize
        // causing problems for snprintf
        DS_init(&clientInput, 0);
        DS_appendMessage(&clientInput, "Room is full\nRoom capacity: ");
        memset(buffer, '\0', buffSize);
        snprintf(buffer, buffSize, "%d\n", MAX_CLIENT_NUM-1);
        DS_appendMessage(&clientInput, buffer);

        // Send the formatted message
        sendMessage(clientInput.message, clientSock);

        // Cleanup
        free(user);
        DS_free(&clientInput);
        errorCheck(close(clientSock), "close()");
        pthread_exit(EXIT_SUCCESS);
    }
    // At this point, user has been accepted and added into the list

    // send a greeting
    sendMessage(GREETING, clientSock);

    memset(buffer, '\0', MSG_BUFF_SIZE);

    // Now start to take in user messages
    while(read(clientSock, buffer, buffSize) > 0)
    {
        // Client told us, it is exiting the chat
        // Move on to cleanup
        if(strcmp(buffer, "QUIT\n") == 0)
        {
            break;
        }

        DS_init(&clientInput, 0);
        DS_appendMessage(&clientInput, buffer);

        int flags = fcntl(clientSock, F_GETFL);
        fcntl(clientSock, F_SETFL, flags | O_NONBLOCK);

        errno = 0;
        // See if there is anymore from the client
        while(read(clientSock, buffer, buffSize) > 0)
        {
            DS_appendMessage(&clientInput, buffer);
        }

        // ignoring errors we would be expecting
        // EFAULT = losing connection to client
        // EAGAIN / EWOULDBLOCk = Nothing else to read from socket
        if(errno != 0 && errno != EFAULT && 
                                (errno != EAGAIN || errno != EWOULDBLOCK))
        {
            fprintf(stderr, "clientThread(): read from clientSock failed\n");
            fprintf(stderr, "error: %s\n", strerror(errno));
            DS_free(&clientInput);
            errorCheck(close(clientSock), "close()");
            pthread_exit(0); //the return val isn't even used
        }

        enqueueMessage(clientInput.message, user);

        DS_free(&clientInput);

        // reset the flags (basically forces us to block for the next input)
        fcntl(clientSock, F_SETFL, flags);
    }

    // Note: by the time the server gets "QUIT\n"
    // The client on the other end, should have already exited
    // Having the server send messages to the client at this point is an error
    
    // Removes the user from the list and informs the room
    removeUser(user);
    
    // close the socket
    errorCheck(close(clientSock), "close()");

    // free up the int we stored on the heap
    free(fd);

    pthread_exit(0);
}

// =========== start of main
int main(int argc, char* argv[])
{
    if(MSG_BUFF_SIZE < MAX_USERNAME_SIZE)
    {
        fprintf(stderr, "MSG_BUFF_SIZE should at least MAX_USERNAME_SIZE\n");
        exit(EXIT_FAILURE);
    }

    // Goes through the arguments and sets any changes to defaults
    parseInput(argc, argv);

    printf("PORT: %d\n", SERVER_PORT);

    // init the data structures for msg queue and user list
    p_init_userlist();
    p_init_msgqueue();

    // Thread attribute for making a detach thread
    pthread_attr_t detachAttr;
    p_errorCheck(pthread_attr_init(&detachAttr), "pthread_attr_init()");
    p_errorCheck(pthread_attr_setdetachstate(&detachAttr, 
                    PTHREAD_CREATE_DETACHED), "pthread_attr_setdetachstate()");

    // Start up the thread for broadcasting
    // Note: We dont need the thread id since we setting the thread to detach
    // No need for the main to join it.
    pthread_t broadcast_tid;
    p_errorCheck(pthread_create(&broadcast_tid, &detachAttr, 
                                    broadcastThread, NULL), "pthread_create" );

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
        // The client thread has to clean this up
        int* clientConn = malloc(sizeof(int));
        checkNull(clientConn, "Failed malloc()");

        printf("Main(): Waiting for client\n");

        *clientConn = accept(serverSocket, NULL, NULL);

        errorCheck(*clientConn, "Failed Accept: ");

        printf("Main(): Client connected, creating client thread\n");

        pthread_t client_tid; // not actually used
        p_errorCheck(pthread_create(&client_tid, &detachAttr, 
                        clientThread, (void*)clientConn),"pthread_create()");
    }

    // Never reaches
    errorCheck(close(serverSocket), "failed close()");      
    p_errorCheck(pthread_attr_destroy(&detachAttr), "pthread_attr_destroy()");
    p_free_userlist();
    p_free_msgqueue();
    return 0;
}