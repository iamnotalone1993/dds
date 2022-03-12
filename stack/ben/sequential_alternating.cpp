#include <thread>
#include <chrono>
#include <bcl/bcl.hpp>
#include "../inc/stack.h"
#include "../../lib/ta.h"

using namespace dds;
using namespace dds::ebs2_na;

int main()
{
        uint32_t	i;
	uint32_t	value;
	uint64_t	num_ops;
        double		start,
			end,
			elapsed_time,
			total_time;

	BCL::init();

        stack<uint32_t> myStack;
	num_ops = TOTAL_OPS / BCL::nprocs();

	start = MPI_Wtime();

	for (i = 0; i < num_ops / 2; ++i)
	{
		//debugging
		#ifdef DEBUGGING
			printf ("[%lu]%u\n", BCL::rank(), i);
		#endif

		myStack.push(value);
		std::this_thread::sleep_for(std::chrono::microseconds(WORKLOAD));

                myStack.pop(value);
		std::this_thread::sleep_for(std::chrono::microseconds(WORKLOAD));
	}

	end = MPI_Wtime();

	elapsed_time = (end - start) - ((double) num_ops * WORKLOAD) / 1000000;

        total_time = BCL::reduce(elapsed_time, MASTER_UNIT, BCL::max<double>{});
        if (BCL::rank() == MASTER_UNIT)
	{
		printf("*********************************************************\n");
		printf("*\tBENCHMARK\t:\tSequential-alternating\t*\n");
		printf("*\tNUM_UNITS\t:\t%lu\t\t\t*\n", BCL::nprocs());
		printf("*\tNUM_OPS\t\t:\t%lu (ops/unit)\t\t*\n", num_ops);
		printf("*\tWORKLOAD\t:\t%u (us)\t\t\t*\n", WORKLOAD);
		printf("*\tSTACK\t\t:\t%s\t\t\t*\n", stack_name.c_str());
		printf("*\tMEMORY\t\t:\t%s\t\t\t*\n", mem_manager.c_str());
                printf("*\tEXEC_TIME\t:\t%f (s)\t\t*\n", total_time);
                printf("*\tTHROUGHPUT\t:\t%f (ops/s)\t*\n", TOTAL_OPS / total_time);
                printf("*********************************************************\n");
	}

        //tracing
        #ifdef  TRACING
		uint64_t	node_elem_ru,
				node_elem_rc,
				node_succ_cs,
				node_fail_cs,
				node_succ_ea,
				node_fail_ea;
		double		node_elapsed_time,
				node_fail_time;
        	ta::na          na;

		if (na.node_num == 1)
			printf("[Proc %lu]%f (s), %f (s), %lu, %lu, %lu, %lu, %lu, %lu\n",
					BCL::rank(), elapsed_time, fail_time,
					succ_cs, fail_cs,
					succ_ea, fail_ea,
					elem_rc, elem_ru);

		MPI_Reduce(&elem_ru, &node_elem_ru, 1, MPI_UINT64_T, MPI_SUM, MASTER_UNIT, na.nodeComm);
		MPI_Reduce(&elem_rc, &node_elem_rc, 1, MPI_UINT64_T, MPI_SUM, MASTER_UNIT, na.nodeComm);
		MPI_Reduce(&succ_cs, &node_succ_cs, 1, MPI_UINT64_T, MPI_SUM, MASTER_UNIT, na.nodeComm);
		MPI_Reduce(&fail_cs, &node_fail_cs, 1, MPI_UINT64_T, MPI_SUM, MASTER_UNIT, na.nodeComm);
		MPI_Reduce(&succ_ea, &node_succ_ea, 1, MPI_UINT64_T, MPI_SUM, MASTER_UNIT, na.nodeComm);
		MPI_Reduce(&fail_ea, &node_fail_ea, 1, MPI_UINT64_T, MPI_SUM, MASTER_UNIT, na.nodeComm);
		MPI_Reduce(&elapsed_time, &node_elapsed_time, 1, MPI_DOUBLE, MPI_MAX, MASTER_UNIT, na.nodeComm);
		MPI_Reduce(&fail_time, &node_fail_time, 1, MPI_DOUBLE, MPI_MAX, MASTER_UNIT, na.nodeComm);
                if (na.rank == MASTER_UNIT)
                        printf("[Node %d]%f (s), %f (s), %lu, %lu, %lu, %lu, %lu, %lu\n",
					na.node_id, node_elapsed_time, node_fail_time,
					node_succ_cs, node_fail_cs,
					node_succ_ea, node_fail_ea,
					node_elem_rc, node_elem_ru);

		if (na.node_num > 1)
		{
			uint64_t total_elem_ru = BCL::reduce(elem_ru, MASTER_UNIT, BCL::sum<uint64_t>{});
			uint64_t total_elem_rc = BCL::reduce(elem_rc, MASTER_UNIT, BCL::sum<uint64_t>{});
			uint64_t total_succ_cs = BCL::reduce(succ_cs, MASTER_UNIT, BCL::sum<uint64_t>{});
			uint64_t total_fail_cs = BCL::reduce(fail_cs, MASTER_UNIT, BCL::sum<uint64_t>{});
			uint64_t total_succ_ea = BCL::reduce(succ_ea, MASTER_UNIT, BCL::sum<uint64_t>{});
			uint64_t total_fail_ea = BCL::reduce(fail_ea, MASTER_UNIT, BCL::sum<uint64_t>{});
			if (BCL::rank() == MASTER_UNIT)
				printf("[TOTAL]%lu, %lu, %lu, %lu, %lu, %lu\n",
						total_succ_cs, total_fail_cs,
						total_succ_ea, total_fail_ea,
						total_elem_rc, total_elem_ru);
		}
        #endif

	BCL::finalize();

	return 0;
}
