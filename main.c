#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int THREAD_WORK_TIME = 5;
static int MAIN_LOCK_TIMEOUT = 1;
static int MUTEX_PROTOCOL = PTHREAD_PRIO_INHERIT;
static int SCHED_ALGO = SCHED_FIFO;

double startTime;
pthread_mutex_t mutex;
pthread_barrier_t barrier;
pthread_t mainThread;
volatile int stop = 0;

double getTime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1E9;
}

void debug(char* fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsprintf(buffer, fmt, args);
    va_end(args);

    printf("%7.3f %-6s %s\n", getTime() - startTime, pthread_self() == mainThread ? "main" : "aux", buffer);
}

void restrictToSingleCore()
{
    int rc;

    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(1, &cpus);

    debug("Restricted execution to a single core");

    rc = sched_setaffinity(0, sizeof(cpus), &cpus);
    if (rc) {
        printf("Unable to set cpu affinity: %s\n", strerror(errno));
        exit(1);
    }
}

void setupMutex()
{
    pthread_mutexattr_t mutexattr;
    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setprotocol(&mutexattr, MUTEX_PROTOCOL);

    pthread_mutex_init(&mutex, &mutexattr);
}

void setThreadPrio(int prio)
{
    int rc;
    struct sched_param schedparam;
    schedparam.sched_priority = sched_get_priority_min(SCHED_ALGO) + prio;
    rc = pthread_setschedparam(pthread_self(), SCHED_ALGO, &schedparam);

    if (rc) {
        printf("Unable to set scheduling parameters: ");
        if (errno == EPERM) {
            printf("CAP_SYS_NICE required.\nPlease run as root.");
        } else {
            printf("%s", strerror(errno));
        }
        printf("\n");
        exit(1);
    }
    debug("Prio set to %d", prio);
}

void *lockThread(void *param)
{
    double end;

    setThreadPrio(0);

    debug("Locking mutex");
    pthread_mutex_lock(&mutex);
    debug("Locked mutex");

    debug("pthread_barrier_wait");
    pthread_barrier_wait(&barrier);
    debug("pthread_barrier_wait done");

    end = getTime() + THREAD_WORK_TIME;
    debug("Work for %d seconds", THREAD_WORK_TIME);

    while (getTime() < end && !stop);
    pthread_mutex_unlock(&mutex);
    debug("Unlocked mutex");

    return NULL;
}

int main()
{
    int rc;
    struct timespec ts;
    double lockStart;
    pthread_t thread;

    mainThread = pthread_self();
    startTime = getTime();

    printf("Time[s] Thread Msg\n");

    restrictToSingleCore();
    setupMutex();
    pthread_barrier_init(&barrier, NULL, 2);

    setThreadPrio(1);

    pthread_create(&thread, NULL, lockThread, NULL);

    debug("pthread_barrier_wait");
    pthread_barrier_wait(&barrier);
    debug("pthread_barrier_wait done");

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += MAIN_LOCK_TIMEOUT;
    debug("pthread_mutex_timedlock(%d)", MAIN_LOCK_TIMEOUT);
    lockStart = getTime();
    rc = pthread_mutex_timedlock(&mutex, &ts);
    debug("pthread_mutex_timedlock done (%3f seconds)", getTime() - lockStart);

    if (rc != ETIMEDOUT)
    {
        debug("ERROR: Expected pthread_mutex_timedlock to timeout (result is: %d)", rc);
    }

    debug("pthread_mutex_timedlock: %d", rc);

    stop = 1;

    pthread_join(thread, NULL);

    return rc == ETIMEDOUT ? 0 : 1;
}