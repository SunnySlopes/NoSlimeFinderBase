// SPDX-License-Identifier: MIT
// Copyright (c) 2026 SunnySlopes

/*
 * NoSlimeFinderBase — cubiomes-backed helpers.
 *
 * Keeps every cubiomes call behind the public C API so the GUI (and any other
 * consumer) never needs to include cubiomes headers itself.
 */

#include "noslimefinder/base.h"

#include "cubiomes/biomes.h"
#include "cubiomes/util.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{

bool valid_mc(int mc)
{
    return mc >= MC_B1_8 && mc <= MC_NEWEST;
}

} // namespace

extern "C" {

int noslime_parse_mc(const char *text)
{
    if (text == nullptr || text[0] == '\0')
        return -1;

    int mc = str2mc(text);
    if (valid_mc(mc))
        return mc;

    /* Also accept a raw cubiomes MCVersion number (e.g. 26.3 -> 35). */
    const char *begin = text;
    if (*begin == '+')
        ++begin;
    errno = 0;
    char *end = nullptr;
    const long parsed = std::strtol(begin, &end, 10);
    if (errno != 0 || end == begin || *end != '\0')
        return -1;
    if (parsed < MC_B1_8 || parsed > MC_NEWEST)
        return -1;
    return static_cast<int>(parsed);
}

const char *noslime_mc_name(int mc)
{
    if (!valid_mc(mc))
        return "unknown";
    const char *name = mc2str(mc);
    return (name != nullptr && name[0] != '\0') ? name : "unknown";
}

int noslime_newest_mc(void)
{
    return MC_NEWEST;
}

int32_t noslime_list_biomes(int mc, NoSlimeBiome *out, int32_t capacity)
{
    if (!valid_mc(mc) || capacity < 0)
        return 0;

    std::vector<std::pair<int, std::string>> list;
    for (int id = 0; id < 256; ++id)
    {
        /* Drop biomes that do not exist in the selected version, e.g. the
         * pre-1.18 mushroom_field_shore, so only real modern biomes show. */
        if (!biomeExists(mc, id) || !isOverworld(mc, id))
            continue;
        const char *name = biome2str(mc, id);
        if (name == nullptr)
            continue;
        list.emplace_back(id, std::string(name));
    }

    /* Requested order: high biome id to low. */
    std::sort(list.begin(), list.end(),
              [](const std::pair<int, std::string> &a,
                 const std::pair<int, std::string> &b) {
                  return a.first > b.first;
              });

    if (out != nullptr)
    {
        const int32_t n = std::min<int32_t>(
            static_cast<int32_t>(list.size()), capacity);
        for (int32_t i = 0; i < n; ++i)
        {
            out[i].id = static_cast<int32_t>(list[static_cast<size_t>(i)].first);
            std::snprintf(out[i].name, sizeof(out[i].name), "%s",
                          list[static_cast<size_t>(i)].second.c_str());
        }
    }

    return static_cast<int32_t>(list.size());
}

} // extern "C"
