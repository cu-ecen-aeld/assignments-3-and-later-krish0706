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
#include <sys/queue.h>
#include <netdb.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>

#define SOCKET_DATA_FILE_PATHNAME "/var/tmp/aesdsocketdata"
#define PORT "9000" // the port to connect to
#define BACKLOG 10
#define RECV_BUF_LEN 100

#define EXIT_SOCKET_FAILURE (-1)
#define EXIT_APP_FAILURE (-1)
#define MAX_TIMESTAMP_LEN 995

struct thread_args_s
{
    struct sockaddr_in remote_client_address;
    pthread_mutex_t * p_mutex;
    int h_recvfd;
    bool b_is_thread_complete;
    pthread_t tid;
};

struct slist_entry_s
{
    struct thread_args_s thread_args;
    SLIST_ENTRY(slist_entry_s) slist_entries;
};

SLIST_HEAD(slist_head_s, slist_entry_s);
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool b_accept_connections = true;

// @brief signal handler to redirect SIGINT and SIGTERM 
// to gracefully exit application
static void signal_handler(int signo)
{
    if ((SIGINT == signo) || SIGTERM == signo)
    {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        b_accept_connections = false;
    }
}

// @brief bind to given node and service 
static bool bind_to_address(char const * const p_node, char const * const p_service, int * const p_socket_fd)
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
static bool open_socket_data_file(char const * const p_pathname, FILE ** const pph_socket_data_file)
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
static bool assign_signal_handler(void)
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
static bool writeback(FILE * const ph_socket_data_file, const int h_recvfd)
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
        syslog(LOG_ERR, "fseek set failed with error %s", strerror(errno));
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

    if (-1 == fseek(ph_socket_data_file, 0, SEEK_END))
    {
        syslog(LOG_ERR, "fseek end failed with error %s", strerror(errno));
        b_status = false;
    }

    return b_status;
}

// @brief to print help string for application
static void print_help_str(void)
{
    printf("Usage: ./aesdsocket [-d]\n");
    printf("Use optional argument -d to daemonize process\n");
}

// @brief function to daemonize the process
static bool daemonize_process(void)
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

// @brief function for service thread, to handle read and writeback on a new connection
static void * service_thread(void * p_arg)
{
    FILE * ph_socket_data_file = NULL;
    char p_buffer[RECV_BUF_LEN] = {0};
    char p_ip_addr_buffer[INET_ADDRSTRLEN];
    int malloc_buf_size = 0;
    int byte_string_len = 0;
    char * p_malloc_buf = NULL;
    char * p_tmp = NULL;
    int return_code; 

    struct thread_args_s * p_thread_args = (struct thread_args_s *)p_arg;

    // log message to syslog "Accecpted connection from xxxx"
    if (NULL == inet_ntop(AF_INET, &p_thread_args->remote_client_address.sin_addr, p_ip_addr_buffer, sizeof(p_ip_addr_buffer)))
    {
        syslog(LOG_ERR, "inet_ntop failed with error: %s\n", strerror(errno));
    }
    else
    {
        // log accept connection message
        syslog(LOG_DEBUG, "Accepted connection from %s\n", p_ip_addr_buffer);
        while (true)
        {
            int bytes_recv = recv(p_thread_args->h_recvfd, p_buffer, RECV_BUF_LEN - 1, 0);
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
                // acquire mutex
                return_code = pthread_mutex_lock(p_thread_args->p_mutex);
                if (return_code != 0)
                {
                    syslog(LOG_ERR, "mutex lock failed with error %s", strerror(return_code));
                }

                // open socket data file in append mode 
                if (!open_socket_data_file(SOCKET_DATA_FILE_PATHNAME, &ph_socket_data_file))
                {
                    syslog(LOG_ERR, "could not create/open %s", SOCKET_DATA_FILE_PATHNAME);
                }

                // write to file
                int bytes_written = fprintf(ph_socket_data_file, "%s", p_malloc_buf);

                if (bytes_written != byte_string_len)
                {
                    syslog(LOG_ERR, "fprintf did not complete write to socket data file");
                }
                else if (bytes_written < 0)
                {
                    syslog(LOG_ERR, "fprintf failed with error %s", strerror(errno));
                }

                // send socketdatafile contents back over socket connection
                if (!writeback(ph_socket_data_file, p_thread_args->h_recvfd))
                {
                    syslog(LOG_ERR, "writeback failed!");
                    break;
                }

                // close socket data file
                fclose(ph_socket_data_file);

                // release mutex
                return_code = pthread_mutex_unlock(p_thread_args->p_mutex);
                if (return_code != 0)
                {
                    syslog(LOG_ERR, "mutex unlock failed with error %s", strerror(return_code));
                }
            }
        }
    }

    // free malloc'd data
    free(p_malloc_buf); 

    // logs closed connection message
    syslog(LOG_DEBUG, "Closed connection from %s\n", p_ip_addr_buffer);
    p_thread_args->b_is_thread_complete = true;
    return NULL;
}

