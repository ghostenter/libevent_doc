#include <event2/event.h>
#include <sys/types.h>
#include <stdlib.h>

union alignment {
    size_t sz;
    void *ptr;
    double dbl;
};

#define ALIGNMENT sizeof(union alignment)

#define OUTPTR(ptr) (((char *) ptr) + ALIGNMENT)
#define INPTR(ptr) (((char *) ptr) - ALIGNMENT)

static size_t total_allocated = 0;
static void *replacement_malloc(size_t sz)
{
    void *chunk = malloc(sz + ALIGNMENT);
    if(!chunk){
        return chunk;
    }

    total_allocated += sz;
    *(size_t *)chunk = sz;
    return OUTPTR(chunk);
}

static void *replacement_realloc(void *ptr, size_t sz)
{
    size_t old_size = 0;
    if(ptr) {
        ptr = INPTR(ptr);
        old_size = *(size_t *)ptr;
    }

    ptr = realloc(ptr, sz + ALIGNMENT);
    if(!ptr){
        return NULL;
    }

    *(size_t *)ptr = sz;
    total_allocated = total_allocated - old_size + sz;
    return OUTPTR(ptr);
}

static void *replacement_free(void *ptr)
{
    ptr = INPTR(ptr);
    total_allocated -=*(size_t *) ptr;
    free(ptr);
}
