/*
    Name: David Zheng
    CS 3393 
    Homework Assignment #4 - Basic Shell
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
} dynamic_array;

// Function prototypes
void printChildStatus(int status);
void checkNull(void* arrayPtr, const char* errorMsg);
void clearArray(dynamic_array* target);
void errorCheck(int errorCode, char* message, int restart);
void IORedirect(dynamic_array* argv);
void executeCMD(char* command, char** args);
void handleSIG(int sig);
void setSimpleDeposition(int depo);

int appendToken(dynamic_array* dest, char* token);
int popToken(dynamic_array* argv, size_t index);
int parseInput(char* userInput, dynamic_array* argv);
int builtin(char* cmd, char** argv);

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
    wait(&status);

    numChild = 0;
    printChildStatus(status);

    // restart the prompt
    // Need to use siglongjmp since sigaction adds the signal into the proc 
    // mask before we enter. So we have to restore the original proc mask
    // if we are jumping out of the signal handler
    siglongjmp(resetPrompt, -2);
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

// Clears the array within a dynamic_array struct
// Just be setting parameters.  (not the same as freeing)
// We might still want to use the array itself
void clearArray(dynamic_array* target)
{
    if(target != NULL && target->length > 0) 
    {
        target->length = 0;
        target->array[0] = NULL;
    }
}

// Adds an string / token to array
int appendToken(dynamic_array* dest, char* token)
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
int popToken(dynamic_array* argv, size_t index)
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
int parseInput(char* userInput, dynamic_array* argv)
{
    if(userInput == NULL || argv == NULL)
    {
        fprintf(stderr, "parseInput(): one of parameters is NULL\n");
        return -1;
    }

    if(argv->length > 0)
    {
        fprintf(stderr, "parseInput(): argv should be \
                                        cleared out before use\n");
        return -2;
    }

    if(strlen(userInput) == 0 || strcmp(userInput, "\n") == 0)
    {
        fprintf(stderr, "parseInput(): no user input\n");
        return -3;
    }

    // Delimiter list
    const char* delim = " \n";

    // get first token
    char* token = strtok(userInput, delim);

    while(token != NULL)
    {
        appendToken(argv, token);
        token = strtok(NULL, delim);
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
void IORedirect(dynamic_array* argv)
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
            errorCheck(fd, argv->array[i+1], 1);

            // redirect
            // Working
            if (strcmp(argv->array[i], "<") == 0)
            {
                // First dup fd into 0, and close 0 if open
                errorCheck(dup2(fd, STDIN_FILENO), "IORedirect: dup2()", 0);
            }
            // Working
            else if (strcmp(argv->array[i], ">>") == 0 || 
                     strcmp(argv->array[i], ">") == 0)
            {
                // First dup2 fd into 0
                errorCheck(dup2(fd, STDOUT_FILENO), "IORedirect: dup2()", 0);
            }
            // Working
            else if(strcmp(argv->array[i], "2>") == 0)
            {
                // First dup2 fd into 2
                errorCheck(dup2(fd, STDERR_FILENO),"IORedirect: dup2()", 0);
            }
            // Working
            // Separating this into own if statement more readable
            else if(strcmp(argv->array[i], "&>") == 0)
            {
                // First dup2 fd into 1 and 2
                errorCheck(dup2(fd, STDOUT_FILENO), "IORedirect: dup2()", 0);
                errorCheck(dup2(fd, STDERR_FILENO), "IORedirect: dup2()", 0);
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


// creates child process to execute the given command
void executeCMD(char* command, char** args)
{
    int status = 0;

    // ignore signals while we setup. avoids potential race condition
    setSimpleDeposition(1);
    // Now we fork and exec on the command
    pid_t cpid = fork();
    // Parent
    if (cpid > 0)
    {
        // inform potential signal handler that we have a potential child
        numChild = 1;
        setSimpleDeposition(2);
        wait(&status);
        printChildStatus(status);
    } 
    // Child
    else if (cpid == 0)
    {
        // Reset to default deposition
        setSimpleDeposition(0);

        if(execvp(command, args) == -1)
        {
            perror("Failed to exec() ");
            printf("\n");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        // Set back to signal handler
        setSimpleDeposition(2);
        perror("Fork() Failed: ");
    }
}

int main()
{

    dynamic_array argv = {array: NULL, length: 0, capacity: 0};

    char* input = NULL;
    char* prompt = NULL;
    size_t inputSize = 0;

    // A duplicate copy of the original set of std in, out, error
    // Used to recover or reset after IORedirection
    // These copies should stay open. No real reason why they would be closed
    // Strict purpose is to be able to recover the original std in, out, 
    // and error
    int stdin_copy = dup(0);
    int stdout_copy = dup(1);
    int stderr_copy = dup(2);

    if(stdin_copy < 0 || stdout_copy < 0 || stderr_copy < 0)
    {
        perror("dup()");
        exit(EXIT_FAILURE);
    }

    // Set diposition fro both SIGINT and SIGQUIT
    setSimpleDeposition(2);

    // Setup for long jump in case of an error in the shell
    sigsetjmp(resetPrompt, 1);
    jmpActive = 1;

    // Get user input
    while(1)
    {
        numChild = 0;
        // Reset std in, out, err upon coming back to do prompt
        // Initially wouldn't really have any affect
        errorCheck(dup2(stdin_copy, 0), "dup2(stdin_copy, 0)", 0);
        errorCheck(dup2(stdout_copy, 1), "dup2(stdout_copy, 1)", 0);
        errorCheck(dup2(stderr_copy, 2), "dup2(stderr_cpy, 2)", 0);

        prompt = getenv("PS1"); // if provided

        if (prompt == NULL) { 
            prompt = ">$"; // else use this for prompt
        }

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

            clearArray(&argv);
            parseInput(input, &argv);

            IORedirect(&argv);

            // command and args for exec() later
            char* command = argv.array[0];
            char** args = argv.array;

            // run the command and re prompt the user
            if(builtin(command, args) > 0)
            {
                continue;
            }

            executeCMD(command, args);

        } 
        else
        {
            perror("Failed to read from stdin: ");
        }
    }

    // Reality: never reaches here
    free(input);
    free(argv.array);
    close(stdin_copy);
    close(stdout_copy);
    close(stderr_copy);
    return 0;
}