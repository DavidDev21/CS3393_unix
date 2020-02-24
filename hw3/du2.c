/*
    Name: David Zheng
    CS 3393 
    Homework Assignment #3 - DU (disk usage)
    Due Date: Feb 26
*/
#define _GNU_SOURCE

/*
    Note to self: 
        du defaults to 1024 byte blocks on most unix distributions
        but stat() reports in 512 byte blocks
*/

#include <stdio.h> // printf
#include <stdlib.h> // std library methods
#include <string.h> // for string methods
#include <sys/stat.h> // for stat struct and function
#include <dirent.h> // directory entry
#include <errno.h> // for errno

#define MEMO_START_CAPACITY 1
// (divide by 2 for consistency with 1024 byte blocks in linux, "du" default)
#define BLOCK_SIZE_CONVERT_FACTOR 2

typedef struct inode_memo {
    ino_t* inodeArray;
    size_t length;
    size_t capacity;
} inode_memo;

// Intended to be used right after a malloc / realloc
// for error checking
void checkAlloc(void* arrayPtr, const char* errorMsg)
{
    if(arrayPtr == NULL)
    {
        perror(errorMsg);
        exit(EXIT_FAILURE);
    }
}

// Don't have a correponding opendir with check
// since the handling of that error depends on the situation

// Close Dir with error checking
void closeDirCheck(DIR* dir)
{
    if(closedir(dir) == -1)
    {
        perror("Failed to close dir: ");
        exit(EXIT_FAILURE);
    }
}

// Creates an char array on the heap with given string to put in
char* initStartDir(size_t size, char* dir)
{
    char* startDir = malloc(size);
    checkAlloc(startDir, "initStartDir(): ");

    startDir[0] = '\0';
    strcat(startDir, dir);

    return startDir;
}

// Find if inode number is in the memo
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
        // Will always occur initially since the first append for memo will 
        // actually allocate space for inodeArray
        return -1;
    }

    // Look for the key
    for(size_t i = 0; i < memo->length; i++)
    {
        if(memo->inodeArray[i] == key)
        {
            return i;
        }
    }
    return -1;
}

// Appends inodeNum to the memo
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

        checkAlloc(temp, "AppendInode(): ");

        memo->inodeArray = temp;
        memo->length = 0;
        memo->capacity = MEMO_START_CAPACITY;
    }

    // Resize if needed
    if(memo->length == memo->capacity)
    {
        ino_t* temp = realloc(memo->inodeArray, 
                                2 * memo->capacity * sizeof(ino_t));

        checkAlloc(temp, "AppendInode(): ");

        memo->inodeArray = temp;
        memo->capacity = 2 * memo->capacity;
    }

    memo->inodeArray[memo->length] = inodeNum;
    memo->length++;

    return 0;
}

/*
           struct stat {
               dev_t     st_dev;          ID of device containing file 
               ino_t     st_ino;          Inode number 
               mode_t    st_mode;         File type and mode 
               nlink_t   st_nlink;        Number of hard links 
               uid_t     st_uid;          User ID of owner 
               gid_t     st_gid;          Group ID of owner 
               dev_t     st_rdev;         Device ID (if special file) 
               off_t     st_size;         Total size, in bytes 
               blksize_t st_blksize;      Block size for filesystem I/O 
               blkcnt_t  st_blocks;       Number of 512B blocks allocated 
*/

