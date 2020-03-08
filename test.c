#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main()
{
    char buf[10] = "h";

    fprintf(stderr, "%d\n", strlen(buf));
   
    fprintf(stderr, "%s\n", buf);

    // // sprintf(), will fail if the buffer doesn't fit what you trying to write to
    // sprintf(buf, "progss%d", 456);

    // printf("%s\n", buf);

    for(int i = 0; i < 10; i++)
    {
        // sprintf is still the shorter way
        // snprintf prevents buffer overflow
        // n = n max bytes to write into buffer
        // sprintf is portable for converting int to string
        char test[10];
        char num[3];

        snprintf(num, 3, "%d", i);

        strcpy(test, "prog");
        strcat(test, num);

        printf("%s\n", test);
    }

    struct init{
        int a;
        char* b;
    };

    // zero out fields in a struct
    struct init test2 = {0};
    fprintf(stderr, "%d %d", test2.a, test2.b == NULL);

        for(int i = 0; i < 10; i++)
    {
        // sprintf is still the shorter way
        // snprintf prevents buffer overflow
        // n = n max bytes to write into buffer
        // sprintf is portable for converting int to string
        char test[10];
        char num[3];

        snprintf(num, 3, "%d", i);

        strcpy(test, "prog");
        strcat(test, num);

        fprintf(stderr, "ERROR: %s\n", test);
    }



    // char** test = malloc(5 * sizeof(char*));

    // test[0] = "TEs";
    // test[1] = "aszx";
    // test[2] = "asdzx";
    // test[3] = "Hello";
    // test[4] = NULL;

    // for(int i = 0; test[i] != NULL; i++)
    // {
    //     printf("%s\n", test[i]);
    // }

    // // test = realloc(test, 10);

    // // test[7] = "test";
    // // for(int i = 0; test[i] != NULL; i++)
    // // {
    // //     printf("%s\n", test[i]);
    // // }

    // char** maybe = malloc(1 * sizeof(char*));

    // maybe[0] = (char*) test;

    // printf("TEST: %s", maybe[0]);

    // free(test);
    return 0;
}