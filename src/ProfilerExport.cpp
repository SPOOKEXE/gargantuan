#include "gargantuan/ProfilerExport.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace gargantuan {
	namespace {
		std::string Escape(std::string_view text) {
			std::string escaped;
			escaped.reserve(text.size());
			for (char character : text) {
				switch (character) {
				case '&':
					escaped += "&amp;";
					break;
				case '<':
					escaped += "&lt;";
					break;
				case '>':
					escaped += "&gt;";
					break;
				case '"':
					escaped += "&quot;";
					break;
				default:
					escaped += character;
				}
			}
			return escaped;
		}

		std::string Fixed(double value, int places) {
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.*f", places, value);
			return buffer;
		}

		// A zone's share of the frame, which is what the widths are drawn from
		double Share(const Profiler::Snapshot &snapshot, double milliseconds) {
			if (snapshot.FrameMilliseconds <= 0.0) {
				return 0.0;
			}
			return std::clamp(milliseconds / snapshot.FrameMilliseconds, 0.0, 1.0);
		}

		// From the name, so a zone keeps its colour between exports
		std::string Hue(std::string_view name) {
			uint32_t hash = 2166136261u;
			for (char character : name) {
				hash = (hash ^ (uint32_t)(unsigned char)character) * 16777619u;
			}
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "hsl(%u 62%% 52%%)", hash % 360);
			return buffer;
		}

		void WriteHtmlZone(
			std::ostringstream &out, const Profiler::Snapshot &snapshot, size_t index, double offset
		) {
			const Profiler::Zone &zone = snapshot.Zones[index];
			double width = Share(snapshot, zone.Milliseconds);

			out << "<div class=\"zone\" style=\"left:" << Fixed(offset * 100.0, 4) << "%;width:"
				<< Fixed(width * 100.0, 4) << "%;top:" << (zone.Depth * 22) << "px;background:" << Hue(zone.Name)
				<< "\" title=\"" << Escape(zone.Name) << " &#10;" << Fixed(zone.Milliseconds, 3)
				<< " ms/frame&#10;" << Fixed(zone.CallsPerFrame, 1) << " calls/frame\"><span>"
				<< Escape(zone.Name) << "</span></div>\n";

			// End to end inside the parent; the gap left is its own time
			double childOffset = offset;
			for (size_t child : zone.Children) {
				WriteHtmlZone(out, snapshot, child, childOffset);
				childOffset += Share(snapshot, snapshot.Zones[child].Milliseconds);
			}
		}

		void WriteMarkdownZone(
			std::ostringstream &out, const Profiler::Snapshot &snapshot, size_t index
		) {
			const Profiler::Zone &zone = snapshot.Zones[index];

			out << "| ";
			for (int indent = 0; indent < zone.Depth; indent++) {
				out << "&nbsp;&nbsp;&nbsp;&nbsp;";
			}
			out << Escape(zone.Name) << " | " << Fixed(zone.Milliseconds, 3) << " | "
				<< Fixed(Share(snapshot, zone.Milliseconds) * 100.0, 1) << "% | "
				<< Fixed(zone.CallsPerFrame, 1) << " |\n";

			for (size_t child : zone.Children) {
				WriteMarkdownZone(out, snapshot, child);
			}
		}

		int DeepestDepth(const Profiler::Snapshot &snapshot) {
			int deepest = 0;
			for (const auto &zone : snapshot.Zones) {
				deepest = std::max(deepest, zone.Depth);
			}
			return deepest;
		}

		bool WriteFile(const std::string &path, const std::string &contents) {
			SDL_IOStream *stream = SDL_IOFromFile(path.c_str(), "wb");
			if (!stream) {
				SDL_Log("Could not open %s: %s", path.c_str(), SDL_GetError());
				return false;
			}

			bool wrote = SDL_WriteIO(stream, contents.data(), contents.size()) == contents.size();
			SDL_CloseIO(stream);

			if (!wrote) {
				SDL_Log("Could not write %s: %s", path.c_str(), SDL_GetError());
			}
			return wrote;
		}
	} // namespace

	std::string BuildProfilerHtml(const Profiler::Snapshot &snapshot) {
		std::ostringstream out;

		out << "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
			<< "<title>Gargantuan profile</title>\n<style>\n"
			<< "body{background:#14141a;color:#e8e8f0;font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;"
			<< "margin:0;padding:24px}\n"
			<< "h1{font-size:16px;margin:0 0 4px}\n"
			<< "p.meta{color:#8a8a9c;margin:0 0 20px}\n"
			<< "h2{font-size:13px;margin:24px 0 8px;color:#9fd4ff}\n"
			<< ".chart{position:relative;width:100%;background:#1c1c26;border-radius:4px;overflow:hidden}\n"
			<< ".zone{position:absolute;height:20px;border-radius:2px;overflow:hidden;white-space:nowrap;"
			<< "box-sizing:border-box;border:1px solid rgba(0,0,0,.35);cursor:default}\n"
			<< ".zone span{padding:0 5px;line-height:20px;font-size:11px;color:#0d0d12;font-weight:600}\n"
			<< ".zone:hover{filter:brightness(1.25)}\n"
			<< "table{border-collapse:collapse;width:100%;margin-top:8px}\n"
			<< "th,td{text-align:left;padding:3px 10px 3px 0;border-bottom:1px solid #262633}\n"
			<< "th{color:#8a8a9c;font-weight:600}\n"
			<< "td.n{text-align:right;font-variant-numeric:tabular-nums}\n"
			<< "</style>\n</head>\n<body>\n";

		out << "<h1>Gargantuan profile</h1>\n<p class=\"meta\">"
			<< Fixed(snapshot.FrameMilliseconds, 3) << " ms per frame &middot; " << snapshot.Frames
			<< " frames averaged over " << Fixed(snapshot.Seconds, 2) << " s &middot; widths are share of a frame"
			<< "</p>\n";

		// One chart per root: the roots are not slices of each other
		for (size_t root : snapshot.Roots) {
			const Profiler::Zone &zone = snapshot.Zones[root];

			out << "<h2>" << Escape(zone.Name) << " &mdash; " << Fixed(zone.Milliseconds, 3) << " ms</h2>\n"
				<< "<div class=\"chart\" style=\"height:" << (DeepestDepth(snapshot) + 1) * 22 << "px\">\n";
			WriteHtmlZone(out, snapshot, root, 0.0);
			out << "</div>\n";
		}

		if (!snapshot.Counters.empty()) {
			out << "<h2>Counters</h2>\n<table>\n<tr><th>Name</th><th class=\"n\">Per frame</th>"
				<< "<th class=\"n\">Total</th></tr>\n";
			for (const auto &counter : snapshot.Counters) {
				out << "<tr><td>" << Escape(counter.Name) << "</td><td class=\"n\">"
					<< Fixed(counter.PerFrame, 1) << "</td><td class=\"n\">" << counter.Total << "</td></tr>\n";
			}
			out << "</table>\n";
		}

		out << "</body>\n</html>\n";
		return out.str();
	}

	std::string BuildProfilerMarkdown(const Profiler::Snapshot &snapshot) {
		std::ostringstream out;

		out << "# Gargantuan profile\n\n"
			<< "- **" << Fixed(snapshot.FrameMilliseconds, 3) << " ms** per frame\n"
			<< "- " << snapshot.Frames << " frames averaged over " << Fixed(snapshot.Seconds, 2) << " s\n"
			<< "- Percentages are share of a whole frame, not of the row above\n\n";

		out << "## Timings\n\n| Zone | ms/frame | Share | Calls/frame |\n| --- | ---: | ---: | ---: |\n";
		for (size_t root : snapshot.Roots) {
			WriteMarkdownZone(out, snapshot, root);
		}

		if (!snapshot.Counters.empty()) {
			out << "\n## Counters\n\n| Name | Per frame | Total |\n| --- | ---: | ---: |\n";
			for (const auto &counter : snapshot.Counters) {
				out << "| " << Escape(counter.Name) << " | " << Fixed(counter.PerFrame, 1) << " | "
					<< counter.Total << " |\n";
			}
		}

		return out.str();
	}

	bool ExportProfile(const Profiler::Snapshot &snapshot, const std::string &directory, std::string &outReport) {
		if (snapshot.Empty()) {
			outReport = "nothing profiled yet";
			return false;
		}

		std::error_code error;
		std::filesystem::create_directories(directory, error);

		std::string html = directory + "/profile.html";
		std::string markdown = directory + "/profile.md";

		bool wroteHtml = WriteFile(html, BuildProfilerHtml(snapshot));
		bool wroteMarkdown = WriteFile(markdown, BuildProfilerMarkdown(snapshot));

		if (wroteHtml && wroteMarkdown) {
			outReport = html + " and " + markdown;
			return true;
		}

		outReport = "could not write the profile";
		return false;
	}
} // namespace gargantuan
