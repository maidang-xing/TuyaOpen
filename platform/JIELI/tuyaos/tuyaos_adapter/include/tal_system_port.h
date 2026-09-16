#ifndef TAL_PORT_SYSTEM_H
#define TAL_PORT_SYSTEM_H

#include <stdint.h>

typedef void (*TAL_PORT_THREAD_FUNC)(void *arg);
typedef void *TAL_PORT_THREAD_HANDLE;
typedef void *TAL_PORT_MUTEX_HANDLE;
typedef void *TAL_PORT_SEM_HANDLE;
typedef void *TAL_PORT_QUEUE_HANDLE;

TAL_PORT_THREAD_HANDLE tal_system_port_thread_create(const char *name, uint32_t stack_size,
                                                uint32_t priority, TAL_PORT_THREAD_FUNC func,
                                                void *arg);
int tal_system_port_thread_release(TAL_PORT_THREAD_HANDLE handle);
TAL_PORT_THREAD_HANDLE tal_system_port_thread_current(void);
int tal_system_port_thread_is_current(TAL_PORT_THREAD_HANDLE handle);

TAL_PORT_MUTEX_HANDLE tal_system_port_mutex_create(void);
int tal_system_port_mutex_lock(TAL_PORT_MUTEX_HANDLE handle, uint32_t timeout_ms);
int tal_system_port_mutex_trylock(TAL_PORT_MUTEX_HANDLE handle);
int tal_system_port_mutex_unlock(TAL_PORT_MUTEX_HANDLE handle);
int tal_system_port_mutex_release(TAL_PORT_MUTEX_HANDLE handle);

TAL_PORT_SEM_HANDLE tal_system_port_sem_create(uint32_t initial_count);
int tal_system_port_sem_wait(TAL_PORT_SEM_HANDLE handle, uint32_t timeout_ms);
int tal_system_port_sem_post(TAL_PORT_SEM_HANDLE handle);
int tal_system_port_sem_release(TAL_PORT_SEM_HANDLE handle);

TAL_PORT_QUEUE_HANDLE tal_system_port_queue_create(uint32_t message_size, uint32_t message_count);
int tal_system_port_queue_post(TAL_PORT_QUEUE_HANDLE handle, const void *message, uint32_t timeout_ms);
int tal_system_port_queue_fetch(TAL_PORT_QUEUE_HANDLE handle, void *message, uint32_t timeout_ms);
int tal_system_port_queue_release(TAL_PORT_QUEUE_HANDLE handle);

uint32_t tal_system_port_ticks(void);
void tal_system_port_delay_ms(uint32_t milliseconds);
uint32_t tal_system_port_enter_critical(void);
void tal_system_port_exit_critical(uint32_t state);
uint32_t tal_system_port_random(void);
void tal_system_port_reset(void);

#endif
