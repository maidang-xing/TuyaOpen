#include "tal_system_port.h"

#include <stdlib.h>
#include <string.h>

#include "system/os/os_api.h"
#include "asm/cpu.h"

typedef struct {
    char name[configMAX_TASK_NAME_LEN];
    TAL_PORT_THREAD_FUNC func;
    void *arg;
} JIELI_THREAD;

typedef struct {
    OS_MUTEX mutex;
} JIELI_MUTEX;

typedef struct {
    OS_SEM sem;
} JIELI_SEM;

typedef struct {
    OS_QUEUE queue;
    uint32_t message_size;
} JIELI_QUEUE;

static int timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == 0xFFFFFFFFu) {
        return 0;
    }
    if (timeout_ms == 0) {
        return 0;
    }
    return (int)((timeout_ms + 9u) / 10u);
}

static u8 map_priority(uint32_t priority)
{
    /* Tuya's lower numeric priority is the more important one. */
    if (priority > 5u) {
        priority = 5u;
    }
    return (u8)(10u + (5u - priority));
}

static void jieli_thread_entry(void *arg)
{
    JIELI_THREAD *thread = (JIELI_THREAD *)arg;
    if (thread != NULL && thread->func != NULL) {
        thread->func(thread->arg);
    }
    free(thread);
    os_task_del_res(OS_TASK_SELF);
}

TAL_PORT_THREAD_HANDLE tal_system_port_thread_create(const char *name, uint32_t stack_size,
                                                uint32_t priority, TAL_PORT_THREAD_FUNC func,
                                                void *arg)
{
    JIELI_THREAD *thread;
    const char *task_name = (name != NULL && name[0] != '\0') ? name : "tuya";

    if (func == NULL) {
        return NULL;
    }
    thread = (JIELI_THREAD *)calloc(1, sizeof(*thread));
    if (thread == NULL) {
        return NULL;
    }
    strncpy(thread->name, task_name, sizeof(thread->name) - 1u);
    thread->func = func;
    thread->arg = arg;

    if (os_task_create(jieli_thread_entry, thread, map_priority(priority),
                       (stack_size + 3u) / 4u, 0, thread->name) != 0) {
        free(thread);
        return NULL;
    }
    return (TAL_PORT_THREAD_HANDLE)thread;
}

int tal_system_port_thread_release(TAL_PORT_THREAD_HANDLE handle)
{
    JIELI_THREAD *thread = (JIELI_THREAD *)handle;
    if (thread == NULL) {
        return -1;
    }
    /* The task owns its context and frees it on normal return. */
    return os_task_del_req(thread->name);
}

TAL_PORT_THREAD_HANDLE tal_system_port_thread_current(void)
{
    return (TAL_PORT_THREAD_HANDLE)os_current_task();
}

int tal_system_port_thread_is_current(TAL_PORT_THREAD_HANDLE handle)
{
    JIELI_THREAD *thread = (JIELI_THREAD *)handle;
    const char *current = os_current_task();
    return thread != NULL && current != NULL && strcmp(thread->name, current) == 0;
}

TAL_PORT_MUTEX_HANDLE tal_system_port_mutex_create(void)
{
    JIELI_MUTEX *mutex = (JIELI_MUTEX *)calloc(1, sizeof(*mutex));
    if (mutex == NULL || os_mutex_create(&mutex->mutex) != 0) {
        free(mutex);
        return NULL;
    }
    return (TAL_PORT_MUTEX_HANDLE)mutex;
}

int tal_system_port_mutex_lock(TAL_PORT_MUTEX_HANDLE handle, uint32_t timeout_ms)
{
    JIELI_MUTEX *mutex = (JIELI_MUTEX *)handle;
    int result = mutex == NULL ? -1 : os_mutex_pend(&mutex->mutex, timeout_to_ticks(timeout_ms));
    return result == OS_TIMEOUT ? -2 : result;
}

int tal_system_port_mutex_trylock(TAL_PORT_MUTEX_HANDLE handle)
{
    JIELI_MUTEX *mutex = (JIELI_MUTEX *)handle;
    return mutex == NULL ? -1 : os_mutex_accept(&mutex->mutex);
}

int tal_system_port_mutex_unlock(TAL_PORT_MUTEX_HANDLE handle)
{
    JIELI_MUTEX *mutex = (JIELI_MUTEX *)handle;
    return mutex == NULL ? -1 : os_mutex_post(&mutex->mutex);
}

