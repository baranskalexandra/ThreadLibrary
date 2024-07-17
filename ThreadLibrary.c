#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#define MAX_THREADS 128
#define STACK_SIZE 1024 * 1024

typedef enum { READY, RUNNING, BLOCKED, TERMINATED } thread_state;

typedef struct {
    void (*function)(void *);
    void *args;
    unsigned int priority;
    jmp_buf context;
    uint8_t *stack;
    thread_state state;
} thread_t;

typedef struct {
    int locked;
    thread_t *owner;
} mutex_t;

typedef struct {
    int signaled;
    thread_t *waiting_threads[MAX_THREADS];
    int wait_count;
} cond_t;

static thread_t *threads[MAX_THREADS];
static int num_threads = 0;
static int current_thread = -1;

int mythreads_start(void (*thread)(void *), void *args, unsigned int priority) {
    if (num_threads >= MAX_THREADS) return -1;
    
    thread_t *t = (thread_t *)malloc(sizeof(thread_t));
    if (!t) return -1;

    t->function = thread;
    t->args = args;
    t->priority = priority;
    t->state = READY;
    t->stack = (uint8_t *)malloc(STACK_SIZE);
    if (!t->stack) {
        free(t);
        return -1;
    }

    if (setjmp(t->context) == 0) {
        t->context[0].__jmpbuf[6] = (long)(t->stack + STACK_SIZE - sizeof(void *));
        memcpy(t->stack + STACK_SIZE - sizeof(void *), &t, sizeof(void *));
    }
    threads[num_threads++] = t;
    return 0;
}

void mythread_exit(void) {
    threads[current_thread]->state = TERMINATED;
    free(threads[current_thread]->stack);
    threads[current_thread]->stack = NULL; // Avoid dangling pointer
    longjmp(threads[current_thread]->context, 1);
}

int mythread_mutex(mutex_t *mutex) {
    if (!mutex) return -1;
    mutex->locked = 0;
    mutex->owner = NULL;
    return 0;
}

int mythread_lock(mutex_t *mutex) {
    if (!mutex) return -1;
    while (__sync_lock_test_and_set(&mutex->locked, 1)) {
        threads[current_thread]->state = BLOCKED;
        longjmp(threads[current_thread]->context, 1);
    }
    mutex->owner = threads[current_thread];
    return 0;
}

int mythread_unlock(mutex_t *mutex) {
    if (!mutex || mutex->owner != threads[current_thread]) return -1;
    mutex->owner = NULL;
    __sync_lock_release(&mutex->locked);
    return 0;
}

int mythread_cond(cond_t *cond) {
    if (!cond) return -1;
    cond->signaled = 0;
    cond->wait_count = 0;
    return 0;
}

int mythread_wait(cond_t *cond, mutex_t *mutex) {
    if (!cond || !mutex) return -1;
    cond->waiting_threads[cond->wait_count++] = threads[current_thread];
    mythread_unlock(mutex);
    threads[current_thread]->state = BLOCKED;
    longjmp(threads[current_thread]->context, 1);
    mythread_lock(mutex);
    return 0;
}

int mythread_signal(cond_t *cond) {
    if (!cond || cond->wait_count == 0) return -1;
    thread_t *t = cond->waiting_threads[--cond->wait_count];
    t->state = READY;
    return 0;
}

int mythread_destroy(void *obj) {
    if (!obj) return -1;
    free(obj);
    return 0;
}

void scheduler() {
    while (1) {
        int highest_priority = -1;
        int next_thread = -1;
        for (int i = 0; i < num_threads; i++) {
            if (threads[i]->state == READY && threads[i]->priority > highest_priority) {
                highest_priority = threads[i]->priority;
                next_thread = i;
            }
        }
        if (next_thread == -1) break;

        current_thread = next_thread;
        threads[current_thread]->state = RUNNING;
        if (setjmp(threads[current_thread]->context) == 0) {
            longjmp(threads[current_thread]->context, 1);
        }
    }
}

void thread_function(void *args) {
    int *num = (int *)args;
    printf("Thread %d started\n", *num);
    mythread_exit();
}

int main() {
    int num1 = 1, num2 = 2;
    mythreads_start(thread_function, &num1, 1);
    mythreads_start(thread_function, &num2, 2);
    scheduler();
    return 0;
}
