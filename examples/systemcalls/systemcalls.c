#include "stdlib.h"
#include "string.h"
#include "fcntl.h"
#include "unistd.h"
#include "syslog.h"
#include "sys/wait.h"
#include "errno.h"
#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    bool b_status = false;
    const int return_code = system(cmd);

    if ((NULL == cmd))
    {
        // if cmd is NULL, the call to system will return a non-zero value if a
        // shell is available in the system
        if (0 == return_code)
        {
            syslog(LOG_DEBUG, "No shell available in the system!");
        } 
        else
        {
            syslog(LOG_DEBUG, "Shell available in the system!");
        }
    }
    else if (-1 == return_code)
    {
        // child process could not be created/run
        // check errno for the error
        syslog(LOG_ERR, "Could not create/run child process for command %s, with error: %s", cmd, strerror(errno));
    }
    else
    {
        if (WIFEXITED(return_code))
        {
            int termination_status = WEXITSTATUS(return_code);
            if (termination_status == 0)
            {
                // command successfully executed
                b_status = true;
            }
            else
            {
                syslog(LOG_ERR, "Child process for command %s terminated with status %d", cmd, termination_status);
            }
        }
        else
        {
            syslog(LOG_ERR, "Child process for command %s did not terminate normally", cmd);
        }
    }

    return b_status;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    bool b_status = false;
    const pid_t pid = fork();

    if (pid == -1)
    {
        // call to fork failed, check errno
        syslog(LOG_ERR, "Could not fork() with error: %s", strerror(errno));
    }
    else if (pid == 0)
    {
        // child process, use exec syscall to replace process with 
        // command
        const char * pathname = command[0];
        char ** const arg = command;
        int return_code = execv(pathname, arg);
        if (return_code == -1)
        {
            // call to exec failed! check errno, and return error to 
            // parent process
            syslog(LOG_ERR, "Could not fork() with error: %s", strerror(errno));
            exit(1);
        }
    }
    else
    {
        // parent process, wait for termination of child process and examine its
        // status
        int return_code;
        if (-1 == waitpid(pid, &return_code, 0))
        {
            syslog(LOG_ERR, "Call to waitpid failed with error: %s", strerror(errno));
        }
        else if (WIFEXITED(return_code))
        {
            int termaination_status = WEXITSTATUS(return_code);
            if (termaination_status == 0)
            {
                // command successfully executed
                b_status = true;
            }
            else
            {
                syslog(LOG_ERR, "Child process for terminated with status %d", termaination_status);
            }
        }
        else
        {
            syslog(LOG_ERR, "Child process for command did not terminate normally");
        }
    }
    va_end(args);

    return b_status;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    bool b_status = false;
    const pid_t pid = fork();

    if (pid == -1)
    {
        // call to fork failed, check errno
        syslog(LOG_ERR, "Could not fork() with error: %s", strerror(errno));
    }
    else if (pid == 0)
    {
        // child process, redirect STDOUT to outputfile
        // then use exec syscall to replace process with 
        // command

        // create outputfile with 0644 permissions
        int h_outputfile = open(outputfile, O_WRONLY | O_TRUNC | O_CREAT, S_IWUSR | S_IRUSR |  S_IRGRP | S_IROTH);
        if (h_outputfile < 0)
        {
            syslog(LOG_ERR, "Could not create outputfile %s with error: %s", outputfile, strerror(errno));
            exit(1);
        }

        // redirect STDOUT to outputfile
        if (dup2(h_outputfile, STDOUT_FILENO) < 0)
        {
            syslog(LOG_ERR, "Could not redirect STDOUT to outputfile %s with error: %s", outputfile, strerror(errno));
            exit(1);
        }
        else
        {
            // STDOUT has been redirected to outputfile, 
            // we can close the h_outputfile fd
            close(h_outputfile);
        }

        const char * pathname = command[0];
        char ** const arg = command;
        int return_code = execv(pathname, arg);
        if (return_code == -1)
        {
            // call to exec failed! check errno, and return error to 
            // parent process
            syslog(LOG_ERR, "Could not fork() with error: %s", strerror(errno));
            exit(1);
        }
    }
    else
    {
        // parent process, wait for termination of child process and examine its
        // status
        int return_code;
        if (-1 == waitpid(pid, &return_code, 0))
        {
            syslog(LOG_ERR, "Call to waitpid failed with error: %s", strerror(errno));
        }
        else if (WIFEXITED(return_code))
        {
            int termaination_status = WEXITSTATUS(return_code);
            if (termaination_status == 0)
            {
                // command successfully executed
                b_status = true;
            }
            else
            {
                syslog(LOG_ERR, "Child process for terminated with status %d", termaination_status);
            }
        }
        else
        {
            syslog(LOG_ERR, "Child process for command did not terminate normally");
        }
    }

    va_end(args);

    return b_status;
}
