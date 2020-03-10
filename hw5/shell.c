/*
    Name: David Zheng
    CS 3393 
    Homework Assignment #5 - Shell (Enhanced)
    Due Date: 
*/

#define _GNU_SOURCE

// Needed for sigaction
#define _POSIX_C_SOURCE

#include <stdio.h> // printf
#include <stdlib.h> // std library methods
#include <string.h> // for string methods
#include <errno.h> // for errno
#include <sys/wait.h> // wait()
#include <unistd.h> // fork(), exec(), and others
#include <fcntl.h> // open()
#include <signal.h> // signals
#include <setjmp.h> // sigsetjmp, siglongjmp
#include <limits.h> // for PATH_MAX
#include <ctype.h> // isspace()

// For starting size of dynamic array
#define START_CAPACITY 1

// jmp location = right before the main loop()
sigjmp_buf resetPrompt;

// flag for making sure setjmp was called and before jumping
static volatile sig_atomic_t jmpActive = 0; 
// num of child processes
static volatile sig_atomic_t numChild = 0;

typedef struct {
    char** array;
    size_t length;
    size_t capacity;
} args_array;

// Its a struct that holds args_arrays
typedef struct {
    args_array** array;
    size_t length;
    size_t capacity;
} args_array_set;


// Function prototypes
void printChildStatus(int status);
void checkNull(void* arrayPtr, const char* errorMsg);
void clearArray(args_array* target);
void errorCheck(int errorCode, char* message, int restart);
void IORedirect(args_array* argv);
void executeCMD(args_array_set* argSet);
void handleSIG(int sig);
void setSimpleDeposition(int depo);
void clearArgsSet(args_array_set* target);
void setupPipeChain(args_array_set* argSet);

int appendToken(args_array* dest, char* token);
int popToken(args_array* argv, size_t index);
int builtin(char* cmd, char** argv);
int appendArgsSet(args_array_set* dest, args_array* item);
int parseInput(char* userInput, args_array_set* result);

args_array* createArgsArray(char* argString);

// Some global structs for sigaction. static to limit the visibility.
static struct sigaction default_act = {.sa_handler= SIG_DFL};
static struct sigaction ignore_act = {.sa_handler= SIG_IGN};
static struct sigaction sigint_act = {.sa_handler= handleSIG, 
                                .sa_flags = SA_RESTART};


// Signal handlers
void handleSIG(int sig)
{
    // Makes sure we don't try to jmp if setjmp hasn't been set yet
    // Or redo the prompt if we dont have a child we have to wait for
    if(jmpActive == 0 || numChild <= 0)
    {
        siglongjmp(resetPrompt, -1);
        return;
    }

    // parent process needs to wait for the child to get terminated
    // by the signal
    int status = 0;
    pid_t childPid;

    while(numChild > 0)
    {
        childPid = wait(&status);
        numChild--;
        printf("Child has returned with PID: %d\n", childPid);
        printChildStatus(status);
    }

    // restart the prompt
    // Need to use siglongjmp since sigaction adds the signal into the proc 
    // mask before we enter. So we have to restore the original proc mask
    // if we are jumping out of the signal handler
    siglongjmp(resetPrompt, -2);
}

// Frees everything in args_array_set, (not free target)
void clearArgsSet(args_array_set* target)
{
    if(target->array != NULL && target->length >= 0)
    {
        printf("ClearSET() TargetLength: %ld\n", target->length);
        for(size_t i = 0; i < target->length; i++)
        {
            // Free the args_array's array then the struct itself
            printf("ClearSET() Target[i] Length: %ld\n", target->array[i]->length);

            for(size_t j = 0; j < target->array[i]->length; j++)
            {
                printf("ClearSET() in argsArray[i]: %ld %s\n", strlen(target->array[i]->array[j]), target->array[i]->array[j]);
            }

            free(target->array[i]->array);
            free(target->array[i]);
        }

        target->length = 0;
        target->array[0] = NULL;

        printf("Out of clearSET()\n");
    }
}

