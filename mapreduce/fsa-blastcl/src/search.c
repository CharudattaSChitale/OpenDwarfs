// search.c
// Copyright (c) 2005, Michael Cameron
// Permission to use this code is freely granted under the BSD license agreement,
// provided that this statement is retained.
//
// Functions for performing stages 1 & 2 of the search algorithm


#include "blast.h"
#include "wordLookupDFA.h"
#include <math.h>
#include <sys/time.h>

extern unsigned char * wordLookupDFA;
extern struct groupFP *wordLookupDFA_groupsFP;
TIMERECORD timeRecord;
//unsigned char querySequenceC[40000];
cl_mem global_sequenceCount;
cl_mem global_numAdditionalTriggerExtensions;

//texture<unsigned char, 1> texSubjectSequences;

struct __attribute__ ((aligned (8))) parameters {
	cl_int 	wordLookupDFA_numCodes;
	cl_uint	additionalQueryPositionOffset;
	cl_int	statistics_ungappedNominalDropoff;
	cl_int	blast_ungappedNominalTrigger;
	cl_int    parameters_A;
	cl_uint	ungappedExtensionsPerThread;
	cl_uint	ungappedExtAdditionalStartLoc;
	char 	parameters_wordSize;
	cl_uchar encoding_numCodes;
	char    parameters_overlap;
};

#define TARGET_THREAD 0
#define UNGAPEXT_PER_THREAD 150
#define TOTAL_UNGAPPED_EXT 1500000
char *print_cl_errstring(cl_int err) {
    switch (err) {
        case CL_SUCCESS: return strdup("Success!");
        case CL_DEVICE_NOT_FOUND: return strdup("Device not found.");
        case CL_DEVICE_NOT_AVAILABLE: return strdup("Device not available");
        case CL_COMPILER_NOT_AVAILABLE: return strdup("Compiler not available");
        case CL_MEM_OBJECT_ALLOCATION_FAILURE: return strdup("Memory object allocation failure");
        case CL_OUT_OF_RESOURCES: return strdup("Out of resources");
        case CL_OUT_OF_HOST_MEMORY: return strdup("Out of host memory");
        case CL_PROFILING_INFO_NOT_AVAILABLE: return strdup("Profiling information not available");
        case CL_MEM_COPY_OVERLAP: return strdup("Memory copy overlap");
        case CL_IMAGE_FORMAT_MISMATCH: return strdup("Image format mismatch");
        case CL_IMAGE_FORMAT_NOT_SUPPORTED: return strdup("Image format not supported");
        case CL_BUILD_PROGRAM_FAILURE: return strdup("Program build failure");
        case CL_MAP_FAILURE: return strdup("Map failure");
        case CL_INVALID_VALUE: return strdup("Invalid value");
        case CL_INVALID_DEVICE_TYPE: return strdup("Invalid device type");
        case CL_INVALID_PLATFORM: return strdup("Invalid platform");
        case CL_INVALID_DEVICE: return strdup("Invalid device");
        case CL_INVALID_CONTEXT: return strdup("Invalid context");
        case CL_INVALID_QUEUE_PROPERTIES: return strdup("Invalid queue properties");
        case CL_INVALID_COMMAND_QUEUE: return strdup("Invalid command queue");
        case CL_INVALID_HOST_PTR: return strdup("Invalid host pointer");

        case CL_INVALID_MEM_OBJECT: return strdup("Invalid memory object");
        case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR: return strdup("Invalid image format descriptor");
        case CL_INVALID_IMAGE_SIZE: return strdup("Invalid image size");
        case CL_INVALID_SAMPLER: return strdup("Invalid sampler");
        case CL_INVALID_BINARY: return strdup("Invalid binary");
        case CL_INVALID_BUILD_OPTIONS: return strdup("Invalid build options");
        case CL_INVALID_PROGRAM: return strdup("Invalid program");
        case CL_INVALID_PROGRAM_EXECUTABLE: return strdup("Invalid program executable");
        case CL_INVALID_KERNEL_NAME: return strdup("Invalid kernel name");
        case CL_INVALID_KERNEL_DEFINITION: return strdup("Invalid kernel definition");
        case CL_INVALID_KERNEL: return strdup("Invalid kernel");
        case CL_INVALID_ARG_INDEX: return strdup("Invalid argument index");
        case CL_INVALID_ARG_VALUE: return strdup("Invalid argument value");
        case CL_INVALID_ARG_SIZE: return strdup("Invalid argument size");
        case CL_INVALID_KERNEL_ARGS: return strdup("Invalid kernel arguments");
        case CL_INVALID_WORK_DIMENSION: return strdup("Invalid work dimension");
        case CL_INVALID_WORK_GROUP_SIZE: return strdup("Invalid work group size");
        case CL_INVALID_WORK_ITEM_SIZE: return strdup("Invalid work item size");
        case CL_INVALID_GLOBAL_OFFSET: return strdup("Invalid global offset");
        case CL_INVALID_EVENT_WAIT_LIST: return strdup("Invalid event wait list");
        case CL_INVALID_EVENT: return strdup("Invalid event");
        case CL_INVALID_OPERATION: return strdup("Invalid operation");
        case CL_INVALID_GL_OBJECT: return strdup("Invalid OpenGL object");
        case CL_INVALID_BUFFER_SIZE: return strdup("Invalid buffer size");
        case CL_INVALID_MIP_LEVEL: return strdup("Invalid mip-map level");
        default: return strdup("Unknown");
    }
}

int getDevID(char * desired, cl_device_id * devices, int numDevices) {
    char buff[128];
    int i;
    for (i = 0; i < numDevices; i++) {
    clGetDeviceInfo(devices[i], CL_DEVICE_NAME, 128, (void *)buff, NULL);
    //printf("%s\n", buff);
    if (strcmp(desired, buff) == 0) return i;
    }
    return -1;
}

void bubblesort(struct ungappedExtension *seqInfo, int nBegin, int nEnd)
{
	struct ungappedExtension temp;
	int i, j;
	for (i = nBegin; i < nEnd - 1; i++)
	{
		for (j = nBegin + 1; j < nEnd - (i - nBegin); j++)
		{
			if (seqInfo[j - 1].tid > seqInfo[j].tid)
			{
				temp = seqInfo[j - 1];
				seqInfo[j - 1] = seqInfo[j];
				seqInfo[j] = temp;
			}
		}
	}
}

int findStartLoc(struct ungappedExtension *ungappedExtensionsPtr, int threadNo, int itemNum)
{
	int i;

	for (i = 0; i < itemNum; i++)
	{
		if (ungappedExtensionsPtr[i].tid == threadNo)
		{
			return i;
		}
	}

	return -1;
}
void search_protein1hit(struct PSSMatrix PSSMatrix, 
						struct PSSMatrixFP PSSMatrixFP, 
						struct sequenceData* sequenceData, 
						cl_uint numSequences, 
						cl_uint tickFrequency)
{
	cl_uint sequenceCount = 0;
	cl_uint descriptionStart = 0, descriptionLength = 0, encodedLength;
	unsigned char *subject, *sequenceEnd, *address;
	cl_int subjectLength, subjectOffset, wordLengthMinusOne, count = 0;
	unsigned char currentWord, *currentBlock;
    struct group* currentGroup;
	//Shucai
	struct groupFP *currentGroupFP;
	unsigned char *startAddressFP = readdb_sequences;

	cl_ushort* queryOffsets, queryOffset;
	struct ungappedExtension* ungappedExtension;
    cl_int diagonal;

	//Shucai
    unsigned char** lastHit;
	cl_uint *lastHitFP;

    wordLengthMinusOne = parameters_wordSize - 1;

    while (sequenceCount < numSequences)
    {
    	descriptionLength = sequenceData[sequenceCount].descriptionLength;
    	descriptionStart = sequenceData[sequenceCount].descriptionStart;
    	subjectLength = sequenceData[sequenceCount].sequenceLength;
    	encodedLength = sequenceData[sequenceCount].encodedLength;
		address = subject = sequenceData[sequenceCount].sequence;
		
        // New sequence, new possible alignment
        alignments_currentAlignment = NULL;

        // Only process sequence if at least as long as the word length
        if (subjectLength >= parameters_wordSize)
        {
            // Start at 000 state in Finite State Automata
			//Shucai
            //currentGroup = wordLookupDFA_groups;
			currentGroupFP = wordLookupDFA_groupsFP;

            // Read first wordLength - 1 chars and advance
            count = wordLengthMinusOne;
            while (count > 0)
            {
                if (*address < wordLookupDFA_numCodes)
				{
					//Shucai
                    //currentGroup = currentGroup->nextGroups + *address;
					currentGroupFP = &wordLookupDFA_groupsFP[currentGroupFP->nextGroups + *address];
                }
				else
				{
					//Shucai
                    //currentGroup = currentGroup->nextGroups;
					currentGroupFP = &wordLookupDFA_groupsFP[currentGroupFP->nextGroups];
				}
                address++;
                count--;
            }

            // Read the rest of the codes, using the Finite State Automata defined
            // by wordLookupDFA to get the query positions of interest
            sequenceEnd = subject + subjectLength;
            while (address < sequenceEnd)
            {
				//Shucai
                //currentBlock = currentGroup->nextWords;
				currentBlock = &wordLookupDFA[currentGroupFP->nextWords];

                // If current code is a regular letter
                if (*address < wordLookupDFA_numCodes)
                {
                    // Use it
                    currentWord = currentBlock[*address];
					//Shucai
                    //currentGroup = currentGroup->nextGroups + *address;
					currentGroupFP = &wordLookupDFA_groupsFP[currentGroupFP->nextGroups + *address];
                }
                else
                {
                    // Else check if we've reached end of the file
                    if (address >= sequenceEnd)
                        break;

                    // If not, we've read a wild code. Use first code instead
                    currentWord = *currentBlock;
					//Shucai
                    //currentGroup = currentGroup->nextGroups;
					currentGroupFP = &wordLookupDFA_groupsFP[currentGroupFP->nextGroups];
                }

                if (currentWord)
                {
                    // Calculate subject offset
                    subjectOffset = address - subject;

                    // At least one query position, stored at an extenal address
                    queryOffsets = ((cl_ushort*)currentBlock) - currentWord;

                    // If the zero flag is stored at the first query position
                    if (!*queryOffsets)
                    {
                        // Go to an outside address for additional positions
                        queryOffsets = wordLookupDFA_additionalQueryPositions
                                     + (*(queryOffsets + 1) * constants_max_int2) + *(queryOffsets + 2);
                    }

                    do
                    {
                        queryOffset = *queryOffsets;

                        #ifndef NO_STAGE2
                        // Calculate the diagonal this hit is on
                        diagonal = subjectOffset - queryOffset;

                        // If we have not extended past this point on this diagonal
                        //Shucai
						//lastHit = hitMatrix_furthest + diagonal;
						lastHitFP = hitMatrix_furthestFP + diagonal;
						//Shucai
						//if (*lastHit < address)
                        if (*lastHitFP < address - startAddressFP)
                        {
                            // Increment tally number of extensions
                            blast_numUngappedExtensions++;

                            // If only one hit triggered this extension
							// Shucai
                            ungappedExtension
                            	//= ungappedExtension_oneHitExtend(PSSMatrix.matrix + queryOffset,
								= ungappedExtension_oneHitExtend(queryOffset,
                                    address, PSSMatrix, PSSMatrixFP, subject, startAddressFP);

                            // Update furthest reached value for the diagonal
							// Shucai
                            //*lastHit = ungappedExtension_subjectEndReached;
							*lastHitFP = ungappedExtension_subjectEndReachedFP;
                            // If extension scores high enough to trigger gapping
                            if (ungappedExtension)
                            {
                                // Increment count of number of trigger extensions
                                blast_numTriggerExtensions++;

                                // Create new alignment if needed
                                if (alignments_currentAlignment == NULL)
                                {
                                    // Create new alignment object
                                    alignments_createNew(descriptionStart, descriptionLength,
                                                         subject, subjectLength, encodedLength);
                                }

                                // Add this extension to the alignment
                                alignments_addUngappedExtension(ungappedExtension);
                            }
                        }
                        #endif

                        queryOffsets++; blast_numHits++;
                    }
                    while (*queryOffsets);
                }

                address++;
            }
        }

        sequenceCount++;

        // Every so often print status .
        if ((sequenceCount % tickFrequency == 0) &&
            parameters_outputType != parameters_xml && parameters_outputType != parameters_tabular)
        {
            #ifndef VERBOSE
            printf(".");
            fflush(stdout);
			#endif
        }
    }
}