// @brief periodic timer callback, appends timestamp to socketdata file
static void timer_callback(union sigval)
{
    char timestamp[MAX_TIMESTAMP_LEN];
    int return_code;
    FILE * ph_socket_data_file = NULL;

    time_t seconds_since_epoch = time(NULL);
    struct tm * p_broken_down_time = localtime(&seconds_since_epoch);
    if (NULL == p_broken_down_time)
    {
        syslog(LOG_ERR, "local time failed with error %s", strerror(errno));
    }

    strftime(timestamp, sizeof(timestamp)/sizeof(timestamp[0]), "%a, %d %b %Y %T %z", p_broken_down_time);
    syslog(LOG_DEBUG, "timestamp:%s", timestamp);

    // acquire mutex
    return_code = pthread_mutex_lock(&mutex);
    if (return_code != 0)
    {
        syslog(LOG_ERR, "mutex lock failed with error %s", strerror(return_code));
    }

    // open socketdata file in append mode
    if (!open_socket_data_file(SOCKET_DATA_FILE_PATHNAME, &ph_socket_data_file))
    {
        syslog(LOG_ERR, "could not create/open %s", SOCKET_DATA_FILE_PATHNAME);
    }

    // write timestamp to file
    int bytes_written = fprintf(ph_socket_data_file, "timestamp:%s\n", timestamp);

    if (bytes_written < 0)
    {
        syslog(LOG_ERR, "fprintf failed with error %s", strerror(errno));
    }

    // close file
    fclose(ph_socket_data_file);

    // release mutex
    return_code = pthread_mutex_unlock(&mutex);
    if (return_code != 0)
    {
        syslog(LOG_ERR, "mutex unlock failed with error %s", strerror(return_code));
    }
}

