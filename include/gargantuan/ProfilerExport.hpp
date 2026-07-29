#pragma once

#include "gargantuan/Profiler.hpp"

#include <string>

namespace gargantuan {
	// A self-contained page: one flame chart per root, plus the counters. No
	// scripts and no fetches, so it opens from the filesystem and keeps working
	// once it has been moved somewhere else or attached to something.
	std::string BuildProfilerHtml(const Profiler::Snapshot &snapshot);

	// The same numbers as tables, for pasting into an issue or diffing two runs
	// against each other. A picture is the better way to see where a frame
	// goes; text is the better way to argue about whether it moved.
	std::string BuildProfilerMarkdown(const Profiler::Snapshot &snapshot);

	// Writes both next to each other. `outReport` says what happened either
	// way, so the caller can put it on screen rather than only in the log.
	bool ExportProfile(const Profiler::Snapshot &snapshot, const std::string &directory, std::string &outReport);
} // namespace gargantuan
