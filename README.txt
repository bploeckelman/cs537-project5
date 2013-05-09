/******************************************************************************
*
*@class: CS 537-2
*@prof: Dr. Michael Swift
*@ta: Collin Engstrom 
*@proj: P5 - Desktop Search Engine
*@due: May 9th, 2013
*
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



# DONE (RUBRIC)
x=done  o=not done
1. Basic search 
x	word exists in text file
x	word exists in pdf
x	word not found at all

2. Advanced Search
x	word exists in simple file specified
x	word does not exist in file specified
x	file specified does not exist
x	waits for indexing to finish large file before returning
	
	


PART 2
1. 	Implements locking mechanism
	x	single mutex for whole table
	x/2	more sophisticated read/write lock
 	o	fine-grain (e.g. bucket-level) lockign

2. 	o? - bounded buffer is implemented/used correctly
3.	x	Waits for threads to exit before terminating program

4. 	o - Code is neat, documented, and easy to understand


Issues to fix:
1. Advanced search - not finding words in file grade_files/c. IS finding it
for files a and b. 

2. Appears that the first file in the file list is not getting indexed. 


Known Issues:


Summary:
 Our code is perfect. We are awesome. 
