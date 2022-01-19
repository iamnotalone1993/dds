#ifndef MEMORY_HP_H
#define MEMORY_HP_H

#include <cstdint>	// uint8_t, uint32_t, uint64_t
#include <vector>	// std::vector
#include <algorithm>	// std::sort, std::unique, std::binary_search
#include <utility>	// std::move

namespace dds
{

namespace hp
{
        template<typename T>
        class memory
        {
        public:
		std::vector<gptr<T>>	list_rec;	// contain reclaimed elems

                memory();
                ~memory();
		gptr<T> malloc();			// allocate global memory
                void free(const gptr<T>&);		// deallocate global memory
		void op_begin();			// indicate the beginning of a concurrent operation
		void op_end();				// indicate the end of a concurrent operation
		bool try_reserve(gptr<T>&,		// try to protect a global pointer from reclamation
				const gptr<gptr<T>>&);
		void unreserve(const gptr<T>&);		// stop protecting a global pointer

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

                void scan();
        };

} /* namespace hp */

} /* namespace dds */

template<typename T>
dds::hp::memory<T>::memory()
{
	if (BCL::rank() == MASTER_UNIT)
		mem_manager = "HP";

        gptr<gptr<T>> temp = reservation = BCL::alloc<gptr<T>>(HPS_PER_UNIT);
	for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
	{
		BCL::store(NULL_PTR, temp);
		++temp;
	}

	pool = pool_rep = BCL::alloc<T>(TOTAL_OPS);
        capacity = pool.ptr + TOTAL_OPS * sizeof(T);

	list_ret.reserve(HP_WINDOW);
}

template<typename T>
dds::hp::memory<T>::~memory()
{
        BCL::dealloc<T>(pool_rep);
	BCL::dealloc<gptr<T>>(reservation);
}

template<typename T>
dds::gptr<T> dds::hp::memory<T>::malloc()
{
        // determine the global address of the new element
        if (!list_rec.empty())
	{
		// tracing
		#ifdef	TRACING
			++elem_re;
		#endif

		gptr<T> addr = list_rec.back();
		list_rec.pop_back();
                return addr;
	}
        else // the list of reclaimed global memory is empty
        {
                if (pool.ptr < capacity)
                        return pool++;
                else // if (pool.ptr == capacity)
		{
			// try one more to reclaim global memory
			scan();
			if (!list_rec.empty())
			{
				// tracing
				#ifdef  TRACING
					++elem_re;
				#endif

				gptr<T> addr = list_rec.back();
				list_rec.pop_back();
				return addr;
			}
		}
        }
	return nullptr;
}

template<typename T>
void dds::hp::memory<T>::free(const gptr<T>& addr)
{
	list_ret.push_back(addr);
	if (list_ret.size() >= HP_WINDOW)
		scan();
}

template<typename T>
void dds::hp::memory<T>::op_begin()
{
	/* No-op */
}

template<typename T>
void dds::hp::memory<T>::op_end()
{
	/* No-op */
}

template<typename T>
bool dds::hp::memory<T>::try_reserve(gptr<T>& ptr, const gptr<gptr<T>>& atom)
{
	gptr<gptr<T>> temp = reservation;
        for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
		if (BCL::aget_sync(temp) == NULL_PTR)
		{
			gptr<T> ptr_new;
			while (true)
			{
                		BCL::aput_sync(ptr, temp);
				ptr_new = BCL::aget_sync(atom);
				if (ptr_new == nullptr)
				{
					BCL::aput_sync(NULL_PTR, temp);
					return false;
				}
				if (ptr_new == ptr)
					return true;
				ptr = ptr_new;
			}
		}
		else // if (BCL::aget_sync(temp) != NULL_PTR)
                	++temp;
	printf("HP:Error\n");
	return false;
}

template<typename T>
void dds::hp::memory<T>::unreserve(const gptr<T>& ptr)
{
	gptr<gptr<T>> temp = reservation;
	for (uint32_t i = 0; i < HPS_PER_UNIT; ++i)
		if (BCL::aget_sync(temp) == ptr)
		{
			BCL::aput_sync(NULL_PTR, temp);
			return;
		}
		else // if (BCL::aget_sync(temp) != ptr)
			++temp;
}

template<typename T>
void dds::hp::memory<T>::scan()
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
			addr = BCL::aget_sync(hp_temp);
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
			list_rec.push_back(addr);
	}

	// Stage 4
	list_ret = std::move(new_dlist);
}

#endif /* MEMORY_HP_H */
