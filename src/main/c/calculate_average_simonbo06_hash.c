#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define THREAD_COUNT 2

#define PRIME (2 ^ 17 - 1)
#define INITIAL_VALUE 5381
#define A 33

#define FILE_PATH "./measurements.txt"

typedef struct
{
    short min;
    short max;
    double aggregate;
    char *key;
} s_bucket;

typedef struct
{
    off_t offset;
    off_t end;
    char *data;
    s_bucket *hashmap;
} s_thread_input;

void *aggregate(void *arg)
{
    s_thread_input *thread_inputs = (s_thread_input *)arg;
    while (1)
    {
        int hash = INITIAL_VALUE;
        while (thread_inputs->data[thread_inputs->offset] != '\n')
        {

            thread_inputs->offset++;
            if (thread_inputs->offset >= thread_inputs->end)
                return NULL;
        }

        char buffer[100];
        memset(buffer, 0, 100);

        int key_len = 0;

        for (key_len = 0; key_len < 100; key_len++)
        {
            thread_inputs->offset++;
            if (thread_inputs->data[thread_inputs->offset] == ';')
                break;
            buffer[key_len] = thread_inputs->data[thread_inputs->offset];
            hash = ((hash * A) + thread_inputs->data[thread_inputs->offset]) % PRIME;
        }

        thread_inputs->offset++;

        short temperature = (thread_inputs->data[thread_inputs->offset] - '0') * 100;
        thread_inputs->offset++;
        temperature += (thread_inputs->data[thread_inputs->offset] - '0') * 10;
        thread_inputs->offset++;
        thread_inputs->offset++;
        temperature += (thread_inputs->data[thread_inputs->offset] - '0');
    }
}

int main()
{
    int fd = open(FILE_PATH, O_RDONLY);

    if (fd == -1)
    {
        perror(FILE_PATH);
        return 1;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1)
    {
        perror("fstat");
        close(fd);
        return 1;
    }
    off_t size = file_stat.st_size;

    char *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return 1;
    }

    off_t chunk_size = size / THREAD_COUNT;

    pthread_t threads[THREAD_COUNT];

    s_thread_input inputs[THREAD_COUNT];

    for (int i = 0; i < THREAD_COUNT; i++)
    {
        inputs[i].offset = i * chunk_size;
        inputs[i].end = i * chunk_size + chunk_size;
        inputs[i].data = data;

        pthread_create(&threads[i], NULL, aggregate, inputs + i);
    }

    for (int i = 0; i < THREAD_COUNT; i++)
        pthread_join(threads[i], NULL);

    munmap(data, size);
    close(fd);
    return 0;
}