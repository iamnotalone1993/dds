#ifndef MEMORY_DANG4_H
#define MEMORY_DANG4_H

#include <cstdint>			// uint32_t...
#include <vector>			// std::vector...
#include <algorithm>			// std::sort...
#include <utility>			// std::move...
#include <cmath>			// exp2l...
//#include "../lib/list.h"		// sds::list_gptr...
#include "../pool/inc/pool_ubd_spsc.h"	// dds::queue_ubd_spsc...

namespace dds
{

namespace dang4
{

using namespace bclx;

template<typename T>
class memory
{
public:
	memory();
	~memory();
	gptr<T> malloc();				// allocate global memory
	void free(const gptr<T>&);			// deallocate global memory
	void retire(const gptr<T>&);			// retire a global pointer
	void op_begin();				// indicate the beginning of a concurrent operation
	void op_end();					// indicate the end of a concurrent operation
	gptr<T> try_reserve(const gptr<gptr<T>>&,	// try to to protect a global pointer from reclamation
			const gptr<T>&);
	gptr<T> reserve(const gptr<gptr<T>>&);		// try to protect a global pointer from reclamation
	void unreserve(const gptr<T>&);			// stop protecting a global pointer

private:
	const gptr<T>		NULL_PTR 	= nullptr;
        const uint32_t		HPS_PER_UNIT 	= 1;
        const uint64_t		HP_TOTAL	= BCL::nprocs() * HPS_PER_UNIT;
        const uint64_t		HP_WINDOW	= HP_TOTAL * 2;
	//const uint64_t		MSG_SIZE	= exp2l(13);

	dds::list_seq<block<T>>         		pool_mem;	// allocate global memory
	gptr<block<T>>         				pool_rep;	// deallocate global memory
	gptr<gptr<T>>					reservation;	// be a reser array of the calling unit
	std::vector<gptr<T>>				list_ret;	// contain retired elems
	dds::list_seq3<block<T>>			lheap;		// be per-unit heap
	std::vector<std::vector<gptr<T>>>		buffers;	// be local buffers
	std::vector<std::vector<dds::pool_ubd_spsc<T>>>	pools;		// be SPSC unbounded pools

        void empty();
};

} /* namespace dang4 */

} /* namespace dds */

template<typename T>
dds::dang4::memory<T>::memory()
{
	if (BCL::rank() == MASTER_UNIT)
		mem_manager = "DANG4";

        gptr<gptr<T>> temp = reservation = BCL::alloc<gptr<T>>(HPS_PER_UNIT);
	for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
	{
		bclx::store(NULL_PTR, temp);
		++temp;
	}

	pool_rep = BCL::alloc<block<T>>(TOTAL_OPS);
	pool_mem.set(pool_rep, TOTAL_OPS);

	for (uint64_t i = 0; i < BCL::nprocs(); ++i)
	{
		pools.push_back(std::vector<dds::pool_ubd_spsc<T>>());
		for (uint64_t j = 0; j < BCL::nprocs(); ++j)
			pools[i].push_back(dds::pool_ubd_spsc<T>(i));
	}

	for (uint64_t i = 0; i < BCL::nprocs(); ++i)
	{
		buffers.push_back(std::vector<gptr<T>>());
		buffers[i].reserve(HP_WINDOW);
	}
}

template<typename T>
dds::dang4::memory<T>::~memory()
{
	for (uint64_t i = 0; i < BCL::nprocs(); ++i)
		for (uint64_t j = 0; j < BCL::nprocs(); ++j)
			pools[i][j].clear();

        BCL::dealloc<block<T>>(pool_rep);
	BCL::dealloc<gptr<T>>(reservation);
}

