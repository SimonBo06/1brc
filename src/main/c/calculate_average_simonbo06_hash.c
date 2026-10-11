#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define THREAD_COUNT 8

#define TABLE_CAPACITY 32768
#define INDEX_MASK (TABLE_CAPACITY - 1) // 32767
#define INITIAL_VALUE 5381

#define FILE_PATH "./measurements.txt"

typedef struct
{
    short min;
    short max;
    uint32_t hash;
    int64_t aggregate; // Changed from double to prevent floating-point drift
    int amount;
    char *key;
    uint8_t key_len;
} s_bucket;

typedef struct
{
    off_t offset;
    off_t end;
    off_t hard_end;
    char *data;
    s_bucket *hashmap;
} s_thread_input;

// Custom comparator for non-null-terminated strings
int compare_buckets(const void *a, const void *b) {
    const s_bucket *bucket_a = *(const s_bucket **)a;
    const s_bucket *bucket_b = *(const s_bucket **)b;

    // Find the minimum length of the two keys
    int min_len = bucket_a->key_len < bucket_b->key_len ? bucket_a->key_len : bucket_b->key_len;

    // Compare the memory up to the minimum length
    int cmp = memcmp(bucket_a->key, bucket_b->key, min_len);

    // If they match up to the min length, the shorter string comes first
    if (cmp == 0) {
        return bucket_a->key_len - bucket_b->key_len;
    }

    return cmp;
}

// Perfectly mimics Java's Math.round tie-breaker rules
double round_to_java(double num) {
    return floor(num + 0.5);
}

void print_hashmap(s_bucket *hashmap) {
    s_bucket *sorted_buckets[TABLE_CAPACITY];
    int entries = 0;

    for (int i = 0; i < TABLE_CAPACITY; i++) {
        if (hashmap[i].hash != 0) {
            sorted_buckets[entries] = &hashmap[i];
            entries++;
        }
    }

    qsort(sorted_buckets, entries, sizeof(s_bucket *), compare_buckets);

    printf("{");
    for (int i = 0; i < entries; i++) {
        s_bucket *b = sorted_buckets[i];

        double mean = (double)b->aggregate / b->amount;

        printf("%.*s=%.1f/%.1f/%.1f%s",
               b->key_len, b->key,
               (double)(b->min) / 10.0,
               round_to_java(mean) / 10.0,
               (double)(b->max) / 10.0,
               (i == entries - 1) ? "" : ", ");
    }
    printf("}\n");
}

void insert_hashmap(s_bucket *hashmap, uint32_t hash, char *key, uint8_t key_len, short temperature) {
    uint32_t index = hash & INDEX_MASK;
    while (1) {
        if (hashmap[index].hash == 0) {
            hashmap[index].hash = hash;
            hashmap[index].key = key;
            hashmap[index].key_len = key_len;
            hashmap[index].aggregate = temperature;
            hashmap[index].amount = 1;
            hashmap[index].min = temperature;
            hashmap[index].max = temperature;

            return;
        }

        if (hashmap[index].hash == hash) {
            if (hashmap[index].key_len == key_len) {
                if (memcmp(key, hashmap[index].key, key_len) == 0) {
                    hashmap[index].aggregate += temperature;
                    hashmap[index].amount++;
                    if (hashmap[index].min > temperature)
                        hashmap[index].min = temperature;
                    if (hashmap[index].max < temperature)
                        hashmap[index].max = temperature;

                    return;
                }
            }
        }
        index++;
        index &= INDEX_MASK;
    }
}

