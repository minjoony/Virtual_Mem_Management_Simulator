// Virual Memory Management Simulator
// One-level page table system with FIFO and LRU
// Two-level page table system with LRU
// Inverted page table with a hashing system 

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#define PAGESIZEBITS 12			// page size = 4Kbytes
#define VIRTUALADDRBITS 32		// virtual address space size = 4Gbytes
#define PAGETABLESIZE 1048576	// one-level의 PageTableSize = 2^20

// PT의 entry구조체. two-level이면 1st PT의 entry가 2nd PT를 가리켜야 하므로 *secondLevelPageTable 사용
struct pageTableEntry {
	//int level;				// page table level (1 or 2)
	char valid;
	struct pageTableEntry2 *secondLevelPageTable;	// valid if this entry is for the first level page table (level = 1)
	int frameNumber;								// valid if this entry is for the second level page table (level = 2)
};

// second Page Table
struct pageTableEntry2 {
	char valid;
	int frameNumber;
};

// RAM의 frame Page 구조체. Doubly Linked List 형식
struct framePage {
	int number;			// frame number
	int pid;			// Process id that owns the frame
	int virtualPageNumber;			// virtual page number using the frameame
	int fVPN;
	int sVPN;
	struct framePage *lruLeft;	// for LRU circular doubly linked list
	struct framePage *lruRight; // for LRU circular doubly linked list
};

struct invertedPageTableEntry {
	int pid;					// process id
	int virtualPageNumber;		// virtual page number
	int frameNumber;			// frame number allocated
	struct invertedPageTableEntry *next;
};

// Process 정보들 저장할 구조체
struct procEntry {
	char *traceName;			// the memory trace name
	int pid;					// process (trace) id
	int ntraces;				// the number of memory traces
	int num2ndLevelPageTable;	// The 2nd level page created(allocated);
	int numIHTConflictAccess; 	// The number of Inverted Hash Table Conflict Accesses
	int numIHTNULLAccess;		// The number of Empty Inverted Hash Table Accesses
	int numIHTNonNULLAcess;		// The number of Non Empty Inverted Hash Table Accesses
	int numPageFault;			// The number of page faults
	int numPageHit;				// The number of page hits
	int eof_valid;				// Check is it the end of the file
	struct pageTableEntry *firstLevelPageTable;
	FILE *tracefp;
};

struct framePage *oldestFrame; // the oldest frame pointer
int firstLevelBits, twoLevelBits, phyMemSizeBits, numProcess, nFrame;
int s_flag = 0;

void initPhyMem(struct framePage *phyMem, int nFrame) {
	int i;
	for(i = 0; i < nFrame; i++) {
		phyMem[i].number = i;
		phyMem[i].pid = -1;
		phyMem[i].virtualPageNumber = -1;
		phyMem[i].lruLeft = &phyMem[(i-1+nFrame) % nFrame];
		phyMem[i].lruRight = &phyMem[(i+1+nFrame) % nFrame];
	}

	oldestFrame = &phyMem[0];
}

