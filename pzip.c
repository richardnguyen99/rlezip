
/**
 * @file pzip.c
 * @author Richard Nguyen (richard.ng0616@gmail.com)
 * @brief Parallel zipping with Run-length encoding and multithreading
 * @version 0.1
 * @date 2022-02-22
 */
#include <stdio.h>       // stderr, stdout, perror, fprintf, fwrite
#include <stdlib.h>      // exit, EXIT_FAILURE, EXIT_SUCCESS, malloc
#include <pthread.h>     // pthread_t, pthread_create, phthread_exit, pthread_join
#include <fcntl.h>       // sysconf, open, O_*
#include <unistd.h>      // _SC_*
#include <string.h>      // memset
#include <semaphore.h>   // sem_t, sem_init, sem_wait, sem_post
#include <sys/sysinfo.h> // get_nprocs
#include <sys/stat.h>    // stat, fstat
#include <sys/mman.h>    // mmap

#include "pzip.h"

/**
 * @brief Main program for pzip
 *
 * {pzip} will take one or multiple files, and compress
 * them using Run-length encoding.
 *
 * It is based on the producer-consumer problem. Basically,
 * the program has one producer thread and many consumer
 * threads.
 *
 * The producer loads files into memory for better performance.
 * It then splits each file into equal-size pages to ensure
 * each consumer thread has a same amout of work.
 *
 * The consumers take one work, or page, to do the compression
 * with RLE. Each listens to the previous thread to finish
 * before push the result back to stdout.
 */
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "pzip: file1 [file2 ...]\n");
        exit(EXIT_FAILURE);
    }

    arg_t *args = malloc(sizeof(arg_t));

    if (args == NULL)
        handle_error("Malloc args");

    args->argc = argc - 1;
    args->argv = argv + 1; // Remove the first argument

    nprocs = get_nprocs();           // Let the system decide how many thread will be used
    nfiles = argc - 1;               // Remove the first argument
    pagesz = sysconf(_SC_PAGE_SIZE); // Let the system decide how big a page is

    order = malloc(sizeof(sem_t) * nprocs);
    if (order == NULL)
        handle_error("malloc order");

    npage_onfile = malloc(sizeof(int) * nfiles);
    if (npage_onfile == NULL)
        handle_error("malloc npage_onfile");

    memset(npage_onfile, 0, sizeof(int) * nfiles);
    // so that producer can start first
    sem_init(&mutex, 0, 1);
    // So that if consumers start before the producer, we make them wait
    sem_init(&filled, 0, 0);
    // So that if only the result of the first work will start first.
    sem_init(&page, 0, 0);

    pthread_t pid, cid[nprocs];

    pthread_create(&pid, NULL, producer, (void *)args);

    for (int i = 0; i < nprocs; i++)
    {
        pthread_create(&cid[i], NULL, consumer, (void *)args);
        sem_init(&order[i], 0, i ? 0 : 1);
    }

    for (int i = 0; i < nprocs; i++)
    {
        pthread_join(cid[i], NULL);
    }

    pthread_join(pid, NULL);

    // Simply write all the results into stdout
    // Use Linux redirection to write into file
    for (result_t *curr = results; curr != NULL; curr = curr->next)
    {
        fwrite((char *)&(curr->count), sizeof(int), 1, stdout);
        fwrite((char *)&(curr->c), sizeof(char), 1, stdout);
    }

    sem_destroy(&filled);
    sem_destroy(&mutex);
    sem_destroy(&page);
    for (int i = 0; i < nprocs; i++)
    {
        sem_destroy(&order[i]);
    }

    for (result_t *curr = results; curr != NULL; curr = results)
    {
        results = results->next;
        free(curr);
        curr = NULL;
    }

    for (work_t *curr = works; curr != NULL; curr = works)
    {
        munmap(curr->addr, curr->pagesz);

        works = works->next;
        free(curr);
        curr = NULL;
    }

    free(order);
    free(npage_onfile);

    return 0;
}

/**
 * @brief Put a new work to new work buffer
 *
 * @param work New work
 */
void wenqueue(work_t work)
{
    if (works == NULL)
    {
        works = malloc(sizeof(work_t));

        if (works == NULL)
        {
            handle_error("malloc work");
            sem_post(&mutex);
        }

        works->addr = work.addr;
        works->filenm = work.filenm;
        works->pagenm = work.pagenm;
        works->pagesz = work.pagesz;

        works->next = NULL;
        work_head = works;
        work_tail = works;
    }
    else
    {
        work_tail->next = malloc(sizeof(work_t));

        if (work_tail->next == NULL)
        {
            handle_error("malloc work");
            sem_post(&mutex);
        }

        work_tail->next->addr = work.addr;
        work_tail->next->filenm = work.filenm;
        work_tail->next->pagenm = work.pagenm;
        work_tail->next->pagesz = work.pagesz;

        work_tail->next->next = NULL;
        work_tail = work_tail->next;
    }
}

/**
 * @brief Function pointer for producer thread
 *
 * @param args Argument type for files and how many
 */
