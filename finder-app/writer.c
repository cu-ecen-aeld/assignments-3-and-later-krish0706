#include <errno.h>
#include <syslog.h>
#include <stdio.h>
#include <string.h>

typedef enum
{
    ARG_PROGRAM,
    ARG_WRITEFILE,
    ARG_WRITESTR,
    ARG_NUM,
} arguement_type_t;

int main(int argc, char *argv[])
{
    if (argc != ARG_NUM)
    {
        // invalid number of arguements 
        syslog(LOG_ERR, "Invalid number of arguments: %d\n", argc);
        return 1;
    }

    // open log
    openlog(NULL, 0, LOG_USER);
    
    
    // open file in w mode
    FILE * f_handle = fopen(argv[ARG_WRITEFILE], "w");
    if (NULL == f_handle)
    {
        // check errno 
        int error = errno;
        syslog(LOG_ERR, "Something went wrong in fopen: %s\n", strerror(error));
        return 1;
    }

    // write to file
    int writestr_length = strlen(argv[ARG_WRITESTR]);
    int bytes_written = fprintf(f_handle, "%s", argv[ARG_WRITESTR]);
    if (bytes_written < 0)
    {
        // check errno
        int error = errno;
        syslog(LOG_ERR, "Something went wrong in fprintf: %s\n", strerror(error));
        return 1;
    }
    else if (bytes_written != writestr_length)
    {
       syslog(LOG_ERR, "Not all bytes written by fprintf\n");
       return 1;
    }
    else
    {
        // file written successfully, do nothing
    }

    // close file
    if (EOF == fclose(f_handle))
    {
        int error = errno;
        syslog(LOG_ERR, "Something went wrong in fclose: %s\n", strerror(error));
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s\n", argv[ARG_WRITESTR], argv[ARG_WRITEFILE]);
    return 0;
}