int appendArgsSet(args_array_set* dest, args_array* item)
{
    printf("I AM IN AasdadadadadadasdasdasdadasdasdasdPPEND\n");
    if(dest == NULL || item == NULL)
    {
        fprintf(stderr, "appendArgsArray(): no valid destionation / item\n");
        exit(EXIT_FAILURE);
    }

    // Initialize if NULL
    if(dest->array == NULL)
    {
        // Plus 1 for null terminate
        args_array** temp = malloc((START_CAPACITY + 1) * sizeof(args_array*));

        checkNull(temp, "appendArgsArray() Failed malloc");

        dest->array = temp;
        dest->length = 0;
        dest->capacity = START_CAPACITY + 1;
    }

    // Resize if needed
    if(dest->length == dest->capacity)
    {
        args_array** temp = realloc(dest->array, 2 * dest->capacity * sizeof(args_array*));

        checkNull(temp, "appendArgsArray() Failed realloc");

        dest->array = temp;
        dest->capacity = 2 * dest->capacity;
    }

    dest->array[dest->length] = item;
    dest->length++;

    // Null terminate
    dest->array[dest->length] = NULL;

    for(args_array** i = dest->array; *i != NULL; i++)
    {
        printf("appendArgsArray(): %s\n", *((*i)->array));
    }

    return 0;
}

// Creates an args_array object on the heap given a string
args_array* createArgsArray(char* argString)
{
    if(argString == NULL || 
        strlen(argString) == 0 || 
            strcmp(argString, "\n") == 0)
    {
        fprintf(stderr, "createArgsArray(): argString is empty or null\n");
        return NULL;
    }

    printf("createArgsArray(): IN CREATE ARGS\n");

    // Delimiter list
    const char* delim = " ";

    // Allocate an args_array on the heap
    args_array* result = malloc(sizeof(args_array));
    result->array = NULL;
    result->length = 0;
    result->capacity = 0;

    checkNull(result, "failed to malloc args_array");

    // get first token
    char* token = strtok(argString, delim);

    while(token != NULL)
    {
        appendToken(result, token);
        token = strtok(NULL, delim);
    }

    printf("createArgsArray(): len %ld\n", result->length);
    printf("createArgsArrayy(): cap %ld\n", result->capacity);

    for(char** i = result->array; *i != NULL; i++)
    {
        printf("createArgsArray(): %s\n", *i);
    }

    return result;
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
       } 
       else
       {
           exit(EXIT_FAILURE);
       }
    }
}

// Clears the array within a args_array struct
// Just be setting parameters.  (not the same as freeing)
// We might still want to use the array itself
void clearArray(args_array* target)
{
    if(target != NULL && target->length > 0) 
    {
        target->length = 0;
        target->array[0] = NULL;
    }
}

// Adds an string / token to array
int appendToken(args_array* dest, char* token)
{
    if(dest == NULL)
    {
        fprintf(stderr, "appendToken(): dest is NULL\n");
        return -1;
    }

    // Initialize if NULL
    if(dest->array == NULL)
    {
        // Plus 1 for null terminate
        char** temp = malloc((START_CAPACITY + 1) * sizeof(char*));

        checkNull(temp, "appendToken() Failed malloc");

        dest->array = temp;
        dest->length = 0;
        dest->capacity = START_CAPACITY + 1;
    }

    // Resize if needed
    if(dest->length == dest->capacity)
    {
        char** temp = realloc(dest->array, 2 * dest->capacity * sizeof(char*));

        checkNull(temp, "appendToken() Failed realloc");

        dest->array = temp;
        dest->capacity = 2 * dest->capacity;
    }

    dest->array[dest->length] = token;
    dest->length++;

    // Null terminate
    dest->array[dest->length] = NULL;

    return 0;
}

// Removes a token from array (argv)
int popToken(args_array* argv, size_t index)
{
    if(argv == NULL)
    {
        fprintf(stderr, "popToken(): given array is null\n");
        return -1;
    }

    // Either nothing to pop or index is way out of range
    if(index >= argv->length || index < 0 || argv->length == 0)
    {
        return -1;
    }

    // Shift items over to the left, writing over the item at index
    for(size_t i = index; argv->array[i] != NULL; i++)
    {
        argv->array[i] = argv->array[i+1];
    }

    // decrease length by 1
    argv->length--;
    return 0;
}