int tal_system_port_mutex_release(TAL_PORT_MUTEX_HANDLE handle)
{
    JIELI_MUTEX *mutex = (JIELI_MUTEX *)handle;
    int result;
    if (mutex == NULL) {
        return -1;
    }
    result = os_mutex_del(&mutex->mutex, OS_DEL_ALWAYS);
    free(mutex);
    return result;
}

TAL_PORT_SEM_HANDLE tal_system_port_sem_create(uint32_t initial_count)
{
    JIELI_SEM *sem = (JIELI_SEM *)calloc(1, sizeof(*sem));
    if (sem == NULL || os_sem_create(&sem->sem, (int)initial_count) != 0) {
        free(sem);
        return NULL;
    }
    return (TAL_PORT_SEM_HANDLE)sem;
}

int tal_system_port_sem_wait(TAL_PORT_SEM_HANDLE handle, uint32_t timeout_ms)
{
    JIELI_SEM *sem = (JIELI_SEM *)handle;
    int result = sem == NULL ? -1 : os_sem_pend(&sem->sem, timeout_to_ticks(timeout_ms));
    return result == OS_TIMEOUT ? -2 : result;
}

int tal_system_port_sem_post(TAL_PORT_SEM_HANDLE handle)
{
    JIELI_SEM *sem = (JIELI_SEM *)handle;
    return sem == NULL ? -1 : os_sem_post(&sem->sem);
}

int tal_system_port_sem_release(TAL_PORT_SEM_HANDLE handle)
{
    JIELI_SEM *sem = (JIELI_SEM *)handle;
    int result;
    if (sem == NULL) {
        return -1;
    }
    result = os_sem_del(&sem->sem, OS_DEL_ALWAYS);
    free(sem);
    return result;
}

TAL_PORT_QUEUE_HANDLE tal_system_port_queue_create(uint32_t message_size, uint32_t message_count)
{
    JIELI_QUEUE *queue;
    if (message_size == 0u || message_count == 0u) {
        return NULL;
    }
    queue = (JIELI_QUEUE *)calloc(1, sizeof(*queue));
    if (queue == NULL || os_q_create(&queue->queue, (QS)message_count) != 0) {
        free(queue);
        return NULL;
    }
    queue->message_size = message_size;
    return (TAL_PORT_QUEUE_HANDLE)queue;
}

int tal_system_port_queue_post(TAL_PORT_QUEUE_HANDLE handle, const void *message, uint32_t timeout_ms)
{
    JIELI_QUEUE *queue = (JIELI_QUEUE *)handle;
    void *copy;
    int result;
    if (queue == NULL || message == NULL) {
        return -1;
    }
    copy = malloc(queue->message_size);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, message, queue->message_size);
    result = os_q_post_to_back(&queue->queue, &copy,
                               timeout_ms == 0xFFFFFFFFu ? -1 : (int)timeout_ms);
    if (result != 0) {
        free(copy);
        return result;
    }
    return 0;
}

int tal_system_port_queue_fetch(TAL_PORT_QUEUE_HANDLE handle, void *message, uint32_t timeout_ms)
{
    JIELI_QUEUE *queue = (JIELI_QUEUE *)handle;
    void *copy = NULL;
    int result;
    if (queue == NULL || message == NULL) {
        return -1;
    }
    result = os_q_pend(&queue->queue, timeout_to_ticks(timeout_ms), &copy);
    if (result == OS_TIMEOUT) {
        return -2;
    }
    if (result != 0) {
        return result;
    }
    if (copy != NULL) {
        memcpy(message, copy, queue->message_size);
        free(copy);
    }
    return 0;
}

int tal_system_port_queue_release(TAL_PORT_QUEUE_HANDLE handle)
{
    JIELI_QUEUE *queue = (JIELI_QUEUE *)handle;
    int result;
    if (queue == NULL) {
        return -1;
    }
    result = os_q_del(&queue->queue, OS_DEL_ALWAYS);
    free(queue);
    return result;
}

uint32_t tal_system_port_ticks(void)
{
    return (uint32_t)os_time_get();
}

void tal_system_port_delay_ms(uint32_t milliseconds)
{
    os_time_dly((int)((milliseconds + 9u) / 10u));
}

uint32_t tal_system_port_enter_critical(void)
{
    portENTER_CRITICAL();
    return 0;
}

void tal_system_port_exit_critical(uint32_t state)
{
    (void)state;
    portEXIT_CRITICAL();
}

uint32_t tal_system_port_random(void)
{
    return (uint32_t)rand32();
}

void tal_system_port_reset(void)
{
    system_reset();
}