template<typename T>
dds::gptr<T> dds::dang4::memory<T>::malloc()
{
	// if ncontig is not empty, return a gptr<T> from it
	if (!lheap.ncontig.empty())
	{
		// tracing
		#ifdef	TRACING
			++elem_ru;
		#endif

		/* debugging *//*
		printf("[%lu]CP1\n", BCL::rank());
		/**/

		gptr<header> ptr = lheap.ncontig.pop_front();
		return {ptr.rank, ptr.ptr + sizeof(header)};
	}

	// if contig is not empty, return a gptr<T> from it
	if (!lheap.contig.empty())
	{
		/* debugging *//*
		printf("[%lu]CP2\n", BCL::rank());
		/**/
	
		gptr<block<T>> ptr = lheap.contig.pop_front();
		return {ptr.rank, ptr.ptr + sizeof(header)};
	}

	// otherwise, scan all pools to get reclaimed elems if any
	for (uint64_t i = 0; i < pools[BCL::rank()].size(); ++i)
	{
		list_seq2 slist;
		if (pools[BCL::rank()][i].get(slist))
			lheap.ncontig.append(slist);
	}
	
	// if ncontig is not empty, return a gptr<T> from it
	if (!lheap.ncontig.empty())
	{
		// tracing
		#ifdef	TRACING
			++elem_ru;
		#endif

		/* debugging *//*
		printf("[%lu]CP3\n", BCL::rank());
		/**/

		gptr<header> ptr = lheap.ncontig.pop_front();
		return {ptr.rank, ptr.ptr + sizeof(header)};
	}

	// otherwise, get elems from the memory pool
	if (!pool_mem.empty())
	{
		/* debugging *//*
		printf("[%lu]CP4\n", BCL::rank());
		/**/

		gptr<block<T>> ptr = pool_mem.pop_front(HP_WINDOW);
        	lheap.contig.set(ptr, HP_WINDOW);

                ptr = lheap.contig.pop_front();
                return {ptr.rank, ptr.ptr + sizeof(header)};
	}

	/* debugging */
	printf("[%lu]CP5\n", BCL::rank());
	/**/

	// try to scan all pools to get relaimed elems one more
	for (uint64_t i = 0; i < pools[BCL::rank()].size(); ++i)
	{
		list_seq2 slist;
		if (pools[BCL::rank()][i].get(slist))
			lheap.ncontig.append(slist);
        }

        // if ncontig is not empty, return a gptr<T> from it
        if (!lheap.ncontig.empty())
        {
		// tracing
		#ifdef	TRACING
			++elem_ru;
		#endif

                gptr<header> ptr = lheap.ncontig.pop_front();
                return {ptr.rank, ptr.ptr + sizeof(header)};
        }
	
	// otherwise, return nullptr
	printf("[%lu]ERROR: memory.malloc\n", BCL::rank());
	return nullptr;
}

template<typename T>
void dds::dang4::memory<T>::free(const gptr<T>& ptr)
{
	buffers[ptr.rank].push_back(ptr);
	if (buffers[ptr.rank].size() >= HP_WINDOW)
	{
		pools[ptr.rank][BCL::rank()].put(buffers[ptr.rank]);
		buffers[ptr.rank].clear();
	}
}

template<typename T>
void dds::dang4::memory<T>::retire(const gptr<T>& ptr)
{
	list_ret.push_back(ptr);
	if (list_ret.size() >= HP_WINDOW)
		empty();
}

template<typename T>
void dds::dang4::memory<T>::op_begin()
{
	/* No-op */
}

template<typename T>
void dds::dang4::memory<T>::op_end()
{
	/* No-op */
}

template<typename T>
dds::gptr<T> dds::dang4::memory<T>::try_reserve(const gptr<gptr<T>>& ptr, const gptr<T>& val_old)
{
	if (val_old == nullptr)
		return nullptr;
	else // if (val_old != nullptr)
	{
		gptr<gptr<T>> temp = reservation;
		for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
			if (bclx::aget_sync(temp) == NULL_PTR)
			{
				bclx::aput_sync(val_old, temp);
				gptr<T> val_new = bclx::aget_sync(ptr);	// one RMA
				if (val_new == nullptr || val_new != val_old)
					bclx::aput_sync(NULL_PTR, temp);
				return val_new;
			}
			else // if (bclx::aget_sync(temp) != NULL_PTR)
				++temp;
		printf("[%lu]ERROR: memory.try_reserve\n", BCL::rank());
		return nullptr;
	}
}

