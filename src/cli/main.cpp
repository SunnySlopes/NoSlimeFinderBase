// SPDX-License-Identifier: MIT
// Copyright (c) 2026 SunnySlopes

/*
 * NoSlimeFinderBase — standalone command line front end
 * ============================================================================
 *
 * Wraps the same C API that the GUI's JNI shim uses. Everything that used to
 * live in the GUI's jni/ folder (search core, cubiomes, SlimeOptimizer) is
 * compiled into the NoSlimeFinderBase static library; this file only parses
 * arguments, prints progress and writes the text report.
 *
 * The report is written to stdout and, when --out is given, to that file too.
 * Progress goes to stderr so `--out report.txt > /dev/null` still shows it.
 */

#include "noslimefinder/base.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

constexpr const char *kCliVersion = NOSLIME_BASE_VERSION_STRING;

/* Quick standalone default: a 4096x4096 square around the origin. The GUI
 * always passes its own (larger) bounds explicitly. */
constexpr int kDefaultMin = -2048;
constexpr int kDefaultMax = 2047;

constexpr int kMaxThreads = 256;
constexpr int kMaxResults = 1000;
constexpr int kWorldCoord = 30000000;
constexpr int64_t kMaxSearchSide = 60000001;

volatile std::sig_atomic_t g_interrupted = 0;

void handleInterrupt(int) { g_interrupted = 1; }

struct Options
{
    long long seed = 0;
    bool seedSet = false;

    int minX = kDefaultMin;
    int maxX = kDefaultMax;
    int minZ = kDefaultMin;
    int maxZ = kDefaultMax;
    bool boundsSet = false;
    int square = 0;

    int threads = 0;
    int mc = -1;

    int biomeY = -63;
    int minArea = 49000;
    int maxResults = 100;
    int candidateLimit = 0;

    std::vector<int32_t> avoidIds;

    std::string outPath;

    bool quiet = false;
    bool listBiomes = false;
    bool help = false;
    bool version = false;
};

void printUsage(std::ostream &os)
{
    os <<
        "NoSlimeFinderBase CLI " << kCliVersion << "\n"
        "\n"
        "Finds AFK points whose 24 < distance <= 128 spawn ring has the least\n"
        "slime-chunk area, optionally avoiding configured biomes.\n"
        "\n"
        "Usage:\n"
        "  NoSlimeFinderBase-cli --seed <seed> [options]\n"
        "  NoSlimeFinderBase-cli --list-biomes [--mc <version>]\n"
        "\n"
        "Required for a search:\n"
        "  --seed <int64>          Minecraft world seed (decimal, may be negative)\n"
        "\n"
        "AFK rectangle (inclusive block coordinates; default -2048..2047):\n"
        "  --min-x <int>           default -2048\n"
        "  --max-x <int>           default 2047\n"
        "  --min-z <int>           default -2048\n"
        "  --max-z <int>           default 2047\n"
        "  --square <side>         symmetric area centered on 0; cannot be combined\n"
        "                          with explicit min/max bounds\n"
        "\n"
        "Search parameters:\n"
        "  --threads <n>           worker threads, 1..256; default all CPU cores\n"
        "  --mc <id|version>       biome version, default newest (26.3)\n"
        "  --biome-y <int>         biome sampling height, default -63\n"
        "  --min-area <blocks>     minimum area, default 49000 (max 49640)\n"
        "  --max-results <n>       maximum hits, default 100 (max 1000)\n"
        "  --avoid <ids>           comma-separated biome ids to avoid,\n"
        "                          e.g. --avoid 14,52 (may be repeated)\n"
        "  --candidate-limit <n>   coarse candidate cap, 0 = automatic\n"
        "\n"
        "Output:\n"
        "  --out <file>            also write the text report to <file>\n"
        "  --quiet                 suppress the progress line\n"
        "  --list-biomes           list Overworld biomes for --mc and exit\n"
        "  --help, -h              show this help\n"
        "  --version               show CLI and API version\n"
        "\n"
        "Version names: 1.18, 1.18.2, 1.19, 1.19.2, 1.19.4, 1.20, 1.20.6,\n"
        "  1.21.3, 1.21.4, 1.21.5, 26.1, 26.2, 26.3, latest; a raw cubiomes\n"
        "  MCVersion number is also accepted.\n";
}

[[noreturn]] void usageError(const std::string &message)
{
    std::cerr << "error: " << message << "\n\n";
    printUsage(std::cerr);
    std::exit(2);
}

[[noreturn]] void runtimeError(const std::string &message)
{
    std::cerr << "error: " << message << "\n";
    std::exit(1);
}

[[noreturn]] void searchError(const std::string &message)
{
    std::cerr << "error: " << message << "\n";
    std::exit(3);
}

