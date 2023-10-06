#include <stdio.h>
#include <string.h>
#include <stdlib.h>	
#include <limits.h>
#include <time.h>


typedef struct {
        int pageNo;
        int modified;
		int second_chance;
} page;
page* pageTable = NULL;		// global array to represent the page table

// For LRU eviction:
typedef struct {
	int frameNo;
	int pageNo;
} LRUEntry;
LRUEntry lruQueue[4096]; // Assuming a maximum of 4096 frames for simplicity

enum    repl {rand_repl, fifo, lru, clock_repl};
int     createMMU( int );
int     checkInMemory( int ) ;
int     allocateFrame( int ) ;
page    selectVictim( int, enum repl ) ;
const   int pageoffset = 12;            /* Page size is fixed to 4 KB */
int     numFrames ;
int 	clockHand = 0;
int 	fifoQueue = 0;

/* Creates the page table structure to record memory allocation */
int     createMMU (int frames)
{

	numFrames = frames;

	// Create and initialize the page table
	pageTable = (page*)malloc(numFrames * sizeof(page));
	if (pageTable == NULL)
	{
		exit(-1);  // Exit with an error code
	}

	for (int i = 0; i < numFrames; i++)
	{
		pageTable[i].pageNo = -1;  // Initialize with an invalid page number
		pageTable[i].modified = 0; // Not modified initially
	}

	return 0;		// Successful MMU creation
}

/* Checks for residency: returns frame no or -1 if not found */
int     checkInMemory(int page_number)
{
	for (int i = 0; i < numFrames; i++)
	{
		if (pageTable[i].pageNo == page_number)
		{
			// Update LRU Queue
			for (int j = 0; j < numFrames; j++) 
			{
				if (lruQueue[j].pageNo == page_number) 
				{
					for (int k = j; k < numFrames - 1; k++) 
					{
						lruQueue[k] = lruQueue[k + 1]; // shift pages in the queue
					}
					lruQueue[numFrames - 1].pageNo = page_number; // move the accessed page to the back
					lruQueue[numFrames - 1].frameNo = i;  // set correct frameNo
					break;
				}
			}
			pageTable[i].second_chance = 1; // Update Clock Algorithm second chance

			return i;  // Page found in memory, return frame no
		}
	}
	return -1;  // Page not found in memory
}

/* allocate page to the next free frame and record where it put it */
int     allocateFrame(int page_number)
{
	for (int i = 0; i < numFrames; i++)
	{
		if (pageTable[i].pageNo == -1)
		{
			pageTable[i].pageNo = page_number;  // Update page table with new page
			pageTable[i].modified = 0;          // New page is not modified
			pageTable[i].second_chance = 1; 	//New page starts with second chance

			// Update LRU Queue
			for (int j = 0; j < numFrames - 1; j++) 
			{
				lruQueue[j] = lruQueue[j + 1];
			}
			lruQueue[numFrames - 1].pageNo = page_number;
			lruQueue[numFrames - 1].frameNo = i;  // set correct frameNo

			return i;  // Return allocated frame number
		}
	}
	return -1;  // No free frames available, handle page replacement
}

