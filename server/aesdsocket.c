/*
 * @file aesdsocket.c
 * @author krish shah
 * @date 2025-02-22
 * @brief opens a socket connection on port 9000, and receives bytes from it
 * till a \n is received. After that, it appends data to a file (/var/tmp/aesdsocketdata)
 * and reads all content from that file and writes it back on the socket connection
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>

#define SOCKET_DATA_FILE_PATHNAME "/var/tmp/aesdsocketdata"
#define PORT "9000" // the port to connect to
#define BACKLOG 10
#define RECV_BUF_LEN 100

#define EXIT_SOCKET_FAILURE (-1)
#define EXIT_APP_FAILURE (-1)

static volatile bool b_accept_connections = true;

// @brief signal handler to redirect SIGINT and SIGTERM 
// to gracefully exit application
void signal_handler(int signo)
{
    if ((SIGINT == signo) || SIGTERM == signo)
    {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        b_accept_connections = false;
    }
}

// @brief bind to given node and service 
bool bind_to_address(char const * const p_node, char const * const p_service, int * const p_socket_fd)
{
    struct addrinfo hints;
    struct addrinfo * p_result;
    int h_sockfd = 0;
    int return_code = 0;
    bool b_status = true;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    return_code = getaddrinfo(p_node, p_service, &hints, &p_result);
    if (return_code != 0)
    {
        syslog(LOG_ERR, "getaddrinfo failed with error: %s\n", gai_strerror(return_code));
        b_status = false;
    }
    else
    {
        // p_result will point to a linked list of address structures, 
        // traverse it, try to connect and bind to each, exit with b_status set to false
        // if cannot connect to any
        struct addrinfo * p_addr_node = p_result;
        for (p_addr_node = p_result; p_addr_node; p_addr_node = p_addr_node->ai_next)
        {
            h_sockfd = socket(p_addr_node->ai_family, p_addr_node->ai_socktype, p_addr_node->ai_protocol);
            if (-1 == h_sockfd)
            {
                syslog(LOG_ERR, "socket failed with error: %s\n", strerror(errno));
                continue; // skip this loop
            }

            int reuse = 1;
            if (-1 == setsockopt(h_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)))
            {
                syslog(LOG_ERR, "setsockopt failed with error: %s\n", strerror(errno));
                continue; // skip this loop
            }

            if (-1 == bind(h_sockfd, p_addr_node->ai_addr, p_addr_node->ai_addrlen))
            {
                syslog(LOG_ERR, "bind failed with error: %s\n", strerror(errno));
                continue; // skip this loop
            }

            break; // so we dont iterate till end of linked list, if we successfully performed the above operations
        }

        freeaddrinfo(p_result); // free the addrinfo linked list

        if (NULL == p_addr_node)
        {
            // could not connect to any address, exit with failure
            syslog(LOG_ERR, "could not bind to any socket returned by getaddrinfo");
            b_status = false;
        }
    }

    *p_socket_fd = h_sockfd;
    return b_status;
}

// @brief open the socket data file for appending received data
bool open_socket_data_file(char const * const p_pathname, FILE ** const pph_socket_data_file)
{
    bool b_status = true;

    *pph_socket_data_file = fopen(p_pathname, "a+");

    if (NULL == *pph_socket_data_file)
    {
        syslog(LOG_ERR, "fopen failed with error %s", strerror(errno));
        b_status = false;
    }

    return b_status;
}

// @brief redirect SIGINT and SIGTERM to signal handler
bool assign_signal_handler(void)
{
    bool b_status = true;
    struct sigaction action = {0};

    action.sa_handler = &signal_handler;
    if (-1 == sigaction(SIGINT, &action, NULL))
    {
        syslog(LOG_ERR, "could not set sigaction for SIGINT with error %s", strerror(errno));
        b_status = false;
    }

    if (-1 == sigaction(SIGTERM, &action, NULL))
    {
        syslog(LOG_ERR, "could not set sigaction for SIGINT with error %s", strerror(errno));
        b_status = false;
    }

    return b_status;
}

// @brief read contents of socket data file and send it back over socket connection
bool writeback(FILE * const ph_socket_data_file, const int h_recvfd)
{
    // loop till eof, then break 
    ssize_t line_size = 0;
    size_t len = 0;
    char * p_line = NULL;
    bool b_status = true;
    bool b_send_fail = false;

    // set file offet to 0 before reading
    if (-1 == fseek(ph_socket_data_file, 0, SEEK_SET))
    {
        syslog(LOG_ERR, "fseek failed with error %s", strerror(errno));
        b_status = false;
    }
    else
    {
        while (!b_send_fail)
        {
            line_size = getline(&p_line, &len, ph_socket_data_file);
            if (-1 == line_size)
            {
                // failure to read, can be due to EOF, exit
                break;
            }

            int bytes_written = 0;
            int total_bytes_written = 0;
            while (total_bytes_written < line_size)
            {
                bytes_written = send(h_recvfd, p_line + total_bytes_written, line_size - total_bytes_written, MSG_NOSIGNAL);
                if (-1 == bytes_written)
                {
                    // send failed
                    syslog(LOG_ERR, "send failed with error %s", strerror(errno));
                    b_send_fail = true;
                    b_status = false;
                    break;
                }
                total_bytes_written += bytes_written;
            }
        }
    }

    free(p_line); 
    return b_status;
}

// @brief to print help string for application
void print_help_str(void)
{
    printf("Usage: ./aesdsocket [-d]\n");
    printf("Use optional argument -d to daemonize process\n");
}

// @brief function to daemonize the process
bool daemonize_process(void)
{
    bool b_status = true;

    pid_t pid = fork();
    if (-1 == pid)
    {
        syslog(LOG_ERR, "fork failed with error: %s\n", strerror(errno));
    }
    else if (0 == pid)
    {
        // child process
        if (-1 == setsid())
        {
            b_status = false;
            syslog(LOG_ERR, "setsid failed with error: %s\n", strerror(errno));
        }
        else if (-1 == chdir("/"))
        {
            b_status = false;
            syslog(LOG_ERR, "chdir failed with error: %s\n", strerror(errno));
        }
        else if (EOF == fcloseall())
        {
            b_status = false;
            syslog(LOG_ERR, "fcloseall failed");
        }

        int null_fd = open("/dev/null", O_WRONLY);
        if (-1 == null_fd)
        {
            b_status = false;
            syslog(LOG_ERR, "open failed with error: %s\n", strerror(errno));
        }
        else 
        {
            if ((-1 == dup2(null_fd, STDOUT_FILENO)) || 
                (-1 == dup2(null_fd, STDERR_FILENO)) ||
                (-1 == dup2(null_fd, STDIN_FILENO)))
            {
                b_status = false;
                syslog(LOG_ERR, "dup2 failed to redirect");
            }
            if (-1 == close(null_fd))
            {
                b_status = false;
                syslog(LOG_ERR, "close failed with error: %s\n", strerror(errno));
            }
        }

    }
    else
    {
        // parent process
        exit(EXIT_SUCCESS);
    }

    return b_status;
}

// @brief block signals specified by p_sig_mask
bool block_signals(sigset_t const * const p_sig_mask)
{
    bool b_status = true;

    if (-1 == sigprocmask(SIG_BLOCK, p_sig_mask, NULL))
    {
        syslog(LOG_ERR, "sigprocmask block failed with error: %s\n", strerror(errno));
        b_status = false;
    }

    return b_status;
}

// @brief unblock signals specified by p_sig_mask
bool unblock_signals(sigset_t const * const p_sig_mask)
{
    bool b_status = true;

    if (-1 == sigprocmask(SIG_UNBLOCK, p_sig_mask, NULL))
    {
        syslog(LOG_ERR, "sigprocmask unblock failed with error: %s\n", strerror(errno));
        b_status = false;
    }

    return b_status;
}

int main(const int argc, char ** const p_argv)
{
    int return_code = 0;
    int h_recvfd = 0;
    int h_sockfd = 0;
    FILE * ph_socket_data_file = NULL;
    char p_ip_addr_buffer[INET_ADDRSTRLEN];
    int opt_char;
    struct sockaddr_in remote_client_addr;
    bool b_daemonize = false;
    sigset_t sig_mask;

    if (argc > 2)
    {
        syslog(LOG_ERR, "Invalid number of arguement");
        print_help_str();
        exit(EXIT_APP_FAILURE);
    }

    while ((opt_char = getopt(argc, p_argv, "d")) != -1)
    {
        switch (opt_char)
        {
            case 'd':
                b_daemonize = true;
            break;

            default:
                syslog(LOG_ERR, "Invalid option %c!", opt_char);
                print_help_str();
                exit(EXIT_APP_FAILURE);
            break;
        }
    }

    // create a signal mask to block SIGINT and SIGTERM when a connection is open
    if (-1 == sigemptyset(&sig_mask))
    {
        syslog(LOG_ERR, "sigemptyset failed with error: %s", strerror(errno));
        exit(EXIT_APP_FAILURE);
    }

    if ((-1 == sigaddset(&sig_mask, SIGINT) || (-1 == sigaddset(&sig_mask, SIGTERM))))
    {
        syslog(LOG_ERR, "sigaddset failed with error: %s", strerror(errno));
        exit(EXIT_APP_FAILURE);
    }

    if (!bind_to_address(NULL, PORT, &h_sockfd))
    {
        syslog(LOG_ERR, "could not bind address provided!");
        exit(EXIT_SOCKET_FAILURE);
    }

    if (b_daemonize)
    {
        if (!daemonize_process())
        {
            syslog(LOG_ERR, "could not daemonize!");
            exit(EXIT_SOCKET_FAILURE);
        }
        else
        {
            syslog(LOG_DEBUG, "daemonized successfully!");
        }
    }

    return_code = listen(h_sockfd, BACKLOG);
    if (-1 == return_code)
    {
        syslog(LOG_ERR, "listen failed with error: %s\n", strerror(errno));
        exit(EXIT_SOCKET_FAILURE);
    }

    if (!open_socket_data_file(SOCKET_DATA_FILE_PATHNAME, &ph_socket_data_file))
    {
        syslog(LOG_ERR, "could not create/open %s", SOCKET_DATA_FILE_PATHNAME);
        exit(EXIT_APP_FAILURE);
    }
    
    if (!assign_signal_handler())
    {
        syslog(LOG_ERR, "could not assign sigaction");
        exit(EXIT_APP_FAILURE);
    }

    while (b_accept_connections)
    {
        socklen_t remote_client_addr_size = sizeof(remote_client_addr);
        h_recvfd = accept(h_sockfd, (struct sockaddr *)&remote_client_addr, &remote_client_addr_size); 
        if (-1 == h_recvfd)
        {
            syslog(LOG_ERR, "accept failed with error: %s\n", strerror(errno));
            break;
        }
        else
        {
            // log message to syslog "Accecpted connection from xxxx"
            if (NULL == inet_ntop(AF_INET, &remote_client_addr.sin_addr, p_ip_addr_buffer, sizeof(p_ip_addr_buffer)))
            {
                syslog(LOG_ERR, "inet_ntop failed with error: %s\n", strerror(errno));
                break;
            }
            syslog(LOG_DEBUG, "Accepted connection from %s\n", p_ip_addr_buffer);
        }

        // connection is now open, block signals till operations are complete
        if (!block_signals(&sig_mask))
        {
            syslog(LOG_ERR, "could not block signals!");
            break;
        }

        char p_buffer[RECV_BUF_LEN] = {0};
        int malloc_buf_size = 0;
        int byte_string_len = 0;
        char * p_malloc_buf = NULL;
        char * p_tmp = NULL;
        while (true)
        {
            int bytes_recv = recv(h_recvfd, p_buffer, RECV_BUF_LEN - 1, 0);
            if (-1 == bytes_recv)
            {
                syslog(LOG_ERR, "recv failed with error %s", strerror(errno));
                break;
            }
            else if (0 == bytes_recv)
            {
                // connection closed
                break;
            }
            p_buffer[bytes_recv] = '\0';

            // check if p_buffer contain \n and set a flag
            bool b_contains_newline = (strchr(p_buffer, '\n') != NULL) ? true : false;
            bool b_is_first_malloc = (0 == malloc_buf_size) ? true : false;
            byte_string_len += bytes_recv;
            malloc_buf_size += bytes_recv + 1; // +1 for null terminator
            
            // allocate memory
            p_tmp = realloc(p_malloc_buf, malloc_buf_size);
            if (p_tmp != NULL)
            {
                p_malloc_buf = p_tmp;
            }
            else
            {
                free(p_malloc_buf);
                break;
            }

            if (b_is_first_malloc)
            {
                // if first malloc, strcpy
                strcpy(p_malloc_buf,  p_buffer);
            }
            else
            {
                // append to existing string in malloc buf
                strcat(p_malloc_buf, p_buffer);
            }

            // if contains newline, write to file, and perform writeback
            if (b_contains_newline)
            {
                int bytes_written = fprintf(ph_socket_data_file, "%s", p_malloc_buf);
                free(p_malloc_buf); // no longer needed

                if (bytes_written != byte_string_len)
                {
                    syslog(LOG_ERR, "fprintf did not complete write to socket data file");
                }
                else if (bytes_written < 0)
                {
                    syslog(LOG_ERR, "fprintf failed with error %s", strerror(errno));
                }

                if (!writeback(ph_socket_data_file, h_recvfd))
                {
                    syslog(LOG_ERR, "writeback failed!");
                    break;
                }
            }
        }
        // logs messsage to syslog "Closed connection from XXX"
        syslog(LOG_DEBUG, "Closed connection from %s\n", p_ip_addr_buffer);

        // connection is now closed, unblock signals 
        if (!unblock_signals(&sig_mask))
        {
            syslog(LOG_ERR, "could not unblock signals!");
            break;
        }
    }

    if (-1 == remove(SOCKET_DATA_FILE_PATHNAME))
    {
        syslog(LOG_DEBUG, "remove failed with error %s", strerror(errno));
    }

    // h_recvfd closed when recv is complete
    close(h_sockfd);
    fclose(ph_socket_data_file);

    if (b_accept_connections == false)
    {
        return 0; // regular cleanup
    }
    else
    {
        return EXIT_APP_FAILURE; // some other failure
    }
}