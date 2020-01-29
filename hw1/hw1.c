/*
    Name: David Zheng
    CS 3393 
    Homework Assignment #1 - Basic C program (Game of Life)
    Due Date: Feb 5, 2020
*/

#include <stdio.h>
#include <string.h>

// Prints out the world to stdout
// and show current generation
void printWorld(char**[] worldArray, int rowLen, int colLen, int gen)
{
    printf("Generation: %d\n", gen);

    for(int row = 1; row < rowLen - 1; row++)
    {
        for(int col = 1; col < colLen - 1; col++)
        {
            printf(worldArray[row][col]);
        }
    }
}

int main(int argc, char *argv[]) {
    printf("Hello Word\n");
    
    printf("Argc: %d\n", argc);
    printf("Argv: ");
    for(int i = 1; i < argc; i++)
    {
        printf("%s ,", argv[i]);
    }
    printf("\n");


    // Default variables
    int rows = 10;
    int cols = 10;
    int generations = 10;
    char* inputFile = "life.txt";

    return 0;
}