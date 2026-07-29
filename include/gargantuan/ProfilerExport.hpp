#pragma once

#include "gargantuan/Profiler.hpp"

#include <string>

namespace gargantuan {
	// Self-contained: no scripts, no fetches, so it opens from the filesystem
	// and survives being moved or attached to something
	std::string BuildProfilerHtml(const Profiler::Snapshot &snapshot);

	// The same numbers as tables, for diffing two runs against each other
	std::string BuildProfilerMarkdown(const Profiler::Snapshot &snapshot);

	// `outReport` says what happened either way, for the caller to show
	bool ExportProfile(const Profiler::Snapshot &snapshot, const std::string &directory, std::string &outReport);
} // namespace gargantuan
