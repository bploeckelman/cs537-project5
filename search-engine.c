#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <semaphore.h>

#include "index.h"

#define DEBUG
#define BOUNDED_BUFFER_SIZE 32

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
	pthread_t collector_thread
    pthread_t *indexer_threads;
    int scan_complete;
    int files_indexed;
} Info;
Info info;


void parseArgs(int argc, char *argv[]);
void initialize();
void startScanner();
void startIndexers();
void startThreadCollector();
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
	startThreadCollector();
    startSearch();
    cleanup();
    return 0;
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
void initBoundedBuffer() {
    // Allocate and initialize the Bounded_Buffer struct
    info.bbp = (Bounded_Buffer *) malloc(sizeof(Bounded_Buffer));
    memset(info.bbp, 0, sizeof(Bounded_Buffer));
    info.bbp->size = BOUNDED_BUFFER_SIZE;

    // Allocate and initialize the buffer
    info.bbp->buffer = (char **) malloc(sizeof(char *) * BOUNDED_BUFFER_SIZE);
    for (int i = 0; i < BOUNDED_BUFFER_SIZE; ++i) {
        info.bbp->buffer[i] = (char *) malloc(sizeof(char) * MAXPATH);
        // TODO : is this supposed to be MAXPATH + 2 for \n\0 ?
        memset(info.bbp->buffer[i], 0, MAXPATH);
    }
#ifdef DEBUG
    printf("info.bbp->buffer = %p\n", info.bbp->buffer);
	printf("info.bbp->fill  = %d\n", info.bbp->fill);
	printf("info.bbp->use   = %d\n", info.bbp->use);
	printf("info.bbp->count = %d\n", info.bbp->count);
	printf("info.bbp->done  = %d\n", info.bbp->done);
	printf("info.bbp->size  = %d\n", info.bbp->size);
#endif
}

//-----------------------------------------------------------------------------
void initialize() {
	if (init_index()) {
        fprintf(stderr, "Failed to initialize hashtable.\n");
        exit(1);
    }

    initBoundedBuffer();
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
    strcpy(info.bbp->buffer[info.bbp->fill], filename);
	//info.bbp->buffer[info.bbp->fill] = filename;
	info.bbp->fill = (info.bbp->fill + 1) % info.bbp->size;
	info.bbp->count++;
}

// Scan files from file list
void* scannerWorker(void *data) {
	char * line;
	if((line = malloc(MAXPATH * sizeof(char))) == NULL) {
		fprintf(stderr, "Failed to allocate memory for file path.\n");
		exit(1);
	}

	while (NULL != fgets(line, MAXPATH, info.file_list)) {
		if (line[strlen(line) - 1] == '\n')
			line[strlen(line) - 1] = 0;
        printf("[%.8x scanner] got line '%s' from file list.\n", pthread_self(), line);

        printf("[%.8x scanner] locking buffer mutex...\n", pthread_self());
		if (pthread_mutex_lock(&mutex_cond.bb_mutex)) {
            perror("pthread_mutex_lock()");
        }
		while (info.bbp->count == BOUNDED_BUFFER_SIZE) { //info.bbp->size) {
            printf("[%.8x scanner] waiting on buffer empty condition...\n", pthread_self());
			pthread_cond_wait(&mutex_cond.empty, &mutex_cond.bb_mutex); 
		}

#ifdef DEBUG
        printf("[%.8x scanner] add_to_buffer[%d] '%s'\n",
            pthread_self(), info.bbp->fill, line);
#endif
		add_to_buffer(line);

        printf("[%.8x scanner] signalling full condition...\n", pthread_self());
		pthread_cond_signal(&mutex_cond.full);

        printf("[%.8x scanner] unlocking buffer mutex...\n", pthread_self());
		if (pthread_mutex_unlock(&mutex_cond.bb_mutex)) {
            perror("pthread_mutex_unlock()");
        }
	}

    printf("[%.8x scanner] finished fetching lines from '%s'.\n", pthread_self(), args.file_list_name);
    fclose(info.file_list);
    info.scan_complete = 1;
	
    return NULL;
}

