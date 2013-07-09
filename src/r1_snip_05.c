#define EVTHREAD_WRITE  0x04
#define EVTHREAD_READ   0x08
#define EVTHREAD_TRY    0x10

#define EVTHREAD_LOCKTYPE_RECURSIVE 1
#define EVTHREAD_LOCKTYPE_READWRITE 2

#define EVTHREAD_LOCK_API_VERSION 1

struct evthread_lock_callbacks {
    int lock_api_version;
    unsigned supported_locktypes;
    void *(*alloc)(unsigned locktype);
    void (*free)(void *lock, unsigned locktype);
    int (*lock)(unsigned mode, void *lock);
    int (*unlock)(unsigned mode, void *lock);
};

int evthread_set_lock_callbacks(const struct evthread_lock_callbacks *);

void evthread_set_id_callback(unsigned long (*id_fn)(void));

struct evthread_condition_callbacks {
    int condition_api_version;
    void *(*alloc_condition)(unsigned condtype);
    void (*free_condition)(void *cond);
    void (*signal_condition)(void *cond, int broadcast);
    void (*wait_condition)(void *cond, void *lock, const struct timeval *timeout);
};

int evthread_set_condition_callbacks(const struct evthread_condition_callbacks *);
