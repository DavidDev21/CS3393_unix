// Wrapper struct for an normal array
typedef struct{
    char** array;
    int length; // number of items in array
    int capacity; // how many slots in array overall
} vector;

// Allocates and setup a vector struct on the heap for use
vector* initVec(int length)
{
    vector* newVec = (vector*) malloc(sizeof(vector));
    newVec->length = length;
    newVec->capacity = length * 2;
    newVec->array = (char**) malloc(length * sizeof(char*));

    return newVec
}

// Returns the length of the array
int vec_getLen(vector* arr)
{
    return arr->length;
}

// Resize the array to fit new items
bool vec_resize(vector* arr)
{
    arr->array = (char**) realloc(arr->array, 2 * arr->capacity * sizeof(char*));

    if(arr->array == NULL)
    {
        return false;
    }

    arr->capacity = 2 * arr->capacity;

    return true;
}

// Adds item to the end of arr
bool vec_appendItem(vector* arr, const char* item)
{
    if(arr->length == arr->capacity)
    {
        // Check if we failed to resize
        if(resizeVec(arr) == false)
        {
            return false;
        }
    }

    arr->length++;
    arr->array[arr->length] = item;

    return true;
}

int vec_findItem(vector* arr, const char* item)
{
    for(int i = 0; i < arr->length; i++)
    {
        if(strcmp(arr->array[i], item) == 0)
        {
            return i;
        }
    }

    return -1;
}