void startScanner() {
#ifdef DEBUG
	printf("startScanner()\n");
#endif
	if (pthread_create(&info.scanner_thread, NULL, scannerWorker, NULL)) {
        fprintf(stderr, "Failed to create scaner thread.\n");
        exit(1);
    }
#ifdef DEBUG
    printf("[%.8x main] created scanner thread #0.\n", pthread_self());
#endif
}

// ----------------------------------------------------------------------------
char * get_from_buffer() {
	char * file = info.bbp->buffer[info.bbp->use];
    printf("[%.8x indexer] get_from_buffer[%d] '%s'\n", pthread_self(), info.bbp->use, file);
	info.bbp->use = (info.bbp->use + 1) % info.bbp->size; 
	info.bbp->count--;
	return file; 
}

void* indexerWorker(void *data) {
    // TODO : read files from list produced by scanner, add words to hash table
	// Not sure about this, but here it goes:
	int BUFFER_SIZE = 1024;
	char buffer[BUFFER_SIZE];

    GetNext:
    printf("[%.8x indexer] locking buffer mutex...\n", pthread_self());
	pthread_mutex_lock(&mutex_cond.bb_mutex);
    while (info.bbp->count == 0) {
        if (info.scan_complete && info.bbp->count == 0) {
            // no more files to scan, exit indexer thread
            pthread_mutex_unlock(&mutex_cond.bb_mutex);
            printf("[%.8x indexer] buffer empty, scan complete, exiting thread...\n", pthread_self());
            return NULL;
        }
        printf("[%.8x indexer] waiting on buffer full condition...\n", pthread_self());
		pthread_cond_wait(&mutex_cond.full, &mutex_cond.bb_mutex);
	}

	char * filename = get_from_buffer();
    printf("[%.8x indexer] signalling empty condition...\n", pthread_self(), filename);
	pthread_cond_signal(&mutex_cond.empty);
    printf("[%.8x indexer] unlocking buffer mutex...\n", pthread_self());
	pthread_mutex_unlock(&mutex_cond.bb_mutex);

    printf("[%.8x indexer] opening file '%s'...\n", pthread_self(), filename);
    FILE *file = fopen(filename, "r");
    int line_number = 1;
    while (!feof(file)) {
        fgets(buffer, MAXPATH, file);
        char *saveptr;
        char *word = strtok_r(buffer, " \n\t-_!@#$%^&*()_+=,./<>?", &saveptr);
        while (word != NULL) {
#ifdef DEBUG
            printf("[%.8x indexer] checking if '%s' is already in index...\n", pthread_self(), word);
#endif
            insert_into_index(word, filename, line_number);
            word = strtok_r(NULL, " \n\t-_!@#$%^&*()_+=,./<>?", &saveptr);
        }
        ++line_number;
    }
    printf("[%.8x indexer] done indexing file '%s'.\n", pthread_self(), filename);
    info.files_indexed++;
    fclose(file);
	addToFileList(filename);
    goto GetNext;
    return NULL;
}

void startIndexers() {
#ifdef DEBUG
	printf("startIndexers()\n");
#endif

    info.indexer_threads = (pthread_t *) calloc(args.num_indexer_threads, sizeof(pthread_t));
	for (int i = 0; i < args.num_indexer_threads; ++i) {
		if (pthread_create(&info.indexer_threads[i], NULL, indexerWorker, NULL)) {
            fprintf(stderr, "Failed to create indexer thread #%d.\n", i);
        } else {
#ifdef DEBUG
            printf("[%.8x main] created indexer thread #%d.\n", pthread_self(), i);
#endif
        }
	}
}

void devourSpaces(char** start){
	char* temp = start*;
	while(temp* == ' '){
		++temp;
	}
}

void devourWord(char** start){
	char* temp = start*;
	while(temp* != ' ' && temp* != '\n' && temp* != '\0'){
		++temp;
	}
}

void* threadCollector(void *threadptr) {
	if (info.indexer_threads != NULL) {
        for (int i = 0; i < args.num_indexer_threads; ++i) {
            printf("Indexer thread #%d joining...\n", i);
            pthread_join(info.indexer_threads[i], NULL);
            printf("Indexer thread #%d completed.\n", i);
        }
	}
	finishedindexing();
}