template <typename T>
bool parseInteger(std::string_view text, T &value)
{
    if (text.empty())
        return false;
    const char *first = text.data();
    const char *last = first + text.size();
    if (*first == '+')
        ++first;
    if (first == last)
        return false;
    const std::from_chars_result result = std::from_chars(first, last, value);
    return result.ec == std::errc() && result.ptr == last;
}

std::string_view requireValue(int argc, char **argv, int &index,
                              std::string_view option)
{
    if (index + 1 >= argc)
        usageError("missing value for " + std::string(option));
    ++index;
    return argv[index];
}

void appendAvoidIds(const std::string &text, std::vector<int32_t> &out)
{
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t comma = text.find(',', start);
        const size_t end = comma == std::string::npos ? text.size() : comma;
        const std::string_view item(text.data() + start, end - start);
        int value = 0;
        if (!parseInteger(item, value) || value < 0 || value > 255)
            usageError("invalid biome id in --avoid: '" + std::string(item) + "'");
        if (std::find(out.begin(), out.end(), value) == out.end())
            out.push_back(value);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
}

Options parseOptions(int argc, char **argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            options.help = true;
        }
        else if (arg == "--version")
        {
            options.version = true;
        }
        else if (arg == "--list-biomes")
        {
            options.listBiomes = true;
        }
        else if (arg == "--seed")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInteger(value, options.seed))
                usageError("invalid --seed: " + std::string(value));
            options.seedSet = true;
        }
        else if (arg == "--min-x" || arg == "--max-x" || arg == "--min-z" ||
                 arg == "--max-z")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            int parsed = 0;
            if (!parseInteger(value, parsed))
                usageError("invalid " + std::string(arg) + ": " + std::string(value));
            if (arg == "--min-x") options.minX = parsed;
            else if (arg == "--max-x") options.maxX = parsed;
            else if (arg == "--min-z") options.minZ = parsed;
            else options.maxZ = parsed;
            options.boundsSet = true;
        }
        else if (arg == "--square")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInteger(value, options.square))
                usageError("invalid --square: " + std::string(value));
            if (options.square <= 0 ||
                static_cast<long long>(options.square) > kMaxSearchSide)
                usageError("--square must be between 1 and " +
                           std::to_string(kMaxSearchSide));
        }
        else if (arg == "--threads")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInteger(value, options.threads))
                usageError("invalid --threads: " + std::string(value));
            if (options.threads < 1 || options.threads > kMaxThreads)
                usageError("--threads must be between 1 and " +
                           std::to_string(kMaxThreads));
        }
        else if (arg == "--mc")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            const std::string text(value);
            options.mc = noslime_parse_mc(text.c_str());
            if (options.mc < 0)
                usageError("invalid --mc: " + text);
        }
        else if (arg == "--biome-y")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInteger(value, options.biomeY))
                usageError("invalid --biome-y: " + std::string(value));
            if (options.biomeY < -64 || options.biomeY > 319)
                usageError("--biome-y must be between -64 and 319");
        }
        else if (arg == "--min-area")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInteger(value, options.minArea))
                usageError("invalid --min-area: " + std::string(value));
            if (options.minArea < 0 || options.minArea > NOSLIME_RING_TOTAL)
                usageError("--min-area must be between 0 and " +
                           std::to_string(NOSLIME_RING_TOTAL));
        }
        else if (arg == "--max-results")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInteger(value, options.maxResults))
                usageError("invalid --max-results: " + std::string(value));
            if (options.maxResults < 1 || options.maxResults > kMaxResults)
                usageError("--max-results must be between 1 and " +
                           std::to_string(kMaxResults));
        }
        else if (arg == "--avoid")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            appendAvoidIds(std::string(value), options.avoidIds);
        }
        else if (arg == "--candidate-limit")
        {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInteger(value, options.candidateLimit))
                usageError("invalid --candidate-limit: " + std::string(value));
            if (options.candidateLimit < 0)
                usageError("--candidate-limit must not be negative");
        }
        else if (arg == "--out")
        {
            options.outPath = std::string(requireValue(argc, argv, i, arg));
            if (options.outPath.empty())
                usageError("--out requires a non-empty path");
        }
        else if (arg == "--quiet")
        {
            options.quiet = true;
        }
        else
        {
            usageError("unknown option: " + std::string(arg));
        }
    }
    return options;
}

const char *stageName(int64_t stage)
{
    switch (stage)
    {
    case NOSLIME_STAGE_IDLE: return "idle";
    case NOSLIME_STAGE_PREFILTER: return "prefilter";
    case NOSLIME_STAGE_SCAN: return "scan";
    case NOSLIME_STAGE_COARSE: return "coarse";
    case NOSLIME_STAGE_EXACT: return "exact";
    case NOSLIME_STAGE_DONE: return "done";
    case NOSLIME_STAGE_CANCELLED: return "cancelled";
    case NOSLIME_STAGE_ERROR: return "error";
    default: return "?";
    }
}

