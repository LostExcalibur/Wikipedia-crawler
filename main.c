#include "hashset.h"
#include "queue.h"
#include "stdio.h"
#include <assert.h>
#include <ctype.h>
#include <curl/curl.h>
#include <curl/easy.h>

#include <myencoding/myosi.h>
#include <myhtml/api.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *url_base = "https://fr.wikipedia.org/wiki/";
static const size_t url_base_len = 30;

static bool page_is_random = true;

static myhtml_t *myhtml;
static hashset_t seen;
static queue_t to_explore;

typedef struct {
    bool print_help;
    const char *start_url;
    size_t steps;
    const char *graph_file_name;
} arguments;

void print_help(char *prog_name) {
    printf("Usage : %s [OPTION]...\n", prog_name);
    printf("Available options are :\n");
    printf("\t--help\tPrint this help message and exit\n");
    printf("\t-u URL\tStart from the article at addresse URL, default is "
           "random article\n");
    printf("\t-s N\tStop after N pages, default is 1000\n");
    printf("\t-g GRAPH\t write the output graph to file GRAPH\n");
    printf("\nNOTE : format for url is simply <Article_name>\n");
}

arguments parse_arguments(int argc, char **argv) {
    arguments args = {0};

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] != '-') {
            fprintf(stderr, "Invalid option '%s'\n", arg);
            fprintf(stderr, "Try running with --help for more information\n");
            exit(EXIT_FAILURE);
        }
        if (strcmp(arg, "-u") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option '-u' requires an argument\n");
                fprintf(stderr,
                        "Try running with --help for more information\n");
                exit(EXIT_FAILURE);
            }
            args.start_url = argv[++i];
        } else if (strcmp(arg, "-s") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option '-s' requires an argument\n");
                fprintf(stderr,
                        "Try running with --help for more information\n");
                exit(EXIT_FAILURE);
            }
            args.steps = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(arg, "-g") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option '-g' requires an argument\n");
                fprintf(stderr,
                        "Try running with --help for more information\n");
                exit(EXIT_FAILURE);
            }
            args.graph_file_name = argv[++i];
        } else if (strcmp(arg, "--help") == 0) {
            args.print_help = true;
            return args; // No need to keep parsing
        }
    }

    return args;
}

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

