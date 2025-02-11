#include "threading.h"
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void* threadfunc(void* thread_param)
{
    int return_code;
    bool b_status = true;
    struct thread_data * const p_thread_data = (struct thread_data *) thread_param;

    return_code = usleep(p_thread_data->wait_to_obtain_ms);
    if (-1 == return_code)
    {
        syslog(LOG_DEBUG, "Call to usleep for wait_to_obtain_ms failed with error: %s", strerror(errno));
        b_status = false;
    }

    return_code = pthread_mutex_lock(p_thread_data->p_mutex);
    if (return_code != 0)
    {
        syslog(LOG_DEBUG, "Call to pthread_mutex_lock failed with error: %s", strerror(return_code));
        b_status = false;
    }

    return_code = usleep(p_thread_data->wait_to_release_ms);
    if (-1 == return_code)
    {
        syslog(LOG_DEBUG, "Call to usleep for wait_to_release_ms failed with error: %s", strerror(errno));
        b_status = false;
    }

    return_code = pthread_mutex_unlock(p_thread_data->p_mutex);
    if (return_code != 0)
    {
        syslog(LOG_DEBUG, "Call to pthread_mutex_unlock failed with error: %s", strerror(return_code));
        b_status = false;
    }

    p_thread_data->thread_complete_success = b_status;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    bool b_status = true;

    // allocate memory for thread_data
    struct thread_data * const p_thread_data = (struct thread_data *)malloc(sizeof(struct thread_data));

    if (NULL == p_thread_data)
    {
        b_status = false;
        syslog(LOG_ERR, "Call to malloc failed!");
    }
    else 
    {
        // pass mutex and waits in thread_data
        p_thread_data->p_mutex = mutex;
        p_thread_data->wait_to_obtain_ms = wait_to_obtain_ms;
        p_thread_data->wait_to_release_ms = wait_to_release_ms;

        // create thread 
        const int return_code = pthread_create(thread, NULL, threadfunc, (void *)p_thread_data);
        if (return_code != 0)
        {
            b_status = false;
            syslog(LOG_ERR, "Call to pthread_create failed with error: %s", strerror(return_code));
        }
    }

    return b_status;
}

