#include <cstdint>		// uint32_t...
#include <bcl/bcl.hpp>		// BCL::init...
#include "../config.h"		// TOTAL_OPS...

using namespace dds;

int main()
{
        uint32_t 	i,
			j;
	gptr<gptr<int>>	ptr;
	double		start,
			end,
			elapsed_time_comm = 0,
			elapsed_time_comm2 = 0,
			elapsed_time_comm3 = 0,
			total_time_comm,
			total_time_comm2,
			total_time_comm3;

	// Initialize PGAS programming model
        BCL::init();

	// Check if the program is valid
	if (BCL::nprocs() != 2)
	{
		printf("ERROR: The number of units must be 2!\n");
		return -1;
	}

	// Initialize shared global memory
	if (BCL::rank() == MASTER_UNIT)
		ptr = BCL::alloc<gptr<int>>(1);
	ptr = BCL::broadcast(ptr, MASTER_UNIT);

	// Communication 1
	BCL::barrier();		// Barrier
       	start = MPI_Wtime();	// Start timing
	if (BCL::rank() == 1)
	{
		for (i = 0; i < NUM_ITERS; ++i)
			for (j = 0; j < NUM_OPS; ++j)
				BCL::rput_sync({i, j}, ptr);
	}
	end = MPI_Wtime();	// Stop timing
	elapsed_time_comm += end - start;

	// Communication 2
	BCL::barrier();		// Barrier
	start = MPI_Wtime();	// Start timing
	if (BCL::rank() == 1)
	{
		for (i = 0; i < NUM_ITERS; ++i)
			for (j = 0; j < NUM_OPS - 1; ++j)
				BCL::rput_async({i, j}, ptr);
		BCL::rput_sync({i, j}, ptr);
	}
	end = MPI_Wtime();	// Stop timing
	elapsed_time_comm2 += end - start;

	// Communication 3
	BCL::barrier();		// Barrier
	start = MPI_Wtime();	// Starting timing
	if (BCL::rank() == 1)
	{
		for (i = 0; i < NUM_ITERS; ++i)
			BCL::rput_sync({i, j});
	}
	end = MPI_Wtime();	// Stop timing
	elapsed_time_comm3 += end - start;

	total_time_comm = BCL::reduce(elapsed_time_comm, MASTER_UNIT, BCL::max<double>{});
	total_time_comm2 = BCL::reduce(elapsed_time_comm2, MASTER_UNIT, BCL::max<double>{});
	total_time_comm3 = BCL::reduce(elapsed_time_comm3, MASTER_UNIT, BCL::max<double>{});
	if (BCL::rank() == MASTER_UNIT)
	{
		printf("*********************************************************\n");
		printf("*\tBENCHMARK\t:\tPrimitive Sequence 2\t*\n");
		printf("*\tNUM_UNITS\t:\t%lu\t\t\t*\n", BCL::nprocs());
		printf("*\tNUM_ITERS\t:\t%lu\t\t\t*\n", NUM_ITERS);
		printf("*\tNUM_OPS\t\t:\t%lu (puts)\t\t*\n", NUM_OPS);
		printf("*\tTOTAL_TIME\t:\t%f (s)\t\t*\n", total_time_comm / NUM_ITERS);
		printf("*\tTOTAL_TIME2\t:\t%f (s)\t\t*\n", total_time_comm2 / NUM_ITERS);
		printf("*\tTOTAL_TIME3\t:\t%f (s)\t\t*\n", total_time_comm3 / NUM_ITERS);
                printf("*********************************************************\n");
	}

	// Finalize PGAS programming model
	BCL::finalize();

	return 0;
}
