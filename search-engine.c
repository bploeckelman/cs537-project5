#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <semaphore.h>

#include "index.h"

#define DEBUG

typedef struct bounded_buffer_s {
	char ** buffer;
	int fill;
	int use;
	int count;
	int done;
	int size; 
} Bounded_Buffer; 
typedef Bounded_Buffer * bounded_buffer_t; 

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
	bounded_buffer_t bbp; 
    pthread_t scanner_thread;
    pthread_t *indexer_threads;
} Info;
Info info;


void parseArgs(int argc, char *argv[]);
void initialize();
void startScanner();
void startIndexers();
void startSearch();
void cleanup();


// ----------------------------------------------------------------------------
// Entry point ----------------------------------------------------------------
// ----------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    parseArgs(argc, argv);

    initialize();
    startScanner();
    startIndexers();
    startSearch();
    cleanup();
    return 0;
}


//-----------------------------------------------------------------------------
char ** initBoundedBuffer() {
	char **buffer = malloc(sizeof(char *) * args.num_indexer_threads);
	
    // TODO : is this supposed to be MAXPATH + 2 for \n\0 ?
	for(int i = 0; i < args.num_indexer_threads; ++i) {
		buffer[i] = malloc(sizeof(char) * MAXPATH);
		memset(buffer[i], 0, MAXPATH);
	} 

	return buffer; 
}

//-----------------------------------------------------------------------------
void initMutexStruct() {
	memset(&mutex_cond, 0, sizeof(Mutex_Cond));

	if (pthread_mutex_init(&mutex_cond.bb_mutex, NULL)) {
        fprintf(stderr, "Failed to initialize bounded buffer mutex.\n");
        exit(1);
    }
	if (pthread_cond_init(&mutex_cond.empty, NULL)) {
        fprintf(stderr, "Failed to initialize empty condition variable.\n");
        exit(1);
    }
    if (pthread_cond_init(&mutex_cond.full, NULL)) {
        fprintf(stderr, "Failed to initialize full condition variable.\n");
        exit(1);
    }
}

//-----------------------------------------------------------------------------
void initialize() {
	if (init_index()) {
        fprintf(stderr, "Failed to initialize hashtable.\n");
        exit(1);
    }

	// info.bbp = initBoundedBuffer();
	Bounded_Buffer bb =
		{initBoundedBuffer(), 0, 0, 0, 0, args.num_indexer_threads};
	info.bbp = &bb; 
	printf("info.bbp->fill %d\n", info.bbp->fill);
	initMutexStruct(); 
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
void add_to_buffer(char * filename) {
	printf("in add_to_buffer: filename = '%s',  info.bbp->fill = %d\n",
        filename, info.bbp->fill);
	info.bbp->buffer[info.bbp->fill] = filename;
	info.bbp->fill = (info.bbp->fill + 1) % info.bbp->size;
	info.bbp->count++;
}

void* scannerWorker(void *data) {
    // TODO : scan files from file list
	Info * ip = (Info *) data; 
	char * line;
	
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

void startScanner() {
	printf("call scannerWorder info.bbp->fill = %d\n", info.bbp->fill);
	if (pthread_create(&info.scanner_thread, NULL, scannerWorker, NULL)) {
        fprintf(stderr, "Failed to create scaner thread.\n");
        exit(1);
    }
}

// ----------------------------------------------------------------------------
char * get_from_buffer() {
	char * file = info.bbp->buffer[info.bbp->use];
	info.bbp->use = (info.bbp->use + 1) % info.bbp->size; 
	info.bbp->count--;
	return file; 
}

void* indexerWorker(void *data) {
    // TODO : read files from list produced by scanner, add words to hash table
	// Not sure about this, but here it goes:
	int BUFFER_SIZE = 1024;
	char buffer[BUFFER_SIZE];
	pthread_mutex_lock(&mutex_cond.bb_mutex);
	while (info.bbp->count == 0) {
		pthread_cond_wait(&mutex_cond.empty, &mutex_cond.bb_mutex);
	}
	char * filename = get_from_buffer();
	printf("filename = %s\n", filename);
	pthread_cond_signal(&mutex_cond.full);
	pthread_mutex_unlock(&mutex_cond.bb_mutex);

    FILE *file = fopen(filename, "r");
    while (!feof(file)) {
        int line_number = 0;
        char *saveptr;
        fgets(buffer, BUFFER_SIZE, file);
        char *word = strtok_r(buffer, " \n\t-_!@#$%^&*()_+=,./<>?", &saveptr);
        while (word != NULL) {
            insert_into_index(word, filename, line_number);
            word = strtok_r(buffer, " \n\t-_!@#$%^&*()_+=,./<>?", &saveptr);
        }
        ++line_number;
    }
    fclose(file);
    return NULL;
}

void startIndexers() {
#ifdef DEBUG
	printf("startIndexer()\n");
#endif

    info.indexer_threads = (pthread_t *) calloc(args.num_indexer_threads, sizeof(pthread_t));
	for (int i = 0; i < args.num_indexer_threads; ++i) {
		if (pthread_create(&info.indexer_threads[i], NULL, indexerWorker, NULL)) {
            fprintf(stderr, "Failed to create indexer thread #%d.\n", i);
        }
	}
}

// ----------------------------------------------------------------------------
void startSearch() {
    // TODO : get search terms and check them against hash table
}

// ----------------------------------------------------------------------------
void cleanup() {
    // Close the file list
    // NOTE: this could be done by the scanner thread when its finished
    fclose(info.file_list);

    // Join any remaining indexer threads and free the array of threads
    for (int i = 0; i < args.num_indexer_threads; ++i) {
        pthread_join(info.indexer_threads[i], NULL);
    }
    free(info.indexer_threads);
}