// Search a protein database using 2-hit extension mode
void search_protein2hit(struct PSSMatrix PSSMatrix, struct PSSMatrixFP PSSMatrixFP, struct sequenceData* sequenceData, cl_uint numSequences, cl_uint tickFrequency)
{
	cl_uint sequenceCount = 0;
	cl_uint descriptionStart = 0, descriptionLength = 0, encodedLength;
	unsigned char *subject, *sequenceEnd;
	cl_int subjectLength, subjectOffset, wordLengthMinusOne, count = 0;
	unsigned char currentWord, *currentBlock, *address;
    struct group* currentGroup;
	//Shucai
	struct groupFP *currentGroupFP;
	unsigned char *startAddressFP = readdb_sequences;

	cl_ushort* queryOffsets, queryOffset;
	struct ungappedExtension* ungappedExtension;
    cl_int diagonal, distance;
	//Shucai
	cl_uint *lastHitFP;

    wordLengthMinusOne = parameters_wordSize - 1;
//wordLookupDFA_print();
    while (sequenceCount < numSequences)
    {
    	descriptionLength = sequenceData[sequenceCount].descriptionLength;
    	descriptionStart = sequenceData[sequenceCount].descriptionStart;
    	subjectLength = sequenceData[sequenceCount].sequenceLength;
    	encodedLength = sequenceData[sequenceCount].encodedLength;
		address = subject = sequenceData[sequenceCount].sequence;

        // New sequence, new possible alignment
        alignments_currentAlignment = NULL;

        // Only process sequence if at least as long as the word length
        if (subjectLength >= parameters_wordSize)
        {
            // Start at 000 state in Finite State Automata
			// Shucai
			currentGroupFP = wordLookupDFA_groupsFP;

            // Read first wordLength - 1 chars and advance
            count = wordLengthMinusOne;
            while (count > 0)
            {
                if (*address < wordLookupDFA_numCodes)
				{
					//Shucai
					currentGroupFP = &wordLookupDFA_groupsFP[currentGroupFP->nextGroups + *address];
                }
				else
				{
					//Shucai
					currentGroupFP = &wordLookupDFA_groupsFP[currentGroupFP->nextGroups];
				}

                address++;
                count--;
            }

            // Read the rest of the codes, using the Finite State Automata defined
            // by wordLookupDFA to get the query positions of cl_interest
            sequenceEnd = subject + subjectLength;
            while (address < sequenceEnd)
            {
				//Shucai
				currentBlock = &wordLookupDFA[currentGroupFP->nextWords];

                // If current code is a regular letter
                if (*address < wordLookupDFA_numCodes)
                {
                    // Use it
                    currentWord = currentBlock[*address];
					//Shucai
					currentGroupFP = &wordLookupDFA_groupsFP[currentGroupFP->nextGroups + *address];
                }
                else
                {
                    // Else check if we've reached end of the file
                    if (address >= sequenceEnd)
                        break;

                    // If not, we've read a wild code. Use first code instead
                    currentWord = currentBlock[0];
					//Shucai
					currentGroupFP = &wordLookupDFA_groupsFP[currentGroupFP->nextGroups];
                }

                if (currentWord)
                {
                    // Calculate subject offset
                    subjectOffset = address - subject;

                    // If at least one query position, stored at an extenal address
                    queryOffsets = ((cl_ushort*)currentBlock) - currentWord;

                    // If the zero flag is stored at the first query position
                    if (!*queryOffsets)
                    {
                        // Go to an outside address for additional positions
                        queryOffsets = wordLookupDFA_additionalQueryPositions
                                     + (*(queryOffsets + 1) * constants_max_int2) + *(queryOffsets + 2);
                    }

                    do
                    {
                        queryOffset = *queryOffsets;

                        #ifndef NO_STAGE2
                        // Calculate the diagonal this hit is on
                        diagonal = subjectOffset - queryOffset;

                        // Calculate distance since last hit
                        //Shucai
						lastHitFP = hitMatrix_furthestFP + diagonal;
                        //Shucai
						distance = (address - startAddressFP) - *lastHitFP;

						//Shucai


                        if (distance >= parameters_A)
                        {
                            // Too far apart, update furthest
							//Shucai
							*lastHitFP = address - startAddressFP;
                        }
                        else if (distance >= parameters_overlap)
                        {
                            // Not overlaping - extension triggered
                            // Increment tally number of extensions
                            blast_numUngappedExtensions++;

                            // Perform ungapped extension start between query/subject start/end
                            // and extending outwards in each direction
                            ungappedExtension
                            	= ungappedExtension_extend(queryOffset,
								//Shucai
                                     address, *lastHitFP, PSSMatrix, PSSMatrixFP, subject, startAddressFP);

                            // Update furthest reached value for the diagonal
                            //Shucai
							*lastHitFP = ungappedExtension_subjectEndReachedFP;

                            // If extension scores high enough to trigger gapping
                            if (ungappedExtension)
                            {
                                // Increment count of number of trigger extensions
                                blast_numTriggerExtensions++;

                                // Create new alignment if needed
                                if (alignments_currentAlignment == NULL)
                                {
                                    // Create new alignment object using subject with wilds
                                    alignments_createNew(descriptionStart, descriptionLength, subject,
                                                         subjectLength, encodedLength);
                                }

                                // Add this extension to the alignment
                                alignments_addUngappedExtension(ungappedExtension);
                            }
                        }
                        #endif

                        queryOffsets++; blast_numHits++;
                    }
                    while (*queryOffsets);
                }

                address++;
            }
        }

        sequenceCount++;

        // Every so often print status .
        if ((sequenceCount % tickFrequency == 0) &&
            parameters_outputType != parameters_xml && parameters_outputType != parameters_tabular)
        {
            #ifndef VERBOSE
            printf(".");
            fflush(stdout);
			#endif
        }
    }
}

// Search a nucleotide database using 1-hit extension mode
void search_nucleotide(struct PSSMatrix PSSMatrix, struct sequenceData* sequenceData,
                       cl_uint numSequences, cl_uint tickFrequency)
{
	cl_uint sequenceCount = 0, codeword;
	cl_uint descriptionStart = 0, descriptionLength = 0, encodedLength;
    cl_uint numPackedBytes, numRemaining;
	unsigned char *subject, *sequenceEnd, prevByte, nextByte, rightMatches, leftMatches;
    unsigned char previousCode, *subjectPosition, *address;
	cl_int subjectLength, subjectOffset, wordLengthMinusOne;
	struct ungappedExtension* ungappedExtension;
	cl_short *queryOffsets, tableEntry, queryOffset, singleQueryOffset[2];
    cl_int previousByteDistance;
    cl_int diagonal;
    unsigned char** lastHit;

    wordLengthMinusOne = parameters_wordSize - 1;
	previousByteDistance = (parameters_wordTableLetters + 4);
	singleQueryOffset[1] = 0;

    while (sequenceCount < numSequences)
    {
    	descriptionLength = sequenceData[sequenceCount].descriptionLength;
    	descriptionStart = sequenceData[sequenceCount].descriptionStart;
    	subjectLength = sequenceData[sequenceCount].sequenceLength;
    	encodedLength = sequenceData[sequenceCount].encodedLength;
		address = subject = sequenceData[sequenceCount].sequence;

        // New sequence, new possible alignment
        alignments_currentAlignment = NULL;

        // Determine number of packed bytes and number of remaining letters
        numPackedBytes = subjectLength / 4;
		numRemaining = subjectLength % 4;

        // Only process sequence if at least as long as the word length
        if (subjectLength >= parameters_wordSize)
        {
            sequenceEnd = subject + numPackedBytes;

            // Read first char and advance
            previousCode = *address;
            address++;

            // Traverse until end of sequence
            while (address < sequenceEnd)
            {
            	// Read next char and update codeword
            	codeword = *address | (previousCode << 8);
                previousCode = *address;
				tableEntry = nucleotideLookup[codeword];

                // Calculate subject offset
                subjectOffset = address - subject;

                #ifdef BITLOOKUP
				if (nucleotideLookup_bitLookup[codeword >> 5] &
                    (1 << (codeword & nucleotideLookup_mask)))
				#endif
                if (tableEntry)
                {
                	if (tableEntry > 0)
                    {
                    	// Just one query position
                    	singleQueryOffset[0] = queryOffset = tableEntry;
						queryOffsets = singleQueryOffset;
                    }
                    else
                    {
                        // Multiple query positions
						queryOffsets = (cl_short *)nucleotideLookup_additionalPositions - tableEntry;
                        queryOffset = *queryOffsets;
                    }

                    subjectPosition = address + 1;
                    do
                    {
                        // Calculate number of matches to left and right of hit
                        nextByte = PSSMatrix.bytePackedCodes[queryOffset];
                        rightMatches = PSSMatrix_packedRightMatches[nextByte ^ *subjectPosition];
                        prevByte = PSSMatrix.bytePackedCodes[queryOffset - previousByteDistance];
                        leftMatches = PSSMatrix_packedLeftMatches[prevByte ^ *(address - parameters_wordTableBytes)];

                        if (rightMatches + leftMatches >= parameters_wordExtraLetters)
                        {
                            #ifndef NO_STAGE2
                            // Calculate subject offset
                            subjectOffset = subjectPosition - subject;

                            // Calculate the diagonal this hit is on
                            diagonal = (subjectOffset * 4 - queryOffset + hitMatrix_queryLength)
                                     & hitMatrix_diagonalMask;

                            // If we have not extended past this point on this diagonal
                            lastHit = hitMatrix_furthest + diagonal;

                            #ifdef VERBOSE
                            if (parameters_verboseDloc == descriptionStart)
                                printf("Hit %d,%d\n", queryOffset, subjectOffset * 4);
                            #endif

                            if (*lastHit < address)
                            {
                                // Perform ungapped extension
                                ungappedExtension
                                	= ungappedExtension_nucleotideExtend(queryOffset,
                                      subjectOffset, PSSMatrix, subject, subjectLength);

                                // Update furthest reached value for the diagonal
                                *lastHit = ungappedExtension_subjectEndReached;

                                #ifdef VERBOSE
                                if (parameters_verboseDloc == descriptionStart)
                                    printf("UngappedExtension %d,%d Score=%d\n", queryOffset,
                                    subjectOffset * 4, ungappedExtension_bestScore);
                                if (parameters_verboseDloc == descriptionStart && ungappedExtension)
                                	ungappedExtension_print(ungappedExtension);
                                #endif

                                // If extension scores high enough to trigger gapping
                                if (ungappedExtension)
                                {
                                    // Increment count of number of trigger extensions
                                    blast_numTriggerExtensions++;

                                    // Create new alignment if needed
                                    if (alignments_currentAlignment == NULL)
                                    {
                                        alignments_createNew(descriptionStart, descriptionLength, subject,
                                                             subjectLength, encodedLength);
                                    }

                                    // Add this extension to the alignment
                                    alignments_addUngappedExtension(ungappedExtension);
                                }

                                blast_numUngappedExtensions++;
                            }
                            #endif
                            blast_numHits++;
                        }

                        queryOffsets++;
                        queryOffset = *queryOffsets;
					}
                    while (queryOffset);
                }

                address++;
            }
        }

        sequenceCount++;

        // Every so often print status .
        if ((sequenceCount % tickFrequency == 0) &&
            parameters_outputType != parameters_xml && parameters_outputType != parameters_tabular)
        {
            #ifndef VERBOSE
            printf(".");
            fflush(stdout);
            #endif
        }
    }
}

// Search a nucleotide database using 1-hit extension mode with large wordsize > 14
void search_nucleotide_longWord(struct PSSMatrix PSSMatrix, struct sequenceData* sequenceData,
                                cl_uint numSequences, cl_uint tickFrequency)
{
	cl_uint sequenceCount = 0, codeword;
	cl_uint descriptionStart = 0, descriptionLength = 0, encodedLength;
    cl_uint numPackedBytes, numRemaining;
	unsigned char *subject, *sequenceEnd, prevByte, nextByte, rightMatches, leftMatches;
    unsigned char previousCode, *subjectPosition, *address;
	cl_int subjectLength, subjectOffset, wordLengthMinusOne;
	struct ungappedExtension* ungappedExtension;
	cl_short *queryOffsets, tableEntry, queryOffset, singleQueryOffset[2];
    cl_int previousByteDistance;
    cl_int diagonal, extraBytesNeeded;
    unsigned char** lastHit;

    wordLengthMinusOne = parameters_wordSize - 1;
	previousByteDistance = parameters_wordTableLetters + parameters_wordExtraBytes * 4 + 4;
	singleQueryOffset[1] = 0;

    while (sequenceCount < numSequences)
    {
    	descriptionLength = sequenceData[sequenceCount].descriptionLength;
    	descriptionStart = sequenceData[sequenceCount].descriptionStart;
    	subjectLength = sequenceData[sequenceCount].sequenceLength;
    	encodedLength = sequenceData[sequenceCount].encodedLength;
		address = subject = sequenceData[sequenceCount].sequence;

        // Determine number of packed bytes and number of remaining letters
        numPackedBytes = subjectLength / 4;
		numRemaining = subjectLength % 4;

        // Only process sequence if at least as long as the word length
        if (subjectLength >= parameters_wordSize)
        {
            sequenceEnd = subject + numPackedBytes;

            // Read first char and advance
            previousCode = *address;
            address++;

            // Traverse until end of sequence
            while (address < sequenceEnd)
            {
            	// Read next char and update codeword
            	codeword = *address | (previousCode << 8);
                previousCode = *address;
				tableEntry = nucleotideLookup[codeword];

                // Calculate subject offset
                subjectOffset = address - subject;

                #ifdef BITLOOKUP
				if (nucleotideLookup_bitLookup[codeword >> 5] &
                    (1 << (codeword & nucleotideLookup_mask)))
				#endif
                if (tableEntry)
                {
                	if (tableEntry > 0)
                    {
                    	// Just one query position
                    	singleQueryOffset[0] = queryOffset = tableEntry;
						queryOffsets = singleQueryOffset;
                    }
                    else
                    {
                        // Multiple query positions
						queryOffsets = (cl_short *)nucleotideLookup_additionalPositions - tableEntry;
                        queryOffset = *queryOffsets;
                    }

                    subjectPosition = address + 1;
                    do
                    {
						extraBytesNeeded = parameters_wordExtraBytes;

						while (extraBytesNeeded)
						{
							// Check for matching bytes to right
                            if (*subjectPosition != PSSMatrix.bytePackedCodes[queryOffset])
                            	break;

//							printf("Match %d,%d\n", queryOffset, (subjectPosition - subject)*4);

                            extraBytesNeeded--;
                            subjectPosition++;
                            subjectOffset++;
                            queryOffset+=4;
						}

						if (!extraBytesNeeded)
						{
                            // Calculate number of matches to left and right of hit
                            nextByte = PSSMatrix.bytePackedCodes[queryOffset];
                            rightMatches = PSSMatrix_packedRightMatches[nextByte ^ *(subjectPosition)];
                            prevByte = PSSMatrix.bytePackedCodes[queryOffset - previousByteDistance];
                            leftMatches = PSSMatrix_packedLeftMatches[prevByte ^ *(address - parameters_wordTableBytes)];

//                            printf("prev at %d,%d\n", queryOffset - previousByteDistance);
//                        	printf("part matches=[%d,%d]\n", leftMatches, rightMatches);

                            if (rightMatches + leftMatches >= parameters_wordExtraLetters)
                            {
//                            	printf("Hit! dloc=%d\n", descriptionStart);
                                #ifndef NO_STAGE2
                                // Calculate subject offset
                                subjectOffset = subjectPosition - subject;

                                // Calculate the diagonal this hit is on
                                diagonal = (subjectOffset * 4 - queryOffset + hitMatrix_queryLength)
                                        & hitMatrix_diagonalMask;

                                // If we have not extended past this point on this diagonal
                                lastHit = hitMatrix_furthest + diagonal;

                                #ifdef VERBOSE
                                if (parameters_verboseDloc == descriptionStart)
                                    printf("Hit %d,%d\n", queryOffset, subjectOffset * 4);
                                #endif
                                if (*lastHit < address)
                                {
                                    // Perform ungapped extension
                                    ungappedExtension
                                        = ungappedExtension_nucleotideExtend(queryOffset,
                                        subjectOffset, PSSMatrix, subject, subjectLength);

                                    // Update furthest reached value for the diagonal
                                    *lastHit = ungappedExtension_subjectEndReached;

                                    #ifdef VERBOSE
                                    if (parameters_verboseDloc == descriptionStart)
                                        printf("UngappedExtension %d,%d Score=%d\n", queryOffset,
                                        subjectOffset * 4, ungappedExtension_bestScore);
                                    if (parameters_verboseDloc == descriptionStart && ungappedExtension)
                                        ungappedExtension_print(ungappedExtension);
                                    #endif

                                    // If extension scores high enough to trigger gapping
                                    if (ungappedExtension)
                                    {
                                        // Increment count of number of trigger extensions
                                        blast_numTriggerExtensions++;

                                        // Create new alignment if needed
                                        if (alignments_currentAlignment == NULL)
                                        {
                                            alignments_createNew(descriptionStart, descriptionLength, subject,
                                                                subjectLength, encodedLength);
                                        }

                                        // Add this extension to the alignment
                                        alignments_addUngappedExtension(ungappedExtension);
                                    }

                                    blast_numUngappedExtensions++;
                                }
                                #endif
                                blast_numHits++;
                            }
                        }

                        queryOffsets++;
                        queryOffset = *queryOffsets;
					}
                    while (queryOffset);
                }

                address++;
            }
        }

        sequenceCount++;

        // Every so often print status .
        if ((sequenceCount % tickFrequency == 0) &&
            parameters_outputType != parameters_xml && parameters_outputType != parameters_tabular)
        {
            #ifndef VERBOSE
            printf(".");
            fflush(stdout);
            #endif
        }

        // New sequence, new possible alignment
        alignments_currentAlignment = NULL;
    }
}

