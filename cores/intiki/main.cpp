/*
  Copyright (c) 2014-2018 Tokita, Hiroshi  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

extern "C" {
#include <contiki.h>
#include <core/sys/process.h>
#include <core/sys/mt.h>
#include <core/sys/autostart.h>
#include <core/lib/list.h>
}


#define ARDUINO_MAIN
#include "Arduino.h"

// Weak empty variant initialization function.
// May be redefined by variant files.
void initVariant() __attribute__((weak));
void initVariant() { }

#include "wiring_private.h"

process_event_t waketimer_event;

static struct main_thread_wait_t
{
	enum main_thread_wait_type type;
	void* run_param;
	void* condition_param;
	fp_run run;
	fp_condition condition;
} main_thread_wait;

LIST(start_processes);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
PROCESS(main_thread, "main_thread");
#pragma GCC diagnostic pop

static void main_cpp_constructor() __attribute__((constructor(101)));
static void main_cpp_constructor()
{
	static start_process_t main_l;
	main_l.process = &main_thread;
	add_start_process(&main_l);
}

static void start_process_list()
{
	start_process_t* p;
	for( p = (start_process_t*)list_head(start_processes);
	     p != NULL;
	     p = (start_process_t*)list_item_next(p) )
	{
		process_start(p->process, NULL);
	}
}

static void exit_process_list()
{
	start_process_t* p;
	for( p = (start_process_t*)list_head(start_processes);
	     p != NULL;
	     p = (start_process_t*)list_item_next(p) )
	{
		process_exit(p->process);
	}
}

struct process * const autostart_processes[] = {NULL};
void
autostart_start(struct process * const processes[])
{
  (void)processes;
  start_process_list();
}
/*---------------------------------------------------------------------------*/
void
autostart_exit(struct process * const processes[])
{
  (void)processes;
  exit_process_list();
}

void add_start_process(start_process_t* p)
{
	list_add(start_processes, p);
}

int initialized = 0;

static void arduino_loop(void* none)
{
	(void)none;

	for (;;)
	{
		if(!initialized) {
			setup();
			initialized = 1;
		}

		loop();
		if (serialEventRun) serialEventRun();
		yield();
	}
	mt_exit();
}

PROCESS_THREAD(main_thread, ev, data)
{
	static struct mt_thread arduino_thread;

	waketimer_event = process_alloc_event();

	PROCESS_BEGIN();

	initVariant();

	mt_init();
	mt_start(&arduino_thread, arduino_loop, NULL);

	PROCESS_PAUSE();

	while(1)
	{
		mt_exec(&arduino_thread);

		if(main_thread_wait.run)
		{
			main_thread_wait.run(main_thread_wait.run_param);
		}

		if(main_thread_wait.type == WT_WAIT_UNTIL && main_thread_wait.condition)
		{
			PROCESS_WAIT_UNTIL( main_thread_wait.condition(ev, data, main_thread_wait.condition_param) );
		}
		else if(main_thread_wait.type == WT_YIELD_UNTIL && main_thread_wait.condition)
		{
			PROCESS_YIELD_UNTIL( main_thread_wait.condition(ev, data, main_thread_wait.condition_param) );
		}
		else if(main_thread_wait.type == WT_YIELD)
		{
			PROCESS_YIELD();
		}
		else if(main_thread_wait.type == WT_PAUSE)
		{
			PROCESS_PAUSE();
		}
	}

	mt_stop(&arduino_thread);
	mt_remove();

	PROCESS_END();
}

#define YIELD_TO_MAIN_THREAD(type_, run_, run_param_, condition_, condition_param_) \
{ \
	main_thread_wait.type = type_; \
	main_thread_wait.run = run_; \
	main_thread_wait.run_param = run_param_; \
	main_thread_wait.condition = condition_; \
	main_thread_wait.condition_param = condition_param_; \
	mt_yield(); \
}

void yield()
{
	delay(0);
}

void yield_until(fp_run run, void* run_param, fp_condition condition, void* condition_param)
{
	YIELD_TO_MAIN_THREAD(WT_YIELD_UNTIL, run, run_param, condition, condition_param);
}

void yield_continue(fp_run run, void* run_param)
{
	YIELD_TO_MAIN_THREAD(WT_PAUSE, run, run_param, NULL, NULL);
}

void main_thread_post(process_event_t event, void* param)
{
	process_post(&main_thread, event, param);
}

void post_continue()
{
	main_thread_post(PROCESS_EVENT_CONTINUE, NULL);
}
 