// Parses through the input string from stdin
// tokenizes it and adds it to our dynamic array representing argv
int parseInput(char* userInput, args_array_set* result)
{
    if(userInput == NULL || 
        strlen(userInput) == 0 || 
            strcmp(userInput, "\n") == 0)
    {
        fprintf(stderr, "parseInput(): userInput is NULL or empty\n");
        return -1;
    }

    if(result == NULL)
    {
        fprintf(stderr, "parseInput(): args_array_set is NULL\n");
        return -2;
    }

    // make sure we don't have pipe in the beginning or end
    if(userInput[0] == '|')
    {
        fprintf(stderr, "Pipe syntax found at the beginning of userInput. Invalid\n");
        return -3;
    }

    // The loop shouldn't even go beyond 3 or 2 characters of the string
    for(size_t i = strlen(userInput) - 1; i >= 0; i--)
    {
        if(userInput[i] == '|')
        {
            fprintf(stderr, "Pipe syntax found at the end of userInput. Invalid\n");
            return -3;
        }
        else if(isspace(userInput[i]) == 0)
        {
            break;
        }
    }

    // Delimiter list
    const char* delim = "|\n";

    // args_array_set* result = malloc(sizeof(args_array_set));
    // result->array = NULL;
    // result->length = 0;
    // result->capacity = 0;

    size_t inputLen = strlen(userInput);

    // get first token (delimited by |)
    char* token = strtok(userInput, delim);
    char* newLocation = userInput;

    while(token != NULL)
    {
        printf("ParseInput() token: %s\n", token);
        printf("ParseInput() tokenLen: %ld\n", strlen(token));
        // newLocation should be set to right after the | so we are parsing through a new set of commands

        // No checks needed for newLocation. It won't go over the boundary of userInput
        // At the last iteration newLocation would be equal to the NULL char at the end of userInput
        // Since userInput will always have a \n at the end of the string.
        newLocation = newLocation + strlen(token) + 1; // + 1 for the null char

        printf("new location: %s\n", newLocation);

        args_array* temp = createArgsArray(token);

        for(char** i = temp->array; *i != NULL; i++)
        {
            printf("ParseInput() confirm: %s\n", *i);
        }

        appendArgsSet(result, temp);

        // Sanity check and safety net
        if(newLocation - userInput >= inputLen)
        {
            printf("Edge Detected\n");
            break;
        }

        token = strtok(newLocation, delim);
    }

    printf("PraseInput() result len: %ld\n", result->length);

    for(size_t i = 0; i < result->length; i++)
    {
        printf("\nSet %ld\n", i);
        printf("\nargSet: %ld\n", result->array[i]->length);
        for(size_t j = 0; j < result->array[i]->length; j++)
        {
            // array[i] is a argArray struct which also has array of char**
            printf("%s ", result->array[i]->array[j]);
        }
        printf("\n");
    }


    return 0;
}

// Checks through a list of commands that the shell should be doing
// on its own process like cd
// return 1 if it executed a command, 0 if it did nothing
int builtin(char* cmd, char** argv)
{
	if (strcmp(cmd, "exit") == 0)
		exit(0);

	if (strcmp(cmd, "cd") == 0) {
        char* newDir = argv[1];

		if (argv[1] == NULL) { // cd by itself
            newDir = getenv("HOME");
			if (chdir(newDir) == -1) {
				perror("chdir");
			}
		}
		else {
			if (chdir(argv[1]) == -1) {
				perror("chdir");
			}
		}

        // make sure to update the environment variables
        // To be consistent with what cd changes.
        char* oldCWD; // old working directory
        oldCWD = getenv("PWD");

        checkNull(oldCWD, "builtin: getenv(PWD) failed");
        errorCheck(setenv("OLDPWD", oldCWD, 1), 
                                        "builtin: setenv(OLDPWD) failed", 0);

        char newCWD[PATH_MAX]; // PATH_MAX should be big enough usually

        // getcwd would return a NULL if anything goes wrong
        checkNull(getcwd(newCWD, PATH_MAX), "builtin: getcwd() failed");

        errorCheck(setenv("PWD", newCWD, 1), "builtin: setenv(PWD) failed", 0);

		return 1;
	}
	return 0;
}