// Search a nucleotide database using 1-hit extension mode, using a large word lookup table
// due to long query sequence
void search_nucleotide_largeTable(struct PSSMatrix PSSMatrix, struct sequenceData* sequenceData,
                                  cl_uint numSequences, cl_uint tickFrequency)
{
	cl_uint sequenceCount = 0, codeword;
	cl_uint descriptionStart = 0, descriptionLength = 0, encodedLength;
    cl_uint numPackedBytes, numRemaining;
	unsigned char *subject, *sequenceEnd, prevByte, nextByte, rightMatches, leftMatches;
    unsigned char previousCode, *subjectPosition, *address;
	cl_int subjectLength, subjectOffset, wordLengthMinusOne;
	struct ungappedExtension* ungappedExtension;
	cl_int *queryOffsets, tableEntry, queryOffset, singleQueryOffset[2];
    cl_int previousByteDistance;
    cl_int diagonal;
    unsigned char** lastHit;

    wordLengthMinusOne = parameters_wordSize - 1;
	previousByteDistance = (parameters_wordTableLetters + 4);
	singleQueryOffset[1] = 0;

    while (sequenceCount < numSequences)
    {
    	descriptionLength = sequenceData[sequenceCount].descriptionLength;
    	descriptionStart = sequenceData[sequenceCount].descriptionStart;
    	subjectLength = sequenceData[sequenceCount].sequenceLength;
    	encodedLength = sequenceData[sequenceCount].encodedLength;
		address = subject = sequenceData[sequenceCount].sequence;

        // New sequence, new possible alignment
        alignments_currentAlignment = NULL;

        // Determine number of packed bytes and number of remaining letters
        numPackedBytes = subjectLength / 4;
		numRemaining = subjectLength % 4;

        // Only process sequence if at least as long as the word length
        if (subjectLength >= parameters_wordSize)
        {
            sequenceEnd = subject + numPackedBytes;

            // Read first char and advance
            previousCode = *address;
            address++;

            // Traverse until end of sequence
            while (address < sequenceEnd)
            {
            	// Read next char and update codeword
            	codeword = *address | (previousCode << 8);
                previousCode = *address;
				tableEntry = nucleotideLookup_large[codeword];

                // Calculate subject offset
                subjectOffset = address - subject;

                #ifdef BITLOOKUP
				if (nucleotideLookup_bitLookup[codeword >> 5] &
                    (1 << (codeword & nucleotideLookup_mask)))
				#endif
                if (tableEntry)
                {
                	if (tableEntry > 0)
                    {
                    	// Just one query position
                    	singleQueryOffset[0] = queryOffset = tableEntry;
						queryOffsets = singleQueryOffset;
                    }
                    else
                    {
                        // Multiple query positions
						queryOffsets = (cl_int *)nucleotideLookup_additionalPositions_large - tableEntry;
                        queryOffset = *queryOffsets;
                    }

                    subjectPosition = address + 1;
                    do
                    {
                        // Calculate number of matches to left and right of hit
                        nextByte = PSSMatrix.bytePackedCodes[queryOffset];
                        rightMatches = PSSMatrix_packedRightMatches[nextByte ^ *subjectPosition];
                        prevByte = PSSMatrix.bytePackedCodes[queryOffset - previousByteDistance];
                        leftMatches = PSSMatrix_packedLeftMatches[prevByte ^ *(address - parameters_wordTableBytes)];

                        if (rightMatches + leftMatches >= parameters_wordExtraLetters)
                        {
                            #ifndef NO_STAGE2
                            // Calculate subject offset
                            subjectOffset = subjectPosition - subject;

                            // Calculate the diagonal this hit is on
                            diagonal = (subjectOffset * 4 - queryOffset + hitMatrix_queryLength)
                                     & hitMatrix_diagonalMask;

                            // If we have not extended past this point on this diagonal
                            lastHit = hitMatrix_furthest + diagonal;

                            #ifdef VERBOSE
                            if (parameters_verboseDloc == descriptionStart)
                                printf("Hit %d,%d\n", queryOffset, subjectOffset * 4);
                            #endif
                            if (*lastHit < address)
                            {
                                // Perform ungapped extension
                                ungappedExtension
                                	= ungappedExtension_nucleotideExtend(queryOffset,
                                      subjectOffset, PSSMatrix, subject, subjectLength);

                                // Update furthest reached value for the diagonal
                                *lastHit = ungappedExtension_subjectEndReached;

                                #ifdef VERBOSE
                                if (parameters_verboseDloc == descriptionStart)
                                    printf("UngappedExtension %d,%d Score=%d\n", queryOffset,
                                    subjectOffset * 4, ungappedExtension_bestScore);
                                if (parameters_verboseDloc == descriptionStart && ungappedExtension)
                                	ungappedExtension_print(ungappedExtension);
                                #endif

                                // If extension scores high enough to trigger gapping
                                if (ungappedExtension)
                                {
                                    // Increment count of number of trigger extensions
                                    blast_numTriggerExtensions++;

                                    // Create new alignment if needed
                                    if (alignments_currentAlignment == NULL)
                                    {
                                        alignments_createNew(descriptionStart, descriptionLength, subject,
                                                             subjectLength, encodedLength);
                                    }

                                    // Add this extension to the alignment
                                    alignments_addUngappedExtension(ungappedExtension);
                                }

                                blast_numUngappedExtensions++;
                            }
                            #endif
                            blast_numHits++;
                        }

                        queryOffsets++;
                        queryOffset = *queryOffsets;
					}
                    while (queryOffset);
                }

                address++;
            }
        }

        sequenceCount++;

        // Every so often print status .
        if ((sequenceCount % tickFrequency == 0) &&
            parameters_outputType != parameters_xml && parameters_outputType != parameters_tabular)
        {
            #ifndef VERBOSE
            printf(".");
            fflush(stdout);
            #endif
        }
    }
}
// SSearch a protein database using Smith-Waterman algorithm
void search_proteinSsearch(struct PSSMatrix PSSMatrix, struct sequenceData* sequenceData,
                           cl_uint numSequences, cl_uint tickFrequency)
{
	cl_uint sequenceCount = 0;
	cl_uint descriptionStart = 0, descriptionLength = 0, encodedLength;
	unsigned char *subject, *address;
	cl_int subjectLength;
	struct gappedExtension* gappedExtension;
	struct dpResults dpResults, reverseDpResults;

    while (sequenceCount < numSequences)
    {
    	descriptionLength = sequenceData[sequenceCount].descriptionLength;
    	descriptionStart = sequenceData[sequenceCount].descriptionStart;
    	subjectLength = sequenceData[sequenceCount].sequenceLength;
    	encodedLength = sequenceData[sequenceCount].encodedLength;
		address = subject = sequenceData[sequenceCount].sequence;

        // New sequence, new possible alignment
        alignments_currentAlignment = NULL;

        // Perform SW score only
		dpResults = smithWatermanScoring_score(PSSMatrix, subjectLength, subject);

//        printf("%d\n", dpResults.bestScore);

        // If above e-value cutoff
		if (dpResults.bestScore >= blast_gappedNominalCutoff
            && alignments_isFinalAlignment(dpResults.bestScore))
		{
            // Perform SW alignment in the reverse direction, to find the start of the optimal alignment
        	reverseDpResults = smithWatermanScoring_scoreReverse(PSSMatrix, subjectLength, subject,
                                                                 dpResults.best);

            // Collect traceback information and store alignment
			gappedExtension = smithWatermanTraceback_build(PSSMatrix, subjectLength, subject,
                                                           reverseDpResults.best, dpResults.best);

            if (reverseDpResults.bestScore != dpResults.bestScore ||
                dpResults.bestScore != gappedExtension->nominalScore)
            {
                fprintf(stderr, "Error: Forward and reverse Smith-Waterman alignment scores do not match\n");
                exit(-1);
            }

            gappedExtension_score(gappedExtension);

			alignments_createNew(descriptionStart, descriptionLength, subject, subjectLength, encodedLength);
			alignments_addGappedExtension(alignments_currentAlignment, gappedExtension);
			alignments_addFinalAlignment(dpResults.bestScore, alignments_currentAlignment);
		}

        // Advance to next sequence
        address += encodedLength - 1;

        sequenceCount++;

        // Every so often print status .
        if ((sequenceCount % tickFrequency == 0) &&
            parameters_outputType != parameters_xml && parameters_outputType != parameters_tabular)
        {
            #ifndef VERBOSE
            printf(".");
            fflush(stdout);
			#endif
        }
    }

	// Sort the final alignments by refined score
	alignments_sortFinalAlignments();
}

// Ssearch a nucleotide database using Smith-waterman algorithm
void search_nucleotideSsearch(struct PSSMatrix PSSMatrix, struct sequenceData* sequenceData,
                              cl_uint numSequences, cl_uint tickFrequency)
{
	cl_uint sequenceCount = 0;
	cl_uint descriptionStart = 0, descriptionLength = 0, encodedLength;
	unsigned char *subject, *unpackedSubject, *address;
	cl_int subjectLength;
	struct gappedExtension* gappedExtension;
	struct dpResults dpResults, reverseDpResults;

    while (sequenceCount < numSequences)
    {
    	descriptionLength = sequenceData[sequenceCount].descriptionLength;
    	descriptionStart = sequenceData[sequenceCount].descriptionStart;
    	subjectLength = sequenceData[sequenceCount].sequenceLength;
    	encodedLength = sequenceData[sequenceCount].encodedLength;
		address = subject = sequenceData[sequenceCount].sequence;

        // New sequence, new possible alignment
        alignments_currentAlignment = NULL;

        // Unpack the subject
        unpackedSubject = encoding_byteUnpack(subject, subjectLength);

        // Re-insert wildcards
        encoding_insertWilds(unpackedSubject, subject + ((subjectLength + 3) / 4),
                             subject + encodedLength);

		// Perform SW score only
		dpResults = smithWatermanScoring_score(PSSMatrix, subjectLength, unpackedSubject);

        // If above e-value cutoff
		if (dpResults.bestScore >= blast_gappedNominalCutoff
            && alignments_isFinalAlignment(dpResults.bestScore))
		{
        	// Perform SW alignment in the reverse direction, to find the start of the optimal alignment
        	reverseDpResults = smithWatermanScoring_scoreReverse(PSSMatrix, subjectLength,
                               unpackedSubject, dpResults.best);

            // Collect traceback information and store alignment
			gappedExtension = smithWatermanTraceback_build(PSSMatrix, subjectLength, unpackedSubject,
                                                           reverseDpResults.best, dpResults.best);

            if (reverseDpResults.bestScore != dpResults.bestScore ||
                dpResults.bestScore != gappedExtension->nominalScore)
            {
                fprintf(stderr, "Error: Forward and reverse Smith-Waterman alignment scores do not match\n");
                fprintf(stderr, "Forward Score=%d End=%d,%d\n", dpResults.bestScore,
                        dpResults.best.queryOffset, dpResults.best.subjectOffset);
                fprintf(stderr, "Reverse Score=%d End=%d,%d\n", reverseDpResults.bestScore,
                        reverseDpResults.best.queryOffset, reverseDpResults.best.subjectOffset);
                fprintf(stderr, "Traceback Score=%d End=%d,%d\n", gappedExtension->nominalScore,
                        gappedExtension->queryEnd, gappedExtension->subjectEnd);
//                exit(-1);
            }

            gappedExtension_score(gappedExtension);

			alignments_createNew(descriptionStart, descriptionLength, unpackedSubject, subjectLength, 0);
			alignments_addGappedExtension(alignments_currentAlignment, gappedExtension);
			alignments_addFinalAlignment(dpResults.bestScore, alignments_currentAlignment);
		}
		else
        {
        	free(unpackedSubject);
        }

        sequenceCount++;

        // Every so often print status .
        if ((sequenceCount % tickFrequency == 0) &&
            parameters_outputType != parameters_xml && parameters_outputType != parameters_tabular)
        {
            #ifndef VERBOSE
            printf(".");
            fflush(stdout);
            #endif
        }
    }
}