void merge_hashmap(s_bucket *master, s_bucket *hashmap) {
    for (int i = 0; i < TABLE_CAPACITY; i++) {
        if (hashmap[i].hash == 0)
            continue;
        uint32_t hash = hashmap[i].hash;
        uint32_t index = hash & INDEX_MASK;
        while (1) {
            if (master[index].hash == 0) {
                master[index].hash = hash;
                master[index].key = hashmap[i].key;
                master[index].key_len = hashmap[i].key_len;
                master[index].aggregate = hashmap[i].aggregate;
                master[index].amount = hashmap[i].amount;
                master[index].min = hashmap[i].min;
                master[index].max = hashmap[i].max;

                break;
            }
            if (master[index].hash == hash) {
                if (master[index].key_len == hashmap[i].key_len) {
                    if (memcmp(master[index].key, hashmap[i].key, hashmap[i].key_len) == 0) {
                        master[index].aggregate += hashmap[i].aggregate;
                        master[index].amount += hashmap[i].amount;
                        if (master[index].min > hashmap[i].min)
                            master[index].min = hashmap[i].min;
                        if (master[index].max < hashmap[i].max)
                            master[index].max = hashmap[i].max;

                        break;
                    }
                }
            }
            index++;
            index &= INDEX_MASK;
        }
    }
}

void *aggregate(void *arg) {
    s_thread_input *thread_inputs = (s_thread_input *)arg;
    thread_inputs->hashmap = calloc(TABLE_CAPACITY, sizeof(s_bucket));

    if (thread_inputs->offset > 0) {
        if (thread_inputs->data[thread_inputs->offset - 1] != '\n') {
            while (thread_inputs->offset < thread_inputs->hard_end &&
                   thread_inputs->data[thread_inputs->offset] != '\n') {
                thread_inputs->offset++;
            }

            if (thread_inputs->offset < thread_inputs->hard_end)
                thread_inputs->offset++;
        }
    }

    while (thread_inputs->offset < thread_inputs->end) {
        char *key = &(thread_inputs->data[thread_inputs->offset]);
        uint8_t key_len = 0;

        int hash = INITIAL_VALUE;
        for (; key_len <= 100; key_len++) {
            char c = thread_inputs->data[thread_inputs->offset];
            thread_inputs->offset++;
            if (c == ';')
                break;

            hash = ((hash << 5) + hash) + c;
        }

        // Prevent hash collapse
        if (hash == 0)
            hash = 1;

        int sign = 1;
        if (thread_inputs->data[thread_inputs->offset] == '-') {
            sign = -1;
            thread_inputs->offset++;
        }

        short temperature = 0;
        while (thread_inputs->data[thread_inputs->offset] != '\n') {
            char c = thread_inputs->data[thread_inputs->offset];
            thread_inputs->offset++;

            // Only parse hard integers to ignore \r or trailing garbage
            if (c >= '0' && c <= '9') {
                temperature = temperature * 10 + (c - '0');
            }
        }
        temperature *= sign;

        insert_hashmap(thread_inputs->hashmap, hash, key, key_len, temperature);

        if (thread_inputs->offset < thread_inputs->hard_end)
            thread_inputs->offset++;
    }
    return NULL;
}

int main() {
    int fd = open(FILE_PATH, O_RDONLY);

    if (fd == -1) {
        perror(FILE_PATH);
        return 1;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {
        perror("fstat");
        close(fd);
        return 1;
    }
    off_t size = file_stat.st_size;

    char *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    off_t chunk_size = size / THREAD_COUNT;

    pthread_t threads[THREAD_COUNT];
    s_thread_input inputs[THREAD_COUNT];

    for (int i = 0; i < THREAD_COUNT; i++) {
        inputs[i].offset = i * chunk_size;
        if (i == THREAD_COUNT - 1)
            inputs[i].end = size;
        else
            inputs[i].end = i * chunk_size + chunk_size;

        inputs[i].hard_end = size;
        inputs[i].data = data;

        pthread_create(&threads[i], NULL, aggregate, inputs + i);
    }

    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
        if (i > 0)
            merge_hashmap(inputs[0].hashmap, inputs[i].hashmap);
    }

    print_hashmap(inputs[0].hashmap);

    munmap(data, size);
    close(fd);
    return 0;
}