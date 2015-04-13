//
//    Copyright (C) 1995-1999 AT&T Laboratories Cambridge
//
//    This file is part of the omnithread library
//
//    The omnithread library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU Library General Public
//    License as published by the Free Software Foundation; either
//    version 2 of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    Library General Public License for more details.
//
//    You should have received a copy of the GNU Library General Public
//    License along with this library; if not, write to the Free
//    Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
//    02111-1307, USA
//

//
// Implementation of OMNI thread abstraction for OS/2 threads
//

#include <stdlib.h>
#include <errno.h>
#include <process.h>
#include <stddef.h>
#include <stdio.h>

//using namespace std;
//#include <iostream>

#include <omnithread.h>

#define DB(x)
//{ /*DosEnterCritSec();*/ x; /*DosExitCritSec();*/ }

static void get_time_now(unsigned long* abs_sec, unsigned long* abs_nsec);

///////////////////////////////////////////////////////////////////////////
//
// Mutex
//
///////////////////////////////////////////////////////////////////////////


omni_mutex::omni_mutex(void)
{
    DosCreateMutexSem( NULL , &crit , 0 , FALSE );
}

omni_mutex::~omni_mutex(void)
{
    DosCloseMutexSem( crit );
}

void omni_mutex::lock(void)
{
    DosRequestMutexSem( crit , SEM_INDEFINITE_WAIT );
}

void omni_mutex::unlock(void)
{
    DosReleaseMutexSem( crit );
}



///////////////////////////////////////////////////////////////////////////
//
// Condition variable
//
///////////////////////////////////////////////////////////////////////////


//
// Condition variables are tricky to implement using NT synchronisation
// primitives, since none of them have the atomic "release mutex and wait to be
// signalled" which is central to the idea of a condition variable.  To get
// around this the solution is to record which threads are waiting and
// explicitly wake up those threads.
//
// Here we implement a condition variable using a list of waiting threads
// (protected by a critical section), and a per-thread semaphore (which
// actually only needs to be a binary semaphore).
//
// To wait on the cv, a thread puts itself on the list of waiting threads for
// that cv, then releases the mutex and waits on its own personal semaphore.  A
// signalling thread simply takes a thread from the head of the list and kicks
// that thread's semaphore.  Broadcast is simply implemented by kicking the
// semaphore of each waiting thread.
//
// The only other tricky part comes when a thread gets a timeout from a timed
// wait on its semaphore.  Between returning with a timeout from the wait and
// entering the critical section, a signalling thread could get in, kick the
// waiting thread's semaphore and remove it from the list.  If this happens,
// the waiting thread's semaphore is now out of step so it needs resetting, and
// the thread should indicate that it was signalled rather than that it timed
// out.
//
// It is possible that the thread calling wait or timedwait is not a
// omni_thread. In this case we have to provide a temporary data structure,
// i.e. for the duration of the call, for the thread to link itself on the
// list of waiting threads. _internal_omni_thread_dummy provides such
// a data structure and _internal_omni_thread_helper is a helper class to
// deal with this special case for wait() and timedwait(). Once created,
// the _internal_omni_thread_dummy is cached for use by the next wait() or
// timedwait() call from a non-omni_thread. This is probably worth doing
// because creating a Semaphore is quite heavy weight.

class _internal_omni_thread_helper;

class _internal_omni_thread_dummy : public omni_thread {
public:
  inline _internal_omni_thread_dummy() : next(0) { }
  inline ~_internal_omni_thread_dummy() { }
  friend class _internal_omni_thread_helper;
private:
  _internal_omni_thread_dummy* next;
};

class _internal_omni_thread_helper {
public:
  inline _internal_omni_thread_helper()  {
    d = 0;
    t = omni_thread::self();
    if (!t) {
      omni_mutex_lock sync(cachelock);
      if (cache) {
    d = cache;
    cache = cache->next;
      }
      else {
    d = new _internal_omni_thread_dummy;
      }
      t = d;
    }
  }
  inline ~_internal_omni_thread_helper() {
    if (d) {
      omni_mutex_lock sync(cachelock);
      d->next = cache;
      cache = d;
    }
  }
  inline operator omni_thread* () { return t; }
  inline omni_thread* operator->() { return t; }

