/*
    Name: David Zheng
    CS 3393 
    Homework Assignment #2 - Process Env
    Due Date: Feb 15, 2020
*/
#define _GNU_SOURCE

#include <stdio.h> // printf
#include <stdlib.h> // std library methods
#include <string.h> // for string methods
#include <unistd.h> // for environ

extern char** environ;

// Extracts the key from var before first delimiter in var
// Returns a cstring on the heap that is null terminated
char* extractKey(char* var, char delim)
{
    // Extract the key part of the environment variable
    size_t varLen = strlen(var);
    char* key = malloc(1 + varLen * sizeof(char));

    if (key == NULL)
    {
        perror("Failed malloc():");
        exit(EXIT_FAILURE);
    }

    // NULL out the array
    memset(key, '\0', 1 + varLen * sizeof(char));

    // copy the key portion before the delimiter (should be '=')
    size_t j = 0;
    while(j < varLen && var[j] != delim)
    {
        key[j] = var[j];
        j++;
    }

    return key;
}

// returns the index of the environment variable in argEnv[]
// returns -1 if not found.
int envIndexOf(char** argEnv, int argEnvLen, char* var)
{
    // Extract the key / name part of the environment variable
    char* key = extractKey(var, '=');

    // Search for the key
    for(int i = 0; i < argEnvLen; i++)
    {
        // Extract the key / name of the environment variables
        char* argEnvKey = extractKey(argEnv[i], '=');

        // Found existing environment variable, return index
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
    // argEnv holds all the environment variables passed from the command line
    // It also holds any inherited environment variables from environ if 
    // -i flag is omitted
    char** argEnv = NULL;
    int argEnvLen = 0;
    int argEnvCap = 1;
    int argvIndex = 1;

    // check for parameters
    if(argc > 1)
    {
        // If we are not ignoring inherited environment variables
        // Add inherited environment variable to our list
        if(strcmp(argv[1], "-i") != 0)
        {
            // Copy everything from environ to new array
            for(int i = 0; environ[i] != NULL; i++)
            {
                // Resize and realloc if needed
                if(argEnv == NULL || argEnvLen == argEnvCap)
                {
                    char** temp = (char**) realloc(argEnv, 
                                                2 * argEnvCap * sizeof(char*));
                    if (temp == NULL)
                    {
                        perror("Realloc failed: ");
                        exit(EXIT_FAILURE);
                    }  

                    argEnv = temp;

                    argEnvCap *= 2;
                }

                argEnv[argEnvLen] = environ[i];

                argEnvLen++;
            }
        } 
        // Don't copy environ if -i flag was set
        else
        {
            char** temp = (char**) malloc(argEnvCap * sizeof(char*));

            if(temp == NULL)
            {
                perror("Malloc failed: ");
                exit(EXIT_FAILURE);
            }

            argEnv = temp;
            
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
                // Resize if needed
                if(argEnv == NULL || argEnvLen == argEnvCap)
                {
                    char** temp = (char**) realloc(argEnv, 
                                                2 * argEnvCap * sizeof(char*));  
                    if (temp == NULL)
                    {
                        perror("Realloc failed: ");
                        exit(EXIT_FAILURE);
                    }  

                    argEnv = temp;

                    argEnvCap *= 2;
                }

                // Append env variable to argEnv
                argEnv[argEnvLen] = argv[argvIndex];

                argEnvLen++;
            }

            argvIndex++;
        }

        argEnv[argEnvLen] = NULL;

        // set environ to argEnv
        environ = argEnv;

        // Check if we were told to run a program
        // argvIndex at this point would be after any environment variables
        // that was passed in the command line. "env [-i] [name=value]..."
        if (argv[argvIndex] != NULL)
        {
            char* command = argv[argvIndex];
            char** commandArgs = &argv[argvIndex];
            
            // Check if exec() has failed
            if(execvp(command, commandArgs) == -1)
            {
                free(argEnv);
                perror("Failed exec(): ");
                exit(EXIT_FAILURE);
            }

        } 
        else
        {
            // Print env vars that we have
            for(int i = 0 ; environ[i] != NULL; i ++)
            {
                printf("%s\n", environ[i]);
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
    free(argEnv);
    return 0;
}
