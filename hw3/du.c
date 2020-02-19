/*
    Name: David Zheng
    CS 3393 
    Homework Assignment #3 - DU (disk usage)
    Due Date: TBD
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

#define MEMO_START_CAPACITY 10

typedef struct inode_memo {
    ino_t* inodeArray;
    int length;
    int capacity;
} inode_memo;

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
        fprintf(stderr, "findInode(): inodeArray in inode_memo is NULL\n");
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

// Prints out sb data
void printFileStat(struct stat* sb)
{
    printf("I-node number:            %ld\n", (long) sb->st_ino);

    printf("File type:                ");

    switch (sb->st_mode & S_IFMT) {
    case S_IFBLK:  printf("block device\n");            break;
    case S_IFCHR:  printf("character device\n");        break;
    case S_IFDIR:  printf("directory\n");               break;
    case S_IFIFO:  printf("FIFO/pipe\n");               break;
    case S_IFLNK:  printf("symlink\n");                 break;
    case S_IFREG:  printf("regular file\n");            break;
    case S_IFSOCK: printf("socket\n");                  break;
    default:       printf("unknown?\n");                break;
    }

    printf("Mode:                     %lo (octal)\n",
            (unsigned long) sb->st_mode);

    printf("Link count:               %ld\n", (long) sb->st_nlink);
    printf("Ownership:                UID=%ld   GID=%ld\n",
            (long) sb->st_uid, (long) sb->st_gid);

    printf("Preferred I/O block size: %ld bytes\n",
            (long) sb->st_blksize);
    printf("File size:                %lld bytes\n",
            (long long) sb->st_size);
    printf("Blocks allocated:         %lld\n",
            (long long) sb->st_blocks);
}

void printdir(char *dir, int depth)
{
    DIR *dp;
    struct dirent *entry;
    struct stat statbuf;
    int spaces = depth*4;

    if((dp = opendir(dir)) == NULL) {
        fprintf(stderr,"cannot open directory: %s\n", dir);
        return;
    }
    //chdir(dir);
       char cwd[PATH_MAX];
   if (getcwd(cwd, sizeof(cwd)) != NULL) {
       printf("Current working dir: %s\n", cwd);
   } else {
       perror("getcwd() error");
       return;
   }

   /*
        Why we need to chdir into the dir we opened
        - when we are doing lstat on a given directory entry for dir
        - lstat is doing a relative lookup for that path given by directory entry
        - since d_name is just the directoy / file name
        - Therefore, if we dont change directory of the process to the one we are looking
        - through the directory table. Then lstat would return us an empty statbuf.
        - Since it can't find the file or directory in teh current working directory.
   */
    while((entry = readdir(dp)) != NULL) {
        printf("LOOKING AT: %s\n", entry->d_name);
        lstat(entry->d_name,&statbuf);
        printFileStat(&statbuf);
        if(S_ISDIR(statbuf.st_mode)) {
            /* Found a directory, but ignore . and .. */
            if(strcmp(".",entry->d_name) == 0 || 
                strcmp("..",entry->d_name) == 0)
                continue;
            printf("%*s%s/\n",spaces,"",entry->d_name);
            /* Recurse at a new indent level */
            printf("RECURSIVING\n");
            printdir(entry->d_name,depth+1);
            printf("COMING BACK\n");
        }
        else printf("%*s%s\n",spaces,"",entry->d_name);
    }
    chdir("..");
    closedir(dp);
}

blkcnt_t diskUsage(char* dir, inode_memo* memo)
{
    DIR* dirptr;
    struct dirent* dirEntry;
    struct stat statbuf;

    blkcnt_t totalBlocksUsed = 0;

    // try to open the dir
    dirptr = opendir(dir);

    // If we can't open the directory, 
    // we just return 0 since we can't get any info about it
    if(dirptr == NULL)
    {
        fprintf(stderr,"cannot open directory: %s\n", dir);
        return 0;
    }
    
    // Change current working directory
    // For lstat() to work properly with relative pathnames
    chdir(dir);

    errno = 0;
    while((dirEntry = readdir(dirptr)) != NULL)
    {
        // In case readdir() failed
        if(errno != 0)
        {
            perror("diskUsage() - error on readdir(): ");
            exit(EXIT_FAILURE);
        }
        // ignore "." and ".." directory entries
        if(strcmp(".",dirEntry->d_name) == 0 || strcmp("..",dirEntry->d_name) == 0)
        {
            continue;
        }

        // Get info on the inode
        lstat(dirEntry->d_name, &statbuf);

        // Check if dirEntry is an directory or regular file
        if(S_ISDIR(statbuf.st_mode))
        {
            //printf("GOING IN DEEP\n");
            // recurse into the directory
            totalBlocksUsed += diskUsage(dirEntry->d_name, memo);
        }
        else if(S_ISREG(statbuf.st_mode))
        {
            if(findINode(memo, statbuf.st_ino) == -1)
            {
                appendInode(memo, statbuf.st_ino);

                printf("%s : %s : %ld\n" , dir, dirEntry->d_name, statbuf.st_blocks);
                totalBlocksUsed += statbuf.st_blocks;
            }
        }

        // reset errno for readdir() error checking
        errno = 0;
    }
    //printf("COMING OUT\n");
    printf("%ld        %s/\n", totalBlocksUsed, dir);
    // Return to original directory
    chdir("..");

    return totalBlocksUsed;
}

int main(int argc, char* argv[])
{
    if (argc > 2) {
        fprintf(stderr, "Usage: %s <pathname>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    //struct stat sb;

    char* startDir = ".";
    inode_memo memo; // for tracking inode numbers we have encountered
    
    // Zero out
    memo.inodeArray = NULL;
    memo.length = 0;
    memo.capacity = 0;

    if (argc == 2) {
        startDir = argv[1];
    }

    printf("%ld %s\n", diskUsage(startDir, &memo), startDir);
    
    // if (stat(argv[1], &sb) == -1) {
    //     perror("stat");
    //     exit(EXIT_FAILURE);
    // }
    // printf("%d", S_IFMT);
    // printFileStat(&sb);

    // printf("==================\n\n");

    //printdir(argv[1],0);


    return 0;
}