std::string versionLabel(int mc)
{
    std::ostringstream out;
    out << noslime_mc_name(mc) << "(" << mc << ")";
    return out.str();
}

std::string formatReport(const Options &options, const NoSlimeParams &params,
                         const std::vector<NoSlimeResult> &hits,
                         double elapsedSeconds)
{
    std::ostringstream out;
    out << "# NoSlimeFinderBase CLI " << kCliVersion << "\n";
    out << "# seed=" << options.seed
        << " afk=[" << params.afk_x0 << "," << params.afk_x1 << "]x["
        << params.afk_z0 << "," << params.afk_z1 << "]"
        << " ring=" << NOSLIME_RING_INNER << "<d<=" << NOSLIME_RING_OUTER
        << " threads=" << params.threads
        << " mc=" << versionLabel(params.mc)
        << " biome_y=" << params.biome_y
        << " min_area=" << params.min_area
        << " max_results=" << params.max_results
        << " avoid=";
    if (params.avoid_count <= 0)
        out << "none";
    else
    {
        for (int32_t i = 0; i < params.avoid_count; ++i)
        {
            if (i > 0)
                out << ",";
            out << params.avoid_ids[i];
        }
    }
    out << " hits=" << hits.size()
        << " elapsed=" << std::fixed << std::setprecision(3) << elapsedSeconds
        << "s\n";
    out << "# columns: x z area slime chunks avoid_x avoid_y tp\n";
    out << "x\tz\tarea\tslime\tchunks\tavoid_x\tavoid_y\ttp\n";
    for (const NoSlimeResult &r : hits)
    {
        out << r.x << '\t' << r.z << '\t' << r.area << '\t' << r.slime << '\t'
            << r.chunks << '\t' << r.avoid_x << '\t' << r.avoid_y << '\t'
            << "/tp " << r.x << " -64 " << r.z << " area=" << r.area
            << " slime=" << r.slime << " chunks=" << r.chunks << " avoid="
            << r.avoid_x << "+" << r.avoid_y << '\n';
    }
    return out.str();
}

