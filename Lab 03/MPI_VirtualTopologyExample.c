//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MPI_Virtual_Topology_Example
// ---------------------------------------------------------------------------------------------------------------------------------------
//
// Author - Vishnu Monn for FIT3143, Monash University
// Last modified date - 2th October 2025
// Note: Generative AI was not used to write the code below
// License: None
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <mpi.h>
#include <math.h>
#include <stdbool.h>

#define SHIFT_ROW 0
#define SHIFT_COL 1
#define DISP 1
#define NO_NEIGHBOR -2     // Value to indicate no neighbor

#define MSG_REPORT 1
#define MSG_DISABLE 2
#define MSG_TERMINATE 3

int master_io(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols);
int slave_io(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols);
static inline int TwoDToOneD(int i, int j, int cols){return (i * cols) + j;}
void printGrid(int rows, int cols, int **ppData, int masterCol);

int main(int argc, char **argv)
{
	int rank, size;
	int nrows, ncols;
	MPI_Comm new_comm;
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	if (argc == 3) {
        nrows = atoi(argv[1]);
        ncols = atoi(argv[2]);
        if ((nrows * ncols) != (size-1)) {
            if (rank == 0)
                printf("ERROR: nrows*ncols=%d * %d = %d != %d\n", nrows, ncols, nrows * ncols, size);
			MPI_Finalize();
			return 0;
        }
    } else {
        nrows = ncols = 0;
    }
	// Seed the pseudo-random number generator 
	srand(time(NULL) + rank);

	MPI_Comm_split( MPI_COMM_WORLD, rank == (size-1), 0, &new_comm);
	if (rank == size-1) 
		master_io( MPI_COMM_WORLD, new_comm, nrows, ncols);
	else
		slave_io( MPI_COMM_WORLD, new_comm, nrows, ncols);
	MPI_Finalize();
	return 0;
}

void printGrid(int rows, int cols, int **ppData, int masterCol)
{
	int idx;
	for(int i = 0; i < rows; i++){
		for(int j = 0; j < cols; j++){
			idx = TwoDToOneD(i, j, cols);
			if(ppData[idx][0] == MSG_REPORT){
				printf("(%d,%d)A:%d", i, j, ppData[idx][1]); // A --> Active
			}else if(ppData[idx][0] == MSG_DISABLE){
				printf("(%d,%d)D:%d", i, j, ppData[idx][1]); // D --> Disabled
			}else if(ppData[idx][0] == MSG_TERMINATE){
				printf("(%d,%d)X:%d", i, j, ppData[idx][1]); // X --> Terminate
			}
			printf("\t");
		}
		printf("\n");
	}
	for(int i = 0; i < cols; i++){
		if(masterCol == i){
			printf("M_Position\t");
		}else{
			printf("__________\t");
		}	
	}
	printf("\n\n");
	fflush(stdout);
}

