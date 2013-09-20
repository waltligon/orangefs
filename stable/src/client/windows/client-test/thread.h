/* Copyright (C) 2011 Omnibond LLC
   Windows client tests -- thread declaractions */

#ifndef __THREAD_H
#define __THREAD_H

#ifdef WIN32

#define THREAD_WAIT_SIGNALED           0
#define THREAD_WAIT_TIMEOUT       0x102L
#define THREAD_WAIT_INFINITE 0xFFFFFFFFL

/*int thread_create(void *handle, void *(*start_routine)(void *), void *arg);*/

int thread_wait(uintptr_t handle, unsigned int timeout);

int thread_wait_multiple(unsigned int count, uintptr_t *handles, 
                                  int wait_all, unsigned int timeout);

int get_thread_exit_code(uintptr_t handle, unsigned int *code);

#endif

#endif