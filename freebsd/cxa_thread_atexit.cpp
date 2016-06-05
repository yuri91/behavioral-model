#include <stdbool.h>
#include <cstdlib>
#include <threads.h>

#include <new>
#include <atomic>

namespace {
  // One element in a singly-linked stack of cleanups.
  struct elt
  {
    void (*destructor)(void *);
    void *object;
    elt *next;
  };

  // Keep a per-thread list of cleanups in tss storage.
  tss_t key;

  // Run the specified stack of cleanups.
  void run (void *p)
  {
    elt *e = static_cast<elt*>(p);
    while (e)
    {
	    elt *old_e = e;
	    e->destructor (e->object);
	    e = e->next;
	    delete (old_e);
    }
  }

  // Run the stack of cleanups for the current thread.
  void run ()
  {
    void *e;
	  e = tss_get (key);
	  tss_set (key, NULL);
    run (e);
  }

  // Initialize the key for the cleanup stack.  We use a static local for
  // key init/delete rather than atexit so that delete is run on dlclose.
  void key_init() {
    struct key_s {
      key_s() { tss_create (&key, run); }
      ~key_s() { tss_delete (key); }
    };
    static key_s ks;
    // Also make sure the destructors are run by std::exit.
    // FIXME TLS cleanups should run before static cleanups and atexit
    // cleanups.
    std::atexit (run);
  }
}

static std::atomic_flag init_done = ATOMIC_FLAG_INIT;

extern "C" int
__cxa_thread_atexit (void (*dtor)(void *), void *obj, void */*dso_handle*/)
{
  // Do this initialization once.
  if (!init_done.test_and_set()){
    key_init();
  }

  elt *first;
  first = static_cast<elt*>(tss_get(key));

  elt *new_elt = new (std::nothrow) elt;
  if (!new_elt)
    return -1;
  new_elt->destructor = dtor;
  new_elt->object = obj;
  new_elt->next = first;

  tss_set(key, new_elt);

  return 0;
}

