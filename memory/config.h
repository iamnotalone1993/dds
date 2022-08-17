#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>	// uint64_t...
#include <string>	// std::string...
#include <cmath>	// exp2l...

namespace dds
{

	/* Macros */
	#define		MEM_DANG3

        /* Constants */
	const uint64_t	MASTER_UNIT	= 0;
	const uint64_t	TOTAL_OPS	= exp2l(23);

	/* Varriables */
	std::string	mem_manager;
	
	// tracing
	/*#ifdef  TRACING
        	uint64_t	succ_cs		= 0;
		uint64_t	succ_ea 	= 0;
		uint64_t	fail_cs 	= 0;
		uint64_t	fail_ea 	= 0;
		double		fail_time	= 0;
		uint64_t	elem_rc		= 0;
		uint64_t	elem_ru		= 0;
	#endif*/

} /* namespace dds */

#endif /* CONFIG_H */