/* Selects a victim for eviction/discard according to the replacement algorithm,  returns chosen frame_no  */
page    selectVictim(int page_number, enum repl  mode)
{
    page    victim;
	int     frame_no = -1;
    
	switch (mode) 
	{
		case rand_repl:
		{
			// Randomly select a page to evict
			frame_no = rand() % numFrames;
			victim.pageNo = pageTable[frame_no].pageNo;
			victim.modified = pageTable[frame_no].modified;

			pageTable[frame_no].pageNo = page_number;
			pageTable[frame_no].modified = 0;

			break;
		}
		
		case fifo:
			// Use choose the victim according to FIFO queue
			victim.pageNo = pageTable[fifoQueue].pageNo;
			victim.modified = pageTable[fifoQueue].modified;

			pageTable[fifoQueue].pageNo = page_number;
			pageTable[fifoQueue].modified = 0;

			fifoQueue = (fifoQueue + 1) % numFrames;  // Advance the queue

			break;

		case lru:
		{
			frame_no = lruQueue[0].frameNo;

			victim.pageNo = lruQueue[0].pageNo;
			victim.modified = pageTable[frame_no].modified;

			for (int i = 0; i < numFrames - 1; i++) 
			{
				lruQueue[i] = lruQueue[i + 1];
			}

			lruQueue[numFrames - 1].pageNo = page_number;
			lruQueue[numFrames - 1].frameNo = frame_no;  // Update the frameNo for the most recent page
			
			pageTable[frame_no].pageNo = page_number;
			pageTable[frame_no].modified = 0;

			break;
		}		

		case clock_repl:
		{
			while (1) 
			{
				if (pageTable[clockHand].second_chance == 0) 
				{
					//select the victim if second chance is 0
					victim.pageNo = pageTable[clockHand].pageNo;
					victim.modified = pageTable[clockHand].modified;

					pageTable[clockHand].pageNo = page_number;
					pageTable[clockHand].modified = 0;
					pageTable[clockHand].second_chance = 1;

					clockHand = (clockHand + 1) % numFrames; // Move the hand to the next position
					
					break;
				}
				else 
				{
					//second chance is not 0, so convert 1 to 0 and move hand to next position
                    pageTable[clockHand].second_chance = 0; 
					clockHand = (clockHand + 1) % numFrames;
                };
			}
		}
    }		
    return (victim) ;

}

		
int main(int argc, char *argv[])
{
  
	char	*tracename;
	int	page_number,frame_no, done ;
	int	do_line, i;
	int	no_events, disk_writes, disk_reads;
	int     debugmode;
 	enum	repl  replace;
	int	allocated=0; 
	int	victim_page;
        unsigned address;
    	char 	rw;
	page	Pvictim;
	FILE	*trace;

	srand((unsigned)time(NULL));

    if (argc < 5) 
	{
             printf("Usage: ./memsim inputfile numberframes replacementmode debugmode \n");
             exit (-1);
	}
	else 
	{
        tracename = argv[1];	
		trace = fopen( tracename, "r");
		if (trace == NULL ) 
		{
             printf( "Cannot open trace file %s \n", tracename);
             exit ( -1);
		}
		numFrames = atoi(argv[2]);
        if (numFrames < 1) 
		{
            printf( "Frame number must be at least 1\n");
            exit ( -1);
        }
        if (strcmp(argv[3], "lru\0") == 0)
            replace = lru;
	    else if (strcmp(argv[3], "rand\0") == 0)
			 replace = rand_repl;
	    else if (strcmp(argv[3], "clock\0") == 0)
             replace = clock_repl;		 
	    else if (strcmp(argv[3], "fifo\0") == 0)
             replace = fifo;		 
        else 
	    {
             printf( "Replacement algorithm must be rand/fifo/lru/clock  \n");
             exit ( -1);
	    }

        if (strcmp(argv[4], "quiet\0") == 0)
			debugmode = 0;
	    else if (strcmp(argv[4], "debug\0") == 0)
            debugmode = 1;
        else 
	    {
             printf( "Replacement algorithm must be quiet/debug  \n");
             exit ( -1);
	    }
	}
	
	done = createMMU (numFrames);
	if ( done == -1 ) 
	{
		 printf( "Cannot create MMU" ) ;
		 exit(-1);
    }
	no_events = 0 ;
	disk_writes = 0 ;
	disk_reads = 0 ;

    do_line = fscanf(trace,"%x %c",&address,&rw);
	while (do_line == 2)
	{
		page_number =  address >> pageoffset;
		frame_no = checkInMemory(page_number) ;    /* ask for physical address */

		if ( frame_no == -1 )
		{
			disk_reads++ ;			/* Page fault, need to load it into memory */
			if (debugmode) 
				printf( "Page fault %8d \n", page_number) ;
			if (allocated < numFrames)  			/* allocate it to an empty frame */
			{
                frame_no = allocateFrame(page_number);
				allocated++;
			}
            else
		    {
				Pvictim = selectVictim(page_number, replace) ;   /* returns page number of the victim  */
		        frame_no = checkInMemory(page_number) ;    /* find out the frame the new page is in */


				for (int i = 0; i < numFrames; i++)
				{
					if (pageTable[i].pageNo == Pvictim.pageNo)
					{
						pageTable[i].pageNo = page_number;
						pageTable[i].modified = 0;
						break;
					}
				}

		        if (Pvictim.modified)           /* need to know victim page and modified  */
	 			{			    
					disk_writes++;
					if (debugmode) 
						printf( "Disk write %8d \n", Pvictim.pageNo) ;
				}
				else if (debugmode) 
					printf( "Discard    %8d \n", Pvictim.pageNo) ;
			}
		}
		if ( rw == 'R')
		{
			if (debugmode) 
				printf( "reading    %8d \n", page_number) ;
		}
		else if ( rw == 'W')
		{
			if (debugmode) 
				printf( "writting   %8d \n", page_number) ;

			// mark page in page table as written - modified  
			pageTable[frame_no].modified = 1;
		}
		else 
		{
			printf( "Badly formatted file. Error on line %d\n", no_events+1); 
			exit (-1);
		}

		no_events++;
        do_line = fscanf(trace,"%x %c",&address,&rw);
	}

	printf( "total memory frames:  %d\n", numFrames);
	printf( "events in trace:      %d\n", no_events);
	printf( "total disk reads:     %d\n", disk_reads);
	printf( "total disk writes:    %d\n", disk_writes);
	printf( "page fault rate:      %.4f\n", (float) disk_reads/no_events);
}
				
