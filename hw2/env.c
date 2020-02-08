/*
    Name: David Zheng
    CS 3393 
    Homework Assignment #2 - Process Env
    Due Date: Feb 5, 2020
*/
#define _GNU_SOURCE

#include <stdio.h> // printf
#include <stdlib.h> // std library methods
#include <string.h> // for string methods
#include <stdbool.h> // for true / false boolean
#include <unistd.h> // for environ

extern char** environ;

// Extracts the key from var before first delimiter in var
// Returns a cstring on the heap that is null terminated
char* extractKey(char* var, char delim)
{
    // Extract the key part of the environment variable
    size_t varLen = strlen(var);
    char* key = malloc(1 + varLen * sizeof(char));
    memset(key, '\0', 1 + varLen * sizeof(char));

    size_t j = 0;
    while(j < varLen && var[j] != delim)
    {
        key[j] = var[j];
        j++;
    }

    return key;
}

int envIndexOf(char** argEnv, int argEnvLen, char* var)
{
    // Extract the key part of the environment variable
    char* key = extractKey(var, '=');

    printf("KEY: %s\n", key);

    // Search for the key
    for(int i = 0; i < argEnvLen; i++)
    {
        char* argEnvKey = extractKey(argEnv[i], '=');

        if(strcmp(argEnvKey, key) == 0)
        {
            free(argEnvKey);
            free(key);
            return i;
        }

        free(argEnvKey);
    }

    free(key);
    return -1;
}

int main(int argc, char* argv[]) {
    // variables
    char** argEnv = (char**) malloc(10 * sizeof(char*)); // Used to hold all the env vars passed from argv
    int argEnvLen = 0;
    int argEnvCap = 10;
    int argvIndex = 1;

    // check for parameters
    if(argc > 1)
    {
        // If we are not ignoring inherited environment variables
        // Add inherited environment variable to our list
        if(strcmp(argv[1], "-i") != 0)
        {
            // Copy everything from environ
            for(int i = 0; environ[i] != NULL; i++)
            {
                if(argEnvLen == argEnvCap)
                {
                    char** temp = (char**) realloc(argEnv, 2 * argEnvCap * sizeof(char*));  
                    if (temp == NULL)
                    {
                        perror("Realloc failed: ");
                        return -1;
                    }  

                    argEnv = temp;

                    argEnvCap *= 2;
                }

                argEnv[argEnvLen] = environ[i];

                argEnvLen++;
            }
        } 
        // Don't copy environ
        else
        {
            // Move pass -i flag
            argvIndex++;
        }

        // Goes through each possible name=var pair passed from command line
        while(argv[argvIndex] != NULL && strchr(argv[argvIndex], '=') != NULL)
        {
            // Check if env var already in our list
            // If so, modify instead
            int envIndex = envIndexOf(argEnv, argEnvLen, argv[argvIndex]);
            if(envIndex != -1)
            {
                argEnv[envIndex] = argv[argvIndex];
            }
            else 
            {
                if(argEnvLen == argEnvCap)
                {
                    char** temp = (char**) realloc(argEnv, 2 * argEnvCap * sizeof(char*));  
                    if (temp == NULL)
                    {
                        perror("Realloc failed: ");
                        return -1;
                    }  

                    argEnv = temp;

                    argEnvCap *= 2;
                }

                argEnv[argEnvLen] = argv[argvIndex];

                argEnvLen++;
            }

            argvIndex++;
        }

        // argEnv must be null terminated for execvpe()
        argEnv[argEnvLen] = NULL;

        // Check if we were told to run a program
        if (argv[argvIndex] != NULL)
        {
            char* command = argv[argvIndex];
            char** commandArgs = &argv[argvIndex];

            printf("Command: %s\n", command);

            if(execvpe(command, commandArgs, argEnv) == -1)
            {
                perror("Failed exec(): ");
                return -1;
            }

        } 
        else
        {
            // Print env vars that we have
            for(int i = 0 ; i < argEnvLen; i ++)
            {
                printf("%s\n", argEnv[i]);
            }
        }
    } 
    else 
    {
        // Just print the environment from environ
        // Nothing to be done
        for(int i = 0; environ[i] != NULL; i++)
        {
            printf("%s\n", environ[i]);
        }
    }
    // END GOAL
    return 0;
}
