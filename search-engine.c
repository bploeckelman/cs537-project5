#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <semaphore.h>

#include "index.h"

// #define DEBUG
// #define LOCKS
// #define VERBOSE
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
    pthread_t collector_thread;
    pthread_t *indexer_threads;
    pthread_mutex_t scanner_mutex;
    int scan_complete;
    int files_indexed;
} Info;
Info info;

struct stringnode {
    char* string;
    struct stringnode* next;
};

struct stringnode* indexedfilelist;
struct stringnode* endofindexedfilelist;
char* searchfor;
int indexcomplete;
pthread_mutex_t filelistlock;
pthread_cond_t  searchcomplete;


void parseArgs(int argc, char *argv[]);
void initialize();
void startScanner();
void startIndexers();
void startThreadCollector();
void startSearch();
void cleanup();

void addToFileList(char* filename);
void finishedindexing();
int waitUntilFileIsIndexed(char* filename);


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

    // Initialize buffer mutex and condition vars
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

    // Initialize scanner mutex
    if (pthread_mutex_init(&info.scanner_mutex, NULL)) {
        fprintf(stderr, "Failed to initialize scanner mutex.\n");
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
void initAdvSearchLocks() {
    // Initialize search helper vars
    indexedfilelist = NULL;
    endofindexedfilelist = NULL;
    searchfor = NULL;
    indexcomplete = 0;

    // Initialize adv. search mutex
    if (pthread_cond_init(&searchcomplete, NULL)) {
        perror("pthread_cond_init");
        exit(1);
    }
}

//-----------------------------------------------------------------------------
void initialize() {
	if (init_index()) {
        fprintf(stderr, "Failed to initialize hashtable.\n");
        exit(1);
    }

    initBoundedBuffer();
	initMutexStruct(); 
    initAdvSearchLocks();	
}

// ----------------------------------------------------------------------------
void usage() {
    fprintf(stderr, "Usage: search-index <num-indexer-threads> <file-list>\n");
    exit(1);
}

// ----------------------------------------------------------------------------
void parseArgs(int argc, char *argv[]) {
    // Enough args?
    if (argc != 3) {
        usage();
    }

    // Parse argument strings
    memset(&args, 0, sizeof(Args));
    // TODO : use strtol instead of atoi
    args.num_indexer_threads = atoi(argv[1]);
    args.file_list_name = argv[2];

    // Validate number of threads
    if (args.num_indexer_threads < 1) {
        fprintf(stderr, "Number of indexer threads must be > 0.\n");
        exit(1);
    }

    // Validate files list
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
// Scanner related ------------------------------------------------------------
// ----------------------------------------------------------------------------
void add_to_buffer(char * filename) {
    // Copy the specified filename into the bounded buffer
    strcpy(info.bbp->buffer[info.bbp->fill], filename);
    // Update fill index and buffer count
	info.bbp->fill = (info.bbp->fill + 1) % info.bbp->size;
	info.bbp->count++;
}

// ----------------------------------------------------------------------------
// Scan files from file list
void* scannerWorker(void *data) {
    // Allocate space for a filename + path
	char *line;
	if((line = malloc(MAXPATH * sizeof(char))) == NULL) {
		fprintf(stderr, "Failed to allocate memory for file path.\n");
		exit(1);
	}

    // Get filenames from files list and add to bounded buffer
	while (NULL != fgets(line, MAXPATH, info.file_list)) {
        // Chomp newline from file path
		if (line[strlen(line) - 1] == '\n')
			line[strlen(line) - 1] = 0;
#ifdef DEBUG
        printf("[%.8x scanner] got line '%s' from file list.\n", pthread_self(), line);
#endif

#ifdef LOCKS
        printf("[%.8x scanner] locking buffer mutex...\n", pthread_self());
#endif
        // Lock and wait on empty condition if neccessary 
		if (pthread_mutex_lock(&mutex_cond.bb_mutex)) {
            perror("pthread_mutex_lock()");
        }
		while (info.bbp->count == BOUNDED_BUFFER_SIZE) {
#ifdef LOCKS
            printf("[%.8x scanner] waiting on buffer empty condition...\n", pthread_self());
#endif
			pthread_cond_wait(&mutex_cond.empty, &mutex_cond.bb_mutex); 
		}

#ifdef DEBUG
        printf("[%.8x scanner] add_to_buffer[%d] '%s'\n", pthread_self(), info.bbp->fill, line);
#endif
        // Add filename + path to bounded buffer
		add_to_buffer(line);

#ifdef LOCKS
        printf("[%.8x scanner] signalling full condition...\n", pthread_self());
#endif
        // Signal full condition and unlock
		pthread_cond_signal(&mutex_cond.full);
#ifdef LOCKS
        printf("[%.8x scanner] unlocking buffer mutex...\n", pthread_self());
#endif
		if (pthread_mutex_unlock(&mutex_cond.bb_mutex)) {
            perror("pthread_mutex_unlock()");
        }
	}

#ifdef DEBUG
    printf("[%.8x scanner] finished fetching lines from '%s'.\n", pthread_self(), args.file_list_name);
#endif
    fclose(info.file_list);

#ifdef LOCKS
    printf("[%.8x scanner] locking scanner mutex.\n", pthread_self());
#endif
    pthread_mutex_lock(&info.scanner_mutex);
    info.scan_complete = 1;
#ifdef LOCKS
    printf("[%.8x scanner] unlocking scanner mutex.\n", pthread_self());
#endif
    pthread_mutex_unlock(&info.scanner_mutex);
	
    return NULL;
}

// ----------------------------------------------------------------------------
void startScanner() {
#ifdef DEBUG
	printf("startScanner()\n");
#endif
    // Create scanner thread and run it as scheduled
	if (pthread_create(&info.scanner_thread, NULL, scannerWorker, NULL)) {
        fprintf(stderr, "Failed to create scaner thread.\n");
        exit(1);
    }
#ifdef DEBUG
    printf("[%.8x main] created scanner thread #0.\n", pthread_self());
#endif
}

// ----------------------------------------------------------------------------
// Indexer related ------------------------------------------------------------
// ----------------------------------------------------------------------------
char* get_from_buffer() {
    // Get a filename from the bounded buffer
	char * file = info.bbp->buffer[info.bbp->use];
#ifdef DEBUG
    printf("[%.8x indexer] get_from_buffer[%d] '%s'\n", pthread_self(), info.bbp->use, file);
#endif 
    // Update use index and buffer count
	info.bbp->use = (info.bbp->use + 1) % info.bbp->size; 
	info.bbp->count--;
	return file; 
}

// ----------------------------------------------------------------------------
//Read files from list produced by scanner, add words to hash table
void* indexerWorker(void *data) {
    GetNext: // Get the next element from the bounded buffer
#ifdef LOCKS
    printf("[%.8x indexer] locking buffer mutex...\n", pthread_self());
#endif
    // Lock and wait on full condition if neccessary 
	pthread_mutex_lock(&mutex_cond.bb_mutex);
    while (info.bbp->count == 0) {
        // See if there are no more files to scan, if so, exit this indexer thread
        if (info.scan_complete && info.bbp->count == 0) {
            pthread_mutex_unlock(&mutex_cond.bb_mutex);
#ifdef DEBUG 
            printf("[%.8x indexer] buffer empty, scan complete, exiting thread...\n", pthread_self());
#endif 
            return NULL;
        }
#ifdef LOCKS
        printf("[%.8x indexer] waiting on buffer full condition...\n", pthread_self());
#endif
		pthread_cond_wait(&mutex_cond.full, &mutex_cond.bb_mutex);
	}

    // Get the next filename + path from the bounded buffer
	char *filename = get_from_buffer();
#ifdef LOCKS
    printf("[%.8x indexer] signalling empty condition...\n", pthread_self(), filename);
#endif 
    // Signalling empty condition
	pthread_cond_signal(&mutex_cond.empty);

#ifdef LOCKS
    printf("[%.8x indexer] unlocking buffer mutex...\n", pthread_self());
#endif 
    // Unlocking buffer mutex
	pthread_mutex_unlock(&mutex_cond.bb_mutex);

#ifdef DEBUG
    printf("[%.8x indexer] opening file '%s'...\n", pthread_self(), filename);
#endif 
    // Open filename from buffer and read lines
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        char buf[MAXPATH + 2];
        memset(buf, 0, MAXPATH + 2);
        sprintf(buf, "fopen('%s')", filename);
        perror(buf);
    } else {
        printf("[%.8x indexer] file '%s' opened.\n", pthread_self(), filename);
    }

    int line_number = 1;
    char *line = NULL;
    size_t len = 0;
    size_t read;

    // Get a new line (of arbitrary length) from the file
    while ((read = getline(&line, &len, file)) != -1) {
#ifdef DEBUG
        printf("[%.8x indexer] line of length %zu retreived\n\t'%s'\n", pthread_self(), read, line);
#endif
        // Tokenize the line into words to be inserted into index
        char *saveptr;
        char *word = strtok_r(line, " \n\t-_!@#$%^&*()_+=,./<>?", &saveptr);
        while (word != NULL) {
#ifdef VERBOSE 
            printf("[%.8x indexer] checking if '%s' is already in index...\n", pthread_self(), word);
#endif
            // Insert word into index (if not already in index)
            insert_into_index(word, filename, line_number);
            word = strtok_r(NULL, " \n\t-_!@#$%^&*()_+=,./<>?", &saveptr);
        }
        ++line_number;
    }

    // Cleanup memory for getline
    free(line);

#ifdef DEBUG
    printf("[%.8x indexer] done indexing file '%s'.\n", pthread_self(), filename);
#endif 
    // Cleanup file
    fclose(file);
    // Update list of files indexed
    // TODO : lock me?
    info.files_indexed++;
	addToFileList(filename);

    // Go grab the next file to index from the buffer
    goto GetNext;

    return NULL;
}

// ----------------------------------------------------------------------------
void startIndexers() {
#ifdef DEBUG
	printf("startIndexers()\n");
#endif
    // Allocate memory for indexer thread array
    info.indexer_threads = (pthread_t *) calloc(args.num_indexer_threads, sizeof(pthread_t));
    if (info.indexer_threads == NULL) {
        fprintf(stderr, "Failed to allocate memory for indexer thread array.\n");
        exit(1);
    }

    // Create all the indexer threads
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

// ----------------------------------------------------------------------------
// Chomp chomp chomp
void devourSpaces(char** start){
	char* temp = *start;
	while(*temp == ' '){
		++temp;
	}
}

// ----------------------------------------------------------------------------
// Om nom nom
void devourWord(char** start){
	char* temp = *start;
	while(*temp != ' ' && *temp != '\n' && *temp != '\0'){
		++temp;
	}
}

// ----------------------------------------------------------------------------
void* threadCollector(void *threadptr) {
    // Join all indexer threads to finish them cleanly
	if (info.indexer_threads != NULL) {
        for (int i = 0; i < args.num_indexer_threads; ++i) {
#ifdef DEBUG
            printf("Indexer thread #%d joining...\n", i);
#endif
            pthread_join(info.indexer_threads[i], NULL);
#ifdef DEBUG
            printf("Indexer thread #%d completed.\n", i);
#endif
        }
	}

	finishedindexing();

#ifdef DEBUG
	printf("Indexing complete = %d.\n", indexcomplete);
#endif
	return NULL; 
}

// ----------------------------------------------------------------------------
void startThreadCollector() {
#ifdef DEBUG
	printf("startThreadCollector()\n");
#endif
    // Start collector thread that collects 
	if (pthread_create(& info.collector_thread, NULL, threadCollector, NULL)) {
		fprintf(stderr, "Failed to create collector thread.\n");
        exit(1);
	} else {
#ifdef DEBUG
		printf("[%.8x main] created collector thread.\n", pthread_self());
#endif
	}
}

// ----------------------------------------------------------------------------
// Search related -------------------------------------------------------------
// ----------------------------------------------------------------------------
void doBasicSearch(char * word) {
#ifdef DEBUG
	printf("input: '%s'\n", word); 
#endif

    // Search for word in index and report results
	index_search_results_t *results = find_in_index(word);
	if (results) {
        // Print found for each result
		for (int i = 0; i < results->num_results; ++i) {
			index_search_elem_t *result = &results->results[i];
			printf("FOUND: %s %d\n", result->file_name, result->line_number);
		}

#ifdef DEBUG
		printf("%d results found...\n", results->num_results);
#endif
	} else {
        // No results found for word
		printf("Word not found\n");
	}
}

// ----------------------------------------------------------------------------
void doAdvancedSearch(char * filename, char * word) {
    // If file hasn't been indexed yet, wait to complete search until it is
    if (-1 == waitUntilFileIsIndexed(filename)) {
        // Indexing complete, specified file not found
        printf("ERROR: File <%s> not found\n", filename);
        return;
    }

#ifdef DEBUG
    printf("input: '%s' '%s'\n", filename, word); 
#endif

    // Search for word in index and report results
    index_search_results_t *results = find_in_index(word);
    if (results) {
        int count = 0;
        // Check through all results for specified filename
        for (int i = 0; i < results->num_results; ++i) {
            index_search_elem_t *result = &results->results[i];

            // Report results only for specified filename
            if(!strcmp(filename, result->file_name)){
                printf("FOUND: %s %d\n", result->file_name, result->line_number);
                ++count;
            }
        }

        // Not found in specified file
        if (count == 0) {
            printf("Word not found\n");
        }
#ifdef DEBUG
        printf("%d results found before filtering by filename...\n", results->num_results);
#endif
    } else {
        printf("Word not found\n");
    }	
}

// ----------------------------------------------------------------------------
// Get search terms and check them against hash table
void startSearch() {
    // Initialize buffer for search query string
    int BUFFER_SIZE = 1024;
    char line[BUFFER_SIZE];
    memset(line, 0, sizeof(char) * BUFFER_SIZE);

    // Setup two words for splitting search input (for adv. search)
	char* word1 = NULL;
	char* word2 = NULL;

    // Get a line from stdin to use for search query
    while (fgets(line, BUFFER_SIZE, stdin)) {
        // Chomp newline
		if(line[strlen(line) - 1] == '\n') {
			line[strlen(line) - 1] = 0; 
        }

        // Get first word
		word1 = strtok(line, " \t\n");
#ifdef DEBUG
		printf("word1 = '%s'\n", word1);
#endif

		if (word1 != NULL) {
            // Get second word
			word2 = strtok(NULL, " \t\n");	
#ifdef DEBUG
			printf("word2 = '%s'\n", word2);
#endif
            // Do the proper search (basic/adv.) depending on how many search terms
            if (word2 == NULL) {
                doBasicSearch(word1);
            } else {
                // Chomp ending whitespace before searching
                if(strtok(NULL," \t\n") == NULL) {
                    doAdvancedSearch(word1, word2);
                } else {
                    printf("***ERROR: Bad input\n");
                }
            }
		}

        // Clear input buffer for next search
        memset(line, 0, sizeof(char) * BUFFER_SIZE);
    }
}

// ----------------------------------------------------------------------------
void cleanup() {
#ifdef DEBUG
    printf("\n\n---------------------CLEANUP---------------------------\n\n");
#endif

    // Join the scanner thread
    pthread_join(info.scanner_thread, NULL);
#ifdef DEBUG
    printf("Scanner thread completed.\n");
#endif

    // Join collector thread (cleanly exits remaining indexer threads)
	pthread_join(info.collector_thread, NULL);
#ifdef DEBUG
    printf("Collector thread completed.\n");
#endif

    // Cleanup memory for indexer threads
    free(info.indexer_threads);
#ifdef DEBUG
    printf("\n\nTotal files indexed: %d\n", info.files_indexed);
#endif

    // Cleanup bounded buffer memory
    for (int i = 0; i < BOUNDED_BUFFER_SIZE; ++i) {
        free(info.bbp->buffer[i]);
    }
    free(info.bbp->buffer);
    free(info.bbp);

    // Cleanup filename list memory
	struct stringnode* temp = indexedfilelist;
	struct stringnode* temp2;
	while (temp != NULL){
		free(temp->string);
		temp2 = temp;
		temp = temp->next;
		free(temp2);
	}
    free(searchfor);

    // Cleanup filename list condition variable
	if (pthread_cond_destroy(&searchcomplete)){
		perror("pthread_cond_destroy");
	}	

    // TODO : cleanup other condition variables and mutexes?
}


// ----------------------------------------------------------------------------
// Filenames  list related ----------------------------------------------------
// TODO: comment and stuff
// ----------------------------------------------------------------------------
void addToFileList(char* filename){
	struct stringnode* newnode = (struct stringnode*)malloc(sizeof(struct stringnode ));
	if ((newnode->string = strdup(filename)) == NULL){
		//mem allocation for string failed
	}
	newnode->next = NULL;
	pthread_mutex_lock(&filelistlock);
	if(indexedfilelist == NULL){
		indexedfilelist = newnode;
	}
	else{
		endofindexedfilelist->next = newnode;
	}
	endofindexedfilelist = newnode;
	if(searchfor != NULL){
		if(!strcmp(filename, searchfor)){
			free(searchfor);
			searchfor = NULL;
			pthread_cond_signal(&searchcomplete);
			pthread_mutex_unlock(&filelistlock);
		}
		else{
			pthread_mutex_unlock(&filelistlock);
		}
	}
	else{
		pthread_mutex_unlock(&filelistlock);
	}
}

void finishedindexing(){
	pthread_mutex_lock(&filelistlock);
	indexcomplete = 1;
	pthread_cond_signal(&searchcomplete);
	pthread_mutex_unlock(&filelistlock);
}
	
int waitUntilFileIsIndexed(char* filename){
	pthread_mutex_lock(&filelistlock);
	struct stringnode* temp = indexedfilelist;
	while(temp != NULL){
		if(!strcmp(filename, temp->string)){
			pthread_mutex_unlock(&filelistlock);
			//it has already been indexed
			return 0;
		}
		temp = temp->next;
	}

	if(indexcomplete){
		pthread_mutex_unlock(&filelistlock);
		return -1;
	}
	searchfor = strdup(filename);
	pthread_cond_wait(&searchcomplete, &filelistlock);
	
	if (searchfor != NULL){
		free(searchfor);
		searchfor = NULL;
		pthread_mutex_unlock(&filelistlock);
		return -1;
	}
	pthread_mutex_unlock(&filelistlock);
	return 0;
}

