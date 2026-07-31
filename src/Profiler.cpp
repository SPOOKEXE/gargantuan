#include "gargantuan/Profiler.hpp"

namespace gargantuan {
	void ProfilerCount(const char *name, uint64_t amount) {
		// Publish to both attached Tracy clients and the local frame graph.
		FrameGraph::RecordCounter(name, (double)amount, false);

#if defined(TRACY_ENABLE)
		// Tracy plots are signed.
		TracyPlot(name, (int64_t)amount);
#endif
	}

	void ProfilerCountTime(const char *name, uint64_t nanoseconds) {
		FrameGraph::RecordCounter(name, (double)nanoseconds / 1000000.0, true);

#if defined(TRACY_ENABLE)
		TracyPlot(name, (double)nanoseconds / 1000000.0);
#endif
	}
}