// Shucai
// Search a protein database using 1-hit extension mode
void search_protein1hitParallel(struct scoreMatrix *scoreMatrixp, 
								struct PSSMatrixFP PSSMatrixFP, 
								struct sequenceData* sequenceData, 
								cl_uint numSequences, 
								cl_uint tickFrequency)
{
	//Shucai
	cl_uint i, j, sequenceCount = 0;
	cl_uint nRoundOffset;

	//PSSMatrix pointers
	struct PSSMatrixFP *PSSMatrixFPD;
	cl_short *matrixBodyD;
	
	//Input database sequence
	struct sequenceDataFP *sequenceDataFP;
	struct sequenceDataFP *sequenceDataFPD;
	unsigned char *sequencesD;
	unsigned char *roundStartAddress;

	//ungapped extension
	struct ungappedExtension *ungappedExtensionsD;
	struct ungappedExtension *ungappedExtension;
	struct ungappedExtension *ungappedExtensionCur, *newUngappedExtension;

	//ungapped extension numbers
	cl_uint *blast_numUngappedExtensionsD, *blast_numUngappedExtensionsH;
	cl_uint *blast_numTriggerExtensionsD, *blast_numTriggerExtensionsH;
	cl_uint *blast_numHitsD, *blast_numHitsH;
	cl_uint *hitMatrix_furthestD;
	cl_uint *hitMatrix_offsetH;
	cl_uint *hitMatrix_offsetD;
	cl_uint preSequenceCount;

	//For time record
	struct timeval t0, t1, t2, t3, t4, t5, t6, t7, t8;

	cl_int wordNum, groupNum;

	//parameters
	struct parameters strParameters;
	struct parameters *parametersD;

	//word lookup table
	struct groupFP *wordLookupDFA_groupD;
	unsigned char *wordLookupDFAD;
	cl_uint wordLookupDFA_size;
	
	//grid and block dimensions
	int nBlockNum = parameters_blockNum;
	int nBlockSize = parameters_threadNum;
	int nTotalThreadNum = nBlockNum * nBlockSize;
	size_t WorkSize = nTotalThreadNum; //Remember that the work is 1-dimensional
//	dim3 dimGrid(nBlockNum, 1);
	size_t LocalSize = nBlockSize; //Again, remember that the work is 1-dimensional
//	dim3 dimBlock(nBlockSize, 1);
	cl_uint zeroPtr = 0;

	cl_int errorCode = CL_SUCCESS;
    cl_uint num_platforms = 0, num_devices = 0, temp_uint, temp_ucl_short;
    if (clGetPlatformIDs(0, NULL, &num_platforms) != CL_SUCCESS) printf("Failed to query platform count!\n");
    printf("Number of Platforms: %d\n", num_platforms);

    cl_platform_id * platforms = (cl_platform_id *) malloc(sizeof(cl_platform_id) * num_platforms);

    if (clGetPlatformIDs(num_platforms, &platforms[0], NULL) != CL_SUCCESS) printf("Failed to get platform IDs\n");

    for (i = 0; i < num_platforms; i++) {
    temp_uint = 0;
        if(clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &temp_uint) != CL_SUCCESS) printf("Failed to query device count on platform %d!\n", i);
    num_devices += temp_uint;
    }
    printf("Number of Devices: %d\n", num_devices);

    cl_device_id * devices = (cl_device_id *) malloc(sizeof(cl_device_id) * num_devices);
    temp_uint = 0;
    for ( i = 0; i < num_platforms; i++) {
        if(clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, num_devices, &devices[temp_uint], &temp_ucl_short) != CL_SUCCESS) printf ("Failed to query device IDs on platform %d!\n", i);
        temp_uint += temp_ucl_short;
        temp_ucl_short = 0;
    }

 cl_context context = clCreateContextFromType(NULL, CL_DEVICE_TYPE_ALL, NULL, NULL, &errorCode);
    if (errorCode != CL_SUCCESS) {
        printf("failed to create GPU context! %s\n", print_cl_errstring(errorCode));
        errorCode = CL_SUCCESS;
    }

int devID = getDevID("AMD Athlon(tm) 64 X2 Dual Core Processor 6000+", devices, num_devices);
devID = devID == -1 ? 0 : devID;

    //create command-queue for the GPU
    cl_command_queue commandQueue = clCreateCommandQueue(context, devices[devID], 0, &errorCode);
    if (errorCode != CL_SUCCESS) {
        printf("failed to create commande queue! %s\n", print_cl_errstring(errorCode));
        errorCode = CL_SUCCESS;
    }

//read OpenCL kernel source
        FILE *kernelFile = fopen("src/search.cl", "r");
        struct stat st;
        fstat(fileno(kernelFile), &st);
        char *kernelSource = (char*) calloc(st.st_size + 1, sizeof (char));
        fread(kernelSource, sizeof (char), st.st_size, kernelFile);
        fclose(kernelFile);


        //build gpu kernel
	cl_program program;
	cl_kernel search_protein1hitKernel, memSet;
        program = clCreateProgramWithSource(context, 1, (const char **) & kernelSource, NULL, &errorCode);
        if (errorCode != CL_SUCCESS) {
            printf("failed to create OpenCL program!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
const char* options = "-g";

        errorCode = clBuildProgram(program, 0, NULL, options, NULL, NULL);
        if (errorCode != CL_SUCCESS) {
            printf("failed to build OpenCL program!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
	search_protein1hitKernel = clCreateKernel(program, "search_protein1hitKernel", &errorCode);
        if (errorCode != CL_SUCCESS) {
            printf("failed to create search_protein1hitParallel kernel!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
	memSet = clCreateKernel(program, "memSet", &errorCode);
        if (errorCode != CL_SUCCESS) {
            printf("failed to create memSet kernel!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }

	//get t0
	gettimeofday(&t0, NULL);

	wordNum = wordLookupDFA_numWords;
	groupNum = wordLookupDFA_numGroups;

	//Allocate GPU buffer for PSSMatrix
	PSSMatrixFPD = (struct PSSMatrixFP *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(struct PSSMatrixFP), NULL, &errorCode);
//	cudaMalloc((void **)&PSSMatrixFPD, sizeof(struct PSSMatrixFP));
	matrixBodyD = (cl_short *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_short) * (PSSMatrixFP.length + 2) * encoding_numCodes, NULL, &errorCode);
//	cudaMalloc((void **)&matrixBodyD, sizeof(cl_short) * (PSSMatrixFP.length + 2) * encoding_numCodes);

	//Copy PSSMatrix to device memory
	clEnqueueWriteBuffer(commandQueue, (cl_mem)PSSMatrixFPD, CL_TRUE, 0, sizeof(struct PSSMatrixFP), (void *)&PSSMatrixFP, 0 , NULL, NULL);
//	cudaMemcpy(PSSMatrixFPD, &PSSMatrixFP, sizeof(struct PSSMatrixFP), cudaMemcpyHostToDevice);
//Think this is the correct way to do the next function call
	clEnqueueWriteBuffer(commandQueue, (cl_mem)matrixBodyD,
	CL_TRUE, 0, sizeof(cl_short) * (PSSMatrixFP.length + 2) * encoding_numCodes, (PSSMatrixFP.matrix - encoding_numCodes), 0, NULL, NULL);
//__kernel void memSet(float value, __global float *mem) {
//    mem[get_global_id(0)] = value;
//}
//	cudaMemcpy(matrixBodyD, (PSSMatrixFP.matrix - encoding_numCodes), 
//	sizeof(cl_short) * (PSSMatrixFP.length + 2) * encoding_numCodes, cudaMemcpyHostToDevice);

	//Each thread is for align of one database sequence
	sequenceDataFP = (struct sequenceDataFP *)global_malloc(numSequences * sizeof(struct sequenceDataFP));
	sequenceDataFPD = (struct sequenceDataFP *)clCreateBuffer(context, CL_MEM_READ_WRITE, numSequences * sizeof(struct sequenceDataFP), NULL, &errorCode);
//	cudaMalloc((void **)&sequenceDataFPD, numSequences * sizeof(struct sequenceDataFP));

	//Allocate buffer for hit matrix offset
	hitMatrix_offsetH = (cl_uint *)global_malloc((nTotalThreadNum + 1) * sizeof(cl_uint));
	hitMatrix_offsetD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint) * (nTotalThreadNum + 1), NULL, &errorCode);
//	cudaMalloc((void **)&hitMatrix_offsetD, (nTotalThreadNum + 1) * sizeof(cl_uint));

	//Allocate ungapped extension buffer on device
	cl_int nUngappedExtensionNum = UNGAPEXT_PER_THREAD * nTotalThreadNum;

	ungappedExtensionsD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(struct ungappedExtension) * nUngappedExtensionNum, NULL, &errorCode);
//	cudaMalloc((void **)&ungappedExtensionsD, nUngappedExtensionNum * sizeof(struct ungappedExtension));
	ungappedExtension = (struct ungappedExtension *)global_malloc(nUngappedExtensionNum * sizeof(struct ungappedExtension));
	
	//Allocate numbers for ungapped extensions
	blast_numUngappedExtensionsH = (cl_uint *)global_malloc(sizeof(cl_uint) * nTotalThreadNum);
	blast_numTriggerExtensionsH = (cl_uint *)global_malloc(sizeof(cl_uint) * nTotalThreadNum);
	blast_numHitsH = (cl_uint *)global_malloc(sizeof(cl_uint) * nTotalThreadNum);

	blast_numUngappedExtensionsD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint) * nTotalThreadNum, NULL, &errorCode);
//	cudaMalloc((void **)&blast_numUngappedExtensionsD, sizeof(cl_uint) * nTotalThreadNum);
	blast_numTriggerExtensionsD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint) * nTotalThreadNum, NULL, &errorCode);
//	cudaMalloc((void **)&blast_numTriggerExtensionsD, sizeof(cl_uint) * nTotalThreadNum);
	blast_numHitsD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint) * nTotalThreadNum, NULL, &errorCode);
//	cudaMalloc((void **)&blast_numHitsD, sizeof(cl_uint) * nTotalThreadNum);

	//Allocate device memory, about 132Mbytes (according to texture limit)
	sequencesD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(unsigned char) * 132000000, NULL, &errorCode);
//	cudaMalloc((void **)&sequencesD, sizeof(unsigned char) * 132000000);

	//Allocate parameters buffer on device
	parametersD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(struct parameters), NULL, &errorCode);
//	cudaMalloc((void **)&parametersD, sizeof(struct parameters));
	strParameters.parameters_wordSize = parameters_wordSize;
	strParameters.encoding_numCodes = encoding_numCodes;
	strParameters.wordLookupDFA_numCodes = wordLookupDFA_numCodes;
	strParameters.additionalQueryPositionOffset = wordNum * sizeof(char) + sizeof(cl_short) * wordLookupDFA_numExtPositions;
	strParameters.blast_ungappedNominalTrigger = blast_ungappedNominalTrigger;
	strParameters.statistics_ungappedNominalDropoff = statistics_ungappedNominalDropoff;
	clEnqueueWriteBuffer(commandQueue, (cl_mem) parametersD, CL_TRUE, 0, sizeof(struct parameters), &strParameters, 0, NULL, NULL);
//	cudaMemcpy(parametersD, &strParameters, sizeof(struct parameters), cudaMemcpyHostToDevice);

	//Allocate word lookup table
	wordLookupDFA_size = sizeof(char) * wordNum + 2 * sizeof(cl_short) * wordLookupDFA_numExtPositions;
	wordLookupDFA_groupD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(struct groupFP) * groupNum, NULL, &errorCode);
//	cudaMalloc((void **)&wordLookupDFA_groupD, sizeof(struct groupFP) * groupNum);

	wordLookupDFAD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, wordLookupDFA_size, NULL, &errorCode);
//	cudaMalloc((void **)&wordLookupDFAD, wordLookupDFA_size);

//Does this really work here?
//If it doesn't, we can enqueue a cheap memset kernel, as follows...
//__kernel void memSet(float value, __global float *mem) {
//    mem[get_global_id(0)] = value;
//}

// Paul - TODO make sure passing it a null pointer to the host side buffer doesn't cause SEGfaults, (itshould actually)
//Send gabriel an update as to whether or not this technique works or if a kernel invocation would be necessary
zeroPtr=0;
	clSetKernelArg(memSet, 0, sizeof(cl_uint), (void *)&zeroPtr);
	clSetKernelArg(memSet, 1, sizeof(cl_mem), (void *)&blast_numUngappedExtensionsD);
	clEnqueueNDRangeKernel(commandQueue, memSet, 1, NULL, &nTotalThreadNum, NULL, 0, NULL, NULL);
//	cudaMemset(blast_numUngappedExtensionsD, 0, sizeof(cl_uint) * nTotalThreadNum);
clFinish(commandQueue);
zeroPtr=0;
	clSetKernelArg(memSet, 0, sizeof(cl_uint), (void *)&zeroPtr);
	clSetKernelArg(memSet, 1, sizeof(cl_mem), (void *)&blast_numHitsD);
	clEnqueueNDRangeKernel(commandQueue, memSet, 1, NULL, &nTotalThreadNum, NULL, 0, NULL, NULL);
//	cudaMemset(blast_numHitsD, 0, sizeof(cl_uint) * nTotalThreadNum);
clFinish(commandQueue);
	
	clEnqueueWriteBuffer(commandQueue, (cl_mem)wordLookupDFA_groupD, CL_TRUE, 0, sizeof(struct groupFP) * groupNum, wordLookupDFA_groupsFP, 0, NULL, NULL);
//	cudaMemcpy(wordLookupDFA_groupD, wordLookupDFA_groupsFP, sizeof(struct groupFP) * groupNum, cudaMemcpyHostToDevice);
//	//Use constant memory for the word lookup table group
//Looks like this is deprecated in the kernel
//	cudaMemcpyToSymbol(wordLookupDFA_groupsC, wordLookupDFA_groupsFP, sizeof(struct groupFP) * groupNum);
//
//	//Use constant memory to store score matrix
//	int scoreMatrixSize = encoding_numCodes * encoding_numCodes;
//	cudaMemcpyToSymbol(scoreMatrixC, 
//					  ((char *)scoreMatrixp->matrix) + sizeof(cl_short *) * encoding_numCodes, 
//					  sizeof(cl_short) * scoreMatrixSize);

	//Use constant memory to store query sequence
	unsigned char *tempQueryCode;
	tempQueryCode = (unsigned char *)global_malloc(sizeof(unsigned char) * (PSSMatrixFP.length + 2));
	memcpy(&tempQueryCode[1], PSSMatrixFP.queryCodes, sizeof(unsigned char) * PSSMatrixFP.length);
	tempQueryCode[0] = encoding_sentinalCode;
	tempQueryCode[PSSMatrixFP.length + 1] = encoding_sentinalCode;