template<typename T>
dds::gptr<T> dds::dang4::memory<T>::reserve(const gptr<gptr<T>>& ptr)
{
	gptr<T> val_old = bclx::aget_sync(ptr);	// one RMA
	if (val_old == nullptr)
	{
		/* debugging *//*
		if (BCL::rank() == 0)
			printf("[%lu]CP1\n", BCL::rank());
		/**/

		return nullptr;
	}
	else // if (val_old != nullptr)
	{
		gptr<gptr<T>> temp = reservation;
		for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
			if (bclx::aget_sync(temp) == NULL_PTR)
			{
				gptr<T> val_new;
				while (true)
				{
					bclx::aput_sync(val_old, temp);
					val_new = bclx::aget_sync(ptr);	// one RMA
					if (val_new == nullptr)
					{
						bclx::aput_sync(NULL_PTR, temp);
						return nullptr;
					}
					else if (val_new == val_old)
					{
						/* debugging *//*
						if (BCL::rank() == 0)
							printf("[%lu]reserve: <%u, %u>\n", BCL::rank(), val_old.rank, val_old.ptr);
						/**/

						return val_old;
					}
					else // if (val_new != val_old)
						val_old = val_new;
				}
			}
			else // if (bclx::aget_sync(temp) != NULL_PTR)
				++temp;

		/* debugging *//*
		if (BCL::rank() == 0)
		{
			gptr<T> test = bclx::aget_sync(reservation);
			printf("<%u, %u>\n", test.rank, test.ptr);
		}
		/**/

		printf("[%lu]ERROR: memory.reserve\n", BCL::rank());
		return nullptr;
	}
}

template<typename T>
void dds::dang4::memory<T>::unreserve(const gptr<T>& ptr)
{
	if (ptr == nullptr)
	{
		// debugging
		if (BCL::rank() == 0)
			printf("[%lu]CP11\n", BCL::rank());

		return;
	}
	else // if (ptr != nullptr)
	{
		gptr<gptr<T>> temp = reservation;
		for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
			if (bclx::aget_sync(temp) == ptr)
			{

				/* debugging *//*
				if (BCL::rank() == 0)
					printf("[%lu]unreserve: <%u, %u>\n", BCL::rank(), ptr.rank, ptr.ptr);
				/**/

				bclx::aput_sync(NULL_PTR, temp);

				/* debugging *//*
				if (BCL::rank() == 0)
				{
					gptr<T> test = bclx::aget_sync(temp);
					printf("[%lu]CP12: <%u, %u>\n", BCL::rank(), test.rank, test.ptr);
				}
				/**/

				return;
			}
			else // if (bclx::aget_sync(temp) != ptr)
				++temp;
		printf("[%lu]ERROR: memory.unreserve\n", BCL::rank());
		return;
	}
}

template<typename T>
void dds::dang4::memory<T>::empty()
{	
	std::vector<gptr<T>>	plist;		// contain non-null hazard pointers
	std::vector<gptr<T>>	new_dlist;	// be dlist after finishing the Scan function
	gptr<gptr<T>> 		hp_temp;	// a temporary variable
	gptr<T>			ptr;		// a temporary variable

	plist.reserve(HP_TOTAL);
	new_dlist.reserve(HP_TOTAL);

	// Stage 1
	hp_temp.ptr = reservation.ptr;
	for (uint64_t i = 0; i < BCL::nprocs(); ++i)
	{
		hp_temp.rank = i;
		for (uint32_t j = 0; j < HPS_PER_UNIT; ++j)
		{
			ptr = bclx::aget_sync(hp_temp);
			if (ptr != NULL_PTR)
				plist.push_back(ptr);
			++hp_temp;
		}
	}
	
	// Stage 2
	std::sort(plist.begin(), plist.end());
	plist.resize(std::unique(plist.begin(), plist.end()) - plist.begin());

	// Stage 3
        while (!list_ret.empty())
	{
		ptr = list_ret.back();
		list_ret.pop_back();
		if (std::binary_search(plist.begin(), plist.end(), ptr))
			new_dlist.push_back(ptr);
		else
		{
			// tracing
			#ifdef	TRACING
				++elem_rc;
			#endif

			free(ptr);
		}
	}

	// Stage 4
	list_ret = std::move(new_dlist);
}

#endif /* MEMORY_DANG4_H */
