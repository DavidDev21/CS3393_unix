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
#include <unistd.h> // fork(), exec()
#include <fcntl.h> // open()
#include <signal.h> // signals
#include <setjmp.h> // longjumps

#define START_CAPACITY 1

sigjmp_buf resetPrompt;

// Signal handlers
void handleSIGINT(int sig)
{
    // Just restart the prompt
    // Stop everything we were doing possibly
    siglongjmp(resetPrompt, 0);
}


typedef struct {
    char** array;
    size_t length;
    size_t capacity;
} dynamic_array;

// Intended to be used right after a malloc / realloc
// for error checking
void checkAlloc(void* arrayPtr, const char* errorMsg)
{
    if(arrayPtr == NULL)
    {
        perror(errorMsg);
        exit(EXIT_FAILURE);
    }
}

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

        checkAlloc(temp, "appendToken() Failed malloc");

        dest->array = temp;
        dest->length = 0;
        dest->capacity = START_CAPACITY + 1;
    }

    // Resize if needed
    if(dest->length == dest->capacity)
    {
        char** temp = realloc(dest->array, 2 * dest->capacity * sizeof(char*));

        checkAlloc(temp, "appendToken() Failed realloc");

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
        fprintf(stderr, "parseInput(): argv should be cleared out before use\n");
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
		if (argv[1] == NULL) { // cd by itself
			if (chdir(getenv("HOME")) == -1) {
				perror("chdir");
			}
		}
		else {
			if (chdir(argv[1]) == -1) {
				perror("chdir");
			}
		}
		return 1;
	}
	return 0;
}

// Checks any given errorCode
// and if it is an error, it will print the error and jump back to the start
// of the shell. reprompting the user for the new command.
// We assume something went wrong and just have the user reissue their command
void errorCheck(int errorCode, char* message)
{
    if(errorCode < 0)
    {
       fprintf(stderr, "shell: %s: %s\n", message, strerror(errno));
       siglongjmp(resetPrompt, errno);
    }
}

// Parses through the input and check and apply any IO redirection operators
// Updates argv accordingly
void IORedirect(dynamic_array* argv)
{
    size_t i = 0;

    // Since argv->array is null terminated, i+1 is either a token or NULL
    // popToken() is safe to do during the iteration given that it also copies
    // the NULL at the end of the array to the left by one, moving the end of the array
    // So the argv->array[i+1] will still guaranteed to see NULL after shift
    while(argv->array[i+1] != NULL)
    {
        int fd = -1; // arbitary to indicate non used fd

        // working
        if(strchr(argv->array[i], '<') != NULL || strchr(argv->array[i], '>') != NULL)
        {
            if(strcmp(argv->array[i], "<") == 0)
            {
                fd = open(argv->array[i+1], O_RDONLY);
            }
            else if (strcmp(argv->array[i], ">>") == 0)
            {
                // Append if exist, create if doesn't
                fd = open(argv->array[i+1], O_WRONLY | O_APPEND | O_CREAT, 0644);
            }
            else if(strcmp(argv->array[i], ">") == 0 || 
                    strcmp(argv->array[i], "2>") == 0||
                    strcmp(argv->array[i], "&>") == 0 )
            {
                // Truncate if exist, create if doesn't
                fd = open(argv->array[i+1], O_WRONLY | O_TRUNC | O_CREAT, 0644);
            }
            else
            {
                // Its not an redirect operator that we support
                continue;
            }

            // Checks if any error occur during open()
            errorCheck(fd, argv->array[i+1]);

            // redirect
            if (strcmp(argv->array[i], "<") == 0)
            {
                // First dup fd into 0, and close 0 if open
                dup2(fd, STDIN_FILENO);
            }
            else if (strcmp(argv->array[i], ">>") == 0 || strcmp(argv->array[i], ">") == 0)
            {
                // First dup2 fd into 0
                dup2(fd, STDOUT_FILENO);
            }
            else if(strcmp(argv->array[i], "2>") == 0)
            {
                // First dup2 fd into 2
                dup2(fd, STDERR_FILENO);
            }
            else if(strcmp(argv->array[i], "&>") == 0)
            {
                // First dup2 fd into 1 and 2
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
            }

            // close the fd for the file we opened since we completed redirection
            close(fd);

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

int main()
{
    // Change signal disposition for the shell
    struct sigaction ignore_act;
    struct sigaction default_act;
    struct sigaction sigint_act;
    sigint_act.sa_handler = handleSIGINT;
    sigint_act.sa_flags = SA_RESTART;

    ignore_act.sa_handler = SIG_IGN;
    default_act.sa_handler = SIG_DFL;
    default_act.sa_flags = SA_RESTART;

    dynamic_array argv = {array: NULL, length: 0, capacity: 0};

    char* input = NULL;
    size_t inputSize = 0;

    int status = 0;

    // A duplicate copy of the original set of std in, out, error
    // Used to recover or reset after IORedirection
    // These copies should stay open. No real reason why they would be closed
    // Strict purpose is to be able to recover the original std in, out, and error
    int stdin_copy = dup(0);
    int stdout_copy = dup(1);
    int stderr_copy = dup(2);

    char* prompt = getenv("PS1"); // if provided

	if (prompt == NULL) { 
		prompt = ">$"; // else use this for prompt
	}

    // Ignoring SIGINT (interrupts)
    sigaction(SIGINT, &sigint_act, NULL);
    sigaction(SIGQUIT, &ignore_act, NULL);

    // Setup for long jump in case of an error in the shell
    sigsetjmp(resetPrompt, 1);

    // Get user input
    while(1)
    {
        printf("\n%s ", prompt);

        // getline includes the \n if there is one, which there will always be one
        // from stdin
        if (getline(&input, &inputSize, stdin) > 0)
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
            // Now we fork and exec on the command

            pid_t cpid = fork();
            // Parent
            if (cpid > 0)
            {
                // Ignore SIGINT while we wait for child
                // Prevents us from jumping without cleaning up after child.
                sigaction(SIGINT, &ignore_act, NULL);
    
                wait(&status);

                // Reset std in, out, err
                dup2(stdin_copy, 0);
                dup2(stdout_copy, 1);
                dup2(stderr_copy, 2);
                
                // Status bit mask
                // 16 bits = 8 bit for exit code | 1 bit for core dump | 7 bit for signal number
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

                // Restore
                sigaction(SIGINT, &sigint_act, NULL);
            } 
            // Child
            else if (cpid == 0)
            {
                // Resetting SIGINT to default deposition
                sigaction(SIGINT, &default_act, NULL);
                sigaction(SIGQUIT, &default_act, NULL);

                if(execvp(command, args) == -1)
                {
                    perror("Failed to exec() ");
                    printf("\n");
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                perror("Fork() Failed: ");
            }
        } 
        else
        {
            perror("Failed to read from stdin: ");
            exit(EXIT_FAILURE);
        }
    }

    // Reality: never reaches here
    free(input);
    close(stdin_copy);
    close(stdout_copy);
    close(stderr_copy);
    return 0;
}