int master_io(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols)
{
	int global_rank;
	int local_rank;
	int global_size;
	int local_size;
	int tick = 0;
	
	MPI_Status status;
	MPI_Status probe_status;
	int flag = 0;
	int recv_Val = 0;
	int send_Val = 0;
	int SlaveActiveCount = 0;
	int SlaveTerminateCount = 0;
	int **ppSlaveGrid;
	FILE *pFile = NULL;
	int terminateSignalSent = 0;
	int master_col = 0;
	int direction = 0;
	
	MPI_Comm_size(world_comm, &global_size);
	MPI_Comm_rank(world_comm, &global_rank);
	
	MPI_Comm_size(comm, &local_size);
	MPI_Comm_rank(comm, &local_rank);

	if(nrows == 0 && ncols == 0){
		nrows = ncols = (int)sqrt(global_size-1);
	}
	
	SlaveActiveCount = nrows * ncols;
	ppSlaveGrid = (int**)malloc(SlaveActiveCount * sizeof(int*));
	for(int i = 0; i < SlaveActiveCount; i++){
		ppSlaveGrid[i] = (int*)malloc(2 * sizeof(int));
	}

	for(int i = 0; i < SlaveActiveCount; i++){
		ppSlaveGrid[i][0] = 1; // 1 --> Active, 2 --> Disabled, 3 --> Terminate
		ppSlaveGrid[i][1] = 0; // Reported value received from slave
	}
	
	printf("Initial grid layout:\n");
	fflush(stdout);
	printGrid(nrows, ncols, ppSlaveGrid, master_col);
	printf("Commence reading from slaves and updating the grid:\n");
	fflush(stdout);

	while(1){
		MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_comm, &flag, &probe_status);
		if(flag == 1){
			flag = 0; // reset the flag;
			switch(probe_status.MPI_TAG)
			{
				case MSG_REPORT:{
					MPI_Recv(&recv_Val, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_REPORT, world_comm, &status);
					ppSlaveGrid[probe_status.MPI_SOURCE][0] = MSG_REPORT;
					ppSlaveGrid[probe_status.MPI_SOURCE][1] = recv_Val;
					break;
				}
				case MSG_DISABLE:{
					MPI_Recv(&recv_Val, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_DISABLE, world_comm, &status);
					ppSlaveGrid[probe_status.MPI_SOURCE][0] = MSG_DISABLE;
					ppSlaveGrid[probe_status.MPI_SOURCE][1] = recv_Val;
					SlaveActiveCount = SlaveActiveCount - 1;
					break;
				}				
				case MSG_TERMINATE:{
					MPI_Recv(&recv_Val, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_TERMINATE, world_comm, &status);
					ppSlaveGrid[probe_status.MPI_SOURCE][0] = MSG_TERMINATE;
					ppSlaveGrid[probe_status.MPI_SOURCE][1] = recv_Val;
					SlaveTerminateCount++;
					break;
				}
				default:{
					printf("Master. Error! Invalid Tag received\n");
					break;
				}
			}
		}
		
		// Print the slave grid
		printGrid(nrows, ncols, ppSlaveGrid, master_col);

		// Send disable signal to the slaves
		for(int i = 0; i < nrows; i++){
			sleep(2 + i); // Default 2 second sleep per row, + 1 for the next row step
			for(int j = 0; j < ncols; j++){
				int index = TwoDToOneD(i, j, ncols); 
				if(ppSlaveGrid[index][0] == MSG_DISABLE || ppSlaveGrid[index][0] == MSG_TERMINATE){
					continue;
				}
				printf("Master. Sending disable signal to rank %d\n", index);
				fflush(stdout);
				send_Val = master_col;
				MPI_Send(&send_Val, 1, MPI_INT, index, MSG_DISABLE, world_comm);
			}
		}
		
		// Set a position for Master relative to the column of the slave grid	
		if (master_col == 0) {
			direction =+ 1; 
		}
		if (master_col == ncols-1){
			direction =- 1;
		}
		master_col += direction;
		printf("Master. Col position %d\n", master_col);
		
		if(SlaveActiveCount == 0 && terminateSignalSent == 0){
			printf("Master: No more active slaves. Sending Terminate signal to all slaves with no delay\n");
			fflush(stdout);
			for(int i = 0; i < (global_size-1); i++){
				send_Val = MSG_TERMINATE;
				MPI_Send(&send_Val, 1, MPI_INT, i, MSG_TERMINATE, world_comm);
			}
			terminateSignalSent = 1;
		}

		tick++;
		printf("Tick: %d\n", tick);
	
		if(SlaveTerminateCount == (global_size-1)){
			// exit the loop
			break;
		}
		sleep(1);
	}
	printf("All Slaves terminated. MPI Master Process finished\n");
	fflush(stdout);	
	
	// Clean up
	for(int i = 0; i < SlaveActiveCount; i++){
		free(ppSlaveGrid[i]);
	}
	free(ppSlaveGrid);
    return 0;
}