  static _internal_omni_thread_dummy* cache;
  static omni_mutex                   cachelock;

private:
  _internal_omni_thread_dummy* d;
  omni_thread*                 t;
};

_internal_omni_thread_dummy* _internal_omni_thread_helper::cache = 0;
omni_mutex                   _internal_omni_thread_helper::cachelock;


omni_condition::omni_condition(omni_mutex* m) : mutex(m)
{
    DosCreateMutexSem( NULL , &crit , 0 , FALSE );
    waiting_head = waiting_tail = NULL;
}


omni_condition::~omni_condition(void)
{
    DosCloseMutexSem( crit );
    DB( if (waiting_head != NULL) {
    cerr << "omni_condition::~omni_condition: list of waiting threads "
         << "is not empty\n";
    } )
}


void omni_condition::wait(void)
{
    _internal_omni_thread_helper me;

    DosRequestMutexSem( crit , SEM_INDEFINITE_WAIT );

    me->cond_next = NULL;
    me->cond_prev = waiting_tail;
    if (waiting_head == NULL)
    waiting_head = me;
    else
    waiting_tail->cond_next = me;
    waiting_tail = me;
    me->cond_waiting = TRUE;

    DosReleaseMutexSem( crit );

    mutex->unlock();

    APIRET result =  DosWaitEventSem(me->cond_semaphore, SEM_INDEFINITE_WAIT);
    ULONG c;
    DosResetEventSem(me->cond_semaphore,&c);

    mutex->lock();

    if (result != 0) throw omni_thread_fatal(result);
}


int omni_condition::timedwait(unsigned long abs_sec, unsigned long abs_nsec)
{
    _internal_omni_thread_helper me;

    DosRequestMutexSem( crit , SEM_INDEFINITE_WAIT );

    me->cond_next = NULL;
    me->cond_prev = waiting_tail;
    if (waiting_head == NULL)
    waiting_head = me;
    else
    waiting_tail->cond_next = me;
    waiting_tail = me;
    me->cond_waiting = TRUE;

    DosReleaseMutexSem( crit );

    mutex->unlock();

    unsigned long now_sec, now_nsec;

    get_time_now(&now_sec, &now_nsec);

    ULONG timeout;
    if ((abs_sec <= now_sec) && ((abs_sec < now_sec) || (abs_nsec < now_nsec)))
      timeout = 0;
    else {
      timeout = (abs_sec-now_sec) * 1000;

      if( abs_nsec < now_nsec )  timeout -= (now_nsec-abs_nsec) / 1000000;
      else                       timeout += (abs_nsec-now_nsec) / 1000000;
    }

    APIRET result = DosWaitEventSem(me->cond_semaphore, timeout);
    ULONG c;
    DosResetEventSem(me->cond_semaphore,&c);

    if ( result == ERROR_TIMEOUT ) {
    DosRequestMutexSem( crit , SEM_INDEFINITE_WAIT );

    if (me->cond_waiting) {
        if (me->cond_prev != NULL)
        me->cond_prev->cond_next = me->cond_next;
        else
        waiting_head = me->cond_next;
        if (me->cond_next != NULL)
        me->cond_next->cond_prev = me->cond_prev;
        else
        waiting_tail = me->cond_prev;
        me->cond_waiting = FALSE;

        DosReleaseMutexSem( crit );

        mutex->lock();
        return 0;
    }

    //
    // We timed out but another thread still signalled us.  Wait for
    // the semaphore (it _must_ have been signalled) to decrement it
    // again.  Return that we were signalled, not that we timed out.
    //

    DosReleaseMutexSem( crit );

    result =  DosWaitEventSem(me->cond_semaphore, SEM_INDEFINITE_WAIT);
    ULONG c;
    DosResetEventSem(me->cond_semaphore,&c);
    }

    if (result != 0)  throw omni_thread_fatal(result);

    mutex->lock();
    return 1;
}


void omni_condition::signal(void)
{
    DosRequestMutexSem( crit , SEM_INDEFINITE_WAIT );

    if (waiting_head != NULL) {
    omni_thread* t = waiting_head;
    waiting_head = t->cond_next;
    if (waiting_head == NULL)
        waiting_tail = NULL;
    else
        waiting_head->cond_prev = NULL;
    t->cond_waiting = FALSE;

    APIRET rc;
    if ((rc=DosPostEventSem(t->cond_semaphore))!=0 )
        {
        DosReleaseMutexSem( crit );
        throw omni_thread_fatal(rc);
        }
    }

    DosReleaseMutexSem( crit );
}