// Parses through the input and check and apply any IO redirection operators
// Updates argv accordingly
// Note: just looks long cause of spacing
void IORedirect(args_array* argv)
{
    size_t i = 0;

    // Since argv->array is null terminated, i+1 is either a token or NULL
    // popToken() is safe to do during the iteration given that it also copies
    // the NULL at the end of the array to the left by one, 
    // moving the end of the array
    // So the argv->array[i+1] will still guaranteed to see NULL after shift
    while(argv->array[i+1] != NULL)
    {
        int fd = -1; // arbitary to indicate non used fd

        // Just checking if the string is a potential redirect operator
        if(strchr(argv->array[i], '<') != NULL || 
            strchr(argv->array[i], '>') != NULL)
        {
            if(strcmp(argv->array[i], "<") == 0)
            {
                fd = open(argv->array[i+1], O_RDONLY);
            }
            else if (strcmp(argv->array[i], ">>") == 0)
            {
                // Append if exist, create if doesn't
                fd = open(argv->array[i+1], O_WRONLY | O_APPEND 
                                                            | O_CREAT, 0644);
            }
            else if(strcmp(argv->array[i], ">") == 0 || 
                    strcmp(argv->array[i], "2>") == 0||
                    strcmp(argv->array[i], "&>") == 0 )
            {
                // Truncate if exist, create if doesn't
                fd = open(argv->array[i+1], O_WRONLY | O_TRUNC 
                                                            | O_CREAT, 0644);
            }
            else
            {
                // Its not an redirect operator that we support
                continue;
            }

            // Checks if any error occur during open()
            errorCheck(fd, argv->array[i+1], 0);

            // redirect
            // Working
            if (strcmp(argv->array[i], "<") == 0)
            {
                // First dup fd into 0, and close 0 if open
                errorCheck(dup2(fd, STDIN_FILENO), "IORedirect: dup2()", 0);
                fprintf(stdin, "stdin test1\n");
            }
            // Working
            else if (strcmp(argv->array[i], ">>") == 0 || 
                     strcmp(argv->array[i], ">") == 0)
            {
                // First dup2 fd into 0
                errorCheck(dup2(fd, STDOUT_FILENO), "IORedirect: dup2()", 0);
                fprintf(stdout, "STDOUT TEST1\n");
            }
            // Working
            else if(strcmp(argv->array[i], "2>") == 0)
            {
                // First dup2 fd into 2
                errorCheck(dup2(fd, STDERR_FILENO),"IORedirect: dup2()", 0);
                                fprintf(stderr, "STDERR TEST1\n");
            }
            // Working
            // Separating this into own if statement more readable
            else if(strcmp(argv->array[i], "&>") == 0)
            {
                // First dup2 fd into 1 and 2
                errorCheck(dup2(fd, STDOUT_FILENO), "IORedirect: dup2()", 0);
                                fprintf(stdout, "STDOUT TEST2\n");
                errorCheck(dup2(fd, STDERR_FILENO), "IORedirect: dup2()", 0);
                                fprintf(stderr, "STDERR TEST2\n");
            }
            
            // Close the file we opened
            errorCheck(close(fd), "IORedirect: close()", 0);
    
            // After redirection, remove the redirection operator and filename
            // from argv
            popToken(argv, i); // once for the operator
            popToken(argv, i); // once again for the file
        }
        else
        {
            i++;
        }
    }
}

// Given the status set by wait(), reads off the status mask
void printChildStatus(int status)
{
    // Status bit mask
    // 16 bits(lower) = 8 bit for exit code | 1 bit for core dump | 7 bit for 
    //                                                            signal number
    int childExitCode = (status & 0xFF00) >> 8;
    int generatedCoreDump = (status & 0x80);
    int signaled = (status & 0x7F);

    if ( childExitCode != 0 )
    {
        printf("Child Failed, Exited with %d\n", childExitCode);
    }
    else if(signaled)
    {
        printf("Child was terminated by a signal: %d\n", signaled);
        if(generatedCoreDump)
        {
            printf("Child process generated a core dump\n");
        }
    }
}

void groupWait(size_t numChildren)
{
    int status = 0;
    pid_t childPid;

    while(numChildren > 0)
    {
        childPid = wait(&status);
        numChildren--;
        if(childPid == -1)
        {
            continue; // no children to wait for
        }
        printf("Child has returned with PID: %d\n", childPid);
        printChildStatus(status);

    }
}

// Function to set the deposition for sigint, sigquit
// allows us to make changes to the deposition easier.
void setSimpleDeposition(int depo)
{
    if(depo == 0)
    {
        // setting to default deposition
        errorCheck(sigaction(SIGINT, &default_act, NULL), "sigaction()", 0);
        errorCheck(sigaction(SIGQUIT, &default_act, NULL), "sigaction()", 0);
    }
    else if(depo == 1)
    {
        // setting to ignore deposition
        errorCheck(sigaction(SIGINT, &ignore_act, NULL), "sigaction()", 0);
        errorCheck(sigaction(SIGQUIT, &ignore_act, NULL), "sigaction()", 0);
    }
    else if(depo == 2)
    {
        // setting to catch deposition
        errorCheck(sigaction(SIGINT, &sigint_act, NULL), "sigaction()", 0);
        errorCheck(sigaction(SIGQUIT, &sigint_act, NULL), "sigaction()", 0);
    }
}

