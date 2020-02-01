/*
    Name: David Zheng
    CS 3393 
    Homework Assignment #1 - Basic C program (Game of Life)
    Due Date: Feb 5, 2020
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MACROS for defaults
#define DEFAULT_INPUT_FILE "life.txt"
#define DEAD_CELL '-'
#define LIVE_CELL '*'

// Constants
const int WORLD_BUFFER = 2; // number of extra rows / cols to add to the world
const int DEFAULT_ROWS = 10;
const int DEFAULT_COLS = 10;
const int DEFAULT_GEN = 10;

// Prints out the world to stdout and show current generation
void printWorld(char** worldArray, int rowLen, int colLen, int gen)
{
    printf("Generation: %d\n", gen);

    for(int row = 1; row < rowLen-1; row++)
    {
        for(int col = 1; col < colLen-1; col++)
        {
            printf("%c", worldArray[row][col]);
        }
        printf("\n");
    }

    printf("==========================================\n");
}

// Generates a 2D array on the heap with rows by cols dimensions
// With buffer rows on all 4 sides for convenience. 
// And init the world based on the worldFile.
char** generateWorld(int numRows, int numCols, FILE* worldFile)
{
    // Error checking
    if (numRows <= 0 || numCols <= 0 || worldFile == NULL)
    {
        return NULL;
    }

    // Allocate the memory for the world
    char** world;

    world = malloc(numRows * sizeof(char*));

    for(int i = 0; i < numRows; i++)
    {
        world[i] = malloc(numCols * sizeof(char));
    }

    // Initialize the world
    for(int row = 1; row < numRows - 1; row++)
    {
        char* buffer;
        size_t bufferSize = 0; 
        int buffIndex = 0;
        int numCharacters;

        if((numCharacters = getline(&buffer, &bufferSize, worldFile)) != -1)
        {
            // Empty row, getline() still reads in the newline character
            if (numCharacters == 1) 
            {
                // initialize the row
                for(int col = 1; col < numCols - 1; col++)
                {
                    world[row][col] = DEAD_CELL;
                }
            }
            else 
            {
                for(int col = 1; col < numCols - 1; col++)
                {
                    if (buffIndex < numCharacters && buffer[buffIndex] == LIVE_CELL)
                    {
                        world[row][col] = LIVE_CELL;
                    }
                    else
                    {
                        world[row][col] = DEAD_CELL;
                    }

                    ++buffIndex;
                }
            }

            // free the space allocated for buffer by getline()
            free(buffer);       
                                                                                         
        } 
        else 
        {
            // initialize the extra rows if no more lines
            for(int col = 1; col < numCols - 1; col++)
            {
                world[row][col] = DEAD_CELL;
            }
        }
    }

    return world;
}

// counts the number of neighbours around cell (r,c)
// rowLen and colLen are the lengths of the world array
int numNeighbours(char** worldArray, int r, int c, int rowLen, int colLen)
{
    // Checking if cell is within world bounds
    // 0 and len - 1 are buffer rows and cols
    if (r <= 0 || c <= 0 || r >= rowLen-1 || c >= colLen)
    {
        return -1;
    }

    int neighbourCount = 0;

    for(int row = r - 1; row < r + 2; row++)
    {
        for(int col = c - 1; col < c + 2; col++)
        {
            if ((row != r || col != c) && worldArray[row][col] == LIVE_CELL)
            {
                ++neighbourCount;
            }
        }
    }

    return neighbourCount;
}

/*
Any live cell with fewer than two live neighbours dies.
Any live cell with two or three live neighbours lives on to the next generation.
Any live cell with more than three live neighbours dies.
Any dead cell with exactly three live neighbours becomes a live cell.
*/
// Performs the game of life
void gameLife(char** worldArray, int rowLen, int colLen, int gen)
{
    char nextGen[rowLen][colLen];

    int iteration = 0;

    printWorld(worldArray, rowLen, colLen, iteration);

    while(iteration < gen) {

        for(int row = 1; row < rowLen - 1; row++)
        {
            for(int col = 1; col < colLen - 1; col++)
            {
                int numLive = numNeighbours(worldArray, row, col, rowLen, colLen);

                if (worldArray[row][col] == DEAD_CELL && numLive == 3)
                {
                    nextGen[row][col] = LIVE_CELL;
                }
                else if(worldArray[row][col] == LIVE_CELL)
                {
                    if(numLive > 3 || numLive < 2)
                    {
                        nextGen[row][col] = DEAD_CELL;
                    }
                    else
                    {
                        nextGen[row][col] = LIVE_CELL;
                    }
                }
                else
                {
                    nextGen[row][col] = DEAD_CELL;
                }
            }
        }

        // Apply the next gen
        for(int row = 1; row < rowLen -1; row++)
        {
            for(int col = 1; col < colLen -1; col++)
            {
                worldArray[row][col] = nextGen[row][col];
            }
        }

        ++iteration;

        printWorld(worldArray, rowLen, colLen, iteration);
    }
}

int main(int argc, char *argv[]) {
    // Default variables
    int rows = DEFAULT_ROWS;
    int cols = DEFAULT_COLS;
    int generations = DEFAULT_GEN;
    char* inputFile = DEFAULT_INPUT_FILE;

    // Check command line args
    if (argc >= 3)
    {
        rows = strtol(argv[1], NULL, 10);
        cols = strtol(argv[2], NULL, 10);
    }
    if (argc >= 4)
    {
        inputFile = argv[3];
    }
    if (argc >= 5)
    {
        generations = strtol(argv[4], NULL, 10);
    }

    printf("Rows: %d\n", rows);
    printf("Cols: %d\n", cols);
    printf("Generations: %d\n", generations);
    printf("InputFile: %s\n", inputFile);

    // Check for errors
    if (rows <= 0 || cols <= 0 || generations <= 0)
    {
        printf("Command Line args invalid: Rows / Columns / Generation must be "
                "positive numbers\n");
        return -1;
    }

    FILE* worldFile = fopen(inputFile, "r");

    if (worldFile == NULL)
    {
        printf("File does not exist: %s\n", inputFile);
        return -1;
    }

    // Add the buffer rows to our world
    rows += WORLD_BUFFER;
    cols += WORLD_BUFFER;

     // Generate world
    char** world;

    world = generateWorld(rows, cols, worldFile);

    if (world == NULL)
    {
        printf("Failed to create world\n");
        return -1;
    }

    gameLife(world, rows, cols, generations);

    // clean up
    for(int i = 0; i < rows; i++)
    {
        free(world[i]);
    }
    free(world);
    return 0;
}