int main(const int argc, char ** const p_argv)
{
    int return_code = 0;

    // check if more than the supported number of arguements have 
    // been provided
    if (argc > 2)
    {
        syslog(LOG_ERR, "Invalid number of arguement");
        print_help_str();
        exit(EXIT_APP_FAILURE);
    }

    int opt_char;
    bool b_daemonize = false;

    // check if -d flag provided to daemonsize process
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

    int h_sockfd = 0;
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

    // note: timer must be created in child process, because 
    // child does not inherit timer from parent
    timer_t timer;
    struct sigevent evp = {0};

    // start after 10s and repeat every 10s
    struct itimerspec interval_timer_spec = {
        .it_interval={.tv_sec=10,.tv_nsec=0},
        .it_value={.tv_sec=10,.tv_nsec=0},
    };

    // create a new thread when timer expires
    evp.sigev_notify = SIGEV_THREAD;
    evp.sigev_notify_function = timer_callback;

    if (-1 == timer_create(CLOCK_MONOTONIC, &evp, &timer))
    {
        syslog(LOG_ERR, "timer_create failed with error: %s\n", strerror(errno));
    }
    else if (-1 == timer_settime(timer, 0, &interval_timer_spec, NULL))
    {
        syslog(LOG_ERR, "timer_settime failed with error: %s\n", strerror(errno));
    }

    if (!assign_signal_handler())
    {
        syslog(LOG_ERR, "could not assign sigaction");
        exit(EXIT_APP_FAILURE);
    }

    return_code = listen(h_sockfd, BACKLOG);
    if (-1 == return_code)
    {
        syslog(LOG_ERR, "listen failed with error: %s\n", strerror(errno));
        exit(EXIT_SOCKET_FAILURE);
    }

    // initialize linked list
    struct slist_head_s slist_head;
    SLIST_INIT(&slist_head);

    while (b_accept_connections)
    {
        struct sockaddr_in remote_client_addr;
        socklen_t remote_client_addr_size = sizeof(remote_client_addr);

        int h_recvfd = accept(h_sockfd, (struct sockaddr *)&remote_client_addr, &remote_client_addr_size); 
        if (-1 == h_recvfd)
        {
            syslog(LOG_ERR, "accept failed with error: %s\n", strerror(errno));
            break;
        }
        else
        {
            // create thread and save thread args structure on linked list
            struct slist_entry_s * p_slist_entry;
            p_slist_entry = malloc(sizeof(struct slist_entry_s));
            if (NULL == p_slist_entry)
            {
                syslog(LOG_ERR, "malloc failed, could not create new thread, exiting!");
            }
            else 
            {
                p_slist_entry->thread_args.h_recvfd = h_recvfd;
                p_slist_entry->thread_args.p_mutex = &mutex;
                p_slist_entry->thread_args.remote_client_address = remote_client_addr;
                p_slist_entry->thread_args.b_is_thread_complete = false;
                return_code = pthread_create(&p_slist_entry->thread_args.tid, NULL, service_thread, (void *)p_slist_entry);
                if (return_code != 0)
                {
                    syslog(LOG_ERR, "thread create failed with error %s", strerror(return_code));
                    free(p_slist_entry);
                }
                else
                {
                    SLIST_INSERT_HEAD(&slist_head, p_slist_entry, slist_entries);
                }
            }
        }

        // check for closed threads to pthread join them, and free 
        // malloc'd memory
        struct slist_entry_s * p_slist_entry = SLIST_FIRST(&slist_head);
        while (p_slist_entry)
        {
            if (p_slist_entry->thread_args.b_is_thread_complete)
            {
                return_code = pthread_join(p_slist_entry->thread_args.tid, NULL);
                if (return_code != 0)
                {
                    syslog(LOG_ERR, "pthread join failed with error %s", strerror(return_code));
                }
                SLIST_REMOVE(&slist_head, p_slist_entry, slist_entry_s, slist_entries);
                free(p_slist_entry);
            }
            p_slist_entry = SLIST_NEXT(p_slist_entry, slist_entries);
        }
    }

    // cleanup!

    // signal to terminate recevied, join every thread
    // and free malloc'd memory
    while (!SLIST_EMPTY(&slist_head))
    {
        struct slist_entry_s * p_slist_entry = SLIST_FIRST(&slist_head);
        return_code = pthread_join(p_slist_entry->thread_args.tid, NULL);
        if (return_code != 0)
        {
            syslog(LOG_ERR, "pthread join failed with error %s", strerror(return_code));
        }
        SLIST_REMOVE_HEAD(&slist_head, slist_entries);
        free(p_slist_entry);
    }

    if (-1 == remove(SOCKET_DATA_FILE_PATHNAME))
    {
        syslog(LOG_ERR, "remove failed with error %s", strerror(errno));
    }

    // h_recvfd closed when recv is complete in respective thread
    close(h_sockfd);

    if (-1 == timer_delete(timer))
    {
        syslog(LOG_ERR, "timer_delete failed with error %s", strerror(errno));
    }

    if (b_accept_connections == false)
    {
        return 0; // regular cleanup
    }
    else
    {
        return EXIT_APP_FAILURE; // some other failure
    }
}