void omni_condition::broadcast(void)
{
    DosRequestMutexSem( crit , SEM_INDEFINITE_WAIT );

    while (waiting_head != NULL) {
    omni_thread* t = waiting_head;
    waiting_head = t->cond_next;
    if (waiting_head == NULL)
        waiting_tail = NULL;
    else
        waiting_head->cond_prev = NULL;
    t->cond_waiting = FALSE;

    APIRET rc;
    if ((rc=DosPostEventSem(t->cond_semaphore))!=0 ) {
        DosReleaseMutexSem( crit );
        throw omni_thread_fatal(rc);
    }
    }

    DosReleaseMutexSem( crit );
}



///////////////////////////////////////////////////////////////////////////
//
// Counting semaphore
//
///////////////////////////////////////////////////////////////////////////


omni_semaphore::omni_semaphore(unsigned int initial)
{
    APIRET rc = DosCreateEventSem( NULL , &nt_sem , 0 , FALSE );

    if (rc != 0 ) {
      DB( cerr << "omni_semaphore::omni_semaphore: DosCreateEventSem error "
         << rc << endl );
      throw omni_thread_fatal(rc);
    }
}


omni_semaphore::~omni_semaphore(void)
{
   APIRET rc = DosCloseEventSem( nt_sem );
  if (rc!=0) {
    DB( cerr << "omni_semaphore::~omni_semaphore: DosCloseEventSem error "
         << rc << endl );
    throw omni_thread_fatal(rc);
  }
}


void omni_semaphore::wait(void)
{
    ULONG cnt;
    APIRET rc = DosWaitEventSem(nt_sem, SEM_INDEFINITE_WAIT);
    if (rc != 0)
    throw omni_thread_fatal(rc);
    DosResetEventSem(nt_sem,&cnt);
}


int
omni_semaphore::trywait(void)
{
    APIRET rc = DosWaitEventSem(nt_sem, SEM_IMMEDIATE_RETURN );
    switch ( rc )
    {

        case 0:
            return 1;
        case ERROR_TIMEOUT:
            return 0;
    }

    throw omni_thread_fatal(rc);
    return 0; /* keep msvc++ happy */
}


void omni_semaphore::post(void)
{
    APIRET rc = DosPostEventSem( nt_sem );
    if ( rc != 0 )
    throw omni_thread_fatal(rc);
}



///////////////////////////////////////////////////////////////////////////
//
// Thread
//
///////////////////////////////////////////////////////////////////////////


//
// Static variables
//

int omni_thread::init_t::count = 0;

omni_mutex* omni_thread::next_id_mutex;
int omni_thread::next_id = 0;
static PULONG self_tls_addr;

static unsigned int stack_size = 0;

//
// Initialisation function (gets called before any user code).
//

omni_thread::init_t::init_t(void)
{
    if (count++ != 0)   // only do it once however many objects get created.
        return;

    DB(cerr << "omni_thread::init: OS/2 implementation initialising\n");

    APIRET rc = DosAllocThreadLocalMemory( 1 , &self_tls_addr );
    if (rc != 0) throw omni_thread_fatal(rc);

    next_id_mutex = new omni_mutex;

    //
    // Create object for this (i.e. initial) thread.
    //

    omni_thread* t = new omni_thread;

    t->_state = STATE_RUNNING;
    t->handle = *_threadid;

    DB(cerr << "initial thread " << t->id() << " OS/2 thread id " << t->handle << endl);

    *self_tls_addr = (ULONG)t;

    rc = DosSetPriority( PRTYS_THREAD , os2_priority(PRIORITY_NORMAL) , 0 , t->handle );
    if ( rc != 0 )
        throw omni_thread_fatal(rc);
}

//
// Wrapper for thread creation.
//

