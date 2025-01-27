/*
* @file writer.c
* @author krish shah
* @brief implements a C application to write the (writestr) to the 
* (writefile), the writefile is the full path of the file on the 
* filesystem. This application overwrites any existing file. 
* this application will return 1 with error if 2 arguments are not 
* specified, or if it is unable to create the file
* 
* Usage:
* ./writer [writefile] [writestr]
*/

#include <errno.h>
#include <syslog.h>
#include <stdio.h>
#include <string.h>

typedef enum
{
    ARG_PROGRAM_NAME,
    ARG_WRITEFILE,
    ARG_WRITESTR,
    ARG_MAX,
} arguement_type_t;

int main(int argc, char *argv[])
{
    if (argc != ARG_MAX)
    {
        // invalid number of arguments 
        printf("Invalid number of arguments passed to writer application: %d\n", argc);
        printf("Usage: ./writer [writefile] [writestr]                                \
                \n\rwriter application will write the (writestr) to the               \
                \n\r(writefile), the writefile is the full path of the file on the    \
                \n\rfilesystem. this application overwrites any existing file.        \
                \n\rthis application will return 1 with error if 2 arguments are not  \
                \n\rspecified, or if it is unable to create the file\n\n");

        syslog(LOG_ERR, "Invalid number of arguments passed to writer application: %d\n", argc);
        return 1;
    }

    // open log, use program name as ident
    // and use the LOG_USER facility
    openlog(NULL, 0, LOG_USER);
    
    
    // open file in w mode to overwrite existing file
    // or create file if it does not exist
    FILE * h_writefile = fopen(argv[ARG_WRITEFILE], "w");
    if (NULL == h_writefile)
    {
        // check errno 
        int error = errno;
        printf("Could not create file %s with error: %s\n", argv[ARG_WRITEFILE], strerror(error));
        syslog(LOG_ERR, "Could not create file %s with error: %s\n", argv[ARG_WRITEFILE], strerror(error));
        return 1;
    }

    // write to file
    int writestr_length = strlen(argv[ARG_WRITESTR]);
    int bytes_written = fprintf(h_writefile, "%s", argv[ARG_WRITESTR]);
    if (bytes_written < 0)
    {
        // check errno
        int error = errno;
        printf("Could not write to file %s with error: %s\n", argv[ARG_WRITESTR], strerror(error));
        syslog(LOG_ERR, "Could not writer to file %s with error: %s\n", argv[ARG_WRITESTR], strerror(error));
        return 1;
    }
    else if (bytes_written != writestr_length)
    {
       printf("Could not write %d bytes to file, only %d bytes written\n", writestr_length, bytes_written);
       syslog(LOG_ERR, "Could not write %d bytes to file, only %d bytes written\n", writestr_length, bytes_written);
       return 1;
    }
    else
    {
        // file written successfully, do nothing
    }

    // close file
    if (EOF == fclose(h_writefile))
    {
        int error = errno;
        printf("Could not close file %s with error: %s\n", argv[ARG_WRITEFILE], strerror(error));
        syslog(LOG_ERR, "Could not close file %s with error: %s\n", argv[ARG_WRITEFILE], strerror(error));
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s\n", argv[ARG_WRITESTR], argv[ARG_WRITEFILE]);
    return 0;
}