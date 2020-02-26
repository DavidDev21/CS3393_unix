/*
    Name: David Zheng
    CS 3393 
    Homework Assignment #4 - Basic Shell
    Due Date: 
*/
#define _GNU_SOURCE

#include <stdio.h> // printf
#include <stdlib.h> // std library methods
#include <string.h> // for string methods
#include <errno.h> // for errno
#include <sys/wait.h> // wait()
#include <unistd.h> // fork(), exec()

#define START_CAPACITY 1

typedef struct {
    char** array;
    size_t length;
    size_t capacity;
} dynamic_array;

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
        if(temp == NULL)
        {
            perror("AppendToken(): ");
            exit(EXIT_FAILURE);
        }

        dest->array = temp;
        dest->length = 0;
        dest->capacity = START_CAPACITY + 1;
    }

    // Resize if needed
    if(dest->length == dest->capacity)
    {
        char** temp = realloc(dest->array, 2 * dest->capacity * sizeof(char*));
        if(temp == NULL)
        {
            perror("AppendToken(): ");
            exit(EXIT_FAILURE);
        }

        dest->array = temp;
        dest->capacity = 2 * dest->capacity;
    }

    dest->array[dest->length] = token;
    dest->length++;

    // Null terminate
    dest->array[dest->length] = NULL;

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

int main()
{
    dynamic_array argv = {array: NULL, length: 0, capacity: 0};

    char* input = NULL;
    size_t inputSize = 0;

    int status = 0;

    // Get user input
    while(1)
    {
        printf(">$ ");

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

            // printf("Input: ");
            // for(size_t i = 0; argv.array[i] != NULL; i++)
            // {
            //     printf("%s ", argv.array[i]);
            // }
            // printf("\n");

            // Now we fork and exec on the command

            pid_t cpid = fork();
            // Parent
            if (cpid > 0)
            {
                wait(&status);
                
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

            } 
            // Child
            else if (cpid == 0)
            {
                char* command = argv.array[0];
                char** args = argv.array;

                if(execvp(command, args) == -1)
                {
                    perror("Failed to exec() ");
                    // printf("Command: %s\n", command);

                    // printf("Args: ");
                    // for(size_t i = 0; args[i] != NULL; i++)
                    // {
                    //     printf("%s ", args[i]);
                    // }
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
    return 0;
}