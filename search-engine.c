#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <semaphore.h>

#include "index.h"

#define DEBUG
// ADDED 
typedef struct bounded_buffer_s {
	char ** buffer;
	int fill;
	int use;
	int count;
	int done;
	int size; 
} Bounded_Buffer; 

// ADDED
typedef Bounded_Buffer * bounded_buffer_t; 
// ADDED
typedef struct mutex_cond {
	pthread_mutex_t bb_mutex;
	pthread_cond_t empty;
	pthread_cond_t full; 
} Mutex_Cond; 
Mutex_Cond mutex_cond; 

typedef struct tag_args {
    int num_indexer_threads;
    const char *file_list_name;
} Args;
Args args;

typedef struct tag_info {
    FILE *file_list;
    // TODO : put bounded buffer in here?
	// ADDED: Bounded_Buffer pointer 
	bounded_buffer_t bbp; 
} Info;
Info info;


void parseArgs(int argc, char *argv[]);
void startScanner(Info info);
// ADDED 
void initBoundedBuffer();
void initMutexStruct();
void startIndexer();
void startSearch();
void cleanup();


// ----------------------------------------------------------------------------
// Entry point ----------------------------------------------------------------
// ----------------------------------------------------------------------------
int main(int argc, char *argv[]) {
	// ADDED 
    parseArgs(argc, argv);

	// ADDED
	initBoundedBuffer();
	initMutexStruct(); 
    startScanner(info);
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

//-----------------------------------------------------------------------------
void initBoundedBuffer() {
	char ** buffer;
	int i;

	buffer = malloc(sizeof(char *) * args.num_indexer_threads);
	
	for(i = 0; i < args.num_indexer_threads; i += 1) {
		buffer[i] = malloc(sizeof(char) * MAXPATH);
		memset(buffer[i], 0, MAXPATH);
	} 

	Bounded_Buffer bb = {buffer, 0, 0, 0, 0, args.num_indexer_threads};
	info.bbp = &bb;
}

//-----------------------------------------------------------------------------
void initMutexStruct() {
	int rc; 
	memset(&mutex_cond, 0, sizeof(Mutex_Cond));
	rc = pthread_mutex_init(&mutex_cond.bb_mutex, NULL);
	assert(rc == 0);
	rc = pthread_cond_init(&mutex_cond.empty, NULL);
	assert(rc == 0);
	rc = pthread_cond_init(&mutex_cond.full, NULL); 
	assert(rc == 0);
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

void add_to_buffer(char * filename) {
	printf("in add_to_buffer and filename = %s\n", filename);
	info.bbp->buffer[info.bbp->fill] = filename;
	info.bbp->fill = (info.bbp->fill) % info.bbp->size;
	info.bbp->count++;
}

void* scannerWorker(void *data) {
    // TODO : scan files from file list
	Info * ip = (Info *) data; 
	char * line;
	int i;
	
	if((line = malloc(MAXPATH * sizeof(char))) == NULL) {
		printf("Error allocating memory in scannerWorker\n");
		exit(1);
	}

	while ((line = fgets(line, MAXPATH, ip->file_list)) != NULL) {
		if (line[strlen(line) - 1] == '\n')
			line[strlen(line) - 1] = 0;

		pthread_mutex_lock(&mutex_cond.bb_mutex);
		while (ip->bbp->count == ip->bbp->size) {
			pthread_cond_wait(&mutex_cond.empty, &mutex_cond.bb_mutex); 
		}

		add_to_buffer(line);
		pthread_cond_signal(&mutex_cond.full);
		pthread_mutex_unlock(&mutex_cond.bb_mutex); 
	}
	
    return NULL;
}

void startScanner(Info info) {
    // TODO : start scanner thread
	printf("call scannerWorder\n");
	pthread_t scanner_thread;
	int rc;
	rc = pthread_create(&scanner_thread, NULL, scannerWorker, &info);
	assert(rc == 0);
	pthread_exit(NULL);
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

	// HOW TO DO THIS?
	// Give it a shot here
	int i; 
	int rc; 
	pthread_t * indexer_threads;
	indexer_threads = (pthread_t *) calloc(args->num_indexer_threads * sizeof(pthread_t));

	for (i = 0; i < args->num_indexer_threads; i += 1) {
		int rc = pthread_create(&indexer_threads[i], NULL, indexerWorker, NULL);
		assert(rc == 0);
	}
	// Do we have to call pthread_join???
	// Or pthread_exit(NULL)???	
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

