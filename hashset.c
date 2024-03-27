#include "hashset.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t hash(const uint8_t *str) {
    uint32_t h = 0x12345678;
    for (; *str; ++str) {
        h ^= *str;
        h *= 0x5bd1e995;
        h ^= h >> 15;
    }
    return h;
}

struct bucket *new_bucket() {
    struct bucket *res = malloc(sizeof(struct bucket));
    res->elems = calloc(2, sizeof(const char *));
    res->capacity = 2;
    res->length = 0;

    return res;
}

void bucket_insert(struct bucket *bucket, const char *key) {
    if (bucket->length >= bucket->capacity) {
        bucket->capacity *= 2;
        bucket->elems =
            reallocarray(bucket->elems, bucket->capacity, sizeof(const char *));
    }

    bucket->elems[bucket->length++] = key;
}

hashset_t new_hashset() {
    hashset_t res = {0};

    return res;
}

void hashset_insert(hashset_t *hashset, const char *key) {
    uint32_t hashed = hash((const uint8_t *)key);
    uint32_t bucket_index = hashed % HASHSET_NUM_BUCKETS;

    struct bucket *bucket = hashset->buckets[bucket_index];
    if (bucket == NULL) {
        hashset->buckets[bucket_index] = bucket = new_bucket();
    }

    bucket_insert(bucket, key);
}

bool hashset_search(hashset_t *hashset, const char *key) {
    uint32_t hashed = hash((const uint8_t *)key);
    uint32_t bucket_index = hashed % HASHSET_NUM_BUCKETS;

    struct bucket *bucket = hashset->buckets[bucket_index];
    if (bucket == NULL) {
        return false;
    }

    for (size_t i = 0; i < bucket->length; i++) {
        if (strcmp(bucket->elems[i], key) == 0) {
            return true;
        }
    }

    return false;
}

uint32_t num_collisions(hashset_t *hashset) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < HASHSET_NUM_BUCKETS; i++) {
        if (hashset->buckets[i]) {
            total += hashset->buckets[i]->length - 1;
        }
    }

    return total;
}

void dump_hashset(hashset_t *hashset) {
    FILE *f = fopen("dump.txt", "w");

    for (uint32_t i = 0; i < HASHSET_NUM_BUCKETS; i++) {
        struct bucket *bucket = hashset->buckets[i];
        if (bucket) {
            fprintf(f, "%d : ", i);
            for (size_t i = 0; i < bucket->length; i++) {
                fprintf(f, "%s ", bucket->elems[i]);
            }
            fprintf(f, "\n");
        }
    }

    fclose(f);
} 

void free_all_in_bucket(struct bucket *bucket) {
    for (size_t i = 0; i < bucket->length; i++) {
        free((void *)bucket->elems[i]);
    }
}

void delete_bucket(struct bucket *bucket, bool should_free_elems) {
    if (should_free_elems) {
        free_all_in_bucket(bucket);
    }
    free(bucket->elems);
    free(bucket);
}

void delete_hashset(hashset_t *hashset, bool should_free_elems) {
    for (uint32_t i = 0; i < HASHSET_NUM_BUCKETS; i++) {
        if (hashset->buckets[i]) {
            delete_bucket(hashset->buckets[i], should_free_elems);
        }
    }
}