blkcnt_t diskUsage(char* dir, inode_memo* memo)
{
    DIR* dirptr;
    struct dirent* dirEntry;
    struct stat statbuf;

    blkcnt_t totalBlocksUsed = 0;

    size_t dirLen = strlen(dir);
    char pathname[PATH_MAX];

    // try to open the dir
    dirptr = opendir(dir);

    // If we can't open the directory, 
    // we just return 0 since we can't get any info about it
    // Killing the program would be excessive since the directory
    // might just be subdirectory that we don't have permission to
    if(dirptr == NULL)
    {
        fprintf(stderr,"cannot open directory: %s\n", dir);
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

        // Checking if our pathname is potentially too long, 
        // +2 = 1 for /, 1 for \0'
        if((dirLen + strlen(dirEntry->d_name) + 2) > PATH_MAX)
        {
            fprintf(stderr, "MAX PATHNAME LENGTH REACHED WHILE \
                                                    TRAVERSING DIRECTORY\n");
            exit(EXIT_FAILURE);
        }

        strcpy(pathname, dir);

        // Special case: when dir is '/' no need to add / before the filename
        // Otherwise, add '/'
        // Avoids cases like dir="/" -> "//<d_name"
        if(strcmp(dir, "/") != 0)
        {
            strcat(pathname, "/");
        }

        strcat(pathname, dirEntry->d_name);

        // Get info on the inode
        if(lstat(pathname, &statbuf) == -1)
        {
            perror("Failed lstat(): ");
            exit(EXIT_FAILURE);
        }

        // ignore "." and ".." directory entries
        if(strcmp(".", dirEntry->d_name) == 0)
        {
            // Count blocks for your current directory (also takes space)
            totalBlocksUsed += (statbuf.st_blocks / BLOCK_SIZE_CONVERT_FACTOR);
            continue;
        } 
        else if(strcmp("..",dirEntry->d_name) == 0)
        {
            continue;
        }

        // Check if dirEntry is an directory or regular file
        if(S_ISDIR(statbuf.st_mode))
        {
            // recurse into the directory
            totalBlocksUsed += diskUsage(pathname, memo);
        }
        else
        {
            // if we haven't already encountered this inode,
            // Count the blocks
            if(findINode(memo, statbuf.st_ino) == -1)
            {
                appendInode(memo, statbuf.st_ino);

                totalBlocksUsed += (statbuf.st_blocks / 
                                                    BLOCK_SIZE_CONVERT_FACTOR);
            }
        }
        // reset errno for readdir() error checking
        errno = 0;
    }

    printf("%ld\t%s\n", totalBlocksUsed, dir);

    closeDirCheck(dirptr);

    return totalBlocksUsed;
}

// Filter input
// gets rid of excessive '/' in dir
// Returns a new string
// if dir is len 0, then we just return a length 0 string
char* filterDir(const char* dir)
{
    size_t dirLen = strlen(dir);

    char* newDir = malloc((dirLen+1) * sizeof(char));

    checkAlloc(newDir, "filterDir(): ");

    size_t newDirPos = 0;

    for(size_t i = 0 ; i < dirLen; i++)
    {
        if(i > 0 && dir[i] == '/' && dir[i-1] == '/')
        {
            continue;
        }

        newDir[newDirPos] = dir[i];
        newDirPos++;
    }

    newDir[newDirPos] = '\0';

    return newDir;
}

int main(int argc, char* argv[])
{
    if (argc > 2) {
        fprintf(stderr, "Usage: %s [directory]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char* startDir = NULL;

    inode_memo memo; // for tracking inode numbers we have encountered
    
    // Zero out
    memo.inodeArray = NULL;
    memo.length = 0;
    memo.capacity = 0;

    if (argc == 2) {
        // Get rid of excessive '/' in input
        char* filteredDir = filterDir(argv[1]);
        
        // Check if it is a directory
        DIR* temp = opendir(filteredDir);

        // If not a directory, we exit.
        if(temp == NULL)
        {
            fprintf(stderr, "Failed to open \"%s\" : ", filteredDir);
            perror("");
            exit(EXIT_FAILURE);
        }

        closeDirCheck(temp);

        // Format the argument
        size_t dirLength = strlen(filteredDir);

        startDir = initStartDir((dirLength + 2) * sizeof(char), filteredDir);

        // Get rid of '/' at the end
        if (dirLength > 1 && filteredDir[dirLength-1] == '/')
        {
            startDir[dirLength-1] = '\0';
        }

        free(filteredDir);
    } 
    else
    {
        startDir = initStartDir(2 * sizeof(char), ".");
    }

    diskUsage(startDir, &memo);

    free(memo.inodeArray);
    free(startDir);

    return 0;
}
