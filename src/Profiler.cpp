#include "gargantuan/Profiler.hpp"

namespace gargantuan {
	void ProfilerCount(const char *name, uint64_t amount) {
#if defined(TRACY_ENABLE)
		// Tracy plots are signed; nothing counted here reaches the difference
		TracyPlot(name, (int64_t)amount);
#else
		(void)name;
		(void)amount;
#endif
	}

	void ProfilerCountTime(const char *name, uint64_t nanoseconds) {
#if defined(TRACY_ENABLE)
		// Milliseconds, so it reads the same way as the zones beside it
		TracyPlot(name, (double)nanoseconds / 1000000.0);
#else
		(void)name;
		(void)nanoseconds;
#endif
	}
} // namespace gargantuan