void oneLevelVMSim(struct procEntry *procTable, struct framePage *phyMemFrames, char FIFOorLRU) {
	int i;
	int eof_cnt = 0;
	int phyMemCNT = 0;
	unsigned addr, Vaddr, Paddr, offset;
	char rw;
	
	// PageTable 동적할당으로 생성
	for(i=0; i < numProcess; i++)
		procTable[i].firstLevelPageTable = (struct pageTableEntry *)malloc(sizeof(struct pageTableEntry) * PAGETABLESIZE);

	if(FIFOorLRU == 'F') {	// FIFO method
		while(eof_cnt != numProcess) {	// 프로세스의 개수만큼 eof를 읽으면 종료. 
			for(i=0; i < numProcess; i++) {
				if(procTable[i].eof_valid == 1)	// 먼저 끝난 더 이상 반복문을 프로세스는 수행하지 않는다.
					continue;

				if(fscanf(procTable[i].tracefp, "%x %c", &addr, &rw) == EOF) {	// 파일의 끝을 읽으면 continue.
					procTable[i].eof_valid = 1;
					eof_cnt++;
					continue;
				}
				else {
					// phyMemSizeBits에 따라서 Vaddr 설정.
					
					//if(phyMemSizeBits == 32)
					Vaddr = addr >> PAGESIZEBITS;
					offset = addr & 0xfff; // offset

					// pageHit
					if(procTable[i].firstLevelPageTable[Vaddr].valid == '1')// && procTable[i].pid == phyMemFrames[procTable[i].firstLevelPageTable[Vaddr].frameNumber].pid)
						procTable[i].numPageHit++;

					// pageFault
					else {
						procTable[i].numPageFault++;

						// pageFault 발생 시 oldestFrame을 맵핑해주고 해당 framePage에 맵핑된 procTable 정보 저장
						if(oldestFrame->virtualPageNumber != -1)
							procTable[oldestFrame->pid].firstLevelPageTable[oldestFrame->virtualPageNumber].valid = '0';

						procTable[i].firstLevelPageTable[Vaddr].frameNumber = oldestFrame->number;
						procTable[i].firstLevelPageTable[Vaddr].valid = '1';
						oldestFrame->virtualPageNumber = Vaddr;
						oldestFrame->pid = procTable[i].pid;
						oldestFrame = oldestFrame->lruRight;
					}

					Vaddr = procTable[i].firstLevelPageTable[Vaddr].frameNumber << PAGESIZEBITS;
					Paddr = Vaddr + offset;

					procTable[i].ntraces++;

					// -s option print statement
					if(s_flag)
						printf("One-Level procID %d traceNumber %d virtual addr %x physical addr %x\n", i, procTable[i].ntraces, addr, Paddr);
				}
			}
		}
	}
	
	else {	// LRU method
		while(eof_cnt != numProcess) {	// 프로세스의 개수만큼 eof를 읽으면 종료. 
			for(i=0; i < numProcess; i++) {
				if(procTable[i].eof_valid == 1)	// 먼저 끝난 더 이상 반복문을 프로세스는 수행하지 않는다.
					continue;

				if(fscanf(procTable[i].tracefp, "%x %c", &addr, &rw) == EOF)	// 파일의 끝을 읽으면 continue.
				{
					procTable[i].eof_valid = 1;
					eof_cnt++;
					continue;
				}
				else
				{
					Vaddr = addr >> PAGESIZEBITS;
					offset = addr & 0xfff; // offset

					// pageHit
					if(procTable[i].firstLevelPageTable[Vaddr].valid == '1')// && procTable[i].pid == phyMemFrames[procTable[i].firstLevelPageTable[Vaddr].frameNumber].pid)
					{
						procTable[i].numPageHit++;

						if(oldestFrame == &phyMemFrames[procTable[i].firstLevelPageTable[Vaddr].frameNumber])
							oldestFrame = oldestFrame->lruRight;

						else {
							unsigned temp = procTable[i].firstLevelPageTable[Vaddr].frameNumber;

							phyMemFrames[temp].lruLeft->lruRight = phyMemFrames[temp].lruRight;
							phyMemFrames[temp].lruRight->lruLeft = phyMemFrames[temp].lruLeft;
							phyMemFrames[temp].lruRight = oldestFrame;
							phyMemFrames[temp].lruLeft = oldestFrame->lruLeft;
							oldestFrame->lruLeft->lruRight = &phyMemFrames[temp];
							oldestFrame->lruLeft = &phyMemFrames[temp];
						}
					}
					
					// pageFault
					else
					{
						procTable[i].numPageFault++;

						// pageFault 발생 시 oldestFrame을 맵핑해주고 해당 framePage에 맵핑된 procTable 정보 저장
						if(oldestFrame->virtualPageNumber != -1)
							procTable[oldestFrame->pid].firstLevelPageTable[oldestFrame->virtualPageNumber].valid = '0';

						procTable[i].firstLevelPageTable[Vaddr].frameNumber = oldestFrame->number;
						procTable[i].firstLevelPageTable[Vaddr].valid = '1';
						oldestFrame->virtualPageNumber = Vaddr;
						oldestFrame->pid = procTable[i].pid;
						oldestFrame = oldestFrame->lruRight;
					}
					
					Vaddr = procTable[i].firstLevelPageTable[Vaddr].frameNumber << PAGESIZEBITS;
					Paddr = Vaddr + offset;

					procTable[i].ntraces++;

					// -s option print statement
					if(s_flag)
						printf("One-Level procID %d traceNumber %d virtual addr %x physical addr %x\n", i, procTable[i].ntraces, addr, Paddr);
				}
			}
		}
	}
	
	for(i=0; i < numProcess; i++) {
		printf("**** %s *****\n",procTable[i].traceName);
		printf("Proc %d Num of traces %d\n",i,procTable[i].ntraces);
		printf("Proc %d Num of Page Faults %d\n",i,procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n",i,procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
	}

	for(i=0; i < numProcess; i++)
		rewind(procTable[i].tracefp);
}

void twoLevelVMSim(struct procEntry *procTable, struct framePage *phyMemFrames) {
	int i;
	int eof_cnt = 0;
	int firstLevelPageTableSize, twoLevelPageTableSize;
	unsigned addr, Vaddr, Paddr, offset, fVPN, sVPN;
	char rw;
	
	twoLevelBits = 32 - PAGESIZEBITS - firstLevelBits;
	// Page Table Size 지정
	firstLevelPageTableSize = 1 << firstLevelBits;
	twoLevelPageTableSize = 1 << twoLevelBits;

	// first Page Table 동적할당으로 생성
	for(i=0; i < numProcess; i++)
		procTable[i].firstLevelPageTable = (struct pageTableEntry *)malloc(sizeof(struct pageTableEntry) * firstLevelPageTableSize);

	while(eof_cnt != numProcess)	// 프로세스의 개수만큼 eof를 읽으면 종료.
	{
		for(i=0; i < numProcess; i++)
		{
			if(procTable[i].eof_valid == 1)	// 먼저 끝난 더 이상 반복문을 프로세스는 수행하지 않는다.
				continue;

			if(fscanf(procTable[i].tracefp, "%x %c", &addr, &rw) == EOF)	// 파일의 끝을 읽으면 continue.
			{
				procTable[i].eof_valid = 1;
				eof_cnt++;
				continue;
			}
			else
			{		
				fVPN = (addr >> PAGESIZEBITS) >> twoLevelBits;
				sVPN = (addr << firstLevelBits) >> PAGESIZEBITS >> firstLevelBits;
				offset = addr & 0xfff;	// offset = 하위 12bits

				// pageHit
				if(procTable[i].firstLevelPageTable[fVPN].valid == '1')
				{
					// second PageTable 접근
					if(procTable[i].firstLevelPageTable[fVPN].secondLevelPageTable[sVPN].valid == '1')	// page Hit
					{
						procTable[i].numPageHit++;

						if(oldestFrame == &phyMemFrames[procTable[i].firstLevelPageTable[fVPN].secondLevelPageTable[sVPN].frameNumber])
							oldestFrame = oldestFrame->lruRight;

						else {
							unsigned temp = procTable[i].firstLevelPageTable[fVPN].secondLevelPageTable[sVPN].frameNumber;

							phyMemFrames[temp].lruLeft->lruRight = phyMemFrames[temp].lruRight;
							phyMemFrames[temp].lruRight->lruLeft = phyMemFrames[temp].lruLeft;
							phyMemFrames[temp].lruRight = oldestFrame;
							phyMemFrames[temp].lruLeft = oldestFrame->lruLeft;
							oldestFrame->lruLeft->lruRight = &phyMemFrames[temp];
							oldestFrame->lruLeft = &phyMemFrames[temp];
						}
					}

					else	// PT2에서의 page Fault
					{
						procTable[i].numPageFault++;

						// pageFault 발생 시 oldestFrame을 맵핑해주고 해당 framePage에 맵핑된 procTable 정보 저장
						if(oldestFrame->virtualPageNumber != -1)	// oldestFrame에 맵핑돼 있던 PT valid = 0으로 수정.
							procTable[oldestFrame->pid].firstLevelPageTable[oldestFrame->fVPN].secondLevelPageTable[oldestFrame->sVPN].valid = '0';

						procTable[i].firstLevelPageTable[fVPN].secondLevelPageTable[sVPN].frameNumber = oldestFrame->number;
						procTable[i].firstLevelPageTable[fVPN].secondLevelPageTable[sVPN].valid = '1';
						procTable[i].firstLevelPageTable[fVPN].valid = '1';
						oldestFrame->virtualPageNumber = (addr >> PAGESIZEBITS);
						oldestFrame->fVPN = fVPN;
						oldestFrame->sVPN = sVPN;
						oldestFrame->pid = procTable[i].pid;
						oldestFrame = oldestFrame->lruRight;
					}
				}


				// PT1에서의 page Fault
				else
				{
					procTable[i].numPageFault++;

					// pageFault 발생 시 oldestFrame을 맵핑해주고 해당 framePage에 맵핑된 procTable 정보 저장
					if(oldestFrame->virtualPageNumber != -1)	// oldestFrame에 맵핑돼 있던 PT valid = 0으로 수정.
						procTable[oldestFrame->pid].firstLevelPageTable[oldestFrame->fVPN].secondLevelPageTable[oldestFrame->sVPN].valid = '0';

					procTable[i].firstLevelPageTable[fVPN].secondLevelPageTable = (struct pageTableEntry2 *)malloc(sizeof(struct pageTableEntry2) * twoLevelPageTableSize);
					procTable[i].num2ndLevelPageTable++;
					procTable[i].firstLevelPageTable[fVPN].secondLevelPageTable[sVPN].frameNumber = oldestFrame->number;
					procTable[i].firstLevelPageTable[fVPN].secondLevelPageTable[sVPN].valid = '1';
					procTable[i].firstLevelPageTable[fVPN].valid = '1';
					oldestFrame->virtualPageNumber = (addr >> PAGESIZEBITS);
					oldestFrame->fVPN = fVPN;
					oldestFrame->sVPN = sVPN;
					oldestFrame->pid = procTable[i].pid;
					oldestFrame = oldestFrame->lruRight;
				}
			}

			procTable[i].ntraces++;
			Paddr = (procTable[i].firstLevelPageTable[fVPN].secondLevelPageTable[sVPN].frameNumber << PAGESIZEBITS) + offset;
			// -s option print statement
			if(s_flag)
				printf("Two-Level procID %d traceNumber %d virtual addr %x physical addr %x\n", i, procTable[i].ntraces,addr,Paddr);
		}
	}
		
	for(i=0; i < numProcess; i++) {
		printf("**** %s *****\n",procTable[i].traceName);
		printf("Proc %d Num of traces %d\n",i,procTable[i].ntraces);
		printf("Proc %d Num of second level page tables allocated %d\n",i,procTable[i].num2ndLevelPageTable);
		printf("Proc %d Num of Page Faults %d\n",i,procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n",i,procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
	}

	for(i=0; i < numProcess; i++)
		rewind(procTable[i].tracefp);
}


void invertedPageVMSim(struct procEntry *procTable, struct framePage *phyMemFrames, int nFrame) {
	int i;
	int j;
	int eof_cnt = 0;
	int invertedBits;
	int iptSize;
	unsigned addr, Vaddr, Paddr, offset, IPN, IPTindex;
	char rw;

	invertedBits = 32 - PAGESIZEBITS;
	iptSize = nFrame;

	struct invertedPageTableEntry * invertedPageTable = (struct invertedPageTableEntry *)malloc(sizeof(struct invertedPageTableEntry) * iptSize);

	// initialize invertedPageTable
	for(i=0; i<iptSize; i++)
	{
		invertedPageTable[i].pid = -1;
		invertedPageTable[i].virtualPageNumber = -1;
		invertedPageTable[i].frameNumber = -1;
		invertedPageTable[i].next = NULL;
	}

	while(eof_cnt != numProcess)	// 프로세스의 개수만큼 eof를 읽으면 종료.
	{
		for(i=0; i < numProcess; i++)
		{
			if(procTable[i].eof_valid == 1)	// 먼저 끝난 프로세스는 더 이상 반복문을 수행하지 않는다.
				continue;

			if(fscanf(procTable[i].tracefp, "%x %c", &addr, &rw) == EOF)	// 파일의 끝을 읽으면 continue.
			{
				procTable[i].eof_valid = 1;
				eof_cnt++;
				continue;
			}

			else
			{
				IPN = addr >> PAGESIZEBITS;
				IPTindex = (IPN + procTable[i].pid) % iptSize;
				offset = addr & 0xfff;	// offset = 하위 12bits
				
				// Entry가 존재하지 않는 경우
				if(invertedPageTable[IPTindex].next == NULL)
				{
					// page fault
					procTable[i].numIHTNULLAccess++;
					procTable[i].numPageFault++;

					// 새로운 항목 만들기
					struct invertedPageTableEntry * newEntry = (struct invertedPageTableEntry *)malloc(sizeof(struct invertedPageTableEntry));
					newEntry->pid = procTable[i].pid;
					newEntry->virtualPageNumber = IPN;
					newEntry->frameNumber = oldestFrame->number;
					newEntry->next = NULL;

					// 새로운 항목 삽입하기
					invertedPageTable[IPTindex].next = newEntry;

					// oldest Frame에 맵핑돼있던 항목 삭제
					if(oldestFrame->virtualPageNumber != -1)
					{
						unsigned del_IPTindex;
						del_IPTindex = (oldestFrame->virtualPageNumber + oldestFrame->pid) % iptSize;

						struct invertedPageTableEntry * del = invertedPageTable[del_IPTindex].next;
						struct invertedPageTableEntry * del_follow = invertedPageTable[del_IPTindex].next;

						while(del != NULL)
						{
							if((del->pid == oldestFrame->pid) && (del->virtualPageNumber == oldestFrame->virtualPageNumber))
							{
								if(del != invertedPageTable[del_IPTindex].next)
								{
									while(del_follow->next != del)
										del_follow = del_follow->next;

									del_follow->next = del->next;
								}
								else
									invertedPageTable[del_IPTindex].next = del->next;

								break;
							}
							else
								del = del->next;
						}
					}
					// oldest Frame 정보 갱신
					oldestFrame->virtualPageNumber = IPN;
					oldestFrame->pid = procTable[i].pid;
					oldestFrame = oldestFrame->lruRight;

					Paddr = (invertedPageTable[IPTindex].next->frameNumber << PAGESIZEBITS) + offset;
				}
				// Entry가 존재하는 경우
				else
				{
					procTable[i].numIHTNonNULLAcess++;
					procTable[i].numIHTConflictAccess++;

					struct invertedPageTableEntry * searching = invertedPageTable[IPTindex].next;
					struct invertedPageTableEntry * searching_follow = invertedPageTable[IPTindex].next;

					while(searching != NULL)	// entry 전체 탐색
					{
						if((searching->pid == procTable[i].pid) && (searching->virtualPageNumber == IPN))
						{
							// Page Hit
							procTable[i].numPageHit++;
							/*
							// 찾은 entry가 맨 앞에 위치해있지 않다면 갱신
							if(searching != invertedPageTable[IPTindex].next)
							{
								while(searching_follow->next != searching)
									searching_follow = searching_follow->next;
								searching_follow->next = searching->next;
								searching->next = invertedPageTable[IPTindex].next;
								invertedPageTable[IPTindex].next = searching;
							}
							*/

							// 찾은 entry에 해당하는 frame 위치 갱신
							if(oldestFrame == &phyMemFrames[searching->frameNumber])
								oldestFrame = oldestFrame->lruRight;

							else
							{
								unsigned temp = searching->frameNumber;

								phyMemFrames[temp].lruLeft->lruRight = phyMemFrames[temp].lruRight;
								phyMemFrames[temp].lruRight->lruLeft = phyMemFrames[temp].lruLeft;
								phyMemFrames[temp].lruRight = oldestFrame;
								phyMemFrames[temp].lruLeft = oldestFrame->lruLeft;
								oldestFrame->lruLeft->lruRight = &phyMemFrames[temp];
								oldestFrame->lruLeft = &phyMemFrames[temp];
							}
							
							Paddr = (searching->frameNumber << PAGESIZEBITS) + offset;
							break;
						}

						else
						{
							searching = searching->next;
							if(searching != NULL)
								procTable[i].numIHTConflictAccess++;
						}
					}

					// entry에 존재하지 않는 경우. page fault
					if(searching == NULL)
					{
						procTable[i].numPageFault++;

						// 추가할 새로운 entry 만들기
						struct invertedPageTableEntry * newEntry = (struct invertedPageTableEntry *)malloc(sizeof(struct invertedPageTableEntry));
						newEntry->pid = procTable[i].pid;
						newEntry->virtualPageNumber = IPN;
						newEntry->frameNumber = oldestFrame->number;
						newEntry->next = NULL;

						// 새로운 항목 entry맨 앞에 삽입하기
						newEntry->next = invertedPageTable[IPTindex].next;
						invertedPageTable[IPTindex].next = newEntry;

						// oldest Frame에 맵핑돼있던 항목 삭제
						if(oldestFrame->virtualPageNumber != -1)
						{
							unsigned del_IPTindex;
							del_IPTindex = (oldestFrame->virtualPageNumber + oldestFrame->pid) % iptSize;
							struct invertedPageTableEntry * del = invertedPageTable[del_IPTindex].next;
							struct invertedPageTableEntry * del_follow = invertedPageTable[del_IPTindex].next;

							while(del != NULL)
							{
								if((del->pid == oldestFrame->pid) && (del->virtualPageNumber == oldestFrame->virtualPageNumber))
								{
									if(del != invertedPageTable[del_IPTindex].next)
									{
										while(del_follow->next != del)
											del_follow = del_follow->next;

										del_follow->next = del->next;
									}

									else
										invertedPageTable[del_IPTindex].next = del->next;

									break;
								}
								else
									del = del->next;
							}
						}

						// oldest Frame 정보 갱신
						oldestFrame->virtualPageNumber = IPN;
						oldestFrame->pid = procTable[i].pid;
						oldestFrame = oldestFrame->lruRight;

						Paddr = (invertedPageTable[IPTindex].next->frameNumber << PAGESIZEBITS) + offset;
					}
				}
			}

			procTable[i].ntraces++;
			// -s option print statement
			if(s_flag)
				printf("IHT procID %d traceNumber %d virtual addr %x physical addr %x\n", i, procTable[i].ntraces,addr,Paddr);

		}

	}

	for(i=0; i < numProcess; i++) {
		printf("**** %s *****\n",procTable[i].traceName);
		printf("Proc %d Num of traces %d\n",i,procTable[i].ntraces);
		printf("Proc %d Num of Inverted Hash Table Access Conflicts %d\n",i,procTable[i].numIHTConflictAccess);
		printf("Proc %d Num of Empty Inverted Hash Table Access %d\n",i,procTable[i].numIHTNULLAccess);
		printf("Proc %d Num of Non-Empty Inverted Hash Table Access %d\n",i,procTable[i].numIHTNonNULLAcess);
		printf("Proc %d Num of Page Faults %d\n",i,procTable[i].numPageFault);
		printf("Proc %d Num of Page Hit %d\n",i,procTable[i].numPageHit);
		assert(procTable[i].numPageHit + procTable[i].numPageFault == procTable[i].ntraces);
		assert(procTable[i].numIHTNULLAccess + procTable[i].numIHTNonNULLAcess == procTable[i].ntraces);
	}

	for(i=0; i < numProcess; i++)
		rewind(procTable[i].tracefp);
}

int main(int argc, char *argv[]) {	// argc : main함수에 전달 된 인자의 개수. 인자를 아무것도 주지 않고 main함수 호출 시 argc=1 (호출 이름 때문.)
									// argv : main함수로 전달 되는 데이터. 문자열의 형태를 띈다.
	int i;

	if (argc < 5) {	//인자 잘못 넣었을 경우 출력.
	     printf("Usage : %s [-s] firstLevelBits PhysicalMemorySizeBits TraceFileNames\n",argv[0]); exit(1);
	}

	// 사용할 변수들 생성 및 초기화.
	if(!strcmp(argv[1],"-s")) { s_flag = 1; } // [-s] 인자 확인하여 s_flag 초기화.
	numProcess =  argc - 4 - s_flag;	// 프로세스의 개수 초기화 (main함수가 받는 인자의 개수에서 traceFileName이 아닌 개수와 s_flag를 뺀다)
	struct procEntry procTable[numProcess];	// 프로세스의 개수만큼 procEntry 생성
	struct procEntry *procTableptr = procTable;
	firstLevelBits = atoi(argv[s_flag + 2]);
	phyMemSizeBits = atoi(argv[s_flag + 3]);

	if (phyMemSizeBits < PAGESIZEBITS) {
		printf("PhysicalMemorySizeBits %d should be larger than PageSizeBits %d\n",phyMemSizeBits,PAGESIZEBITS); exit(1);
	}
	if (VIRTUALADDRBITS - PAGESIZEBITS - firstLevelBits <= 0 ) {
		printf("firstLevelBits %d is too Big for the 2nd level page system\n",firstLevelBits); exit(1);
	}
	
	// initialize procTable for memory simulations
	for(i = 0; i < numProcess; i++) {
		// opening a tracefile for the process
		printf("process %d opening %s\n",i,argv[s_flag + 4 + i]);
	}

	nFrame = (1<<(phyMemSizeBits-PAGESIZEBITS)); assert(nFrame>0);
	struct framePage * phyMemFrames = (struct framePage *)malloc(sizeof(struct framePage) * nFrame);
	initPhyMem(phyMemFrames, nFrame);

	printf("\nNum of Frames %d Physical Memory Size %ld bytes\n",nFrame, (1L<<phyMemSizeBits));

	for(i = 0; i < numProcess; i++) {
		// initialize procTable fields
		procTable[i].traceName = (char *)malloc(strlen(argv[s_flag + 4 + i]) + 1);
		strcpy(procTable[i].traceName, argv[s_flag + 4 + i]);
		procTable[i].pid = i;
		procTable[i].ntraces = 0;
		procTable[i].num2ndLevelPageTable = 0;
		procTable[i].numIHTConflictAccess = 0;
		procTable[i].numIHTNULLAccess = 0;
		procTable[i].numIHTNonNULLAcess = 0;
		procTable[i].numPageFault = 0;
		procTable[i].numPageHit = 0;
		procTable[i].firstLevelPageTable = NULL;
		procTable[i].eof_valid = 0;
		procTable[i].tracefp = fopen(argv[s_flag + 4 + i], "r");
	}

	if (*argv[s_flag + 1] == '0') {	// simType = 0, One-level page table system을 수행
		printf("=============================================================\n");
		printf("The One-Level Page Table with FIFO Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		// call oneLevelVMSim() with FIFO
		oneLevelVMSim(procTableptr, phyMemFrames, 'F');

		initPhyMem(phyMemFrames, nFrame);
		// initialize procTable for the simulation
		for(i = 0; i < numProcess; i++) {
			// initialize procTable fields
			procTable[i].traceName = (char *)malloc(strlen(argv[s_flag + 4 + i]) + 1);
			strcpy(procTable[i].traceName, argv[s_flag + 4 + i]);
			procTable[i].pid = i;
			procTable[i].ntraces = 0;
			procTable[i].num2ndLevelPageTable = 0;
			procTable[i].numIHTConflictAccess = 0;
			procTable[i].numIHTNULLAccess = 0;
			procTable[i].numIHTNonNULLAcess = 0;
			procTable[i].numPageFault = 0;
			procTable[i].numPageHit = 0;
			procTable[i].firstLevelPageTable = NULL;
			procTable[i].eof_valid = 0;
			procTable[i].tracefp = fopen(argv[s_flag + 4 + i], "r");
		}

		printf("=============================================================\n");
		printf("The One-Level Page Table with LRU Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		// call oneLevelVMSim() with LRU
		oneLevelVMSim(procTableptr, phyMemFrames, 'L');

		initPhyMem(phyMemFrames, nFrame);
		// initialize procTable for the simulation
		for(i = 0; i < numProcess; i++) {
			// initialize procTable fields
			procTable[i].traceName = (char *)malloc(strlen(argv[s_flag + 4 + i]) + 1);
			strcpy(procTable[i].traceName, argv[s_flag + 4 + i]);
			procTable[i].pid = i;
			procTable[i].ntraces = 0;
			procTable[i].num2ndLevelPageTable = 0;
			procTable[i].numIHTConflictAccess = 0;
			procTable[i].numIHTNULLAccess = 0;
			procTable[i].numIHTNonNULLAcess = 0;
			procTable[i].numPageFault = 0;
			procTable[i].numPageHit = 0;
			procTable[i].firstLevelPageTable = NULL;
			procTable[i].eof_valid = 0;
			procTable[i].tracefp = fopen(argv[s_flag + 4 + i], "r");
		}
	}




	else if (*argv[s_flag + 1] == '1') {	// simType = 1, Two-level page table system을 수행
		printf("=============================================================\n");
		printf("The Two-Level Page Table Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		// call twoLevelVMSim()
		twoLevelVMSim(procTableptr, phyMemFrames);

		initPhyMem(phyMemFrames, nFrame);
		// initialize procTable for the simulation
		for(i = 0; i < numProcess; i++) {
			// initialize procTable fields
			procTable[i].traceName = (char *)malloc(strlen(argv[s_flag + 4 + i]) + 1);
			strcpy(procTable[i].traceName, argv[s_flag + 4 + i]);
			procTable[i].pid = i;
			procTable[i].ntraces = 0;
			procTable[i].num2ndLevelPageTable = 0;
			procTable[i].numIHTConflictAccess = 0;
			procTable[i].numIHTNULLAccess = 0;
			procTable[i].numIHTNonNULLAcess = 0;
			procTable[i].numPageFault = 0;
			procTable[i].numPageHit = 0;
			procTable[i].firstLevelPageTable = NULL;
			procTable[i].eof_valid = 0;
			procTable[i].tracefp = fopen(argv[s_flag + 4 + i], "r");
		}
	}
	
	// initialize procTable for the simulation

	else if (*argv[s_flag + 1] == '2') {	// simType = 2, Inverted page table system을 수행
		printf("=============================================================\n");
		printf("The Inverted Page Table Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		// call invertedPageVMsim()
		invertedPageVMSim(procTableptr, phyMemFrames, nFrame);

		initPhyMem(phyMemFrames, nFrame);
		// initialize procTable for the simulation
		for(i = 0; i < numProcess; i++) {
			// initialize procTable fields
			procTable[i].traceName = (char *)malloc(strlen(argv[s_flag + 4 + i]) + 1);
			strcpy(procTable[i].traceName, argv[s_flag + 4 + i]);
			procTable[i].pid = i;
			procTable[i].ntraces = 0;
			procTable[i].num2ndLevelPageTable = 0;
			procTable[i].numIHTConflictAccess = 0;
			procTable[i].numIHTNULLAccess = 0;
			procTable[i].numIHTNonNULLAcess = 0;
			procTable[i].numPageFault = 0;
			procTable[i].numPageHit = 0;
			procTable[i].firstLevelPageTable = NULL;
			procTable[i].eof_valid = 0;
			procTable[i].tracefp = fopen(argv[s_flag + 4 + i], "r");
		}
	}

	// initialize procTable for the simulation

	else {	// simType > 3, 모두 수행	
		printf("=============================================================\n");
		printf("The One-Level Page Table with FIFO Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		// call oneLevelVMSim() with FIFO
		oneLevelVMSim(procTableptr, phyMemFrames, 'F');
		
		initPhyMem(phyMemFrames, nFrame);
		// initialize procTable for the simulation
		for(i = 0; i < numProcess; i++) {
			// initialize procTable fields
			procTable[i].traceName = (char *)malloc(strlen(argv[s_flag + 4 + i]) + 1);
			strcpy(procTable[i].traceName, argv[s_flag + 4 + i]);
			procTable[i].pid = i;
			procTable[i].ntraces = 0;
			procTable[i].num2ndLevelPageTable = 0;
			procTable[i].numIHTConflictAccess = 0;
			procTable[i].numIHTNULLAccess = 0;
			procTable[i].numIHTNonNULLAcess = 0;
			procTable[i].numPageFault = 0;
			procTable[i].numPageHit = 0;
			procTable[i].firstLevelPageTable = NULL;
			procTable[i].eof_valid = 0;
			procTable[i].tracefp = fopen(argv[s_flag + 4 + i], "r");
		}

		printf("=============================================================\n");
		printf("The One-Level Page Table with LRU Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		// call oneLevelVMSim() with LRU
		oneLevelVMSim(procTableptr, phyMemFrames, 'L');
		
		initPhyMem(phyMemFrames, nFrame);
		// initialize procTable for the simulation
		for(i = 0; i < numProcess; i++) {
			// initialize procTable fields
			procTable[i].traceName = (char *)malloc(strlen(argv[s_flag + 4 + i]) + 1);
			strcpy(procTable[i].traceName, argv[s_flag + 4 + i]);
			procTable[i].pid = i;
			procTable[i].ntraces = 0;
			procTable[i].num2ndLevelPageTable = 0;
			procTable[i].numIHTConflictAccess = 0;
			procTable[i].numIHTNULLAccess = 0;
			procTable[i].numIHTNonNULLAcess = 0;
			procTable[i].numPageFault = 0;
			procTable[i].numPageHit = 0;
			procTable[i].firstLevelPageTable = NULL;
			procTable[i].eof_valid = 0;
			procTable[i].tracefp = fopen(argv[s_flag + 4 + i], "r");
		}

		printf("=============================================================\n");
		printf("The Two-Level Page Table Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		// call twoLevelVMSim()
		twoLevelVMSim(procTableptr, phyMemFrames);

		initPhyMem(phyMemFrames, nFrame);
		// initialize procTable for the simulation
		for(i = 0; i < numProcess; i++) {
			// initialize procTable fields
			procTable[i].traceName = (char *)malloc(strlen(argv[s_flag + 4 + i]) + 1);
			strcpy(procTable[i].traceName, argv[s_flag + 4 + i]);
			procTable[i].pid = i;
			procTable[i].ntraces = 0;
			procTable[i].num2ndLevelPageTable = 0;
			procTable[i].numIHTConflictAccess = 0;
			procTable[i].numIHTNULLAccess = 0;
			procTable[i].numIHTNonNULLAcess = 0;
			procTable[i].numPageFault = 0;
			procTable[i].numPageHit = 0;
			procTable[i].firstLevelPageTable = NULL;
			procTable[i].eof_valid = 0;
			procTable[i].tracefp = fopen(argv[s_flag + 4 + i], "r");
		}

		printf("=============================================================\n");
		printf("The Inverted Page Table Memory Simulation Starts .....\n");
		printf("=============================================================\n");
		// call invertedPageVMsim()
		invertedPageVMSim(procTableptr, phyMemFrames, nFrame);

		initPhyMem(phyMemFrames, nFrame);
		// initialize procTable for the simulation
		for(i = 0; i < numProcess; i++) {
			// initialize procTable fields
			procTable[i].traceName = (char *)malloc(strlen(argv[s_flag + 4 + i]) + 1);
			strcpy(procTable[i].traceName, argv[s_flag + 4 + i]);
			procTable[i].pid = i;
			procTable[i].ntraces = 0;
			procTable[i].num2ndLevelPageTable = 0;
			procTable[i].numIHTConflictAccess = 0;
			procTable[i].numIHTNULLAccess = 0;
			procTable[i].numIHTNonNULLAcess = 0;
			procTable[i].numPageFault = 0;
			procTable[i].numPageHit = 0;
			procTable[i].firstLevelPageTable = NULL;
			procTable[i].eof_valid = 0;
			procTable[i].tracefp = fopen(argv[s_flag + 4 + i], "r");
		}
	}


	for(i = 0; i<numProcess; i++)
		fclose(procTable[i].tracefp);

	return(0);
}
