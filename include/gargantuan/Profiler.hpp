#pragma once

#include "gargantuan/FrameGraph.hpp"

#include <cstdint>

#if defined(TRACY_ENABLE)
	#include <tracy/Tracy.hpp>
#endif

namespace gargantuan {

#if defined(TRACY_ENABLE)
	#define G_PROFILE(name)          \
		ZoneScopedN(name);           \
		::gargantuan::FrameGraph::Scope _gargantuanFrameGraphScope { name }

	#define G_PROFILE_NAMED(fallback, text, length)                                          \
		ZoneScopedN(fallback);                                                               \
		::gargantuan::FrameGraph::NamedScope _gargantuanFrameGraphScope { fallback,          \
																		 (text),             \
																		 (size_t)(length) }; \
		do {                                                                                 \
			if ((length) > 0) {                                                              \
				ZoneName(text, length);                                                      \
			}                                                                                \
		} while (0)

	#define G_PROFILE_NAMED_STABLE(fallback, view)                                   \
		ZoneScopedN(fallback);                                                       \
		do {                                                                         \
			if ((view).size() > 0) {                                                 \
				ZoneName((view).data(), (view).size());                              \
			}                                                                        \
		} while (0);                                                                 \
		::gargantuan::FrameGraph::Scope _gargantuanFrameGraphScope { view }

	#define G_PROFILE_FRAME() FrameMark

	#define G_PROFILE_ACTIVE() TracyIsConnected
#else
	#define G_PROFILE(name) ::gargantuan::FrameGraph::Scope _gargantuanFrameGraphScope { name }
	#define G_PROFILE_NAMED(fallback, text, length)                                       \
		::gargantuan::FrameGraph::NamedScope _gargantuanFrameGraphScope { fallback,        \
																		  (text),          \
																		  (size_t)(length) }
	#define G_PROFILE_NAMED_STABLE(fallback, view) \
		::gargantuan::FrameGraph::Scope _gargantuanFrameGraphScope { view }
	#define G_PROFILE_FRAME() ((void)0)
	#define G_PROFILE_ACTIVE() false
#endif

	#define G_PROFILE_COLLECTING() (G_PROFILE_ACTIVE() || ::gargantuan::FrameGraph::IsEnabled())

	void ProfilerCount(const char *name, uint64_t amount);

	void ProfilerCountTime(const char *name, uint64_t nanoseconds);
}