void omni_thread_wrapper(void* ptr)
{
    omni_thread* me = (omni_thread*)ptr;

    DB(cerr << "omni_thread_wrapper: thread " << me->id() << " started\n");

    *self_tls_addr = (ULONG)me;

    //
    // Now invoke the thread function with the given argument.
    //

    if (me->fn_void != NULL) {
        (*me->fn_void)(me->thread_arg);
        omni_thread::exit();
    }

    if (me->fn_ret != NULL) {
        void* return_value = (*me->fn_ret)(me->thread_arg);
        omni_thread::exit(return_value);
    }

    if (me->detached) {
        me->run(me->thread_arg);
        omni_thread::exit();
    } else {
        void* return_value = me->run_undetached(me->thread_arg);
        omni_thread::exit(return_value);
    }
}


//
// Constructors for omni_thread - set up the thread object but don't
// start it running.
//

// construct a detached thread running a given function.

omni_thread::omni_thread(void (*fn)(void*), void* arg, priority_t pri)
{
    common_constructor(arg, pri, 1);
    fn_void = fn;
    fn_ret = NULL;
}

// construct an undetached thread running a given function.

omni_thread::omni_thread(void* (*fn)(void*), void* arg, priority_t pri)
{
    common_constructor(arg, pri, 0);
    fn_void = NULL;
    fn_ret = fn;
}

// construct a thread which will run either run() or run_undetached().

omni_thread::omni_thread(void* arg, priority_t pri)
{
    common_constructor(arg, pri, 1);
    fn_void = NULL;
    fn_ret = NULL;
}

// common part of all constructors.

void omni_thread::common_constructor(void* arg, priority_t pri, int det)
{
    _state = STATE_NEW;
    _priority = pri;

    next_id_mutex->lock();
    _id = next_id++;
    next_id_mutex->unlock();

    thread_arg = arg;
    detached = det; // may be altered in start_undetached()

    APIRET rc = DosCreateEventSem( NULL , &cond_semaphore , 0 , FALSE );

    if (rc != 0)
        throw omni_thread_fatal(rc);

    cond_next = cond_prev = NULL;
    cond_waiting = FALSE;

    handle = NULL;
}


//
// Destructor for omni_thread.
//

omni_thread::~omni_thread(void)
{
    DB(cerr << "destructor called for thread " << id() << endl);
    DosCloseEventSem(cond_semaphore);
}


//
// Start the thread
//

void omni_thread::start(void)
{
    omni_mutex_lock l(mutex);

    if (_state != STATE_NEW)
        throw omni_thread_invalid();

    handle = _beginthread(omni_thread_wrapper,NULL,stack_size,(void*)this);

    if ( handle == -1 )
        throw omni_thread_fatal(errno);

    APIRET rc = DosSetPriority( PRTYS_THREAD , os2_priority(_priority) , 0 , handle );
    if ( rc != 0 )
        throw omni_thread_fatal(rc);

    _state = STATE_RUNNING;
}


//
// Start a thread which will run the member function run_undetached().
//

void omni_thread::start_undetached(void)
{
    if ((fn_void != NULL) || (fn_ret != NULL))
        throw omni_thread_invalid();

    detached = 0;
    start();
}


//
// join - simply check error conditions & call WaitForSingleObject.
//

void omni_thread::join(void** status)
{
    mutex.lock();

    if ((_state != STATE_RUNNING) && (_state != STATE_TERMINATED)) {
        mutex.unlock();
        throw omni_thread_invalid();
    }

    mutex.unlock();

    if (this == self())
        throw omni_thread_invalid();

    if (detached)
        throw omni_thread_invalid();

    DB(cerr << "omni_thread::join: doing DosWaitThread\n");

    APIRET rc = DosWaitThread( &handle , DCWW_WAIT );
    if ( rc != 0 )
        throw omni_thread_fatal(rc);

    DB(cerr << "omni_thread::join: DosWaitThread succeeded\n");

    if (status)
      *status = return_val;

    delete this;
}


//
// Change this thread's priority.
//

void omni_thread::set_priority(priority_t pri)
{
    omni_mutex_lock l(mutex);

    if (_state != STATE_RUNNING)
        throw omni_thread_invalid();

    _priority = pri;

    APIRET rc = DosSetPriority( PRTYS_THREAD , os2_priority(pri) , 0 , handle );
    if ( rc != 0 )
        throw omni_thread_fatal(rc);
}


