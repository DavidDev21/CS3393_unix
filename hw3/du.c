/*
    Name: David Zheng
    CS 3393 
    Homework Assignment #3 - DU (disk usage) - Heap Version
    Due Date: Feb 26
*/
#define _GNU_SOURCE

/*
    Note to self: 
        du defaults to 1024 byte blocks on most unix distributions
        but POSIX du does 512 byte blocks
*/

#include <stdio.h> // printf
#include <stdlib.h> // std library methods
#include <string.h> // for string methods
#include <unistd.h> // for other unix file I/O
#include <sys/stat.h> // for stat struct and function
#include <dirent.h> // directory entry
#include <errno.h> // for errno

#define MEMO_START_CAPACITY 1

typedef struct inode_memo {
    ino_t* inodeArray;
    size_t length;
    size_t capacity;
} inode_memo;

// Find the inode in the memo
int findINode(inode_memo* memo, ino_t key)
{
    if(memo == NULL)
    {
        fprintf(stderr, "findInode(): inode_memo is NULL\n");
        return -1;
    }

    // Nothing to look through
    if(memo->inodeArray == NULL)
    {
        // Will always occur initially since the first append for memo will actually
        // allocate space for inodeArray
        return -1;
    }

    // Look for the key
    for(int i = 0; i < memo->length; i++)
    {
        if(memo->inodeArray[i] == key)
        {
            return i;
        }
    }
    return -1;
}

// Adds an inode number to the memo for tracking
int appendInode(inode_memo* memo, ino_t inodeNum)
{
    if(memo == NULL)
    {
        fprintf(stderr, "appendInode(): inode_memo is NULL\n");
        return -1;
    }

    // Initialize if NULL
    if(memo->inodeArray == NULL)
    {
        ino_t* temp = malloc(MEMO_START_CAPACITY * sizeof(ino_t));
        if(temp == NULL)
        {
            perror("AppendInode(): ");
            exit(EXIT_FAILURE);
        }

        memo->inodeArray = temp;
        memo->length = 0;
        memo->capacity = MEMO_START_CAPACITY;
    }

    // Resize if needed
    if(memo->length == memo->capacity)
    {
        ino_t* temp = realloc(memo->inodeArray, 2 * memo->capacity * sizeof(ino_t));
        if(temp == NULL)
        {
            perror("AppendInode(): ");
            exit(EXIT_FAILURE);
        }

        memo->inodeArray = temp;
        memo->capacity = 2 * memo->capacity;
    }

    memo->inodeArray[memo->length] = inodeNum;
    memo->length++;

    return 0;
}

// Tools for dynamic pathname
typedef struct dynamicPath{
    char* charArray;
    size_t length;
    size_t capacity;
} dynamicPath;

// Adds the filename string to the end of pathname
int appendPath(dynamicPath* pathname, char* filename)
{
    if(pathname == NULL)
    {
        fprintf(stderr, "appendPath(): pathname is NULL\n");
        return -1;
    }

    size_t filenameLen = strlen(filename);

    // Initialize if NULL
    if(pathname->charArray == NULL)
    {
        char* temp = malloc((filenameLen + 1) * sizeof(char));
        if(temp == NULL)
        {
            perror("AppendPath(): ");
            exit(EXIT_FAILURE);
        }

        pathname->charArray = temp;
        pathname->charArray[0] = '\0'; //null terminate
        pathname->length = 0;
        pathname->capacity = filenameLen + 1;
    }

    // Resize if needed
    // +1 for null char
    if((pathname->length + filenameLen + 1) >= pathname->capacity)
    {
        char* temp = realloc(pathname->charArray, (filenameLen + 1) + (2 * pathname->capacity * sizeof(char)));
        if(temp == NULL)
        {
            perror("AppendPath(): ");
            exit(EXIT_FAILURE);
        }

        pathname->charArray = temp;
        pathname->capacity = ((filenameLen + 1) + 2 * pathname->capacity);
    }

    // concat path
    // pathname->charArray will always be null terminated
    // since any manipulation to the charArray is either through strcat() or 
    // inserting '\0' manually
    strcat(pathname->charArray, filename);
    pathname->length += filenameLen;

    return 0;
}

// Shrinks by pathname by one level
int shrinkPath(dynamicPath* pathname)
{
    if(pathname == NULL)
    {
        return -1;
    }

    // minus one for null char
    size_t i = pathname->length-1;

    // Find the first from the end '/'
    while(i > 0 && pathname->charArray[i] != '/')
    {
        i--;
    }

    // Put null char at the '/' location to "shrink"
    pathname->charArray[i] = '\0';
    pathname->length = i;

    return 0;
}


