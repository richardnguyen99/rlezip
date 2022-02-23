#ifndef _PZIP_H
#define _PZIP_H

#include <semaphore.h>

#define handle_error(msg)   \
    do                      \
    {                       \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    } while (0)

/* Global object */

// Argument object for producer thread
typedef struct _arg
{
    int argc;
    char **argv;
} arg_t;

// Page object for munmap
typedef struct _page
{
    char *addr;
    long size;
} page_t;

// Work produced by producer
typedef struct _work
{
    char *addr;
    long pagesz;
    long pagenm;
    long filenm;

    struct _work *next;
} work_t;

// Result after a work consumed by consumer
typedef struct _rle
{
    char c;
    int count;

    struct _rle *next;
} result_t;

/* Global variables */
long nprocs; // Number of processes
long nfiles; // Number of files
long pagesz; // Page size
long pagenm; // Page number #
long filenm; // File number #
static int done = 0;
static int curr_page = 0;
static int *npage_onfile;

static work_t *works, *work_head, *work_tail;
static result_t *results, *result_tail;
static sem_t mutex, filled, page;
static sem_t *order;

/* Global functions */
void *
producer(void *args);
void *consumer(void *args);
void wenqueue(work_t work);
work_t *wdequeue();
result_t *compress(work_t work);
void renqueue(result_t *result);

#endif // _PZIP_H