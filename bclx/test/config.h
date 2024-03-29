#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>	// uint64_t...
#include <string>	// std::string...
#include <cmath>	// exp2l...

namespace dds
{

	/* Macros */
	//#define	TRACING

        /* Constants */
	const uint64_t	MASTER_UNIT	= 0;
	const uint64_t	NUM_ITERS	= 10;
	const uint64_t	NUM_OPS		= exp2l(20);

	/* Varriables */
	//std::string	stack_name;
	
	// tracing
	/*#ifdef  TRACING
        	uint64_t	succ_cs		= 0;
	#endif*/

} /* namespace dds */

#endif /* CONFIG_H */