// // handles a single pipe
// void executePIPE(char*** commandSet, size_t numSet)
// {
//     int fd[2];
//     // Ignore signals temp while we setup children.

//     setSimpleDeposition(1);
//     errorCheck(pipe(fd), "pipe()", 0);

//     // First child
//     if(fork() == 0)
//     {
//         // Reset to default
//         setSimpleDeposition(0);

//         // Do any IO redirection for the set
//         IORedirect(commandSet[0]);

//         // Close the reading end
//         errorCheck(close(fd[0]), "close(fd[0])", 0);
//         // Redirect our stdout to the writing end
//         errorCheck(dup2(fd[1], STDOUT_FILENO), "dup2()", 0);

//         // exits if failed exec()
//         errorCheck(execvp(commandSet[0][0], commandSet[0]), "exec()", 0);
//     }

//     // Second child
//     if(fork() == 0)
//     {
//         // Reset to default
//         setSimpleDeposition(0);

//         IORedirect(commandSet[0]);
    
//         // Close the writing end
//         errorCheck(close(fd[1]), "close(fd[1])", 0);

//         errorCheck(dup2(fd[0], STDIN_FILENO), "dup2()", 0);

//         errorCheck(execvp(commandSet[1][0], commandSet[1]), "exec()", 0);
//     }

//     // parent closes both ends to the pipe
//     errorCheck(close(fd[0]), "close()", 0);
//     errorCheck(close(fd[1]), "close()", 0);

//     numChild = 2;
//     // turn back on the signal handler
//     setSimpleDeposition(2);
//     groupWait(2);
// }


