#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main()
{
    char** test = malloc(5 * sizeof(char*));

    test[0] = "TEs";
    test[1] = "aszx";
    test[2] = "asdzx";
    test[3] = "Hello";
    test[4] = NULL;

    for(int i = 0; test[i] != NULL; i++)
    {
        printf("%s\n", test[i]);
    }

    test = realloc(test, 10);

    test[7] = "test";
    for(int i = 0; test[i] != NULL; i++)
    {
        printf("%s\n", test[i]);
    }

    return 0;
}