//
// create - construct a new thread object and start it running.  Returns thread
// object if successful, null pointer if not.
//

// detached version

omni_thread* omni_thread::create(void (*fn)(void*), void* arg, priority_t pri)
{
    omni_thread* t = new omni_thread(fn, arg, pri);
    t->start();
    return t;
}

// undetached version

omni_thread* omni_thread::create(void* (*fn)(void*), void* arg, priority_t pri)
{
    omni_thread* t = new omni_thread(fn, arg, pri);
    t->start();
    return t;
}


//
// exit() _must_ lock the mutex even in the case of a detached thread.  This is
// because a thread may run to completion before the thread that created it has
// had a chance to get out of start().  By locking the mutex we ensure that the
// creating thread must have reached the end of start() before we delete the
// thread object.  Of course, once the call to start() returns, the user can
// still incorrectly refer to the thread object, but that's their problem.
//

void omni_thread::exit(void* return_value)
{
    omni_thread* me = self();

    if (me)
    {
        me->mutex.lock();

        me->_state = STATE_TERMINATED;

        me->mutex.unlock();

        DB(cerr << "omni_thread::exit: thread " << me->id() << " detached "
            << me->detached << " return value " << return_value << endl);

        if (me->detached)
        {
            delete me;
        }
        else
        {
            me->return_val = return_value;
        }
    }
    else
    {
        DB(cerr << "omni_thread::exit: called with a non-omnithread. Exit quietly." << endl);
    }
    _endthread();
}


omni_thread* omni_thread::self(void)
{
    ULONG me = *self_tls_addr;

    if (me == NULL) {
      DB(cerr << "omni_thread::self: called with a non-ominthread. NULL is returned." << endl);
    }
    return (omni_thread*)me;
}


void omni_thread::yield(void)
{
    DosSleep(0);
}


#define MAX_SLEEP_SECONDS (ULONG)4294966    // (2**32-2)/1000

void omni_thread::sleep(unsigned long secs, unsigned long nanosecs)
{
    if (secs <= MAX_SLEEP_SECONDS) {
    DosSleep(secs * 1000 + nanosecs / 1000000);
    return;
    }

    ULONG no_of_max_sleeps = secs / MAX_SLEEP_SECONDS;

    for (ULONG i = 0; i < no_of_max_sleeps; i++)
    DosSleep(MAX_SLEEP_SECONDS * 1000);

    DosSleep((secs % MAX_SLEEP_SECONDS) * 1000 + nanosecs / 1000000);
}


void omni_thread::get_time(unsigned long* abs_sec, unsigned long* abs_nsec,
              unsigned long rel_sec, unsigned long rel_nsec)
{
    get_time_now(abs_sec, abs_nsec);
    *abs_nsec += rel_nsec;
    *abs_sec += rel_sec + *abs_nsec / 1000000000;
    *abs_nsec = *abs_nsec % 1000000000;
}


int omni_thread::os2_priority(priority_t pri)
{
    switch (pri) {

    case PRIORITY_LOW:
       return PRTYC_IDLETIME;

    case PRIORITY_NORMAL:
       return PRTYC_REGULAR;

    case PRIORITY_HIGH:
       return PRTYC_FOREGROUNDSERVER;
    }

    throw omni_thread_invalid();
    return 0; /* keep msvc++ happy */
}


static void get_time_now(unsigned long* abs_sec, unsigned long* abs_nsec)
{
    static int days_in_preceding_months[12]
    = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    static int days_in_preceding_months_leap[12]
    = { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 };

    DATETIME st;

    DosGetDateTime(&st);
    *abs_nsec = st.hundredths * 10000000;

    // this formula should work until 1st March 2100

    ULONG days = ((st.year - 1970) * 365 + (st.year - 1969) / 4
          + ((st.year % 4)
             ? days_in_preceding_months[st.month - 1]
             : days_in_preceding_months_leap[st.month - 1])
          + st.day - 1);

    *abs_sec = st.seconds + 60 * (st.minutes + 60 * (st.hours + 24 * days));
}

void omni_thread::stacksize(unsigned long sz)
{
    stack_size = sz;
}

unsigned long omni_thread::stacksize()
{
    return stack_size;
}

