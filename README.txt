/******************************************************************************
*
*@class: CS 537-2
*@prof: Dr. Michael Swift
*@ta: Collin Engstrom 
*@proj: P5 - Desktop Search Engine
*@due: May 9th, 2013*
*@name: Joseph Francke
*@login: francke   
*
*@name: Brian Ploeckelman
*@login: ploeckel 
*
*@name: Bambridge Peterson
*@login: bambridg
*
*******************************************************************************


Known Issues:
	No known issues that we can detect. 

Summary:

The following are locks we added to the hashtable struct which are initialized 
in the create_hashtable() function:

We added a globallock which is a read-write lock for around the hashtable.
This lock is acquired for writing in the hashtable_expand() function, 
and is acquired for reading in cases where we are hashing a key or getting 
an index into the hashtable for a key.

We have an array called locks containing read-write locks for around each
linked-list in the hashtable. They treat adding to the list as a write access
and searching as read access of the list.

We also have a read-write lock for accesses to the entrycount for the hashtable.
The entrycount field gets accessed by several functions in various places, and 
the lock is acquired for reading or writing as required by its use (for example, 
it is read-locked in hashtable_count and write-locked in hashtable_insert).

Usage of these locks in index.c are commented as appropriate for clarity.


The following are locks added to search-engine.c:
We had a mutex for the bounded buffer, so that the scanner and indexer threads
could access the bounded buffer in parallel. We also had two condition
variables empty and full that signaled the scanner and indexer threads when it
was ok to produce/consume. 

We also had a mutex called filelistlock that covered our list of indexed
files (we used this list for advanced search). 

Notes:

Something to note is that we used a scanner, indexer threads, and the main
thread handles searching, however we also create a thread named 
collect_thread that joins indexer threads in the background and sets the
variable indexcomplete to 1 when all the indexer threads have completed.

The Makefile has a line commented out that calls a script listgen.sh that
itself calls the command find ./dir_name -type f > list_name and we were
unsure whether or not you wanted that in the Makefile as it may overwrite the
list of files you are going to test our code on. 