//	clEnqueueWriteBuffer(commandQueue, (cl_mem)querySequenceC, CL_TRUE, 0, sizeof(unsigned char) * (PSSMatrixFP.length + 2), tempQueryCode, 0, NULL, NULL);
//	cudaMemcpyToSymbol(querySequenceC, tempQueryCode, sizeof(unsigned char) * (PSSMatrixFP.length + 2));
	free(tempQueryCode);

	clEnqueueWriteBuffer(commandQueue, (cl_mem)wordLookupDFAD, CL_TRUE, 0, wordLookupDFA_size, wordLookupDFA, 0, NULL, NULL);
//	cudaMemcpy(wordLookupDFAD, wordLookupDFA, wordLookupDFA_size, cudaMemcpyHostToDevice);
	cl_uint iniVal = nTotalThreadNum;
	
	//get t1
	gettimeofday(&t1, NULL);
	cl_int numSequencesRound, numSequenceProcessed;
	numSequenceProcessed = 0;
	while (sequenceCount < numSequences)
	{
		//get t2
		gettimeofday(&t2, NULL);

		memset(hitMatrix_offsetH, 0, sizeof(cl_int) * (nTotalThreadNum + 1));
		roundStartAddress = sequenceData[sequenceCount].sequence - 1;
		for (i = 0; sequenceCount < numSequences; i++, sequenceCount++)
		{	
			sequenceDataFP[i].descriptionLength = sequenceData[sequenceCount].descriptionLength;
			sequenceDataFP[i].descriptionStart = sequenceData[sequenceCount].descriptionStart;
			sequenceDataFP[i].sequenceLength = sequenceData[sequenceCount].sequenceLength;
			sequenceDataFP[i].encodedLength = sequenceData[sequenceCount].encodedLength;
			sequenceDataFP[i].offset = sequenceData[sequenceCount].sequence - roundStartAddress;
//printf("boop %x %d %d %d %d %d\n", &sequenceDataFP[i], sequenceDataFP[i].descriptionLength, sequenceDataFP[i].descriptionStart, sequenceDataFP[i].sequenceLength, sequenceDataFP[i].encodedLength, sequenceDataFP[i].offset);
			
			//Calculate the longest sequence size aligned by the current thread
			if (sequenceDataFP[i].sequenceLength > hitMatrix_offsetH[(i % nTotalThreadNum) + 1])
			{
				hitMatrix_offsetH[(i % nTotalThreadNum) + 1] = sequenceDataFP[i].sequenceLength;
			}
			
			//about 130MB
			if (sequenceDataFP[i].offset + sequenceData[sequenceCount].encodedLength > 130000000)
			{
				i++;
				sequenceCount++;
				break;
			}
		}
		nRoundOffset = sequenceDataFP[i - 1].offset + sequenceDataFP[i - 1].encodedLength;
		numSequencesRound = i;

		//Calculate the offset of each thread
		for (i = 1; i < nTotalThreadNum + 1; i++)
		{
			hitMatrix_offsetH[i] += hitMatrix_offsetH[i - 1] + (PSSMatrixFP.length - parameters_wordSize + 1);
		}
		
		//copy offset info to device
		clEnqueueWriteBuffer(commandQueue, (cl_mem)hitMatrix_offsetD, CL_TRUE, 0, sizeof(cl_int) * (nTotalThreadNum + 1), hitMatrix_offsetD, 0, NULL, NULL);
//	
//		cudaMemcpy(hitMatrix_offsetD, 
//				   hitMatrix_offsetH, 
//				   (nTotalThreadNum + 1) * sizeof(cl_int), 
//				   cudaMemcpyHostToDevice);

		//get t3
		gettimeofday(&t3, NULL);

		//Allocate device memory
//		cudaMalloc((void **)&sequencesD, sizeof(unsigned char) * (nRoundOffset + 2));
		sequencesD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(unsigned char) * (nRoundOffset + 2), NULL, &errorCode);

		//Allocate diagonal buffers
		int nElemNum = hitMatrix_offsetH[nTotalThreadNum];

		hitMatrix_furthestD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint) * nElemNum, NULL, &errorCode);
//		cudaMalloc((void **)&hitMatrix_furthestD, sizeof(cl_uint) * nElemNum);
zeroPtr=0;
	clSetKernelArg(memSet, 0, sizeof(cl_uint), (void *)&zeroPtr);
	clSetKernelArg(memSet, 1, sizeof(cl_mem), (void *)&hitMatrix_furthestD);
	clEnqueueNDRangeKernel(commandQueue, memSet, 1, NULL, &nElemNum, NULL, 0, NULL, NULL);
clFinish(commandQueue);
//		cudaMemset(hitMatrix_furthestD, 0, sizeof(cl_uint) * nElemNum);
zeroPtr=0;
	clSetKernelArg(memSet, 0, sizeof(cl_uint), (void *)&zeroPtr);
	clSetKernelArg(memSet, 1, sizeof(cl_mem), (void *)&blast_numTriggerExtensionsD);
	clEnqueueNDRangeKernel(commandQueue, memSet, 1, NULL, &nTotalThreadNum, NULL, 0, NULL, NULL);
clFinish(commandQueue);
//		cudaMemset(blast_numTriggerExtensionsD, 0, sizeof(cl_uint) * nTotalThreadNum);
	
		//Copy data to device
		clEnqueueWriteBuffer(commandQueue, (cl_mem)sequenceDataFPD, CL_TRUE, 0, sizeof(struct sequenceDataFP) * numSequencesRound, sequenceDataFP, 0, NULL, NULL);
//		cudaMemcpy(sequenceDataFPD, sequenceDataFP, sizeof(struct sequenceDataFP) * numSequencesRound,
//				   cudaMemcpyHostToDevice);
		clEnqueueWriteBuffer(commandQueue, (cl_mem)sequencesD, CL_TRUE, 0, sizeof(unsigned char) * (nRoundOffset + 2), roundStartAddress, 0, NULL, NULL);
//		cudaMemcpy(sequencesD, roundStartAddress, sizeof(unsigned char) * (nRoundOffset + 2),
//				   cudaMemcpyHostToDevice);
		global_sequenceCount = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint), NULL, &errorCode);
		clEnqueueWriteBuffer(commandQueue, (cl_mem)global_sequenceCount, CL_TRUE, 0, sizeof(cl_uint), &iniVal, 0, NULL, NULL);
//		cudaMemcpyToSymbol(global_sequenceCount, &iniVal, sizeof(cl_uint));
		
		//get t4
		gettimeofday(&t4, NULL);
		
//Another kernel call that needs adding
		//all the required data are copied to device, launch the kernel

	clSetKernelArg(search_protein1hitKernel, 0, sizeof(PSSMatrixFPD), PSSMatrixFPD);
	clSetKernelArg(search_protein1hitKernel, 1, sizeof(matrixBodyD), (void *)matrixBodyD);
	clSetKernelArg(search_protein1hitKernel, 2, sizeof(sequenceDataFPD), sequenceDataFPD);
	clSetKernelArg(search_protein1hitKernel, 3, sizeof(sequencesD), sequencesD);
	clSetKernelArg(search_protein1hitKernel, 4, sizeof(parametersD), parametersD);
	clSetKernelArg(search_protein1hitKernel, 5, sizeof(wordLookupDFA_groupD), wordLookupDFA_groupD);
	clSetKernelArg(search_protein1hitKernel, 6, sizeof(wordLookupDFAD), wordLookupDFAD);
	clSetKernelArg(search_protein1hitKernel, 7, sizeof(blast_numUngappedExtensionsD), blast_numUngappedExtensionsD);
	clSetKernelArg(search_protein1hitKernel, 8, sizeof(blast_numTriggerExtensionsD), blast_numTriggerExtensionsD);
	clSetKernelArg(search_protein1hitKernel, 9, sizeof(blast_numHitsD), blast_numHitsD);
	clSetKernelArg(search_protein1hitKernel, 10, sizeof(hitMatrix_furthestD), hitMatrix_furthestD);
	clSetKernelArg(search_protein1hitKernel, 11, sizeof(hitMatrix_offsetD), hitMatrix_offsetD);
	clSetKernelArg(search_protein1hitKernel, 12, sizeof(ungappedExtensionsD), ungappedExtensionsD);
	clSetKernelArg(search_protein1hitKernel, 13, sizeof(numSequencesRound), (void *)&numSequencesRound);
	clEnqueueNDRangeKernel(commandQueue, search_protein1hitKernel, 1, NULL, &WorkSize, &LocalSize, 0, NULL, NULL);
//		search_protein1hitKernel<<<dimGrid, dimBlock>>>(PSSMatrixFPD,
//													matrixBodyD,
//													sequenceDataFPD,
//													sequencesD,
//													parametersD,
//													wordLookupDFA_groupD,
//													wordLookupDFAD,
//													blast_numUngappedExtensionsD,
//													blast_numTriggerExtensionsD,
//													blast_numHitsD,
//													hitMatrix_furthestD,
//													hitMatrix_offsetD,
//													ungappedExtensionsD,
//													numSequencesRound);
//Looks like another clEnqueueBarrier(commandQueue);
		clEnqueueBarrier(commandQueue);
//		cudaThreadSynchronize();

		//get t5
		gettimeofday(&t5, NULL);

		//Post processing
		//copy hit results back
		clEnqueueReadBuffer(commandQueue, (cl_mem)blast_numTriggerExtensionsD, CL_TRUE, 0, sizeof(cl_uint) * nTotalThreadNum, blast_numTriggerExtensionsH, 0, NULL, NULL);
//		cudaMemcpy(blast_numTriggerExtensionsH, 
//				   blast_numTriggerExtensionsD, 
//				   sizeof(cl_uint) * nTotalThreadNum, 
//				   cudaMemcpyDeviceToHost);

		clEnqueueReadBuffer(commandQueue, (cl_mem)ungappedExtensionsD, CL_TRUE, 0, sizeof(struct ungappedExtension), ungappedExtension, 0, NULL, NULL);
//		cudaMemcpy(ungappedExtension, 
//				   ungappedExtensionsD, 
//				   nUngappedExtensionNum * sizeof(struct ungappedExtension), 
//				   cudaMemcpyDeviceToHost);
		
		//get t6
		gettimeofday(&t6, NULL);
		
		//Add hits to the alignment list
		for (i = 0; i < nTotalThreadNum; i++)
		{
//			printf("id: %d numTriggerExtensions: %d\n", i, blast_numTriggerExtensionsH[i]);
			if (blast_numTriggerExtensionsH[i] > 0)
			{
				ungappedExtensionCur =  ungappedExtension + i * UNGAPEXT_PER_THREAD;
				preSequenceCount = -1;
				for (j = 0; j < blast_numTriggerExtensionsH[i]; j++)
				{
					if (ungappedExtensionCur[j].sequenceCount != preSequenceCount)
					{
						alignments_createNew(sequenceData[ungappedExtensionCur[j].sequenceCount + numSequenceProcessed].descriptionStart,
											 sequenceData[ungappedExtensionCur[j].sequenceCount + numSequenceProcessed].descriptionLength,
											 sequenceData[ungappedExtensionCur[j].sequenceCount + numSequenceProcessed].sequence,
											 sequenceData[ungappedExtensionCur[j].sequenceCount + numSequenceProcessed].sequenceLength,
											 sequenceData[ungappedExtensionCur[j].sequenceCount + numSequenceProcessed].encodedLength);
						preSequenceCount = ungappedExtensionCur[j].sequenceCount;
					}
					
					newUngappedExtension = (struct ungappedExtension *)memBlocks_newEntry(ungappedExtension_extensions);
					memcpy(newUngappedExtension, &ungappedExtensionCur[j], sizeof(struct ungappedExtension));
					alignments_addUngappedExtension(newUngappedExtension);
				}

				blast_numTriggerExtensions += blast_numTriggerExtensionsH[i];
			}
		}

		numSequenceProcessed += numSequencesRound;

		clReleaseMemObject((cl_mem)hitMatrix_furthestD);
//		cudaFree(hitMatrix_furthestD);
		clReleaseMemObject((cl_mem)sequencesD);
//		cudaFree(sequencesD);
		
		//get t7
		gettimeofday(&t7, NULL);

		//aggregate execution time
		timeRecord.preProcessTime 		+= (1000000 * (t3.tv_sec - t2.tv_sec) + t3.tv_usec - t2.tv_usec);
		timeRecord.dataCopyTimeH2D 		+= (1000000 * (t4.tv_sec - t3.tv_sec) + t4.tv_usec - t3.tv_usec);
		timeRecord.searchTime			+= (1000000 * (t5.tv_sec - t4.tv_sec) + t5.tv_usec - t4.tv_usec);
		timeRecord.dataCopyTimeD2H		+= (1000000 * (t6.tv_sec - t5.tv_sec) + t6.tv_usec - t5.tv_usec);
		timeRecord.addUngappedExtensionTime += (1000000 * (t7.tv_sec - t6.tv_sec) + t7.tv_usec - t6.tv_usec);
	}

	//After all sequences are processed
	clEnqueueReadBuffer(commandQueue, (cl_mem)blast_numUngappedExtensionsD, CL_TRUE, 0, sizeof(cl_uint) * nTotalThreadNum, blast_numUngappedExtensionsH, 0, NULL, NULL);
//	cudaMemcpy(blast_numUngappedExtensionsH, 
//			   blast_numUngappedExtensionsD, 
//			   sizeof(cl_uint) * nTotalThreadNum, 
//			   cudaMemcpyDeviceToHost);

	clEnqueueReadBuffer(commandQueue, (cl_mem)blast_numHitsD, CL_TRUE, 0, sizeof(cl_uint) * nTotalThreadNum, blast_numHitsH, 0, NULL, NULL);
//	cudaMemcpy(blast_numHitsH, 
//			   blast_numHitsD, 
//			   sizeof(cl_uint) * nTotalThreadNum, 
//			   cudaMemcpyDeviceToHost);

	for (j = 0; j < nTotalThreadNum; j++)
	{
		blast_numUngappedExtensions += blast_numUngappedExtensionsH[j];
		blast_numHits += blast_numHitsH[j];
	}

	clReleaseMemObject((cl_mem)PSSMatrixFPD);
//	cudaFree(PSSMatrixFPD);
	clReleaseMemObject((cl_mem)matrixBodyD);
//	cudaFree(matrixBodyD);
	clReleaseMemObject((cl_mem)sequenceDataFPD);
//	cudaFree(sequenceDataFPD);
	clReleaseMemObject((cl_mem)ungappedExtensionsD);
//	cudaFree(ungappedExtensionsD);
	clReleaseMemObject((cl_mem)blast_numUngappedExtensionsD);