int listBiomes(const Options &options)
{
    const int mc = options.mc >= 0 ? options.mc : noslime_newest_mc();
    const int32_t count = noslime_list_biomes(mc, nullptr, 0);
    if (count <= 0)
    {
        std::cerr << "error: no biomes for mc=" << versionLabel(mc) << "\n";
        return 1;
    }
    std::vector<NoSlimeBiome> biomes(static_cast<size_t>(count));
    const int32_t filled = noslime_list_biomes(mc, biomes.data(), count);
    for (int32_t i = 0; i < filled; ++i)
        std::cout << biomes[static_cast<size_t>(i)].id << '|'
                  << biomes[static_cast<size_t>(i)].name << '\n';
    std::cerr << "biomes: " << filled << " for mc=" << versionLabel(mc) << "\n";
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    Options options = parseOptions(argc, argv);

    if (options.help)
    {
        printUsage(std::cout);
        return 0;
    }
    if (options.version)
    {
        std::cout << "NoSlimeFinderBase-cli " << kCliVersion
                  << " (API " << NOSLIME_BASE_VERSION_MAJOR << "."
                  << NOSLIME_BASE_VERSION_MINOR << "."
                  << NOSLIME_BASE_VERSION_PATCH << ")\n";
        return 0;
    }
    if (options.listBiomes)
        return listBiomes(options);

    if (!options.seedSet)
        usageError("--seed is required");
    if (options.square > 0 && options.boundsSet)
        usageError("--square cannot be combined with explicit --min-x/--max-x/"
                   "--min-z/--max-z");

    if (options.square > 0)
    {
        const int side = options.square;
        const int min = -(side / 2);
        options.minX = min;
        options.maxX = min + side - 1;
        options.minZ = min;
        options.maxZ = min + side - 1;
    }

    if (options.minX > options.maxX)
        usageError("--min-x must be <= --max-x");
    if (options.minZ > options.maxZ)
        usageError("--min-z must be <= --max-z");
    if (options.minX < -kWorldCoord || options.maxX > kWorldCoord ||
        options.minZ < -kWorldCoord || options.maxZ > kWorldCoord)
        usageError("search bounds are outside the Minecraft world border (" +
                   std::to_string(kWorldCoord) + ")");

    const long long width =
        static_cast<long long>(options.maxX) - options.minX + 1;
    const long long height =
        static_cast<long long>(options.maxZ) - options.minZ + 1;
    if (width <= 0 || height <= 0)
        usageError("search area must be non-empty");
    if (width > kMaxSearchSide || height > kMaxSearchSide)
        usageError("search area is too large (maximum " +
                   std::to_string(kMaxSearchSide) + " blocks per side)");

    if (options.mc < 0)
        options.mc = noslime_newest_mc();
    if (options.threads <= 0)
    {
        const unsigned int hardware = std::thread::hardware_concurrency();
        options.threads = static_cast<int>(
            std::min<unsigned int>(kMaxThreads, hardware == 0 ? 1u : hardware));
    }

    NoSlimeParams params{};
    params.seed = static_cast<int64_t>(options.seed);
    params.afk_x0 = options.minX;
    params.afk_x1 = options.maxX;
    params.afk_z0 = options.minZ;
    params.afk_z1 = options.maxZ;
    params.threads = options.threads;
    params.biome_y = options.biomeY;
    params.max_results = options.maxResults;
    params.min_area = options.minArea;
    params.candidate_limit = options.candidateLimit;
    params.mc = options.mc;
    params.avoid_ids = options.avoidIds.empty() ? nullptr : options.avoidIds.data();
    params.avoid_count = static_cast<int32_t>(options.avoidIds.size());

    /* Open the report file before starting so a bad path fails fast. */
    std::ofstream outFile;
    if (!options.outPath.empty())
    {
        outFile.open(options.outPath, std::ios::out | std::ios::trunc);
        if (!outFile)
            runtimeError("cannot open --out file: " + options.outPath);
    }

    if (!options.quiet)
    {
        std::cerr << "[base] searching seed=" << options.seed
                  << " afk=[" << params.afk_x0 << "," << params.afk_x1 << "]x["
                  << params.afk_z0 << "," << params.afk_z1 << "]"
                  << " threads=" << params.threads
                  << " mc=" << versionLabel(params.mc)
                  << " biome_y=" << params.biome_y
                  << " min_area=" << params.min_area
                  << " avoid=" << params.avoid_count << "\n";
    }

    std::signal(SIGINT, handleInterrupt);

    std::atomic<bool> finished{false};
    std::atomic<bool> cancelSent{false};
    std::atomic<bool> printedProgress{false};
    const auto started = std::chrono::steady_clock::now();

    std::thread progressThread([&]() {
        int64_t lastStageValue = -1;
        while (!finished.load(std::memory_order_relaxed))
        {
            int64_t progress[8] = {0};
            noslime_get_progress(progress);

            if (g_interrupted == 1 && !cancelSent.exchange(true))
                noslime_cancel();

            if (!options.quiet && progress[0] != NOSLIME_STAGE_IDLE)
            {
                if (progress[0] != lastStageValue)
                {
                    if (printedProgress.load())
                        std::fputc('\n', stderr);
                    lastStageValue = progress[0];
                }
                const double percent =
                    progress[5] > 0
                        ? std::min(100.0, 100.0 * static_cast<double>(progress[4]) /
                                             static_cast<double>(progress[5]))
                        : 0.0;
                std::fprintf(stderr, "\r[%-9s %5.1f%%] %lld/%lld hits=%lld elapsed=%.1fs%s",
                             stageName(progress[0]), percent,
                             static_cast<long long>(progress[4]),
                             static_cast<long long>(progress[5]),
                             static_cast<long long>(progress[7]),
                             static_cast<double>(progress[6]) / 1000.0,
                             progress[2] != 0 ? " [paused]" : "");
                std::fflush(stderr);
                printedProgress.store(true);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    const int rc = noslime_run(&params);
    finished.store(true);
    progressThread.join();
    if (!options.quiet && printedProgress.load())
        std::fputc('\n', stderr);

    const double elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
            .count();

    if (rc == 1 || g_interrupted == 1)
    {
        std::cerr << "cancelled\n";
        return 4;
    }
    if (rc < 0)
    {
        const char *message = noslime_last_error();
        searchError(message != nullptr && message[0] != '\0' ? message
                                                             : "search failed");
    }

    const int32_t count = noslime_result_count();
    std::vector<NoSlimeResult> hits(static_cast<size_t>(count > 0 ? count : 0));
    if (count > 0)
        noslime_get_results(hits.data(), count);

    const std::string report =
        formatReport(options, params, hits, elapsedSeconds);

    std::cout << report;
    std::cout.flush();
    if (outFile.is_open())
    {
        outFile << report;
        outFile.flush();
        if (!outFile)
            runtimeError("failed to write --out file: " + options.outPath);
    }

    std::cerr << "[base] done in " << std::fixed << std::setprecision(0)
              << (elapsedSeconds * 1000.0) << " ms, " << hits.size()
              << " hit(s)";
    if (!options.outPath.empty())
        std::cerr << ", report written to " << options.outPath;
    std::cerr << "\n";
    return 0;
}
