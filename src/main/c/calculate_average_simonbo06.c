#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define THREAD_COUNT 2

#define FILE_PATH "./measurements.txt"

typedef struct s_knot
{
    struct s_knot *children[256];
    short min;
    short max;
    double aggregate;
    int amount;
    unsigned char is_initialized;
    char *key;

} s_knot;

typedef struct
{
    off_t offset;
    off_t end;
    off_t size;

    char *data;
    s_knot trie;

} s_thread_input;

void print_trie(s_knot *knot)
{
    for (int i = 0; i < 256; i++)
    {
        if (knot->amount > 0)
            printf("Key;\n"); // TODO
        if (knot->children[i])
            print_trie(knot->children[i]);
    }
}

void insert_trie(s_knot *trie, unsigned char *key, int key_len, int temperature)
{
    s_knot *knot = trie;
    for (int i = 0; i < key_len; i++)
    {
        if (!(knot->children[key[i]]))
        {
            s_knot *k;
            k = malloc(sizeof(s_knot));
            knot->children[key[i]] = k;
            knot = k;
        }
    }

    if (knot->is_initialized != 0xFF)
    {
        knot->max = temperature;
        knot->min = temperature;
        knot->aggregate = temperature;
        knot->amount = 1;
        knot->is_initialized = 0xFF;
        knot->key = malloc()
    }
    if (temperature < knot->min)
        knot->min = temperature;
    if (temperature > knot->max)
        knot->max = temperature;

    knot->aggregate += temperature;
    knot->amount++;
}

void *aggregate(void *arg)
{
    s_thread_input *thread_inputs = (s_thread_input *)arg;

    while (1)
    {
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
        }

        thread_inputs->offset++;

        short temperature = (thread_inputs->data[thread_inputs->offset] - '0') * 100;
        thread_inputs->offset++;
        temperature += (thread_inputs->data[thread_inputs->offset] - '0') * 10;
        thread_inputs->offset++;
        thread_inputs->offset++;
        temperature += (thread_inputs->data[thread_inputs->offset] - '0');

        insert_trie(&(thread_inputs->trie), (unsigned char *)buffer, key_len, temperature);
    }
}

void combine_trie(s_knot *trie, s_thread_input *input)
{
    s_knot *private_trie = &(input->trie);
    // TODO
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
        inputs[i].size = size;
        inputs[i].data = data;
        pthread_create(&threads[i], NULL, aggregate, inputs + i);
    }

    for (int i = 0; i < THREAD_COUNT; i++)
        pthread_join(threads[i], NULL);

    s_knot trie;
    for (int i = 0; i < THREAD_COUNT; i++)
        combine_trie(&trie, inputs);

    munmap(data, size);
    close(fd);
    return 0;
}