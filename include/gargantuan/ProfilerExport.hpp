#pragma once

#include "gargantuan/Profiler.hpp"

#include <string>

namespace gargantuan {
	// Self-contained HTML: no scripts or fetches.
	std::string BuildProfilerHtml(const Profiler::Snapshot &snapshot);

	// Tables matching the HTML values for run diffs.
	std::string BuildProfilerMarkdown(const Profiler::Snapshot &snapshot);

	// `outReport` describes success or failure for display.
	bool ExportProfile(const Profiler::Snapshot &snapshot, const std::string &directory, std::string &outReport);
} // namespace gargantuan
