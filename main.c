#include "stdio.h"
#include <assert.h>
#include <curl/curl.h>
#include <curl/easy.h>

#include <myencoding/myosi.h>
#include <myhtml/api.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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
        return myhtml_get_nodes_by_tag_id(tree, NULL, MyHTML_TAG_A, NULL);
    }

    // Links that we want to crawl will be inside a div with id=bodyContent
    printf("Number of nodes that match id = bodyContent : %zu\n",
           nodes->length);
    assert(nodes->length == 1);
    myhtml_tree_node_t *body = nodes->list[0];

    return myhtml_get_nodes_by_tag_id_in_scope(tree, NULL, body, MyHTML_TAG_A,
                                               NULL);
}

int main(int argc, char **argv) {
    receive_buffer buffer = {0};

    const char * url = "https://fr.wikipedia.org/wiki/Pelure_(fruit)";
    if (argc > 1) {
        url = argv[1];
    }

    curl_global_init(CURL_GLOBAL_ALL);
    myhtml_t *myhtml = myhtml_create();
    myhtml_init(myhtml, MyHTML_OPTIONS_DEFAULT, 1, 0);

    myhtml_tree_t *tree = myhtml_tree_create();
    myhtml_tree_init(tree, myhtml);

    CURL *handle = curl_easy_init();
    // curl_easy_setopt(handle, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1);

    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);

    curl_easy_setopt(
        handle, CURLOPT_URL, url
        // "https://fr.wikipedia.org/wiki/Sp%C3%A9cial:Page_au_hasard"
    );

    CURLcode success = curl_easy_perform(handle);

    myhtml_collection_t *all_link_nodes = parse_html(&buffer, tree);

    if (!all_link_nodes || !all_link_nodes->list || !all_link_nodes->length) {
        printf("Did not find any links, exiting\n");
        goto end;
    }

    printf("Done parsing, found %zu links\n", all_link_nodes->length);
    for (int i = 0; all_link_nodes && all_link_nodes->list && i < all_link_nodes->length;
         i++) {
        myhtml_tree_node_t *link_node = all_link_nodes->list[i];

        if (!link_node)
            continue;

        myhtml_tree_attr_t *href_attr =
            myhtml_attribute_by_key(link_node, "href", 4);

        if (!href_attr)
            continue;

        const char *link = myhtml_attribute_value_string(href_attr)->data;

        if (link) {
            printf("%s\n", link);
        }
    }

    // printf("%s", buffer.data);

end:
    myhtml_collection_destroy(all_link_nodes);
    myhtml_tree_destroy(tree);
    myhtml_destroy(myhtml);

    free(buffer.data);

    return success;
}