// Version 3 with malloc for pathname
blkcnt_t diskUsage(dynamicPath* dir, inode_memo* memo)
{
    DIR* dirptr;
    struct dirent* dirEntry;
    struct stat statbuf;

    blkcnt_t totalBlocksUsed = 0;

    // Special case: the initial directory we are traversing is /
    // main() removes the first trailing '/' from the end of path
    // Otherwise, dir would never be 0 initially, or reach 0
    if(dir->length == 0)
    {
        appendPath(dir, "/");
    }

    // try to open the dir
    dirptr = opendir(dir->charArray);

    // If we can't open the directory, 
    // we just return 0 since we can't get any info about it
    if(dirptr == NULL)
    {
        fprintf(stderr,"cannot open directory: %s\n", dir->charArray);
        return 0;
    }

    errno = 0;
    while((dirEntry = readdir(dirptr)) != NULL)
    {
        // In case readdir() failed
        if(errno != 0)
        {
            perror("diskUsage() - error on readdir(): ");
            exit(EXIT_FAILURE);
        }

        // No point in adding '/' if it already has a '/' at the end
        // The check should be pretty quick and not that costly
        if(dir->charArray[dir->length-1] != '/')
        {
            appendPath(dir, "/");
        }

        appendPath(dir, dirEntry->d_name);

        // Get info on the inode
        if(lstat(dir->charArray, &statbuf) == -1)
        {
            perror("Failed lstat(): ");
            exit(EXIT_FAILURE);
        }

        // ignore "." and ".." directory entries
        if(strcmp(".", dirEntry->d_name) == 0)
        {
            // Count blocks for your current directory (also takes space)
            totalBlocksUsed += (statbuf.st_blocks / 2);
            shrinkPath(dir);
            continue;
        } 
        else if(strcmp("..",dirEntry->d_name) == 0)
        {
            shrinkPath(dir);
            continue;
        }

        // Check if dirEntry is an directory or regular file
        if(S_ISDIR(statbuf.st_mode))
        {
            // recurse into the directory
            totalBlocksUsed += diskUsage(dir, memo);
        }
        else
        {
            if(findINode(memo, statbuf.st_ino) == -1)
            {
                appendInode(memo, statbuf.st_ino);

                totalBlocksUsed += (statbuf.st_blocks / 2);
            }
        }
        // reset errno for readdir() error checking
        errno = 0;
        shrinkPath(dir);

    }

    // Special case: the initial directory we are traversing is /
    // main() removes the first trailing '/' from the end of path

    // shrink() ends up clearing the whole path in cases where its
    // "/<some_string>", since it inserts '\0' at the first occurence of /
    // This is strictly for output expectations when running '/' as directory
    if(dir->length == 0)
    {
        appendPath(dir, "/");
    }

    printf("%ld\t%s\n", totalBlocksUsed, dir->charArray);

    // Close the directory
    if(closedir(dirptr) == -1)
    {
        perror("Failed to close dir: ");
        exit(EXIT_FAILURE);
    }

    //printf("After Shrink, end of call: %s\n", dir->charArray);
    return totalBlocksUsed;
}


int main(int argc, char* argv[])
{
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [directory]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    dynamicPath startDir;
    inode_memo memo; // for tracking inode numbers we have encountered
    
    // Zero out
    memo.inodeArray = NULL;
    memo.length = 0;
    memo.capacity = 0;

    startDir.charArray = NULL;
    startDir.length = 0;
    startDir.capacity = 0;

    if (argc == 2) {
        // Check if it is a directory
        DIR* temp = opendir(argv[1]);

        // If not a directory, we exit.
        if(temp == NULL)
        {
            fprintf(stderr, "%s : is not a directory\n", argv[1]);
            exit(EXIT_FAILURE);
        }

        // close the directory
        if(closedir(temp) == -1)
        {
            perror("Failed to close dir: ");
            exit(EXIT_FAILURE);
        }

        // Format the argument to not have a trailing '/'
        // This makes shrinking our pathname easier and cleaner
        size_t dirLength = strlen(argv[1]);

        startDir.charArray = malloc((dirLength + 2) * sizeof(char));
        startDir.capacity = dirLength + 2;

        if(startDir.charArray == NULL)
        {
            perror("Failed malloc(): ");
            exit(EXIT_FAILURE);
        }

        startDir.charArray[0] = '\0';
        strcat(startDir.charArray, argv[1]);
        startDir.length = dirLength;

        // Get rid of the trailing /
        if (argv[1][dirLength-1] == '/')
        {
            startDir.charArray[dirLength-1] = '\0';
            startDir.length--;
        }
    }
    else // default if no path given by user
    {
        startDir.charArray = malloc(2 * sizeof(char));
        startDir.capacity = 2;

        if(startDir.charArray == NULL)
        {
            perror("Failed malloc(): ");
            exit(EXIT_FAILURE);
        }

        startDir.charArray[0] = '\0';
        strcat(startDir.charArray, ".");
        startDir.length = 1;
    }

    diskUsage(&startDir, &memo);

    free(memo.inodeArray);
    free(startDir.charArray);

    return 0;
}