int slave_io(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols)
{
	int ndims = 2, global_size, global_rank, local_size, local_rank, reorder, my_cart_rank, ierr;
    int nbr_i_lo, nbr_i_hi;  // Top and bottom neighbors
    int nbr_j_lo, nbr_j_hi;  // Left and right neighbors
    MPI_Comm comm2D;
    int dims[ndims], coord[ndims];
    int wrap_around[ndims];
	int randNum, avgNum;
	int recvNum_left = 0, recvNum_right = 0, recvNum_top = 0, recvNum_bottom = 0;
	MPI_Status status;
	MPI_Request request;
	MPI_Status probe_status;
	int flag = 0;
	int recv_Val = 0;
	int slave_tick = 0;
	int loop = 1;
	int isDisabled = 0;
	
	MPI_Comm_size(world_comm, &global_size);
	MPI_Comm_rank(world_comm, &global_rank);
	MPI_Comm_size(comm, &local_size);
	MPI_Comm_rank(comm, &local_rank);
	if(nrows == 0 && ncols == 0){
		nrows = ncols = (int)sqrt(local_size);
        dims[0] = dims[1] = 0;
	}else{
		dims[0] = nrows;
        dims[1] = ncols;
	}
	
	MPI_Dims_create(local_size, ndims, dims);
    wrap_around[0] = wrap_around[1] = 0;  // No wrap-around neighbors
    reorder = 0;  // Do not allow reordering
    ierr = MPI_Cart_create(comm, ndims, dims, wrap_around, reorder, &comm2D);
	if(ierr != MPI_SUCCESS)
		printf("Error in cart create\n");
    
    // Get the coordinates and neighbors
    MPI_Cart_coords(comm2D, local_rank, ndims, coord);
    MPI_Cart_rank(comm2D, coord, &my_cart_rank);
    MPI_Cart_shift(comm2D, SHIFT_ROW, DISP, &nbr_i_lo, &nbr_i_hi);  // Top and bottom neighbors
    MPI_Cart_shift(comm2D, SHIFT_COL, DISP, &nbr_j_lo, &nbr_j_hi);  // Left and right neighbors

    // Replace MPI_PROC_NULL with NO_NEIGHBOR (-2) for non-existent neighbors
    nbr_i_lo = (nbr_i_lo == MPI_PROC_NULL) ? NO_NEIGHBOR : nbr_i_lo;
    nbr_i_hi = (nbr_i_hi == MPI_PROC_NULL) ? NO_NEIGHBOR : nbr_i_hi;
    nbr_j_lo = (nbr_j_lo == MPI_PROC_NULL) ? NO_NEIGHBOR : nbr_j_lo;
    nbr_j_hi = (nbr_j_hi == MPI_PROC_NULL) ? NO_NEIGHBOR : nbr_j_hi;
	
	//printf("Slave. Local size: %d. Local rank: %d. Cart rank: %d. Coord: (%d, %d). Left: %d. Right: %d. Top: %d. Bottom: %d\n", local_size, local_rank, my_cart_rank, coord[0], coord[1], nbr_j_lo, nbr_j_hi, nbr_i_lo, nbr_i_hi);

    while(1) {
		// Check for terminate signal from Master
		MPI_Iprobe((global_size-1), MPI_ANY_TAG, world_comm, &flag, &probe_status);
		if(flag == 1){
			flag = 0; // reset the flag;
			switch(probe_status.MPI_TAG)
			{
				case MSG_DISABLE:{
					MPI_Recv(&recv_Val, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_DISABLE, world_comm, &status);
					fflush(stdout);
					
					if(recv_Val == coord[1] && isDisabled == 0){
						printf("Slave :%d: Receive Disable signal. Master at Col %d. Respond to acknowledge\n", global_rank, recv_Val);
						fflush(stdout);
						recv_Val = MSG_DISABLE;
						MPI_Send(&recv_Val, 1, MPI_INT, global_size-1, MSG_DISABLE, world_comm);
						isDisabled = 1;
					}
					break;
				}				
				case MSG_TERMINATE:{
					MPI_Recv(&recv_Val, 1, MPI_INT, probe_status.MPI_SOURCE, MSG_TERMINATE, world_comm, &status);
					printf("Slave :%d: Receive Terminate signal %d. Respond to acknowledge\n", global_rank, recv_Val);
					fflush(stdout);
					MPI_Send(&recv_Val, 1, MPI_INT, global_size-1, MSG_TERMINATE, world_comm);
					loop = 0; // prepare to exit the while loop
					break;
				}
				default:{
					printf("Slave. Error! Invalid Tag received\n");
					break;
				}
			}
		}

		if(loop == 0){
			// exit the while loop
			break;
		}
		
		// Some sample work to communiate with neighbours
		// Exchange numbers with neighbours and compute average number
		// Note: If disabled, the neighbourhood communication remains active, but no more reports send to Master.
		randNum  = rand() % 50;

		MPI_Request send_request[4];
        MPI_Request receive_request[4];
        MPI_Status send_status[4];
        MPI_Status receive_status[4];
	
		MPI_Isend(&randNum, 1, MPI_INT, nbr_i_lo, 0, comm2D, &send_request[0]);
		MPI_Isend(&randNum, 1, MPI_INT, nbr_i_hi, 0, comm2D, &send_request[1]);
		MPI_Isend(&randNum, 1, MPI_INT, nbr_j_lo, 0, comm2D, &send_request[2]);
		MPI_Isend(&randNum, 1, MPI_INT, nbr_j_hi, 0, comm2D, &send_request[3]);

		MPI_Irecv(&recvNum_top, 1, MPI_INT, nbr_i_lo, 0, comm2D, &receive_request[0]);
		MPI_Irecv(&recvNum_bottom, 1, MPI_INT, nbr_i_hi, 0, comm2D, &receive_request[1]);
		MPI_Irecv(&recvNum_left, 1, MPI_INT, nbr_j_lo, 0, comm2D, &receive_request[2]);
		MPI_Irecv(&recvNum_right, 1, MPI_INT, nbr_j_hi, 0, comm2D, &receive_request[3]);
		
		// Delay for 1 second so that the asynch messages are sent/received
		sleep(1);
		int test_flag = 0;
		for(int i = 0; i < 4; i++){
			MPI_Test(&send_request[i], &test_flag, MPI_STATUS_IGNORE);
			if(test_flag != 1){
				MPI_Cancel(&send_request[i]);
			}
			MPI_Test(&receive_request[i], &test_flag, MPI_STATUS_IGNORE);
			if(test_flag != 1){
				MPI_Cancel(&receive_request[i]);
			}
		}

		if(isDisabled == 0){
			avgNum = (recvNum_left + recvNum_right + recvNum_top + recvNum_bottom) / 4;
			// Send the report back to master every 4 seconds
			if(slave_tick % 4 == 0){
				sleep(2 + coord[0]); // Default 2 second sleep + 1 second per row based on coordinate position
				MPI_Send(&avgNum, 1, MPI_INT, global_size-1, MSG_REPORT, world_comm);
			}
		}
		sleep(1);
		slave_tick++;
    }
	
	MPI_Comm_free(&comm2D);
    return 0;
}