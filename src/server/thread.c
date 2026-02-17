/*
 * $Id: thread.c,v 1.3 2001/11/13 17:05:13 alex Exp $
 *
 * (C) 2001 Void Technologies
 * Author: Alex Fiori <fiorix@gmail.com>
 */

#include "VTserver.h"

pthread_mutex_t mutex  = PTHREAD_MUTEX_INITIALIZER;

void thread_lock (void)
{
	pthread_mutex_lock (&mutex);
	return;
}

void thread_unlock (void)
{
	pthread_mutex_unlock (&mutex);
	return;
}