myhtml_collection_t *parse_html(myhtml_tree_t *tree) {
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

const char *article_name(const char *link) {
    const char *start = strrchr(link, '/');
    if (!start) {
        start = link;
    } else {
        start++; // Skip the '/'
    }
    size_t length = strlen(start);

    // Remove anchors to avoid duplicating links
    char *anchor_pos = strchr(start, '#');
    if (anchor_pos) {
        length = anchor_pos - start;
    }

    char *buffer = malloc(length + 1);

    memcpy(buffer, start, length);
    buffer[length] = '\0';

    return buffer;
}

const char *full_url(const char *link) {
    size_t length = strlen(link);
    char *res = malloc(length + url_base_len + 1);
    memcpy(res, url_base, url_base_len);
    memcpy(res + url_base_len, link, length + 1);

    return res;
}

void url_decode(const char *src, char *dst) {
    unsigned char a, b;
    while (*src) {
        if (*src == '%' && (a = src[1]) && (b = src[2]) && isxdigit(a) &&
            isxdigit(b)) {
            if (a >= 'a') {
                a -= 'a' - 'A';
            }
            if (a >= 'A') {
                a -= 'A' - 10;
            } else {
                a -= '0';
            }
            if (b >= 'a') {
                b -= 'a' - 'A';
            }
            if (b >= 'A') {
                b -= 'A' - 10;
            } else {
                b -= '0';
            }
            *dst++ // printf("%u\n", a * 16 + b);
                = a * 16 + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

char *get_page_title(myhtml_tree_t *tree) {
    myhtml_collection_t *collection =
        myhtml_get_nodes_by_tag_id(tree, NULL, MyHTML_TAG_TITLE, NULL);

    if (!collection || !collection->list || !collection->length) {
        printf("No TITLE node\n");
        return NULL;
    }
    myhtml_tree_node_t *text_node = myhtml_node_child(collection->list[0]);

    if (!text_node) {
        return NULL;
    }

    const char *text = myhtml_node_text(text_node, NULL);
    myhtml_collection_destroy(collection);

    return (char *)text;
}

void filter_links(myhtml_collection_t *link_nodes, queue_t *result) {
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
            const char *canonical = article_name(link);
            enqueue(result, canonical);
        }
    }
}

void explore(const char *link, CURL *handle, FILE *graph_file) {
    receive_buffer buffer = {0};
    const char *full_link = full_url(link);
    printf("Exploring %s\n", page_is_random ? "random page" : full_link);

    // curl_easy_setopt(handle, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1);

    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);

    curl_easy_setopt(handle, CURLOPT_URL, full_link);

    CURLcode errcode = curl_easy_perform(handle);

    if (errcode) {
        printf("curl failed : %s\n", curl_easy_strerror(errcode));
        goto end1;
    }

    myhtml_tree_t *tree = myhtml_tree_create();
    myhtml_tree_init(tree, myhtml);
    myhtml_parse(tree, MyENCODING_UTF_8, buffer.data, buffer.size);

    if (page_is_random) {
        char *page_title = get_page_title(tree);

        if (!page_title) {
            fprintf(stderr, "Count not grab page title ???\n");
            exit(EXIT_FAILURE);
        }

        size_t length = strlen(page_title);
        page_title[length - 15] = '\0';
        link = page_title;

        printf("Random page explored is %s\n", link);

        page_is_random = false;
    }

    myhtml_collection_t *all_link_nodes = parse_html(tree);

    if (!all_link_nodes || !all_link_nodes->list || !all_link_nodes->length) {
        printf("Did not find any links, returning\n");
        goto end2;
    }

    // printf("Done parsing, found %zu links\n", all_link_nodes->length);

    size_t before = to_explore.length;
    queue_t new_links = new_queue();
    filter_links(all_link_nodes, &new_links);

    const char *node = dequeue(&new_links);
    while (node) {
        url_decode(node, (char *)node);
        if (strcmp(node, link))
            fprintf(graph_file, "\t\"%s\" -> \"%s\";\n", link, node);
        if (!hashset_search(&seen, node)) {
            // printf("Adding %s to explore\n", canonical);
            hashset_insert(&seen, node);
            enqueue(&to_explore, node);
        } else {
            free((void *)node);
        }

        node = dequeue(&new_links);
    }

    printf("Queued %zu more links to explore\n", to_explore.length - before);

end2:
    myhtml_collection_destroy(all_link_nodes);
    myhtml_tree_destroy(tree);
end1:
    free(buffer.data);
    free((void *)full_link);
}

int main(int argc, char **argv) {
    arguments arguments = parse_arguments(argc, argv);

    if (arguments.print_help) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    const char *start_url = "Spécial:Page_au_hasard";
    if (arguments.start_url) {
        page_is_random = strcmp(start_url, arguments.start_url) == 0;
        start_url = arguments.start_url;
    }

    printf("Starting from url %s\n", start_url);

    size_t steps = 100;
    if (arguments.steps) {
        steps = arguments.steps;
    }

    const char *graph_file_name = "out.dot";
    if (arguments.graph_file_name) {
        graph_file_name = arguments.graph_file_name;
    }

    printf("Exploring for %zu steps\n", steps);

    FILE *graph_file = fopen(graph_file_name, "w");
    fprintf(graph_file, "digraph {\n");

    myhtml = myhtml_create();
    myhtml_init(myhtml, MyHTML_OPTIONS_DEFAULT, 1, 0);

    seen = new_hashset();
    to_explore = new_queue();

    curl_global_init(CURL_GLOBAL_ALL);
    CURL *handle = curl_easy_init();

    const char *url = start_url;
    // TODO : introduce some level of parallelization here
    do {
        explore(url, handle, graph_file);
        url = dequeue(&to_explore);
        steps--;
    } while (url && steps > 0);

    fprintf(graph_file, "}");
    fclose(graph_file);

    curl_easy_cleanup(handle);
    curl_global_cleanup();

    myhtml_destroy(myhtml);

    printf("Done exploring ! We still had %zu elements to see, and had %d "
           "collisions\n",
           to_explore.length, num_collisions(&seen));

    delete_hashset(&seen, true);
    delete_queue(&to_explore, false);

    return EXIT_SUCCESS;
}
