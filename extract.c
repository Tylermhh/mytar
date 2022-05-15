#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>
#include "util.h"
#include "header.h"

#define OCTAL 8
#define BLOCKSIZE 512
#define REG_FLAG '0'
#define REG_FLAG_ALT '\0'
#define SYM_FLAG '2'
#define DIR_FLAG '5'
#define MAX_NAME_LEN 100
#define MAX_PREFIX_LEN 155
#define MAX_LINK_LEN 100

int extract_cmd(char* fileName, int verboseBool, int strictBool) {
    int fd;
    struct header headerBuffer;

    errno = 0;
    fd = open(fileName, O_RDONLY);
    
    if(errno) {
        fprintf(stderr, "Couldn't open file %s: %s",
            fileName, strerror(errno));
        exit(errno);
    }

    errno = 0;
    while(read(fd, &headerBuffer, sizeof(struct header)) > 0) {
        unsigned long int fileSize;
        unsigned char typeFlag = *headerBuffer.typeflag;
        struct stat statBuffer;
        struct utimbuf newTime;
        char *filePath;

        /* Check read() error */
        if(errno) {
            perror("Couldn't read header");
            exit(errno);
        }

        fileSize = strtol(headerBuffer.size, NULL, OCTAL);

        errno = 0;
        /* +4 for leading "./", concat "/", and null-terminator */
        filePath = calloc(MAX_NAME_LEN + MAX_PREFIX_LEN + 4, sizeof(char));
        if(errno) {
            perror("Couldn't calloc filePath");
            exit(errno);
        }

        /* NOTE: We're using strncat to avoid undefined behavior.
         * Prefix and name aren't necessarily null-terminated, and the normal
         * strcat just looks for a null terminator in the source string
         * to stop. strncat stops at the null terminator or after n chars,
         * whichever comes first. */

        /* Leading ./ for a valid relative path */
        strcat(filePath, "./");
        /* If there's something in prefix, add it and a slash */
        if(headerBuffer.prefix[0]) {
            strncat(filePath, (char *)&headerBuffer.prefix, MAX_PREFIX_LEN);
            strcat(filePath, "/");
        }
        /* Add the name unconditionally */
        strncat(filePath, (char *)&headerBuffer.name, MAX_NAME_LEN);
        

        /* TODO: Restore permissions (if applicable?) */
        /* P.S. I know that I don't have to put every case in curly brackets
         * normally. But this gets around a quirk of C that throws a fit
         * about mixed declarations otherwise. Something about a case just
         * being a label, not a separate scope. The brackets make it a 
         * separate scope. Thanks StackOverflow! Cool tidbit. */
        switch (typeFlag) {
            case REG_FLAG_ALT:
            case REG_FLAG: {
                int fd;
                /* NOTE: This currently grants everything to user.
                 * We'll either need to change these perms in this call,
                 * or just set them again later. If user mysteriously has
                 * perms, this is probably the culprit. */
                if((fd = creat(filePath, S_IRWXU)) == -1) {
                    perror("Couldn't create file");
                    exit(errno);
                }
                /* TODO: Write all of the data that comes after the header
                 * into this new file. Its file descriptor is stored in fd. */
                break;
            }
            case SYM_FLAG: {
                /* We're doing this to catch issues with it possibly not being
                 * null-terminated. Basically just slapping on our own
                 * null-terminator(s) at the end. */ 
                char *linkValue; 
                errno = 0;
                linkValue = calloc(MAX_LINK_LEN + 1, sizeof(char));
                if(errno) {
                    perror("Couldn't calloc linkValue");
                    exit(errno);
                }

                strncpy(linkValue, (char *)&headerBuffer.linkname,
                    MAX_LINK_LEN);

                /* Make a symlink with name filePath that points
                 * to linkValue. It's easy to get mixed up here! */
                if(symlink(linkValue, filePath)) {
                    perror("Couldn't create symlink");
                    exit(errno);
                }

                free(linkValue);
                break;
            }
            case DIR_FLAG: {
                /* NOTE: Again, watch out for these perms. */
                if(mkdir(filePath, S_IRWXU)) {
                    perror("Couldn't mkdir");
                    exit(errno);
                }
                break;
            }
            default: {
                fprintf(stderr, "Invalid typeflag! '%c'", typeFlag);
                exit(EXIT_FAILURE);
            }
        }
       
        
        /* Stat the created file to get its current times */
        if(lstat(filePath, &statBuffer)) {
            perror("Failed to stat created file!");
            exit(errno);
        } 

        /* TODO: Shoot. I just realised something. Putting a file into a
         * directory will change the directory's mtime. This is annoying.
         * I think we'll have to loop through every header again and
         * set the mtime for all directories again. But don't set their
         * files' times on this pass! */

        /* Preserve atime */
        newTime.actime = statBuffer.st_atime;
        /* Set mtime to mtime from header */
        newTime.modtime = strtol(headerBuffer.mtime, NULL, OCTAL);
        /* Actually write the time */
        if(utime(filePath, &newTime)) {
            perror("Couldn't set utime");
            exit(errno);
        }

        /* NOTE: We shouldn't be touching the created file after this.
         * The mtime could be disturbed. Do any operations on it before 
         * setting mtime. */

        /* Skip over the body to next header */
        if(fileSize > 0) {
            /* If the file has size > 0, skip ahead by the required # blocks */
            if(lseek(fd, (fileSize / BLOCKSIZE + 1) * BLOCKSIZE, SEEK_CUR) == -1) {
                perror("Couldn't lseek to next header");
                exit(errno);
            }
        } 
        free(filePath);
        errno = 0;
    }
    close(fd);
    return 0;
}