void *producer(void *args)
{
    arg_t *arg = (arg_t *)args;
    char **fnames = arg->argv;

    for (int i = 0; i < arg->argc; i++)
    {
        int fd = open(fnames[i], O_RDONLY);

        if (fd == -1)
            handle_error("open error");

        struct stat sb;

        if (fstat(fd, &sb) == -1)
            handle_error("fstat error");

        if (sb.st_size == 0)
            continue;

        // Page for file
        // Divide each file into equal-size pages
        int p4f = sb.st_size / pagesz;

        // Ensure that we get all the size.
        if ((double)sb.st_size / pagesz > p4f)
            p4f++;

        // Simply load the content of one file into memory
        // In a nutshell, the content can be accessed as
        // an array of character in memory
        char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

        // Offset helps with moving around the file in memory.
        // It acts like an index in array
        int offset = 0;
        npage_onfile[i] = p4f;

        // Page-splitting process starts
        for (int j = 0; j < p4f; j++)
        {
            // it should be less than or equal to the default page size
            // so that each consumer will likely have a same amout of work
            int curr_pagesz = (j < p4f - 1) ? pagesz : sb.st_size - ((p4f - 1) * pagesz);
            offset += curr_pagesz;

            work_t work;
            work.addr = addr;
            work.filenm = i;
            work.pagenm = j;
            work.pagesz = curr_pagesz;
            work.next = NULL;

            // Critical section in the work buffer
            sem_wait(&mutex);
            wenqueue(work);
            sem_post(&mutex);
            sem_post(&filled); // Signal the consumer threads that there is work to do

            addr += curr_pagesz;
        }

        close(fd);
    }

    // Flag the producer is done
    done = 1;

    // Wake up all sleeping consumers
    for (int i = 0; i < nprocs; i++)
    {
        sem_post(&filled);
    }

    pthread_exit(NULL);
}

/**
 * @brief Remove one work out of the work queue
 */
work_t *wdequeue()
{
    if (work_head == NULL)
        return NULL;

    work_t *tmp = work_head;
    work_head = work_head->next;

    return tmp;
}

/**
 * @brief Compress algorithm using Run-length coding
 *
 * @param work A buffer associated in a work
 */
result_t *compress(work_t work)
{
    result_t *result = malloc(sizeof(result_t));
    if (result == NULL)
        handle_error("malloc result");
    result_t *tmp = result;

    int count = 0;
    char c, last;

    for (int i = 0; i < work.pagesz; i++)
    {
        c = work.addr[i];
        if (count && last != c)
        {
            tmp->c = last;
            tmp->count = count;

            if ((tmp->next = malloc(sizeof(result_t))) == NULL)
                handle_error("Malloc result");

            tmp = tmp->next;
            count = 0;
        }

        last = c;
        count++;
    }

    if (count)
    {
        tmp->c = last;
        tmp->count = count;
        tmp->next = NULL;
    }

    return result;
}

/**
 * @brief Put a new RLE result to the result queue
 */
void renqueue(result_t *result)
{
    if (results == NULL)
    {
        results = result;
    }
    else
    {
        // There will be cases that splitting process
        // will split a continuous sequence of a character
        // So make sure to add them back.
        if (result_tail->c == result->c)
        {
            result_tail->count += result->count;
            result = result->next;
        }

        result_tail->next = result;
    }

    result_t *curr = result;
    for (; curr->next != NULL; curr = curr->next)
    {
    }

    result_tail = curr;
}

/**
 * @brief Function pointer for producer thread
 *
 * A little bit headache here...
 *
 * Each consumer thread will take one work out of the
 * work buffer and do the compression.
 *
 * First, it will wait for the producer to create at
 * least one work. If there are works, it then checks
 * for mutual exclusion so that only either one consumer
 * or one producer can access the work buffer.
 *
 * Then, it will be put into the result buffer and wait
 * for the previous work to be done.
 */
void *consumer(void *args)
{
    work_t *work;
    while (!done || work_head != NULL)
    {
        sem_wait(&filled);
        sem_wait(&mutex);
        // Final check because it may pass
        // the semaphore filled
        // Prevent moving work_head otherwise
        // the work buffer will be corrupted.
        if (work_head == work_tail && !done)
        {
            sem_post(&mutex);
            continue;
        }
        // No need to work when there is no work
        else if (work_head == NULL)
        {
            sem_post(&mutex);
            return NULL;
        }
        else
        {
            work = work_head;
            work_head = work_head->next;
            sem_post(&mutex);
        }

        result_t *result = compress(*work);

        // Very first work.
        if (work->filenm == 0 && work->pagenm == 0)
        {
            sem_wait(&order[0]);
            renqueue(result);

            if (work->pagenm == npage_onfile[work->filenm] - 1)
            {
                sem_post(&order[0]);
                curr_page++;
            }
            else
                sem_post(&order[1]);

            sem_post(&page);
        }
        else
        {
            while (1)
            {
                sem_wait(&page);
                // Tell the consumer to wait for
                // the correct file number to arrive
                if (curr_page != work->filenm)
                {
                    sem_post(&page);
                    continue;
                }
                if (curr_page == nfiles)
                {
                    sem_post(&page);
                    return NULL;
                }
                sem_post(&page);

                sem_wait(&order[work->pagenm % nprocs]); // Wait for the previous work
                sem_wait(&page);
                renqueue(result);
                if (work->filenm == curr_page && work->pagenm < npage_onfile[work->filenm] - 1)
                {
                    sem_post(&order[(work->pagenm + 1) % nprocs]);
                }
                else if (work->filenm == curr_page && work->pagenm == npage_onfile[work->filenm] - 1)
                {
                    sem_post(&order[0]); // Reset the ordering and increase the current file number
                    curr_page++;
                }
                sem_post(&page);
                break;
            }
        }
    }

    return NULL;
}
