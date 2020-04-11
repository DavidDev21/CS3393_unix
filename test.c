#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// Struct for a string of unknown sizes (allocated on the heap)
typedef struct
{
    char* message;
    size_t length;
    size_t capacity;
} dynamic_string;
void checkNull(void* arrayPtr, const char* errorMsg)
{
    if(arrayPtr == NULL)
    {
        perror(errorMsg);
        exit(EXIT_FAILURE);
    }
}

// dest should be the address to the pointer
// This func should be for adding message
int DS_appendMessage(dynamic_string* dest, const char* message)
{
    if(dest == NULL)
    {
        fprintf(stderr, "DS_appendMessage(): dest is NULL\n");
        return -1;
    }
    if(message == NULL)
    {
        fprintf(stderr, "DS_appendMessage(): message is NULL\n");
        return -1;
    }

    size_t msgLen = strlen(message);
    size_t newCap = 0;

    if(dest->message == NULL)
    {
        newCap = (msgLen + 1)* sizeof(char);
        dest->message = (char*) malloc(newCap);
        checkNull(dest->message, "Failed malloc()");
        dest->capacity = newCap;
        memset(dest->message, '\0', newCap);
    }
    else if(dest->length + msgLen + 1 >= dest->capacity)
    {
        newCap = 2 * (msgLen + dest->capacity + 1) * sizeof(char);
        char* temp = (char*) realloc(dest->message, newCap);
        checkNull(temp, "Failed realloc()");
        dest->message = temp;
        dest->capacity = newCap;
    }

    strcat(dest->message, message);
    dest->length = dest->length + msgLen;

    return 0;
}

void init_DS(dynamic_string* target)
{
    target->message = NULL;
    target->length = 0;
    // cap doesn't matter
}

void free_DS(dynamic_string* target)
{
    free(target->message);
}

int main()
{
    dynamic_string test1;
    init_DS(&test1);
    DS_appendMessage(&test1, "HELLO WORLD?\n");
    printf("%s", test1.message);
    DS_appendMessage(&test1, "THISWORKS?\n");
    printf("%s", test1.message);
    free_DS(&test1);

    // char buf[10] = "h";

    // fprintf(stderr, "%d\n", strlen(buf));
   
    // fprintf(stderr, "%s\n", buf);

    // // // sprintf(), will fail if the buffer doesn't fit what you trying to write to
    // // sprintf(buf, "progss%d", 456);

    // // printf("%s\n", buf);

    // for(int i = 0; i < 10; i++)
    // {
    //     // sprintf is still the shorter way
    //     // snprintf prevents buffer overflow
    //     // n = n max bytes to write into buffer
    //     // sprintf is portable for converting int to string
    //     char test[10];
    //     char num[3];

    //     snprintf(num, 3, "%d", i);

    //     strcpy(test, "prog");
    //     strcat(test, num);

    //     printf("%s\n", test);
    // }

    // struct init{
    //     int a;
    //     char* b;
    // };

    // // zero out fields in a struct
    // struct init test2 = {0};
    // fprintf(stderr, "%d %d", test2.a, test2.b == NULL);

    //     for(int i = 0; i < 10; i++)
    // {
    //     // sprintf is still the shorter way
    //     // snprintf prevents buffer overflow
    //     // n = n max bytes to write into buffer
    //     // sprintf is portable for converting int to string
    //     char test[10];
    //     char num[3];

    //     snprintf(num, 3, "%d", i);

    //     strcpy(test, "prog");
    //     strcat(test, num);

    //     fprintf(stderr, "ERROR: %s\n", test);
    // }



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