// creates child process to execute the given command
void executeCMD(args_array_set* argSet)
{
    int status = 0;

    if(argSet == NULL)
    {
        fprintf(stderr, "executeCMD(): argSet is NULL\n");
        return;
    }

    if(argSet-> length == 0)
    {
        fprintf(stderr, "executeCMD(): argSet has length 0. No Set\n");
        return;
    }

    // temp ignore signals while we setup
    setSimpleDeposition(1);

    // Now we fork and exec on the command
    pid_t cpid = fork();

    // Parent
    if (cpid > 0)
    {
        numChild = 1; // inform potential signal handler that we have a child

        setSimpleDeposition(2); // change back to original
        wait(&status);
        printChildStatus(status);
    } 
    // Child
    else if (cpid == 0)
    {
        // Reset SIGINT, SIGQUIT to default deposition
        setSimpleDeposition(0);

        // Do IO Redirection
        IORedirect(argSet->array[0]);

        // command and args for exec() later
        char* command = argSet->array[0]->array[0];
        char** args = argSet->array[0]->array;

        if(execvp(command, args) == -1)
        {
            perror("Failed to exec() ");
            printf("\n");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        setSimpleDeposition(2); // change back to original
        perror("Fork() Failed: ");
    }
}

void executePIPE(args_array_set* argSet)
{
    // First we would like to fork as the shell, and have the child
    // setup the piping for all the sets of commands in argSet
    int status = 0;

    if(argSet == NULL)
    {
        fprintf(stderr, "executeCMD(): argSet is NULL\n");
        return;
    }

    if(argSet-> length == 0)
    {
        fprintf(stderr, "executeCMD(): argSet has length 0. No Set\n");
        return;
    }

    // temp ignore signals while we setup
    setSimpleDeposition(1);

    // Now we fork and exec on the command
    pid_t cpid = fork();

    // Parent
    if (cpid > 0)
    {
        // inform potential signal handler that we have m
        numChild = argSet->length; 

        setSimpleDeposition(2); // change back to original
        groupWait(numChild);
    } 
    // Child
    else if (cpid == 0)
    {
        // Reset SIGINT, SIGQUIT to default deposition
        setSimpleDeposition(0);

        // Sets up the chain of pipes that we have
        // By creating another child and connecting the child 
        // to the pipes
        setupPipeChain(argSet);
    }
    else
    {
        setSimpleDeposition(2); // change back to original
        perror("Fork() Failed: ");
    }
}

void setupPipeChain(args_array_set* argSet)
{
    int fd[2];
    int previousIn = STDIN_FILENO;
    int status = 0;
    pid_t forkPID = 0;

    // Steps 0: create a pipe, and a child
    // Steps 1: remember the read end of the previous pipe
    //      - if there is no previous pipe, then we are the first in the chain
    //            thus, we read from stdin, and no changes needed to be made
    // Steps 2: In the child, we dup2(previousIn, 0), so we that read from the pipe that was created before
    // Steps 3: In the child, we dup2(fd[1], 1), so that we write to the write end of the pipe
    // Steps 4: In the child, close(fd[1], fd[0], PreviousIn) given, that we no longer need them
    //      - Note: PreviousIn in the current parent would still be open
    // Steps 5: In the parent, close(previousIn and fd[1]) we remember fd[0] as the new previousIn, for the next child in the chain
    // Steps 6: Repeat for n-1 sets
    // Steps 7: we are the last set in the chain, dup2(previousIn, 0). exec(last set)

    for(size_t i = 0; i < argSet->length-1; i++)
    {
        // Create pipe
        errorCheck(pipe(fd), "failed to create pipe", 0);
        // Fork a child
        forkPID = fork();

        if(forkPID == 0)
        {
            // Child redirects its stdin to be from previousIn
            if(previousIn != 0)
            {
                dup2(previousIn, 0);
            }
            // Close unused fd
            close(fd[0]);

            // Redirect out to pipe write end
            dup2(fd[1], 1);

            close(fd[1]);

            // command and args for exec() later
            char* command = argSet->array[i]->array[0];
            char** args = argSet->array[i]->array;

            // Exec()
            if(execvp(command, args) == -1)
            {
                perror("Failed to exec() ");
                printf("\n");
                exit(EXIT_FAILURE);
            }
        }
        else if(forkPID > 0)
        {
            // Wait for the child to end or finish writing to the pipe
            wait(&status);
            printChildStatus(status);

            // Close the write end on the parent
            close(fd[1]);

            // Parent remembers the previous read end for the next child in the chain
            previousIn = fd[0];
        }
        else
        {
            perror("Failed fork");
            exit(EXIT_FAILURE);
        }
    }

    // We should be the last one in the chain
    // Read in from the last pipe
    dup2(previousIn, 0);

    char* command = argSet->array[argSet->length-1]->array[0];
    char** args = argSet->array[argSet->length-1]->array;

    // Exec()
    if(execvp(command, args) == -1)
    {
        perror("Failed to exec() ");
        printf("\n");
        exit(EXIT_FAILURE);
    }
}

int main()
{
    args_array_set argSet = {array: NULL, length: 0, capacity: 0};
    // args_array argv = {array: NULL, length: 0, capacity: 0};

    char* input = NULL;
    char* prompt = NULL;
    size_t inputSize = 0;

    // Set diposition fro both SIGINT and SIGQUIT
    setSimpleDeposition(2);

    // Setup for long jump in case of an error in the shell
    sigsetjmp(resetPrompt, 1);
    jmpActive = 1;

    // Get user input
    while(1)
    {
        numChild = 0;

        prompt = getenv("PS1"); // if provided

        if (prompt == NULL) { 
            prompt = ">$"; // else use this for prompt
        }

        clearArgsSet(&argSet);

        printf("\n%s ", prompt);

        // getline includes the \n if there is one
        // , which there will always be one from stdin
        if (getline(&input, &inputSize, stdin) != -1)
        {
            // Nothing to do if user just gives you nothing
            if(strcmp(input, "\n") == 0)
            {
                continue;
            }

            parseInput(input, &argSet);
                
            printf("ARGSET IS WHAT: %ld\n", argSet.length);
            if(argSet.length == 1)
            {
                // command and args for exec() later
                char* command = argSet.array[0]->array[0];
                char** args = argSet.array[0]->array;

                // run the command and re prompt the user
                if(builtin(command, args) > 0)
                {
                    continue;
                }

                executeCMD(&argSet);
            }
            else
            {
                printf("More than one set: %ld\n", argSet.length);

                executePIPE(&argSet);
            }
        } 
        else
        {
            perror("Failed to read from stdin: ");
        }
    }

    // Reality: never reaches here
    free(input);
    clearArgsSet(&argSet);
    free(argSet.array);
    return 0;
}