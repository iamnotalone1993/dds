#ifndef MEMORY_BL2_H
#define MEMORY_BL2_H

#include <cstdint>	// uint64_t...
#include <vector>	// std::vector...
#include <algorithm>	// std::sort...
#include <utility>	// std::move...

namespace dds
{

namespace bl2
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

	gptr<T>         	pool;		// allocate global memory
	gptr<T>         	pool_rep;	// deallocate global memory
	uint64_t		capacity;	// contain global memory capacity (bytes)
	gptr<gptr<T>>		reservation;	// be an array of hazard pointers of the calling unit
	std::vector<gptr<T>>	list_ret;	// contain retired elems
	std::vector<gptr<T>>	list_rec;	// contain reclaimed elems
	topology		topo;		// contain node information

	void empty();
};

} /* namespace bl2 */

} /* namespace dds */

template<typename T>
dds::bl2::memory<T>::memory()
{
	if (BCL::rank() == MASTER_UNIT)
		mem_manager = "BASELINE2";

        gptr<gptr<T>> temp = reservation = BCL::alloc<gptr<T>>(HPS_PER_UNIT);
	for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
	{
		store(NULL_PTR, temp);
		++temp;
	}

	pool = pool_rep = BCL::alloc<T>(TOTAL_OPS);
        capacity = pool.ptr + TOTAL_OPS * sizeof(T);

	list_ret.reserve(HP_WINDOW);
}

template<typename T>
dds::bl2::memory<T>::~memory()
{
        BCL::dealloc<T>(pool_rep);
	BCL::dealloc<gptr<T>>(reservation);
}

template<typename T>
bclx::gptr<T> dds::bl2::memory<T>::malloc()
{
	// determine the global address of the new element
	if (topo.node_num == 1)
	{
		if (pool.ptr < capacity)
			return pool++;
	}
	else // if (topo.node_num > 1)
	{
		if (pool.ptr < capacity)
		{
			gptr<T> ptr = pool++;
			if (topo.node_id % 2 == 0)
				return {ptr.rank + topo.size, ptr.ptr};
			else // if (topo.node_id % 2 != 0)
				return {ptr.rank - topo.size, ptr.ptr};
		}
	}
	return nullptr;
}

template<typename T>
void dds::bl2::memory<T>::free(const gptr<T>& ptr)
{
	list_ret.push_back(ptr);
	if (list_ret.size() >= HP_WINDOW)
		empty();
}

template<typename T>
void dds::bl2::memory<T>::op_begin()
{
	/* No-op */
}

template<typename T>
void dds::bl2::memory<T>::op_end()
{
	/* No-op */
}

template<typename T>
bclx::gptr<T> dds::bl2::memory<T>::try_reserve(const gptr<gptr<T>>& ptr, const gptr<T>& val_old)
{
	if (val_old == nullptr)
		return nullptr;
	else // if (val_old != nullptr)
	{
		gptr<gptr<T>> temp = reservation;
		for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
			if (aget_sync(temp) == NULL_PTR)
			{
				aput_sync(val_old, temp);
				gptr<T> val_new = aget_sync(ptr);	// one RMA
				if (val_new == nullptr || val_new != val_old)
					aput_sync(NULL_PTR, temp);
				return val_new;
			}
			else // if (aget_sync(temp) != NULL_PTR)
				++temp;
		printf("HP:Error\n");
		return nullptr;
	}
}

template<typename T>
bclx::gptr<T> dds::bl2::memory<T>::reserve(const gptr<gptr<T>>& ptr)
{
	gptr<T> val_old = aget_sync(ptr);	// one RMA
	if (val_old == nullptr)
		return nullptr;
	else // if (val_old != nullptr)
	{
		gptr<gptr<T>> temp = reservation;
		for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
			if (aget_sync(temp) == NULL_PTR)
			{
				gptr<T> val_new;
				while (true)
				{
					aput_sync(val_old, temp);
					val_new = aget_sync(ptr);	// one RMA
					if (val_new == nullptr)
					{
						aput_sync(NULL_PTR, temp);
						return nullptr;
					}
					else if (val_new == val_old)
						return val_old;
					else // if (val_new != val_old)
						val_old = val_new;
				}
			}
			else // if (aget_sync(temp) != NULL_PTR)
				++temp;
		printf("HP:Error\n");
		return nullptr;
	}
}

template<typename T>
void dds::bl2::memory<T>::unreserve(const gptr<T>& ptr)
{
	if (ptr == nullptr)
		return;
	else // if (ptr != nullptr)
	{
		gptr<gptr<T>> temp = reservation;
		for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
			if (aget_sync(temp) == ptr)
			{
				aput_sync(NULL_PTR, temp);
				return;
			}
			else // if (aget_sync(temp) != ptr)
				++temp;
		printf("HP:Error\n");
		return;
	}
}

template<typename T>
void dds::bl2::memory<T>::empty()
{	
	std::vector<gptr<T>>	plist;		// contain non-null hazard pointers
	std::vector<gptr<T>>	new_dlist;	// be dlist after finishing the Scan function
	gptr<gptr<T>> 		hp_temp;	// a temporary variable
	gptr<T>			addr;		// a temporary variable

	plist.reserve(HP_TOTAL);
	new_dlist.reserve(HP_TOTAL);

	// Stage 1
	hp_temp.ptr = reservation.ptr;
	for (uint64_t i = 0; i < BCL::nprocs(); ++i)
	{
		hp_temp.rank = i;
		for (uint32_t j = 0; j < HPS_PER_UNIT; ++j)
		{
			addr = aget_sync(hp_temp);
			if (addr != NULL_PTR)
				plist.push_back(addr);
			++hp_temp;
		}
	}
	
	// Stage 2
	std::sort(plist.begin(), plist.end());
	plist.resize(std::unique(plist.begin(), plist.end()) - plist.begin());

	// Stage 3
        while (!list_ret.empty())
	{
		addr = list_ret.back();
		list_ret.pop_back();
		if (std::binary_search(plist.begin(), plist.end(), addr))
			new_dlist.push_back(addr);
		else
		{
			// tracing
			#ifdef	TRACING
				++elem_rc;
			#endif

			list_rec.push_back(addr);
		}
	}

	// Stage 4
	list_ret = std::move(new_dlist);
}

#endif /* MEMORY_BL2_H */