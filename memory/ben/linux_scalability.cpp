#include <cstdint>		// uint64_t...
#include <cmath>		// exp2l...
#include <bclx/bclx.hpp>	// bclx::gptr...

/* Benchmark-specific tuning parameters */
const uint64_t	BLOCK_SIZE	= 64;
const uint64_t	NUM_ITERS	= 7 * exp2l(25);

int main()
{
	BCL::init();	// initialize the PGAS runtime

	bclx::gptr<void>	ptr;
	bclx::timer		tim;
	bclx::memory		mem;

	bclx::barrier_sync();	// synchronize
	tim.start();	// start the timer
	for (uint64_t i = 0; i < NUM_ITERS / BCL::nprocs(); ++i)
	{
		ptr = mem.malloc(BLOCK_SIZE);
		mem.free(ptr);
	}
	tim.stop();	// stop the timer
	double elapsed_time = tim.get();	// calculate the elapsed time
	bclx::barrier_sync();	// synchronize

	double total_time = bclx::reduce(elapsed_time, bclx::MASTER_UNIT, BCL::max<double>{});
	if (BCL::rank() == bclx::MASTER_UNIT)
	{
		printf("*****************************************************************\n");
		printf("*\tBENCHMARK\t:\tLinux-scalability\t\t*\n");
		printf("*\tNUM_UNITS\t:\t%lu\t\t\t\t*\n", BCL::nprocs());
		printf("*\tNUM_OPS\t\t:\t%lu (ops/unit)\t\t*\n", 2 * NUM_ITERS / BCL::nprocs());
		printf("*\tNUM_ITERS\t:\t%lu\t\t\t*\n", NUM_ITERS);
		printf("*\tBLOCK_SIZE\t:\t%lu (bytes)\t\t\t*\n", BLOCK_SIZE);
		printf("*\tMEM_ALLOC\t:\t%s\t\t\t*\n", mem.get_name());
		printf("*\tEXEC_TIME\t:\t%f (s)\t\t\t*\n", total_time);
		printf("*\tTHROUGHPUT\t:\t%f (ops/s)\t*\n", 2 * NUM_ITERS / total_time);
		printf("*****************************************************************\n");
	}

        // debugging
        #ifdef  DEBUGGING
        if (BCL::rank() == bclx::MASTER_UNIT)
        {
                //printf("[%lu]cnt_buffers = %lu\n", BCL::rank(), bclx::cnt_buffers);
                printf("[%lu]cnt_ncontig = %lu\n", BCL::rank(), bclx::cnt_ncontig);
		//printf("[%lu]cnt_ncontig2 = %lu\n", BCL::rank(), bclx::cnt_ncontig2);
                printf("[%lu]cnt_contig = %lu\n", BCL::rank(), bclx::cnt_contig);
                printf("[%lu]cnt_bcl = %lu\n", BCL::rank(), bclx::cnt_bcl);
		printf("[%lu]cnt_lfree = %lu\n", BCL::rank(), bclx::cnt_lfree);
		//printf("[%lu]cnt_rfree = %lu\n", BCL::rank(), bclx::cnt_rfree);
        }
        #endif

	BCL::finalize();	// finalize the PGAS runtime

	return 0;
}
