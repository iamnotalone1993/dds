#ifndef MEMORY_DANG5_H
#define MEMORY_DANG5_H

#include <cstdint>		// uint32_t...
#include <vector>		// std::vector...
#include <algorithm>		// std::sort...
#include <utility>		// std::move...
#include <cmath>		// exp2l...
#include "../pool/inc/pool.h"	// dds::pool_ubd_mpsc...

namespace dds
{

namespace dang5
{

/* Macros */
using namespace bclx;

/* Datatypes */
template<typename T>
struct list_seq3
{
	sds::list<block<T>>	contig;
	list_seq2<T>		ncontig;
};

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

	sds::list<block<T>>         			pool_mem;	// allocate global memory
	gptr<block<T>>         				pool_rep;	// deallocate global memory
	gptr<gptr<T>>					reservation;	// be a reser array of the calling unit
	std::vector<gptr<T>>				list_ret;	// contain retired elems
	list_seq3<T>					lheap;		// be per-unit heap
	std::vector<std::vector<gptr<T>>>		buffers;	// be local buffers
	std::vector<dds::pool_ubd_mpsc<T>>		pools;		// be MPSC unbounded pools

        void empty();
};

} /* namespace dang5 */

} /* namespace dds */

template<typename T>
dds::dang5::memory<T>::memory()
{
	if (BCL::rank() == MASTER_UNIT)
		mem_manager = "DANG5";

        gptr<gptr<T>> temp = reservation = BCL::alloc<gptr<T>>(HPS_PER_UNIT);
	for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
	{
		bclx::store(NULL_PTR, temp);
		++temp;
	}

	pool_rep = BCL::alloc<block<T>>(TOTAL_OPS);
	if (pool_rep == nullptr)
	{
		printf("[%lu]ERROR: memory.memory\n", BCL::rank());
		return;
	}
	pool_mem.set(pool_rep, TOTAL_OPS);

	list_ret.reserve(HP_WINDOW);

	for (uint64_t i = 0; i < BCL::nprocs(); ++i)
	{
		pools.push_back(dds::pool_ubd_mpsc<T>(i));
		buffers.push_back(std::vector<gptr<T>>());
	}
}

template<typename T>
dds::dang5::memory<T>::~memory()
{
	for (uint64_t i = 0; i < BCL::nprocs(); ++i)
		pools[i].clear();

        BCL::dealloc<block<T>>(pool_rep);
	BCL::dealloc<gptr<T>>(reservation);
}

