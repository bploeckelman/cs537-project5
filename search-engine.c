#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <semaphore.h>

#include "index.h"

#define BOUNDED_BUFFER_SIZE 32

// ----------------------------------------------------------------------------
// Data structures and globals
// ----------------------------------------------------------------------------
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


// ----------------------------------------------------------------------------
// Function forward declarations
// ----------------------------------------------------------------------------
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

    //printf("info.bbp->buffer = %p\n", info.bbp->buffer);
	//printf("info.bbp->fill  = %d\n", info.bbp->fill);
	//printf("info.bbp->use   = %d\n", info.bbp->use);
	//printf("info.bbp->count = %d\n", info.bbp->count);
	//printf("info.bbp->done  = %d\n", info.bbp->done);
	//printf("info.bbp->size  = %d\n", info.bbp->size);
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

        // Lock and wait on empty condition if neccessary 
		if (pthread_mutex_lock(&mutex_cond.bb_mutex)) {
            perror("pthread_mutex_lock()");
        }
		while (info.bbp->count == BOUNDED_BUFFER_SIZE) {
			pthread_cond_wait(&mutex_cond.empty, &mutex_cond.bb_mutex); 
		}

        // Add filename + path to bounded buffer
		add_to_buffer(line);

        // Signal full condition and unlock
		pthread_cond_signal(&mutex_cond.full);

		if (pthread_mutex_unlock(&mutex_cond.bb_mutex)) {
            perror("pthread_mutex_unlock()");
        }
	}

    fclose(info.file_list);

    pthread_mutex_lock(&info.scanner_mutex);
    info.scan_complete = 1;
    pthread_mutex_unlock(&info.scanner_mutex);
	
    return NULL;
}

// ----------------------------------------------------------------------------
void startScanner() {
    // Create scanner thread and run it as scheduled
	if (pthread_create(&info.scanner_thread, NULL, scannerWorker, NULL)) {
        fprintf(stderr, "Failed to create scaner thread.\n");
        exit(1);
    }
}

// ----------------------------------------------------------------------------
// Indexer related ------------------------------------------------------------
// ----------------------------------------------------------------------------
char* get_from_buffer() {
    // Get a filename from the bounded buffer
	char * file = info.bbp->buffer[info.bbp->use];
    // Update use index and buffer count
	info.bbp->use = (info.bbp->use + 1) % info.bbp->size; 
	info.bbp->count--;
	return file; 
}

// ----------------------------------------------------------------------------
//Read files from list produced by scanner, add words to hash table
void* indexerWorker(void *data) {
    GetNext: // Get the next element from the bounded buffer
    // Lock and wait on full condition if neccessary 
	pthread_mutex_lock(&mutex_cond.bb_mutex);
    while (info.bbp->count == 0) {
        // See if there are no more files to scan, if so, exit this indexer thread
        if (info.scan_complete && info.bbp->count == 0) {
            pthread_mutex_unlock(&mutex_cond.bb_mutex);
            return NULL;
        }
		pthread_cond_wait(&mutex_cond.full, &mutex_cond.bb_mutex);
	}

    // Get the next filename + path from the bounded buffer
	char *filename = get_from_buffer();

    // Signalling empty condition
	pthread_cond_signal(&mutex_cond.empty);

    // Unlocking buffer mutex
	pthread_mutex_unlock(&mutex_cond.bb_mutex);

    // Open filename from buffer and read lines
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        char buf[MAXPATH + 2];
        memset(buf, 0, MAXPATH + 2);
        sprintf(buf, "fopen('%s')", filename);
        perror(buf);
    }

    int line_number = 1;
    char *line = NULL;
    size_t len = 0;
    size_t read;

    // Get a new line (of arbitrary length) from the file
    while ((read = getline(&line, &len, file)) != -1) {
        // Tokenize the line into words to be inserted into index
        char *saveptr;
        char *word = strtok_r(line, " \n\t-_!@#$%^&*()[]{}:;_+=,./<>?", &saveptr);
        while (word != NULL) {
            // Insert word into index (if not already in index)
            insert_into_index(word, filename, line_number);
            word = strtok_r(NULL, " \n\t-_!@#$%^&*()[]{}:;_+=,./<>?", &saveptr);
        }
        ++line_number;
    }

    // Cleanup memory for getline
    free(line);

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
            pthread_join(info.indexer_threads[i], NULL);
        }
	}

	finishedindexing();

	return NULL; 
}

