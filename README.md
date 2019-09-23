# pthread_mutex_timedlock and PTHREAD_PRIO_INHERIT bug
----------------------------------------

This sample shows a bug in glibc / linux kernel regarding pthread mutexes and 
priority inheritance.

A call to pthread_mutex_timedlock does not return after the timeout has expired,
if the thread that holds the mutex does not return it and is bound to the same
core as the caller of pthread_mutex_timedlock (or the system has only one core).

I would say that the followin paragraph from the
[POSIX standard](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutex_timedlock.html)
states, that it is indeed a bug:

> As a consequence of the priority inheritance rules (for mutexes initialized with the PRIO_INHERIT protocol), if a timed mutex wait is terminated because its timeout expires, the priority of the owner of the mutex shall be adjusted as necessary to reflect the fact that this thread is no longer among the threads waiting for the mutex.

## Samplecode
-----
The code demonstrating the behavior is quite easy:

1. Set cpu affinity to a single core
2. Setup (mutex, barrier)
3. Set priority to lowest prio + 1 (SCHED_FIFO)
4. Create thread (does not run because of SCHED_FIFO)
5. Wait for Barrier
6. In Thread:
    1. Set priority to lowest prio
    2. Lock mutex
    3. Wait for Barrier
7. Call pthread_mutex_timedlock with a timeout of 1 second
8. In Thread:
    1. Bussy loop until 5 seconds have passed
    2. Unlock Mutex
9. pthread_mutex_timedlock returns

Between 8.1. and 8.2. the timeout of the pthread_mutex_timedlock has exceeded,
the priority of the thread should have been lowered and the main thread should run.
But the main thread runs again only, after the thread unlocks the mutex. The
main thread will then hold the lock, but 4 seconds **after** the timeout.

Changing the scheduling algorithm to SCHED_RR, it works better, but the timeout 
is still almost 100ms late (100ms is the time slice of SCHED_RR).

Not using PTHREAD_PRIO_INHERIT (i.e. using PTHREAD_PRIO_NONE) works perfect in this
situation.


### Output (SCHED_FIFO)
```
Time[s] Thread Msg
  0.000 main   Restricted execution to a single core
  0.000 main   Prio set to 1
  0.000 main   pthread_barrier_wait
  0.000 aux    Prio set to 0
  0.000 aux    Locking mutex
  0.000 aux    Locked mutex
  0.000 aux    pthread_barrier_wait
  0.000 main   pthread_barrier_wait done
  0.000 main   pthread_mutex_timedlock(1)
  0.000 aux    pthread_barrier_wait done
  0.000 aux    Work for 5 seconds
  5.000 main   pthread_mutex_timedlock done (5.000022 seconds)
  5.000 main   ERROR: Expected pthread_mutex_timedlock to timeout (result is: 0)
  5.000 main   pthread_mutex_timedlock: 0
  5.000 aux    Unlocked mutex
```

### Output (SCHED_RR)
```
Time[s] Thread Msg
  0.000 main   Restricted execution to a single core
  0.000 main   Prio set to 1
  0.000 main   pthread_barrier_wait
  0.000 aux    Prio set to 0
  0.000 aux    Locking mutex
  0.000 aux    Locked mutex
  0.000 aux    pthread_barrier_wait
  0.000 main   pthread_barrier_wait done
  0.000 main   pthread_mutex_timedlock(1)
  0.000 aux    pthread_barrier_wait done
  0.000 aux    Work for 5 seconds
  1.098 main   pthread_mutex_timedlock done (1.097743 seconds)
  1.098 main   pthread_mutex_timedlock: 110
  1.098 aux    Unlocked mutex
```

### Output (PTHREAD_PRIO_NONE)
```
Time[s] Thread Msg
  0.000 main   Restricted execution to a single core
  0.000 main   Prio set to 1
  0.000 main   pthread_barrier_wait
  0.000 aux    Prio set to 0
  0.000 aux    Locking mutex
  0.000 aux    Locked mutex
  0.000 aux    pthread_barrier_wait
  0.000 main   pthread_barrier_wait done
  0.000 main   pthread_mutex_timedlock(1)
  0.000 aux    pthread_barrier_wait done
  0.000 aux    Work for 5 seconds
  1.001 main   pthread_mutex_timedlock done (1.000386 seconds)
  1.001 main   pthread_mutex_timedlock: 110
  1.001 aux    Unlocked mutex
```


## Compile and run
```
    gcc -pthread -o pthread_mutex_timedlock_prio_bug main.c && sudo ./pthread_mutex_timedlock_prio_bug
```

**Note**: Running as root is required, otherwise setting priorities is not allowed.