//	cudaFree(blast_numUngappedExtensionsD);
	clReleaseMemObject((cl_mem)blast_numTriggerExtensionsD);
//	cudaFree(blast_numTriggerExtensionsD);
	clReleaseMemObject((cl_mem)blast_numHitsD);
//	cudaFree(blast_numHitsD);
	clReleaseMemObject((cl_mem)parametersD);
//	cudaFree(parametersD);
	clReleaseMemObject((cl_mem)wordLookupDFA_groupD);
//	cudaFree(wordLookupDFA_groupD);
	clReleaseMemObject((cl_mem)wordLookupDFAD);
//	cudaFree(wordLookupDFAD);
	clReleaseMemObject((cl_mem)hitMatrix_offsetD);
//	cudaFree(hitMatrix_offsetD);
	clReleaseMemObject((cl_mem)sequencesD);
//	cudaFree(sequencesD);

	clReleaseCommandQueue(commandQueue);
	clReleaseContext(context);

	free(sequenceDataFP);
	free(ungappedExtension);
	free(blast_numUngappedExtensionsH);
	free(blast_numTriggerExtensionsH);
	free(blast_numHitsH);
	free(hitMatrix_offsetH);
	free(devices);
	free(platforms);

	//get t8
	gettimeofday(&t8, NULL);

	//Record time
	timeRecord.iniTime 				= 1000000 * (t1.tv_sec - t0.tv_sec) + t1.tv_usec - t0.tv_usec;
	timeRecord.postProcessTime 		= 1000000 * (t8.tv_sec - t7.tv_sec) + t8.tv_usec - t7.tv_usec;
	timeRecord.hitUngappedExtTime 	= 1000000 * (t8.tv_sec - t1.tv_sec) + t8.tv_usec - t1.tv_usec;
}



// Shucai
// Search a protein database using 2-hit extension mode
void search_protein2hitParallel(struct scoreMatrix *scoreMatrixp, 
								struct PSSMatrix PSSMatrix,
								struct PSSMatrixFP PSSMatrixFP, 
								struct sequenceData* sequenceData, 
								cl_uint numSequences, 
								cl_uint tickFrequency)
{
	//Shucai
	cl_uint i, j, sequenceCount = 0;
	cl_uint nRoundOffset;

	//PSSMatrix pointers
	struct PSSMatrixFP *PSSMatrixFPD;
	cl_short *matrixBodyD;
	
	//Input database sequence
	struct sequenceDataFP *sequenceDataFP;
	struct sequenceDataFP *sequenceDataFPD;
	unsigned char *sequencesD;
	unsigned char *roundStartAddress;

	//ungapped extension
	struct ungappedExtension *ungappedExtensionsD;
	struct ungappedExtension *ungappedExtension;
	struct ungappedExtension *ungappedExtensionCur, *newUngappedExtension, *additionalUngappedExtension;

	//ungapped extension numbers
	cl_uint *blast_numUngappedExtensionsD, *blast_numUngappedExtensionsH;
	cl_uint *blast_numTriggerExtensionsD, *blast_numTriggerExtensionsH;
	cl_uint numAdditionalTriggerExtensions, numExtensions;
	cl_uint *blast_numHitsD, *blast_numHitsH;
	cl_uint *hitMatrix_furthestD;
	cl_uint *hitMatrix_offsetH;
	cl_uint *hitMatrix_offsetD;
	cl_int preSequenceCount;

	//For time record
	struct timeval t0, t1, t2, t3, t4, t5, t6, t7, t8, t9;

	cl_int wordNum, groupNum;

	//parameters
	struct parameters strParameters;
	struct parameters *parametersD;

	//word lookup table
	struct groupFP *wordLookupDFA_groupD;
	unsigned char *wordLookupDFAD;
	cl_uint wordLookupDFA_size;
	
	//grid and block dimensions
	//int nBlockNum = parameters_blockNum;
	//int nBlockSize = parameters_threadNum;
	int nBlockNum = 56;
	int nBlockSize = 128;
	int nTotalThreadNum = nBlockNum * nBlockSize;
	size_t WorkSize = nTotalThreadNum;
	size_t SetSize;
//	dim3 dimGrid(nBlockNum, 1);
	size_t LocalSize = nBlockSize;
//	dim3 dimBlock(nBlockSize, 1);
	cl_uint zeroPtr = 0;

	cl_int errorCode = CL_SUCCESS;
    cl_uint num_platforms = 0, num_devices = 0, temp_uint, temp_ucl_short;
    if (clGetPlatformIDs(0, NULL, &num_platforms) != CL_SUCCESS) printf("Failed to query platform count!\n");
    printf("Number of Platforms: %d\n", num_platforms);

    cl_platform_id * platforms = (cl_platform_id *) malloc(sizeof(cl_platform_id) * num_platforms);

    if (clGetPlatformIDs(num_platforms, &platforms[0], NULL) != CL_SUCCESS) printf("Failed to get platform IDs\n");

    for (i = 0; i < num_platforms; i++) {
    temp_uint = 0;
        if(clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &temp_uint) != CL_SUCCESS) printf("Failed to query device count on platform %d!\n", i);
    num_devices += temp_uint;
    }
    printf("Number of Devices: %d\n", num_devices);

    cl_device_id * devices = (cl_device_id *) malloc(sizeof(cl_device_id) * num_devices);
    temp_uint = 0;
    for ( i = 0; i < num_platforms; i++) {
        if(clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, num_devices, &devices[temp_uint], &temp_ucl_short) != CL_SUCCESS) printf ("Failed to query device IDs on platform %d!\n", i);
        temp_uint += temp_ucl_short;
        temp_ucl_short = 0;
    }

int devID = getDevID("AMD Athlon(tm) 64 X2 Dual Core Processor 6000+", devices, num_devices);
devID = devID == -1 ? 0 : devID;

 cl_context context = clCreateContext(NULL, 1, &devices[devID], NULL, NULL, &errorCode); //clCreateContextFromType(NULL, CL_DEVICE_TYPE_ALL, NULL, NULL, &errorCode);
    if (errorCode != CL_SUCCESS) {
        printf("failed to create GPU context! %s\n", print_cl_errstring(errorCode));
        errorCode = CL_SUCCESS;
    }


    //create command-queue for the GPU
    cl_command_queue commandQueue = clCreateCommandQueue(context, devices[devID], 0, &errorCode);
    if (errorCode != CL_SUCCESS) {
        printf("failed to create commande queue! %s\n", print_cl_errstring(errorCode));
        errorCode = CL_SUCCESS;
    }

