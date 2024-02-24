#ifndef __HASHSET_H
#define __HASHSET_H

#include <stdbool.h>
#include <stddef.h>

#define HASHSET_NUM_BUCKETS 10000

struct bucket {
    const char **elems;
    size_t capacity;
    size_t length;
};

typedef struct {
    struct bucket *buckets[HASHSET_NUM_BUCKETS];
} hashset_t;

hashset_t new_hashset();
void hashset_insert(hashset_t *hashset, const char *key);
bool hashset_search(hashset_t *hashset, const char *key);
void delete_hashset(hashset_t *hashset);

#endif // __HASHSET_H