template<typename T>
bclx::gptr<T> dds::dang5::memory<T>::malloc()
{
	// if buffers[BCL::rank()] is not empty, return a gptr<T> from it
	if (!buffers[BCL::rank()].empty())
	{
		// tracing
		#ifdef  TRACING
			++elem_ru;
		#endif

		// debugging
		#ifdef	DEBUGGING
			++cnt_buffers;
		#endif
		
		gptr<T> ptr = buffers[BCL::rank()].back();
		buffers[BCL::rank()].pop_back();
		return ptr;
	}

	// if lheap.ncontig is not empty, return a gptr<T> from it
	if (!lheap.ncontig.empty())
	{
		// tracing
		#ifdef	TRACING
			++elem_ru;
		#endif

		// debugging
		#ifdef	DEBUGGING
			++cnt_ncontig;
		#endif

		gptr<header> ptr = lheap.ncontig.pop();
		return {ptr.rank, ptr.ptr + sizeof(header)};
	}

	// if lheap.contig is not empty, return a gptr<T> from it
	if (!lheap.contig.empty())
	{
		// debugging
		#ifdef	DEBUGGING
			++cnt_contig;
		#endif

		gptr<block<T>> ptr = lheap.contig.pop();
		return {ptr.rank, ptr.ptr + sizeof(header)};
	}

	// otherwise, scan my hosted pool to get reclaimed elems if any
	list_seq2<T> slist;
	if (pools[BCL::rank()].get(slist))			
		lheap.ncontig.append(slist);
	
	// if lheap.ncontig is not empty, return a gptr<T> from it
	if (!lheap.ncontig.empty())
	{
		// tracing
		#ifdef	TRACING
			++elem_ru;
		#endif

		// debugging
		#ifdef	DEBUGGING
			++cnt_ncontig2;
		#endif

		gptr<header> ptr = lheap.ncontig.pop();
		return {ptr.rank, ptr.ptr + sizeof(header)};
	}

	// otherwise, get elems from the memory pool
	if (!pool_mem.empty())
	{
		// debugging
		#ifdef	DEBUGGING
			++cnt_pool;
		#endif

		gptr<block<T>> ptr = pool_mem.pop(HP_WINDOW);
        	lheap.contig.set(ptr, HP_WINDOW);

                ptr = lheap.contig.pop();
                return {ptr.rank, ptr.ptr + sizeof(header)};
	}

	// try to scan my hosted pool to get relaimed elems one more
	list_seq2<T> slist2;
	if (pools[BCL::rank()].get(slist2))
		lheap.ncontig.append(slist2);

        // if lheap.ncontig is not empty, return a gptr<T> from it
        if (!lheap.ncontig.empty())
        {
		// tracing
		#ifdef	TRACING
			++elem_ru;
		#endif

                gptr<header> ptr = lheap.ncontig.pop();
                return {ptr.rank, ptr.ptr + sizeof(header)};
        }
	
	// otherwise, return nullptr
	printf("[%lu]ERROR: memory.malloc\n", BCL::rank());
	return nullptr;
}

template<typename T>
void dds::dang5::memory<T>::free(const gptr<T>& ptr)
{
	// debugging
	#ifdef	DEBUGGING
		++cnt_free;
	#endif

	buffers[ptr.rank].push_back(ptr);
	if (ptr.rank != BCL::rank() && buffers[ptr.rank].size() >= HP_WINDOW)
	{
		// debugging
		#ifdef	DEBUGGING
			++cnt_rfree;
		#endif

		pools[ptr.rank].put(buffers[ptr.rank]);
		buffers[ptr.rank].clear();
	}
}

template<typename T>
void dds::dang5::memory<T>::retire(const gptr<T>& ptr)
{
	list_ret.push_back(ptr);
	if (list_ret.size() >= HP_WINDOW)
		empty();
}

template<typename T>
void dds::dang5::memory<T>::op_begin() {}

template<typename T>
void dds::dang5::memory<T>::op_end() {}

template<typename T>
bclx::gptr<T> dds::dang5::memory<T>::try_reserve(const gptr<gptr<T>>& ptr, const gptr<T>& val_old)
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
bclx::gptr<T> dds::dang5::memory<T>::reserve(const gptr<gptr<T>>& ptr)
{
	gptr<T> val_old = bclx::aget_sync(ptr);	// one RMA
	if (val_old == nullptr)
		return nullptr;
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
						return val_old;
					else // if (val_new != val_old)
						val_old = val_new;
				}
			}
			else // if (bclx::aget_sync(temp) != NULL_PTR)
				++temp;

		printf("[%lu]ERROR: memory.reserve\n", BCL::rank());
		return nullptr;
	}
}

template<typename T>
void dds::dang5::memory<T>::unreserve(const gptr<T>& ptr)
{
	if (ptr == nullptr)
		return;
	else // if (ptr != nullptr)
	{
		gptr<gptr<T>> temp = reservation;
		for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
			if (bclx::aget_sync(temp) == ptr)
			{
				bclx::aput_sync(NULL_PTR, temp);
				return;
			}
			else // if (bclx::aget_sync(temp) != ptr)
				++temp;
		printf("[%lu]ERROR: memory.unreserve\n", BCL::rank());
		return;
	}
}

template<typename T>
void dds::dang5::memory<T>::empty()
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

#endif /* MEMORY_DANG5_H */
