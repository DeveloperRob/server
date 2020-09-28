/*****************************************************************************

Copyright (c) 2006, 2015, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2019, 2020, MariaDB Corporation.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

*****************************************************************************/

#include "ut0list.h"
#include "mem0mem.h"
#include "ut0wqueue.h"

/*******************************************************************//**
@file ut/ut0wqueue.cc
A work queue

Created 4/26/2006 Osku Salerma
************************************************************************/

/****************************************************************//**
Create a new work queue.
@return work queue */
ib_wqueue_t*
ib_wqueue_create(void)
/*===================*/
{
	ib_wqueue_t*	wq = static_cast<ib_wqueue_t*>(
		ut_malloc_nokey(sizeof(*wq)));

	mysql_mutex_init(0, &wq->mutex, nullptr);

	wq->items = ib_list_create();
	wq->event = os_event_create(0);

	return(wq);
}

/****************************************************************//**
Free a work queue. */
void
ib_wqueue_free(
/*===========*/
	ib_wqueue_t*	wq)	/*!< in: work queue */
{
	mysql_mutex_destroy(&wq->mutex);
	ib_list_free(wq->items);
	os_event_destroy(wq->event);

	ut_free(wq);
}

/** Add a work item to the queue.
@param[in,out]	wq		work queue
@param[in]	item		work item
@param[in,out]	heap		memory heap to use for allocating list node
@param[in]	wq_locked	work queue mutex locked */
void
ib_wqueue_add(ib_wqueue_t* wq, void* item, mem_heap_t* heap, bool wq_locked)
{
	if (!wq_locked) {
		mysql_mutex_lock(&wq->mutex);
	}

	ib_list_add_last(wq->items, item, heap);
	os_event_set(wq->event);

	if (!wq_locked) {
		mysql_mutex_unlock(&wq->mutex);
	}
}

/****************************************************************//**
Wait for a work item to appear in the queue.
@return work item */
void*
ib_wqueue_wait(
/*===========*/
	ib_wqueue_t*	wq)	/*!< in: work queue */
{
	ib_list_node_t*	node;

	for (;;) {
		os_event_wait(wq->event);

		mysql_mutex_lock(&wq->mutex);

		node = ib_list_get_first(wq->items);

		if (node) {
			ib_list_remove(wq->items, node);

			if (!ib_list_get_first(wq->items)) {
				/* We must reset the event when the list
				gets emptied. */
				os_event_reset(wq->event);
			}

			break;
		}

		mysql_mutex_unlock(&wq->mutex);
	}

	mysql_mutex_unlock(&wq->mutex);

	return(node->data);
}


/********************************************************************
Wait for a work item to appear in the queue for specified time. */
void*
ib_wqueue_timedwait(
/*================*/
					/* out: work item or NULL on timeout*/
	ib_wqueue_t*	wq,		/* in: work queue */
	ulint		wait_in_usecs)	/* in: wait time in micro seconds */
{
	ib_list_node_t*	node = NULL;

	for (;;) {
		ulint		error;
		int64_t		sig_count;

		mysql_mutex_lock(&wq->mutex);

		node = ib_list_get_first(wq->items);

		if (node) {
			ib_list_remove(wq->items, node);

			mysql_mutex_unlock(&wq->mutex);
			break;
		}

		sig_count = os_event_reset(wq->event);

		mysql_mutex_unlock(&wq->mutex);

		error = os_event_wait_time_low(wq->event,
					       (ulint) wait_in_usecs,
					       sig_count);

		if (error == OS_SYNC_TIME_EXCEEDED) {
			break;
		}
	}

	return(node ? node->data : NULL);
}

/********************************************************************
Return first item on work queue or NULL if queue is empty
@return work item or NULL */
void*
ib_wqueue_nowait(
/*=============*/
	ib_wqueue_t*	wq)		/*<! in: work queue */
{
	ib_list_node_t*	node = NULL;

	mysql_mutex_lock(&wq->mutex);

	if(!ib_list_is_empty(wq->items)) {
		node = ib_list_get_first(wq->items);

		if (node) {
			ib_list_remove(wq->items, node);

		}
	}

	/* We must reset the event when the list
	gets emptied. */
	if(ib_list_is_empty(wq->items)) {
		os_event_reset(wq->event);
	}

	mysql_mutex_unlock(&wq->mutex);

	return (node ? node->data : NULL);
}
/** Check if queue is empty.
@param wq wait queue
@return whether the queue is empty */
bool ib_wqueue_is_empty(ib_wqueue_t* wq)
{
	mysql_mutex_lock(&wq->mutex);
	bool is_empty = ib_list_is_empty(wq->items);
	mysql_mutex_unlock(&wq->mutex);
	return is_empty;
}

/********************************************************************
Get number of items on queue.
@return number of items on queue */
ulint
ib_wqueue_len(
/*==========*/
	ib_wqueue_t*	wq)		/*<! in: work queue */
{
	ulint len = 0;

	mysql_mutex_lock(&wq->mutex);
	len = ib_list_len(wq->items);
	mysql_mutex_unlock(&wq->mutex);

        return(len);
}
