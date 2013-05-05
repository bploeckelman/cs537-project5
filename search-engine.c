#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "index.h"

#define DEBUG

typedef struct tag_args {
    int num_indexer_threads;
    const char *file_list_name;
} Args;
Args args;

typedef struct tag_info {
    FILE *file_list;
    // TODO : put bounded buffer in here?
} Info;
Info info;


void parseArgs(int argc, char *argv[]);
void startScanner();
void startIndexer();
void startSearch();
void cleanup();


// ----------------------------------------------------------------------------
// Entry point ----------------------------------------------------------------
// ----------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    parseArgs(argc, argv);
    startScanner();
    startIndexer();
    startSearch();
    cleanup();
    return 0;
}


// ----------------------------------------------------------------------------
void usage() {
    fprintf(stderr, "Usage: search-index <num-indexer-threads> <file-list>\n");
    exit(1);
}

// ----------------------------------------------------------------------------
void parseArgs(int argc, char *argv[]) {
    if (argc != 3) {
        usage();
    }

    memset(&args, 0, sizeof(Args));
    // TODO : use strtol instead of atoi
    args.num_indexer_threads = atoi(argv[1]);
    args.file_list_name = argv[2];

    if (args.num_indexer_threads < 1) {
        fprintf(stderr, "Number of indexer threads must be > 0.\n");
        exit(1);
    }

    memset(&info, 0, sizeof(Info));
    info.file_list = fopen(args.file_list_name, "r");
    if (info.file_list == NULL) {
        char buf[MAXPATH];
        memset(buf, 0, sizeof(buf));
        sprintf(buf, "fopen('%s')", args.file_list_name);
        perror(buf);
        exit(1);
    }

#ifdef DEBUG
    printf("Args: num indexer threads = %d\n", args.num_indexer_threads);
    printf("Args: file list name = '%s'\n", args.file_list_name);
#endif
}

// ----------------------------------------------------------------------------
void* scannerWorker(void *data) {
    // TODO : scan files from file list
    return NULL;
}

void startScanner() {
    // TODO : start scanner thread
}

// ----------------------------------------------------------------------------
void* indexerWorker(void *data) {
    // TODO : read files from list produced by scanner, add words to hash table
/*
    FILE *file = fopen(filename, "r");
    while (!feof(file)) {
        int line_number = 0;
        char *saveptr;
        fgets(buffer, buffer_len, file);
        char *word = strtok_r(buffer, " \n\t-_!@#$%^&*()_+=,./<>?", &saveptr);
        while (word != NULL) {
            insert_into_index(word, file_name, line_number);
            word = strtok_r(buffer, " \n\t-_!@#$%^&*()_+=,./<>?", &saveptr);
        }
        ++line_number;
    }
    fclose(file);
*/
    return NULL;
}

void startIndexer() {
    // TODO : start indexer thread
}

// ----------------------------------------------------------------------------
void startSearch() {
    // TODO : get search terms and check them against hash table
}

// ----------------------------------------------------------------------------
void cleanup() {
    // TODO : cleanup anything else that needs to be cleaned up
    fclose(info.file_list);
}