// ----------------------------------------------------------------------------
void startThreadCollector() {
    // Start collector thread that collects 
	if (pthread_create(& info.collector_thread, NULL, threadCollector, NULL)) {
		fprintf(stderr, "Failed to create collector thread.\n");
        exit(1);
	}
}

// ----------------------------------------------------------------------------
// Search related -------------------------------------------------------------
// ----------------------------------------------------------------------------
void doBasicSearch(char * word) {
    // Search for word in index and report results
	index_search_results_t *results = find_in_index(word);
	if (results) {
        // Print found for each result
		for (int i = 0; i < results->num_results; ++i) {
			index_search_elem_t *result = &results->results[i];
			printf("FOUND: %s %d\n", result->file_name, result->line_number);
		}
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
		if (word1 != NULL) {
            // Get second word
			word2 = strtok(NULL, " \t\n");	
            // Do the proper search (basic/adv.) depending on how many search terms
            if (word2 == NULL) {
                doBasicSearch(word1);
            } else {
                // Chomp ending whitespace before searching
                if(strtok(NULL," \t\n") == NULL) {
                    doAdvancedSearch(word1, word2);
                } else {
                    printf("ERROR: Bad input\n");
                }
            }
		}

        // Clear input buffer for next search
        memset(line, 0, sizeof(char) * BUFFER_SIZE);
    }
}

// ----------------------------------------------------------------------------
void cleanup() {
    // Join the scanner thread
    pthread_join(info.scanner_thread, NULL);

    // Join collector thread (cleanly exits remaining indexer threads)
	pthread_join(info.collector_thread, NULL);

    // Cleanup memory for indexer threads
    free(info.indexer_threads);

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
	if(searchfor != NULL){
		free(searchfor);
	}

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
	//make a linked list node for the file to be added
	struct stringnode* newnode = (struct stringnode*)malloc(sizeof(struct stringnode ));
	if ((newnode->string = strdup(filename)) == NULL){
		//mem allocation for string failed
	}
	newnode->next = NULL;
	
	//get the lock to modify the filename linked list
	pthread_mutex_lock(&filelistlock);
	
	//if the linked list is empty, make the new node the head
	if(indexedfilelist == NULL){
		indexedfilelist = newnode;
	}
	else{
		//otherwise make the linked list's tail's next node point to the newnode
		endofindexedfilelist->next = newnode;
	}
	//make the newnode the tail
	endofindexedfilelist = newnode;
	
	//check if a search is waiting for a file
	if(searchfor != NULL){
		//if yes, check if what we are indexing is the file
		if(!strcmp(filename, searchfor)){
			//if it is, signal that we have indexed that file
			searchfor = NULL;
			pthread_cond_signal(&searchcomplete);
		}
	}
	//and then unlock the filename linked list lock
	pthread_mutex_unlock(&filelistlock);
}

// ----------------------------------------------------------------------------
void finishedindexing(){
	//if the collector calls this, we check out the filename linked list lock
	pthread_mutex_lock(&filelistlock);
	//set the variable that indicates that the indexer threads are complete
	indexcomplete = 1;
	//signal the search in case it is waiting on files that will not be indexed
	pthread_cond_signal(&searchcomplete);
	pthread_mutex_unlock(&filelistlock);
}
	
// ----------------------------------------------------------------------------
int waitUntilFileIsIndexed(char* filename){
	//get the indexed filenames linked list lock
	pthread_mutex_lock(&filelistlock);
	//grab the head of the list
	struct stringnode* temp = indexedfilelist;
	//iterate through the list
	while(temp != NULL){
		//if we find the filename, then the file is already indexed so return
		if(!strcmp(filename, temp->string)){
			pthread_mutex_unlock(&filelistlock);
			//it has already been indexed
			return 0;
		}
		temp = temp->next;
	}
	//file is not completed
	if(indexcomplete){
		//if indexing is complete, then our search will never succeed
		pthread_mutex_unlock(&filelistlock);
		return -1;
	}
	//leave the filename here so indexers can watch for it
	searchfor = filename;
	pthread_cond_wait(&searchcomplete, &filelistlock);
	
	if (searchfor != NULL){
		//was signalled because indexing finished, so search fails
		searchfor = NULL;
		pthread_mutex_unlock(&filelistlock);
		return -1;
	}
	//was signalled because file is now indexed
	pthread_mutex_unlock(&filelistlock);
	return 0;
}