// ----------------------------------------------------------------------------
void startThreadCollector() {
#ifdef DEBUG
	printf("startThreadCollector()\n");
#endif
	if (pthread_create(& info.collector_thread, NULL, threadCollector, NULL)) {
		fprintf(stderr, "Failed to create collector thread.\n");
	} else {
#ifdef DEBUG
		printf("[%.8x main] created collector thread.\n", pthread_self());
#endif
	}
}
// ----------------------------------------------------------------------------
void startSearch() {
    // TODO : get search terms and check them against hash table
    printf("Starting search...\n");

    int BUFFER_SIZE = 1024;
    char line[BUFFER_SIZE];
	char* firstword = NULL;
	char* secondword = NULL;
	char* temp;
    memset(line, 0, sizeof(char) * BUFFER_SIZE);

    while (fgets(line, BUFFER_SIZE, stdin)) {
		temp = line;
		devourSpaces(&temp, end);
		if (temp* != '\n' && temp* != '\0'){
			firstword = temp;
		}
		devourWord(&temp);
		if(temp* == ' '){
			temp* = '\0';
			++temp;
			devourSpaces(&temp, end);
			if (temp* != '\n' && temp* != '\0'){
				secondword = temp;
				devourWord(&temp);
				if(temp* == ' '){
					temp* = '\0';
					++temp;
					devourSpaces(&temp, end);
					if (temp* != '\n' && temp* != '\0'){
						//too many args, not sure if it should error
					}
				)
				else{
					temp* = '\0';
				}
			}
			
		}
		else{
			temp* = '\0';
		}

		if (firstword == NULL){
			//input did not have any words
		}
		else{
			if (!strcmp(firstword, "EXIT\0")) {
				break;
			}
			
			if (secondword == NULL){
				//standard search
				printf("input: '%s'\n", firstword); 
				index_search_results_t *results = find_in_index(firstword);
				if (results) {
					printf("%d results found...\n", results->num_results);
					for (int i = 0; i < results->num_results; ++i) {
						index_search_elem_t *result = &results->results[i];
						printf("FOUND: %s %d\n", result->file_name, result->line_number);
					}
				} else {
					printf("Word not found\n");
				}
			}
			else{
				//advanced search
				waitUntilFileIsIndexed(firstword);
				
				printf("input: '%s' '%s'\n", firstword, secondword); 
				index_search_results_t *results = find_in_index(secondword);
				if (results) {
					printf("%d results found before filtering by filename...\n", results->num_results);
					for (int i = 0; i < results->num_refsults; ++i) {
						index_search_elem_t *result = &results->results[i];
						if(!strcmp(firstword, result->file_name){
							printf("FOUND: %s %d\n", result->file_name, result->line_number);
						}
					}
				} else {
					printf("Word not found\n");
				}
			}
		}
        memset(line, 0, sizeof(char) * BUFFER_SIZE);
    }
}

// ----------------------------------------------------------------------------
void cleanup() {
    printf("\n\n---------------------CLEANUP---------------------------\n\n");
    // Join the scanner thread
    pthread_join(info.scanner_thread, NULL);
    printf("Scanner thread completed.\n");
	pthread_join(info.collector_thread, NULL);
    printf("Collector thread completed.\n");
    // Join any remaining indexer threads and free the array of threads
    if (info.indexer_threads != NULL) {
        //for (int i = 0; i < args.num_indexer_threads; ++i) {
        //    printf("Indexer thread #%d joining...\n", i);
        //    pthread_join(info.indexer_threads[i], NULL);
        //    printf("Indexer thread #%d completed.\n", i);
        //}
        free(info.indexer_threads);
    }
    printf("\n\nFiles indexed: %d\n", info.files_indexed);

    for (int i = 0; i < BOUNDED_BUFFER_SIZE; ++i) {
        free(info.bbp->buffer[i]);
    }
    free(info.bbp->buffer);
    free(info.bbp);
}

