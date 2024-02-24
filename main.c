#include "hashset.h"
#include "queue.h"
#include "stdio.h"
#include <assert.h>
#include <curl/curl.h>
#include <curl/easy.h>

#include <myencoding/myosi.h>
#include <myhtml/api.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *url_base = "https://fr.wikipedia.org";
static const size_t url_base_len = 24;

myhtml_t *myhtml;
hashset_t explored;
queue_t to_explore;

bool startswith(const char *str, const char *prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

typedef struct {
    char *data;
    size_t size;
} receive_buffer;

size_t write_data(void *buffer, size_t size, size_t nmemb, void *userp) {
    receive_buffer *recv_buffer = (receive_buffer *)userp;
    size_t real_size = size * nmemb;

    char *ptr = realloc(recv_buffer->data, recv_buffer->size + real_size + 1);
    if (!ptr)
        return 0;

    recv_buffer->data = ptr;
    memcpy(&recv_buffer->data[recv_buffer->size], buffer, real_size);
    recv_buffer->size += real_size;
    recv_buffer->data[recv_buffer->size] = 0;

    return real_size;
}

myhtml_collection_t *parse_html(receive_buffer *buffer, myhtml_tree_t *tree) {
    myhtml_parse(tree, MyENCODING_UTF_8, buffer->data, buffer->size);

    myhtml_collection_t *nodes = myhtml_get_nodes_by_attribute_value(
        tree, NULL, NULL, false, "id", 2, "bodyContent", strlen("bodyContent"),
        NULL);
    if (!nodes || !nodes->length || !nodes->list) {
        myhtml_collection_destroy(nodes);
        return myhtml_get_nodes_by_tag_id(tree, NULL, MyHTML_TAG_A, NULL);
    }

    // Links that we want to crawl will be inside a div with id=bodyContent
    // printf("Number of nodes that match id = bodyContent : %zu\n",
    //        nodes->length);
    assert(nodes->length == 1);
    myhtml_tree_node_t *body = nodes->list[0];

    myhtml_collection_t *res = myhtml_get_nodes_by_tag_id_in_scope(
        tree, NULL, body, MyHTML_TAG_A, NULL);
    myhtml_collection_destroy(nodes);
    return res;
}

bool should_explore(const char *link) {
    if (startswith(link, "#"))
        return false;
    if (startswith(link, "/w/"))
        return false;

    if (startswith(link, "/wiki/")) {
        if (strchr(link, ':')) {
            return false;
        }
        return true;
    }
    return false;
}

const char *canonize(const char *link) {
    size_t length = strlen(link);
    char *buffer = malloc(url_base_len + length + 1);

    memcpy(buffer, url_base, url_base_len);

    // Remove anchors to avoid duplicating links
    char *anchor_pos = strchr(link, '#');
    if (anchor_pos) {
        length = anchor_pos - link;
    }

    memcpy(buffer + url_base_len, link, length);
    buffer[url_base_len + length] = '\0';

    return buffer;
}

void filter_and_queue_links(myhtml_collection_t *link_nodes) {
    for (int i = 0; link_nodes && link_nodes->list && i < link_nodes->length;
         i++) {
        myhtml_tree_node_t *link_node = link_nodes->list[i];

        if (!link_node)
            continue;

        myhtml_tree_attr_t *href_attr =
            myhtml_attribute_by_key(link_node, "href", 4);

        if (!href_attr)
            continue;

        const char *link = myhtml_attribute_value_string(href_attr)->data;

        if (!link) {
            continue;
        }

        if (should_explore(link)) {
            const char *canonical = canonize(link);
            if (hashset_search(&explored, canonical)) {
                // printf("Not adding %s, already seen\n", canonical);
                free((void *)canonical);
            } else {
                // printf("Adding %s to explore\n", canonical);
                enqueue(&to_explore, canonical);
            }
        }
    }
}

// TODO : actually do something when exploring a page, like saving to disk or
// constructing a graph
void explore(const char *link, CURL *handle) {
    receive_buffer buffer = {0};
    printf("Exploring %s\n", link);

    // curl_easy_setopt(handle, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1);

    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);

    curl_easy_setopt(
        handle, CURLOPT_URL, link
        // "https://fr.wikipedia.org/wiki/Sp%C3%A9cial:Page_au_hasard"
    );

    CURLcode errcode = curl_easy_perform(handle);

    if (errcode) {
        printf("curl failed : %s\n", curl_easy_strerror(errcode));
        goto end1;
    }

    hashset_insert(&explored, link);

    myhtml_tree_t *tree = myhtml_tree_create();
    myhtml_tree_init(tree, myhtml);

    myhtml_collection_t *all_link_nodes = parse_html(&buffer, tree);

    if (!all_link_nodes || !all_link_nodes->list || !all_link_nodes->length) {
        printf("Did not find any links, returning\n");
        goto end2;
    }

    // printf("Done parsing, found %zu links\n", all_link_nodes->length);

    size_t before = to_explore.length;
    filter_and_queue_links(all_link_nodes);
    printf("Need to explore %zu more links\n", to_explore.length - before);

end2:
    myhtml_collection_destroy(all_link_nodes);
    myhtml_tree_destroy(tree);
end1:
    free(buffer.data);
}

int main(int argc, char **argv) {
    const char *article = "/wiki/Peel_(fruit)";

    if (argc > 1) {
        article = argv[1];
    }
    const char *start_url = canonize(article);

    myhtml = myhtml_create();
    myhtml_init(myhtml, MyHTML_OPTIONS_DEFAULT, 1, 0);

    explored = new_hashset();
    to_explore = new_queue();

    curl_global_init(CURL_GLOBAL_ALL);
    CURL *handle = curl_easy_init();

    const char *url = start_url;
    size_t steps = 100;
    // TODO : introduce some level of parallelization here
    do {
        printf("HUH\n");
        explore(url, handle);
        url = dequeue(&to_explore);
        steps--;
    } while (url && steps > 0);

    curl_easy_cleanup(handle);
    curl_global_cleanup();

    myhtml_destroy(myhtml);

    printf("Done exploring ! We still had %zu elements to see, and had %d "
           "collisions\n",
           to_explore.length, num_collisions(&explored));

    delete_hashset(&explored, true);
    delete_queue(&to_explore, true);

    free((void *)url);

    return EXIT_SUCCESS;
}