//read OpenCL kernel source
        FILE *kernelFile = fopen("src/search.cl", "r");
        struct stat st;
        fstat(fileno(kernelFile), &st);
        char *kernelSource = (char*) calloc(st.st_size + 1, sizeof (char));
        fread(kernelSource, sizeof (char), st.st_size, kernelFile);
        fclose(kernelFile);


        //build gpu kernel
	cl_program program;
	cl_kernel search_protein2hitKernel, memSet;
        program = clCreateProgramWithSource(context, 1, (const char **) & kernelSource, NULL, &errorCode);
        if (errorCode != CL_SUCCESS) {
            printf("failed to create OpenCL program!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }

const char* options = "-g";

        errorCode = clBuildProgram(program, 0, NULL, options, NULL, NULL);
        if (errorCode != CL_SUCCESS) {
            printf("failed to build OpenCL program!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }

        char cBuffer[102400];
cl_build_status build_status;
        clGetProgramBuildInfo(program, NULL, CL_PROGRAM_BUILD_STATUS, sizeof (cl_build_status), &build_status, NULL);
        clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, NULL, cBuffer, NULL);
        printf("%s\n", cBuffer);
	search_protein2hitKernel = clCreateKernel(program, "search_protein2hitKernel", &errorCode);
        if (errorCode != CL_SUCCESS) {
            printf("failed to create search_protein2hitParallel kernel!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
	memSet = clCreateKernel(program, "memSet", &errorCode);
        if (errorCode != CL_SUCCESS) {
            printf("failed to create memSet kernel!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
free(kernelSource);
	//get t0
	gettimeofday(&t0, NULL);

	wordNum = wordLookupDFA_numWords;
	groupNum = wordLookupDFA_numGroups;

	global_numAdditionalTriggerExtensions = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint), NULL, &errorCode);
//printf("Kernel Arg 14 Size: %d\n", sizeof(cl_uint));
	//Allocate GPU buffer for PSSMatrix
	PSSMatrixFPD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(struct PSSMatrixFP), NULL, &errorCode);
//printf("Kernel Arg 0 Size: %d\n", sizeof(struct PSSMatrixFP));
	if (errorCode != CL_SUCCESS) {
            printf("failed to create PSSMatrixFPD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMalloc((void **)&PSSMatrixFPD, sizeof(struct PSSMatrixFP));
	matrixBodyD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_short) * (PSSMatrixFP.length + 2) * encoding_numCodes, NULL, &errorCode);
//printf("Kernel Arg 1 Size: %d\n", sizeof(cl_short) * (PSSMatrixFP.length + 2) * encoding_numCodes);
	if (errorCode != CL_SUCCESS) {  
            printf("failed to create matrixBodyD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMalloc((void **)&matrixBodyD, sizeof(cl_short) * (PSSMatrixFP.length + 2) * encoding_numCodes);

	//Copy PSSMatrix to device memory
	errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)PSSMatrixFPD, CL_TRUE, 0, sizeof(struct PSSMatrixFP), &PSSMatrixFP, 0, NULL, NULL);
	if (errorCode != CL_SUCCESS) {
            printf("failed to write PSSMatrixFPD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMemcpy(PSSMatrixFPD, &PSSMatrixFP, sizeof(struct PSSMatrixFP), cudaMemcpyHostToDevice);
	errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)matrixBodyD, CL_TRUE, 0, sizeof(cl_short) * (PSSMatrixFP.length + 2) * encoding_numCodes, (PSSMatrixFP.matrix - encoding_numCodes), 0, NULL, NULL);
	if (errorCode != CL_SUCCESS) {
            printf("failed to write matrixBodyD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }

printf("HOST MATRIX %d. %d. \t%d %d %d %d %d %d %d %d %d %d\n", PSSMatrixFP.matrix, encoding_numCodes, (PSSMatrixFP.matrix + encoding_numCodes)[0], (PSSMatrixFP.matrix + encoding_numCodes)[1], (PSSMatrixFP.matrix - encoding_numCodes)[2], (PSSMatrixFP.matrix - encoding_numCodes)[3], (PSSMatrixFP.matrix - encoding_numCodes)[4], (PSSMatrixFP.matrix - encoding_numCodes)[5], (PSSMatrixFP.matrix - encoding_numCodes)[6], (PSSMatrixFP.matrix - encoding_numCodes)[7], (PSSMatrixFP.matrix - encoding_numCodes)[8], (PSSMatrixFP.matrix - encoding_numCodes)[9]);
//	cudaMemcpy(matrixBodyD, (PSSMatrixFP.matrix - encoding_numCodes), 
//	sizeof(cl_short) * (PSSMatrixFP.length + 2) * encoding_numCodes,cudaMemcpyHostToDevice);

	//Each thread is for align of one database sequence
	sequenceDataFP = (struct sequenceDataFP *)global_malloc(numSequences * sizeof(struct sequenceDataFP));
	sequenceDataFPD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, numSequences * sizeof(struct sequenceDataFP), NULL, &errorCode);
//printf("Kernel Arg 2 Size: %d\n", numSequences * sizeof(struct sequenceDataFP));
	if (errorCode != CL_SUCCESS) {
            printf("failed to create sequenceDataFPD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMalloc((void **)&sequenceDataFPD, numSequences * sizeof(struct sequenceDataFP));

	//Allocate buffer for hit matrix offset
	hitMatrix_offsetH = (cl_uint *)global_malloc((nTotalThreadNum + 1) * sizeof(cl_uint));
	hitMatrix_offsetD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint) * (nTotalThreadNum +1), NULL, &errorCode);
//printf("Kernel Arg 11 Size: %d\n", sizeof(cl_uint) * (nTotalThreadNum +1));
if (errorCode != CL_SUCCESS) {
            printf("failed to create hitMatrix_offsetD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMalloc((void **)&hitMatrix_offsetD, (nTotalThreadNum + 1) * sizeof(cl_uint));

	//Allocate ungapped extension buffer on device
	//cl_int nUngappedExtensionNum = UNGAPEXT_PER_THREAD * nTotalThreadNum;
	cl_int nUngappedExtensionNum = TOTAL_UNGAPPED_EXT;
	strParameters.ungappedExtensionsPerThread = nUngappedExtensionNum / nTotalThreadNum - 1;
	strParameters.ungappedExtAdditionalStartLoc = strParameters.ungappedExtensionsPerThread * nTotalThreadNum;

	ungappedExtensionsD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(struct ungappedExtension) * nUngappedExtensionNum, NULL, &errorCode);
//printf("Kernel Arg 12 Size: %d\n", sizeof(struct ungappedExtension) * nUngappedExtensionNum);
if (errorCode != CL_SUCCESS) {
            printf("failed to create ungappedExtensionsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMalloc((void **)&ungappedExtensionsD, nUngappedExtensionNum * sizeof(struct ungappedExtension));
	ungappedExtension = (struct ungappedExtension *)global_malloc(nUngappedExtensionNum * sizeof(struct ungappedExtension));

//Paul - added for testing
//clEnqueueWriteBuffer(commandQueue, (cl_mem)ungappedExtensionsD, CL_TRUE, 0, sizeof(struct ungappedExtension) * nUngappedExtensionNum, ungappedExtension, 0, NULL, NULL);


	//Allocate numbers for ungapped extensions
	blast_numUngappedExtensionsH = (cl_uint *)global_malloc(sizeof(cl_uint) * nTotalThreadNum);
	blast_numTriggerExtensionsH = (cl_uint *)global_malloc(sizeof(cl_uint) * nTotalThreadNum);
	blast_numHitsH = (cl_uint *)global_malloc(sizeof(cl_uint) * nTotalThreadNum);

	blast_numUngappedExtensionsD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint) * nTotalThreadNum, NULL, &errorCode);
//printf("Kernel Arg 7 Size: %d\n", sizeof(cl_uint) * nTotalThreadNum);
if (errorCode != CL_SUCCESS) {
            printf("failed to create numUngappedExtensionsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMalloc((void **)&blast_numUngappedExtensionsD, sizeof(cl_uint) * nTotalThreadNum);
	blast_numTriggerExtensionsD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint) * nTotalThreadNum, NULL, &errorCode);
//printf("Kernel Arg 8 Size: %d\n", sizeof(cl_uint) *nTotalThreadNum);
if (errorCode != CL_SUCCESS) {
            printf("failed to create numTriggerExtensionsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMalloc((void **)&blast_numTriggerExtensionsD, sizeof(cl_uint) * nTotalThreadNum);
	blast_numHitsD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint) * nTotalThreadNum, NULL, &errorCode);
//printf("Kernel Arg 9 Size: %d\n", sizeof(cl_uint) * nTotalThreadNum);
if (errorCode != CL_SUCCESS) {
            printf("failed to create numHitsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMalloc((void **)&blast_numHitsD, sizeof(cl_uint) * nTotalThreadNum);
	
	//Allocate device memory, about 132Mbytes (according to texture limit)
	sequencesD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(unsigned char) * 132000000, NULL, &errorCode);
if (errorCode != CL_SUCCESS) {
            printf("failed to create numHitsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//printf("Kernel Arg 3 Size: %d\n", sizeof(unsigned char) * 132000000); 
//	cudaMalloc((void **)&sequencesD, sizeof(unsigned char) * 132000000);

	//Allocate parameters buffer on device
	parametersD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(struct parameters), NULL, &errorCode);
if (errorCode != CL_SUCCESS) {
            printf("failed to create numHitsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//printf("Kernel Arg 4 Size: %d\n", sizeof(struct parameters));
//	cudaMalloc((void **)&parametersD, sizeof(struct parameters));
	strParameters.parameters_wordSize = parameters_wordSize;
	strParameters.encoding_numCodes = encoding_numCodes;
	strParameters.wordLookupDFA_numCodes = wordLookupDFA_numCodes;
	strParameters.additionalQueryPositionOffset = wordNum * sizeof(char) + sizeof(cl_short) * wordLookupDFA_numExtPositions;
	strParameters.blast_ungappedNominalTrigger = blast_ungappedNominalTrigger;
	strParameters.statistics_ungappedNominalDropoff = statistics_ungappedNominalDropoff;
	strParameters.parameters_A = parameters_A;
	strParameters.parameters_overlap = parameters_overlap;
	errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)parametersD, CL_TRUE, 0, sizeof(struct parameters), &strParameters, 0, NULL, NULL);
//	cudaMemcpy(parametersD, &strParameters, sizeof(struct parameters), cudaMemcpyHostToDevice);

	//Allocate word lookup table
	wordLookupDFA_size = sizeof(char) * wordNum + 2 * sizeof(cl_short) * wordLookupDFA_numExtPositions;
	wordLookupDFA_groupD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(struct groupFP) * groupNum, NULL, &errorCode);

if (errorCode != CL_SUCCESS) {
            printf("failed to create numHitsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//printf("Kernel Arg 5 Size: %d\n", sizeof(struct groupFP) * groupNum);
//	cudaMalloc((void **)&wordLookupDFA_groupD, sizeof(struct groupFP) * groupNum);
	wordLookupDFAD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, wordLookupDFA_size, NULL, &errorCode);
if (errorCode != CL_SUCCESS) {
            printf("failed to create numHitsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//printf("Kernel Arg 6 Size: %d\n", wordLookupDFA_size);
//	cudaMalloc((void **)&wordLookupDFAD, wordLookupDFA_size);
SetSize=nTotalThreadNum;
zeroPtr=0;
	clSetKernelArg(memSet, 0, sizeof(cl_uint), (void *)&zeroPtr);
	clSetKernelArg(memSet, 1, sizeof(cl_mem), (void *)&blast_numUngappedExtensionsD);
	clEnqueueNDRangeKernel(commandQueue, memSet, 1, NULL, &SetSize, &LocalSize, 0, NULL, NULL);
//clFinish(commandQueue);
//	cudaMemset(blast_numUngappedExtensionsD, 0, sizeof(cl_uint) * nTotalThreadNum);
zeroPtr=0;
	clSetKernelArg(memSet, 0, sizeof(cl_uint), (void *)&zeroPtr);
	clSetKernelArg(memSet, 1, sizeof(cl_mem), (void *)&blast_numHitsD);
	clEnqueueNDRangeKernel(commandQueue, memSet, 1, NULL, &SetSize, &LocalSize, 0, NULL, NULL);
//clFlush(commandQueue);
//clFinish(commandQueue);
//	cudaMemset(blast_numHitsD, 0, sizeof(cl_uint) * nTotalThreadNum);
	int grr = 0;
//for (grr; grr < 400; grr++) {
//printf("chargroups[%d] %d %x, groups[%d] %d %x\n", grr, ((char*)wordLookupDFA_groupsFP)[grr], &((char*)wordLookupDFA_groupsFP)[grr], grr, wordLookupDFA_groupsFP[grr], &wordLookupDFA_groupsFP[grr]);
//}
	errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)wordLookupDFA_groupD, CL_TRUE, 0, sizeof(struct groupFP) * groupNum, wordLookupDFA_groupsFP, 0, NULL, NULL);
if (errorCode != CL_SUCCESS) {
            printf("failed to create numHitsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMemcpy(wordLookupDFA_groupD, wordLookupDFA_groupsFP, sizeof(struct groupFP) * groupNum, cudaMemcpyHostToDevice);
//	//Use constant memory for the word lookup table group
//deprecated apparently?	clEnqueueWriteBuffer(commandQueue, (cl_mem)wordLookupDFA_groupsC, CL_TRUE, 0, sizeof(struct groupFP) * groupNum, wordLookupDFA_groupsFP, 0, NULL, NULL);
//	cudaMemcpyToSymbol(wordLookupDFA_groupsC, wordLookupDFA_groupsFP, sizeof(struct groupFP) * groupNum);

//
	//Use constant memory to store score matrix
	int scoreMatrixSize = encoding_numCodes * encoding_numCodes;
//MemcpyToSymbol seems to be pretty much like enqueueing a write of a CL_MEM_READ_ONLY buffer
//also deprecated?	clEnqueueWriteBuffer(commandQueue, (cl_mem)scoreMatrixC, CL_TRUE, 0, sizeof(cl_short) * scoreMatrixSize, ((char *)scoreMatrixp->matrix) + sizeof(cl_short *) * encoding_numCodes, 0, NULL, NULL);
//	cudaMemcpyToSymbol(scoreMatrixC, 
//					  ((char *)scoreMatrixp->matrix) + sizeof(cl_short *) * encoding_numCodes, 
//					  sizeof(cl_short) * scoreMatrixSize);

	//Use constant memory to store query sequence
	unsigned char *tempQueryCode;
	tempQueryCode = (unsigned char *)global_malloc(sizeof(unsigned char) * (PSSMatrixFP.length + 2));
	memcpy(&tempQueryCode[1], PSSMatrixFP.queryCodes, sizeof(unsigned char) * PSSMatrixFP.length);
	tempQueryCode[0] = encoding_sentinalCode;
	tempQueryCode[PSSMatrixFP.length + 1] = encoding_sentinalCode;
//	errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)querySequenceC, CL_TRUE, 0, sizeof(unsigned char) * (PSSMatrixFP.length + 2), tempQueryCode, 0, NULL, NULL);
//	cudaMemcpyToSymbol(querySequenceC, tempQueryCode, sizeof(unsigned char) * (PSSMatrixFP.length + 2));
	free(tempQueryCode);

	errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)wordLookupDFAD, CL_TRUE, 0, wordLookupDFA_size, wordLookupDFA, 0, NULL, NULL);
if (errorCode != CL_SUCCESS) {
            printf("failed to create numHitsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMemcpy(wordLookupDFAD, wordLookupDFA, wordLookupDFA_size, cudaMemcpyHostToDevice);
	cl_uint iniVal = nTotalThreadNum;	
	//get t1
	gettimeofday(&t1, NULL);
	cl_int numSequencesRound, numSequenceProcessed;
	numSequenceProcessed = 0;
	while (sequenceCount < numSequences)
	{
		//get t2
		gettimeofday(&t2, NULL);

		memset(hitMatrix_offsetH, 0, sizeof(cl_int) * (nTotalThreadNum + 1));
		roundStartAddress = sequenceData[sequenceCount].sequence - 1;
		for (i = 0; sequenceCount < numSequences; i++, sequenceCount++)
		{	
			sequenceDataFP[i].descriptionLength = sequenceData[sequenceCount].descriptionLength;
			sequenceDataFP[i].descriptionStart = sequenceData[sequenceCount].descriptionStart;
			sequenceDataFP[i].sequenceLength = sequenceData[sequenceCount].sequenceLength;
			sequenceDataFP[i].encodedLength = sequenceData[sequenceCount].encodedLength;
			sequenceDataFP[i].offset = sequenceData[sequenceCount].sequence - roundStartAddress;
			
			//Calculate the longest sequence size aligned by the current thread
			if (sequenceDataFP[i].sequenceLength > hitMatrix_offsetH[(i % nTotalThreadNum) + 1])
			{
				hitMatrix_offsetH[(i % nTotalThreadNum) + 1] = sequenceDataFP[i].sequenceLength;
			}
			
			//about 130MB
			if (sequenceDataFP[i].offset + sequenceData[sequenceCount].encodedLength > 130000000)
			{
				i++;
				sequenceCount++;
				break;
			}
		}
		nRoundOffset = sequenceDataFP[i - 1].offset + sequenceDataFP[i - 1].encodedLength;
		numSequencesRound = i;
		
		//Calculate the offset of each thread
		for (i = 1; i < nTotalThreadNum + 1; i++)
		{
			hitMatrix_offsetH[i] += hitMatrix_offsetH[i - 1] + (PSSMatrixFP.length - parameters_wordSize + 1);
		}

		//copy offset info to device
		errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)hitMatrix_offsetD, CL_TRUE, 0, sizeof(cl_int) * (nTotalThreadNum + 1), hitMatrix_offsetH, 0, NULL, NULL);
//		cudaMemcpy(hitMatrix_offsetD, 
//				   hitMatrix_offsetH, 
//				   (nTotalThreadNum + 1) * sizeof(cl_int), 
//				   cudaMemcpyHostToDevice);

		//get t3
		gettimeofday(&t3, NULL);

		//Allocate diagonal buffers
		int nElemNum = hitMatrix_offsetH[nTotalThreadNum];

		hitMatrix_furthestD = (void *)clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint) * nElemNum, NULL, &errorCode);
if (errorCode != CL_SUCCESS) {
            printf("failed to create numHitsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//printf("Kernel Arg 10 Size: %d\n", sizeof(cl_uint) * nElemNum);
//		cudaMalloc((void **)&hitMatrix_furthestD, sizeof(cl_uint) * nElemNum);
printf("testingtesting Local:%d Work:%d ElemNum:%d ThreadNum:%d\n", LocalSize, WorkSize, nElemNum * sizeof(cl_uint), nTotalThreadNum * sizeof(cl_uint));
SetSize=nElemNum;
zeroPtr=0;
	clSetKernelArg(memSet, 0, sizeof(cl_uint), (void *)&zeroPtr);
	clSetKernelArg(memSet, 1, sizeof(cl_mem), (void *)&hitMatrix_furthestD);
	clEnqueueNDRangeKernel(commandQueue, memSet, 1, NULL, &SetSize, NULL, 0, NULL, NULL);

//clFlush(commandQueue);
clFinish(commandQueue);
//		cudaMemset(hitMatrix_furthestD, 0, sizeof(cl_uint) * nElemNum);
SetSize=nTotalThreadNum;
zeroPtr=0;
	clSetKernelArg(memSet, 0, sizeof(cl_uint), (void *)&zeroPtr);
	clSetKernelArg(memSet, 1, sizeof(cl_mem), (void *)&blast_numTriggerExtensionsD);
	clEnqueueNDRangeKernel(commandQueue, memSet, 1, NULL, &SetSize, &LocalSize, 0, NULL, NULL);

//clFlush(commandQueue);
clFinish(commandQueue);
//		cudaMemset(blast_numTriggerExtensionsD, 0, sizeof(cl_uint) * nTotalThreadNum);
	
		//Copy data to device
		errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)sequenceDataFPD, CL_TRUE, 0, sizeof(struct sequenceDataFP) * numSequencesRound, sequenceDataFP, 0, NULL, NULL);
if (errorCode != CL_SUCCESS) {
            printf("failed to write sequenceDataFPD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//		cudaMemcpy(sequenceDataFPD, sequenceDataFP, sizeof(struct sequenceDataFP) * numSequencesRound,
//				   cudaMemcpyHostToDevice);
		errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)sequencesD, CL_TRUE, 0, sizeof(unsigned char) * (nRoundOffset + 2), roundStartAddress, 0, NULL, NULL);
if (errorCode != CL_SUCCESS) {
            printf("failed to create sequencesD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//		cudaMemcpy(sequencesD, roundStartAddress, sizeof(unsigned char) * (nRoundOffset + 2),
//				   cudaMemcpyHostToDevice);

		global_sequenceCount = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint), NULL, &errorCode);
		errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)global_sequenceCount, CL_TRUE, 0, sizeof(cl_uint), &iniVal, 0, NULL, NULL);		
if (errorCode != CL_SUCCESS) {
            printf("failed to create global_sequenceCount buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//		cudaMemcpyToSymbol(global_sequenceCount, &iniVal, sizeof(cl_uint));

		numAdditionalTriggerExtensions = 0;
		errorCode = clEnqueueWriteBuffer(commandQueue, (cl_mem)global_numAdditionalTriggerExtensions, CL_TRUE, 0, sizeof(cl_uint), &numAdditionalTriggerExtensions, 0, NULL, NULL);
if (errorCode != CL_SUCCESS) {
            printf("failed to create numHitsD buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//		cudaMemcpyToSymbol(global_numAdditionalTriggerExtensions, 
//						   &numAdditionalTriggerExtensions, 
//						   sizeof(cl_uint)); clReleaseMemObject();
//	
		
		//get t4
		gettimeofday(&t4, NULL);

printf("UNGAPPED HOST %d\n", sizeof(struct ungappedExtension));		

//ANOTHER KERNEL CALL
		//all the required data are copied to device, launch the kernel
		clSetKernelArg(search_protein2hitKernel, 0, sizeof(cl_mem), (void*)&PSSMatrixFPD);
		clSetKernelArg(search_protein2hitKernel, 1, sizeof(cl_mem), (void*)&matrixBodyD);
		clSetKernelArg(search_protein2hitKernel, 2, sizeof(cl_mem), (void*)&sequenceDataFPD);
		clSetKernelArg(search_protein2hitKernel, 3, sizeof(cl_mem), (void*)&sequencesD);
		clSetKernelArg(search_protein2hitKernel, 4, sizeof(cl_mem), (void*)&parametersD);
		clSetKernelArg(search_protein2hitKernel, 5, sizeof(cl_mem), (void*)&wordLookupDFA_groupD);
		clSetKernelArg(search_protein2hitKernel, 6, sizeof(cl_mem), (void*)&wordLookupDFAD);
		clSetKernelArg(search_protein2hitKernel, 7, sizeof(cl_mem), (void*)&blast_numUngappedExtensionsD);
		clSetKernelArg(search_protein2hitKernel, 8, sizeof(cl_mem), (void*)&blast_numTriggerExtensionsD);
		clSetKernelArg(search_protein2hitKernel, 9, sizeof(cl_mem), (void*)&blast_numHitsD);
		clSetKernelArg(search_protein2hitKernel, 10, sizeof(cl_mem), (void*)&hitMatrix_furthestD);
		clSetKernelArg(search_protein2hitKernel, 11, sizeof(cl_mem), (void*)&hitMatrix_offsetD);
		clSetKernelArg(search_protein2hitKernel, 12, sizeof(cl_mem), (void*)&ungappedExtensionsD);
		clSetKernelArg(search_protein2hitKernel, 13, sizeof(cl_int), (void*)&numSequencesRound);
		clSetKernelArg(search_protein2hitKernel, 14, sizeof(cl_mem), (void*)&global_numAdditionalTriggerExtensions);
// printf("Sizes: %d %d\n", WorkSize, LocalSize);
		errorCode = clEnqueueNDRangeKernel(commandQueue, search_protein2hitKernel, 1, NULL, &WorkSize, &LocalSize, 0, NULL, NULL);
//		search_protein2hitKernel<<<dimGrid, dimBlock>>>(PSSMatrixFPD,
//													matrixBodyD,
//													sequenceDataFPD,
//													sequencesD,
//													parametersD,
//													wordLookupDFA_groupD,
//													wordLookupDFAD,
//													blast_numUngappedExtensionsD,
//													blast_numTriggerExtensionsD,
//													blast_numHitsD,
//													hitMatrix_furthestD,
//													hitMatrix_offsetD,
//													ungappedExtensionsD,
//			
//													numSequencesRound);
		//errorCode = clFinish(commandQueue);
		printf("EnqueueNDRangeKernel status: %s\n", print_cl_errstring(errorCode));	
		errorCode = clEnqueueBarrier(commandQueue);

		printf("EnqueueBarrier status: %s\n", print_cl_errstring(errorCode));
//		cudaThreadSynchronize();
	errorCode =clFinish(commandQueue);
if (errorCode != CL_SUCCESS) {
		printf("search_protein2hitKernel reported error status: %s\n", print_cl_errstring(errorCode));
		errorCode = CL_SUCCESS;
}
		//get t5
		gettimeofday(&t5, NULL);
		//Post processing
		//copy hit results back
		errorCode = clEnqueueReadBuffer(commandQueue, (cl_mem)blast_numTriggerExtensionsD, CL_TRUE, 0, sizeof(cl_uint) * nTotalThreadNum, blast_numTriggerExtensionsH, 0, NULL, NULL);
if (errorCode != CL_SUCCESS) {
            printf("failed to read numTriggerExtensions buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//		cudaMemcpy(blast_numTriggerExtensionsH, 
//				   blast_numTriggerExtensionsD, 
//				   sizeof(cl_uint) * nTotalThreadNum, 
//				   cudaMemcpyDeviceToHost);

		errorCode = clEnqueueReadBuffer(commandQueue, (cl_mem)ungappedExtensionsD, CL_TRUE, 0, sizeof(struct ungappedExtension) * nUngappedExtensionNum, ungappedExtension, 0, NULL, NULL);
if (errorCode != CL_SUCCESS) {
            printf("failed to read ungappedExtension buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//		cudaMemcpy(ungappedExtension, 
//				   ungappedExtensionsD, 
//				   nUngappedExtensionNum * sizeof(struct ungappedExtension), 
//				   cudaMemcpyDeviceToHost);


		errorCode = clEnqueueReadBuffer(commandQueue, (cl_mem)global_numAdditionalTriggerExtensions, CL_TRUE, 0, sizeof(cl_uint), &numAdditionalTriggerExtensions, 0, NULL, NULL);
if (errorCode != CL_SUCCESS) {
            printf("failed to read numAdditionalTriggerExtensions buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//		cudaMemcpyFromSymbol(&numAdditionalTriggerExtensions,
//							 global_numAdditionalTriggerExtensions,
//							 sizeof(cl_uint));
		
		//get t6
		gettimeofday(&t6, NULL);

printf("global_numAdditionalTriggerExtensions: %d\n", numAdditionalTriggerExtensions);
		
		//Add hits to the alignment list
		//Additional buffer is used, sort ungapped extensions
		//according to sequence index
		if (numAdditionalTriggerExtensions > 0)
		{
			additionalUngappedExtension = ungappedExtension + strParameters.ungappedExtAdditionalStartLoc;
			bubblesort(additionalUngappedExtension, 0, numAdditionalTriggerExtensions);
		}

		for (i = 0; i < nTotalThreadNum; i++)
		{
			if (blast_numTriggerExtensionsH[i] > 0)
			{
				//ungappedExtensionCur =  ungappedExtension + i * UNGAPEXT_PER_THREAD;
				ungappedExtensionCur =  ungappedExtension + i * strParameters.ungappedExtensionsPerThread;
				preSequenceCount = -1;
				numExtensions = (blast_numTriggerExtensionsH[i] > strParameters.ungappedExtensionsPerThread) ? 
								strParameters.ungappedExtensionsPerThread:
								blast_numTriggerExtensionsH[i];

				for (j = 0; j < numExtensions; j++)
				{
					if (ungappedExtensionCur[j].sequenceCount != preSequenceCount)
					{
						alignments_createNew(sequenceData[ungappedExtensionCur[j].sequenceCount + numSequenceProcessed].descriptionStart,
											 sequenceData[ungappedExtensionCur[j].sequenceCount + numSequenceProcessed].descriptionLength,
											 sequenceData[ungappedExtensionCur[j].sequenceCount + numSequenceProcessed].sequence,
											 sequenceData[ungappedExtensionCur[j].sequenceCount + numSequenceProcessed].sequenceLength,
											 sequenceData[ungappedExtensionCur[j].sequenceCount + numSequenceProcessed].encodedLength);
						preSequenceCount = ungappedExtensionCur[j].sequenceCount;
					}
					
					newUngappedExtension = (struct ungappedExtension *)memBlocks_newEntry(ungappedExtension_extensions);
					memcpy(newUngappedExtension, &ungappedExtensionCur[j], sizeof(struct ungappedExtension));
					alignments_addUngappedExtension(newUngappedExtension);
				}

				//Add additional extensions
				if (blast_numTriggerExtensionsH[i] > strParameters.ungappedExtensionsPerThread)
				{
					int tempStartLoc = findStartLoc(additionalUngappedExtension, i, numAdditionalTriggerExtensions);
					numExtensions = blast_numTriggerExtensionsH[i] - strParameters.ungappedExtensionsPerThread;

					for (j = tempStartLoc; j < numExtensions + tempStartLoc; j++)
					{
						if (additionalUngappedExtension[j].sequenceCount != preSequenceCount)
						{
							alignments_createNew(sequenceData[additionalUngappedExtension[j].sequenceCount + numSequenceProcessed].descriptionStart,
												 sequenceData[additionalUngappedExtension[j].sequenceCount + numSequenceProcessed].descriptionLength,
												 sequenceData[additionalUngappedExtension[j].sequenceCount + numSequenceProcessed].sequence,
												 sequenceData[additionalUngappedExtension[j].sequenceCount + numSequenceProcessed].sequenceLength,
												 sequenceData[additionalUngappedExtension[j].sequenceCount + numSequenceProcessed].encodedLength);
							preSequenceCount = additionalUngappedExtension[j].sequenceCount;
						}
	
						newUngappedExtension = (struct ungappedExtension *)memBlocks_newEntry(ungappedExtension_extensions);
						memcpy(newUngappedExtension, &additionalUngappedExtension[j], sizeof(struct ungappedExtension));
						alignments_addUngappedExtension(newUngappedExtension);
					}
				}

				blast_numTriggerExtensions += blast_numTriggerExtensionsH[i];
			}
		}

		numSequenceProcessed += numSequencesRound;

		clReleaseMemObject((cl_mem)hitMatrix_furthestD);
//		cudaFree(hitMatrix_furthestD);

		//get t7
		gettimeofday(&t7, NULL);

		//gapped extension for the current chunk of sequences on the GPU
//		alignments_findGoodAlignmentsGPU(&PSSMatrixFPD,	//GPU buffer
//									  PSSMatrixFP,
//									  scoreMatrixp,
//									  &matrixBodyD,		//GPU buffer
//									  &sequenceDataFPD, //GPU buffer
//									  &sequencesD,		//GPU buffer
//									  nRoundOffset);
		//use cpu for gapped extension
		alignments_findGoodAlignments(PSSMatrix, PSSMatrixFP);
		//get t9
		gettimeofday(&t9, NULL);
		timeRecord.gappedAlignmentTime += (1000000 * (t9.tv_sec - t7.tv_sec) + t9.tv_usec - t7.tv_usec);

		//aggregate execution time
		timeRecord.preProcessTime 		+= (1000000 * (t3.tv_sec - t2.tv_sec) + t3.tv_usec - t2.tv_usec);
		timeRecord.dataCopyTimeH2D 		+= (1000000 * (t4.tv_sec - t3.tv_sec) + t4.tv_usec - t3.tv_usec);
		timeRecord.searchTime			+= (1000000 * (t5.tv_sec - t4.tv_sec) + t5.tv_usec - t4.tv_usec);
		timeRecord.dataCopyTimeD2H		+= (1000000 * (t6.tv_sec - t5.tv_sec) + t6.tv_usec - t5.tv_usec);
		timeRecord.addUngappedExtensionTime += (1000000 * (t7.tv_sec - t6.tv_sec) + t7.tv_usec - t6.tv_usec);
	}

	clFinish(commandQueue);

	//After all sequences are processed
	errorCode = clEnqueueReadBuffer(commandQueue, (cl_mem)blast_numUngappedExtensionsD, CL_TRUE, 0, sizeof(cl_uint) * nTotalThreadNum, blast_numUngappedExtensionsH, 0, NULL, NULL);
if (errorCode != CL_SUCCESS) {
            printf("failed to read ungappedExtensions buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMemcpy(blast_numUngappedExtensionsH, 
//			   blast_numUngappedExtensionsD, 
//			   sizeof(cl_uint) * nTotalThreadNum, 
//			   cudaMemcpyDeviceToHost);
printf("nTotalThreadNum %d\n", nTotalThreadNum);

	errorCode = clEnqueueReadBuffer(commandQueue, (cl_mem)blast_numHitsD, CL_TRUE, 0, sizeof(cl_uint) *nTotalThreadNum, blast_numHitsH, 0, NULL, NULL);
if (errorCode != CL_SUCCESS) {
            printf("failed to read numHits buffer!\n\t%s\n", print_cl_errstring(errorCode));
            errorCode = CL_SUCCESS;
        }
//	cudaMemcpy(blast_numHitsH, 
//			   blast_numHitsD, 
//			   sizeof(cl_uint) * nTotalThreadNum, 
//			   cudaMemcpyDeviceToHost);

	for (j = 0; j < nTotalThreadNum; j++)
	{
//		if (!(j % 100)) printf("blast_numHitsH[%d] = %d\n\tblast_numHits = %d\n", j, blast_numHitsH[j], blast_numHits);
		blast_numUngappedExtensions += blast_numUngappedExtensionsH[j];
		blast_numHits += blast_numHitsH[j];
	}

	clReleaseMemObject((cl_mem)PSSMatrixFPD);
//	cudaFree(PSSMatrixFPD);
	clReleaseMemObject((cl_mem)matrixBodyD);
//	cudaFree(matrixBodyD);
	clReleaseMemObject((cl_mem)sequenceDataFPD);
//	cudaFree(sequenceDataFPD);
	clReleaseMemObject((cl_mem)ungappedExtensionsD);
//	cudaFree(ungappedExtensionsD);
	clReleaseMemObject((cl_mem)blast_numUngappedExtensionsD);
//	cudaFree(blast_numUngappedExtensionsD);
	clReleaseMemObject((cl_mem)blast_numTriggerExtensionsD);
//	cudaFree(blast_numTriggerExtensionsD);
	clReleaseMemObject((cl_mem)blast_numHitsD);
//	cudaFree(blast_numHitsD);
	clReleaseMemObject((cl_mem)parametersD);
//	cudaFree(parametersD);
	clReleaseMemObject((cl_mem)wordLookupDFA_groupD);
//	cudaFree(wordLookupDFA_groupD);
	clReleaseMemObject((cl_mem)wordLookupDFAD);
//	cudaFree(wordLookupDFAD);
	clReleaseMemObject((cl_mem)hitMatrix_offsetD);
//	cudaFree(hitMatrix_offsetD);
	clReleaseMemObject((cl_mem)sequencesD);
//	cudaFree(sequencesD);

	clReleaseCommandQueue(commandQueue);
	clReleaseContext(context);

	free(sequenceDataFP);
	free(ungappedExtension);
	free(blast_numUngappedExtensionsH);
	free(blast_numTriggerExtensionsH);
	free(blast_numHitsH);
	free(hitMatrix_offsetH);

	//get t8
	gettimeofday(&t8, NULL);

	//Record time
	timeRecord.iniTime 				= 1000000 * (t1.tv_sec - t0.tv_sec) + t1.tv_usec - t0.tv_usec;
	timeRecord.postProcessTime 		= 1000000 * (t8.tv_sec - t7.tv_sec) + t8.tv_usec - t7.tv_usec;
	timeRecord.hitUngappedExtTime 	= 1000000 * (t8.tv_sec - t1.tv_sec) + t8.tv_usec - t1.tv_usec;
}

