#include "nixinfo_renderer.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct color {
    Uint8 red;
    Uint8 green;
    Uint8 blue;
};

static const struct color panel_color = {225, 230, 222};
static const struct color panel_highlight = {249, 251, 245};
static const struct color panel_shadow = {74, 89, 91};
static const struct color heading_color = {38, 104, 126};
static const struct color heading_text_color = {248, 249, 241};
static const struct color text_color = {18, 30, 34};
static const struct color muted_text_color = {76, 88, 88};
static const struct color error_text_color = {142, 47, 39};
static const struct color graph_background_color = {20, 31, 35};
static const struct color graph_grid_color = {54, 72, 74};
static const struct color graph_cpu_color = {78, 207, 184};
static const struct color graph_memory_color = {242, 181, 74};

static bool set_color(SDL_Renderer *renderer, struct color color)
{
    return SDL_SetRenderDrawColor(renderer,
                                  color.red,
                                  color.green,
                                  color.blue,
                                  SDL_ALPHA_OPAQUE);
}

static bool fill_rect(SDL_Renderer *renderer,
                      struct nb_rect rect,
                      struct color color)
{
    const SDL_FRect destination = {
        (float)rect.x,
        (float)rect.y,
        (float)rect.width,
        (float)rect.height
    };

    return rect.width <= 0 || rect.height <= 0 ||
           (set_color(renderer, color) &&
            SDL_RenderFillRect(renderer, &destination));
}

static bool render_bevel(SDL_Renderer *renderer, struct nb_rect rect)
{
    const float left = (float)rect.x;
    const float top = (float)rect.y;
    const float right = (float)(rect.x + rect.width - 1);
    const float bottom = (float)(rect.y + rect.height - 1);

    if (rect.width <= 1 || rect.height <= 1) {
        return true;
    }
    return set_color(renderer, panel_highlight) &&
           SDL_RenderLine(renderer, left, bottom, left, top) &&
           SDL_RenderLine(renderer, left, top, right, top) &&
           set_color(renderer, panel_shadow) &&
           SDL_RenderLine(renderer, right, top, right, bottom) &&
           SDL_RenderLine(renderer, right, bottom, left, bottom);
}

static bool render_outline(SDL_Renderer *renderer,
                           struct nb_rect rect,
                           struct color color)
{
    const SDL_FRect outline = {
        (float)rect.x,
        (float)rect.y,
        (float)rect.width,
        (float)rect.height
    };

    return rect.width <= 0 || rect.height <= 0 ||
           (set_color(renderer, color) &&
            SDL_RenderRect(renderer, &outline));
}

static bool render_clipped_text(SDL_Renderer *renderer,
                                int x,
                                int y,
                                int right,
                                const char *text,
                                struct color color)
{
    char clipped[128];
    size_t maximum_characters;
    size_t index;

    if (text == NULL || text[0] == '\0' || right - x < 8) {
        return true;
    }
    maximum_characters = (size_t)((right - x) / 8);
    if (maximum_characters >= sizeof(clipped)) {
        maximum_characters = sizeof(clipped) - 1;
    }
    for (index = 0;
         index < maximum_characters && text[index] != '\0';
         ++index) {
        clipped[index] = text[index];
    }
    clipped[index] = '\0';

    return clipped[0] == '\0' ||
           (set_color(renderer, color) &&
            SDL_RenderDebugText(renderer,
                                (float)x,
                                (float)y,
                                clipped));
}

static bool render_row(SDL_Renderer *renderer,
                       struct nb_rect content,
                       int y,
                       const char *label,
                       const char *value)
{
    char row[384];
    int character_count;

    if (y < content.y || y + 8 > content.y + content.height) {
        return true;
    }
    character_count = snprintf(row,
                               sizeof(row),
                               "%s: %s",
                               label,
                               value != NULL ? value : "Unavailable");
    if (character_count < 0) {
        return false;
    }
    return render_clipped_text(renderer,
                               content.x + 18,
                               y,
                               content.x + content.width - 12,
                               row,
                               text_color);
}

static const char *available_text(
    const struct nb_nixinfo_system_snapshot *snapshot,
    uint32_t flag,
    const char *text)
{
    return (snapshot->available & flag) != 0 ? text : "Unavailable";
}

static bool render_about(SDL_Renderer *renderer, struct nb_rect content)
{
    static const char *const lines[] = {
        "NixInfo is the first NixBench reference application.",
        "Its windows, menus, and commands are application-owned.",
        "The in-process event contract will later cross an IPC boundary.",
        "On NetBSD, system details use native interfaces."
    };
    size_t index;

    for (index = 0; index < sizeof(lines) / sizeof(lines[0]); ++index) {
        if (!render_clipped_text(renderer,
                                 content.x + 18,
                                 content.y + 54 + ((int)index * 20),
                                 content.x + content.width - 12,
                                 lines[index],
                                 index == 2 ? muted_text_color : text_color)) {
            return false;
        }
    }
    return true;
}

static bool render_usage_trace(
    SDL_Renderer *renderer,
    struct nb_rect chart,
    const struct nb_nixinfo_usage_history *history,
    uint32_t availability,
    bool cpu,
    struct color color)
{
    bool previous_valid = false;
    float previous_x = 0.0f;
    float previous_y = 0.0f;
    size_t index;

    if (!set_color(renderer, color)) {
        return false;
    }
    for (index = 0; index < history->count; ++index) {
        struct nb_nixinfo_usage_sample sample;
        const size_t age = history->count - 1 - index;
        const float x = (float)(chart.x + chart.width - 1) -
                        (float)(age * (size_t)(chart.width - 1)) /
                            (float)(NB_NIXINFO_USAGE_HISTORY_CAPACITY - 1);
        unsigned int percent;
        float y;

        if (!nb_nixinfo_usage_history_at(history, index, &sample) ||
            (sample.available & availability) == 0) {
            previous_valid = false;
            continue;
        }
        percent = cpu ? sample.cpu_percent : sample.memory_percent;
        if (percent > 100) {
            percent = 100;
        }
        y = (float)(chart.y + chart.height - 1) -
            ((float)percent * (float)(chart.height - 1) / 100.0f);
        if (previous_valid &&
            !SDL_RenderLine(renderer, previous_x, previous_y, x, y)) {
            return false;
        }
        if (!previous_valid &&
            !SDL_RenderPoint(renderer, x, y)) {
            return false;
        }
        previous_valid = true;
        previous_x = x;
        previous_y = y;
    }
    return true;
}

static bool render_usage_history(
    SDL_Renderer *renderer,
    struct nb_rect graph,
    const struct nb_nixinfo_usage_history *history)
{
    struct nb_rect chart;
    struct nb_nixinfo_usage_sample latest;
    char legend[128];
    char cpu_value[16];
    char memory_value[16];
    int grid;

    if (graph.width < 100 || graph.height < 70) {
        return true;
    }
    chart = (struct nb_rect){graph.x + 8,
                             graph.y + 25,
                             graph.width - 16,
                             graph.height - 33};
    if (!fill_rect(renderer, graph, graph_background_color) ||
        !render_outline(renderer, graph, panel_shadow)) {
        return false;
    }
    if (history == NULL || history->count == 0 ||
        !nb_nixinfo_usage_history_at(history,
                                     history->count - 1,
                                     &latest)) {
        return render_clipped_text(renderer,
                                   graph.x + 10,
                                   graph.y + 9,
                                   graph.x + graph.width - 8,
                                   "SYSTEM LOAD  awaiting native samples...",
                                   panel_highlight);
    }
    if ((latest.available & NB_NIXINFO_USAGE_HAS_CPU) != 0) {
        (void)snprintf(cpu_value,
                       sizeof(cpu_value),
                       "%u%%",
                       latest.cpu_percent);
    } else {
        (void)snprintf(cpu_value, sizeof(cpu_value), "--");
    }
    if ((latest.available & NB_NIXINFO_USAGE_HAS_MEMORY) != 0) {
        (void)snprintf(memory_value,
                       sizeof(memory_value),
                       "%u%%",
                       latest.memory_percent);
    } else {
        (void)snprintf(memory_value, sizeof(memory_value), "--");
    }
    (void)snprintf(legend,
                   sizeof(legend),
                   "SYSTEM LOAD  CPU %s cyan  MEM %s amber  (120 seconds)",
                   cpu_value,
                   memory_value);
    if (!render_clipped_text(renderer,
                             graph.x + 10,
                             graph.y + 9,
                             graph.x + graph.width - 8,
                             legend,
                             panel_highlight)) {
        return false;
    }
    if (!set_color(renderer, graph_grid_color)) {
        return false;
    }
    for (grid = 1; grid < 4; ++grid) {
        const float y = (float)(chart.y +
                                (chart.height - 1) * grid / 4);

        if (!SDL_RenderLine(renderer,
                            (float)chart.x,
                            y,
                            (float)(chart.x + chart.width - 1),
                            y)) {
            return false;
        }
    }
    return render_outline(renderer, chart, graph_grid_color) &&
           render_usage_trace(renderer,
                              chart,
                              history,
                              NB_NIXINFO_USAGE_HAS_MEMORY,
                              false,
                              graph_memory_color) &&
           render_usage_trace(renderer,
                              chart,
                              history,
                              NB_NIXINFO_USAGE_HAS_CPU,
                              true,
                              graph_cpu_color);
}

static bool render_system(
    SDL_Renderer *renderer,
    struct nb_rect content,
    const struct nb_nixinfo *nixinfo,
    const struct nb_nixinfo_window_state *state)
{
    const struct nb_nixinfo_system_snapshot *snapshot = &state->snapshot;
    char value[NB_NIXINFO_SYSTEM_TEXT_CAPACITY * 2];
    char formatted[160];
    int y = content.y + 52;

    if (state->revision == 0) {
        return render_clipped_text(renderer,
                                   content.x + 18,
                                   y,
                                   content.x + content.width - 12,
                                   "System information is unavailable.",
                                   error_text_color);
    }

    if ((snapshot->available & NB_NIXINFO_SYSTEM_HAS_SYSTEM_NAME) != 0 &&
        (snapshot->available & NB_NIXINFO_SYSTEM_HAS_RELEASE) != 0) {
        (void)snprintf(value,
                       sizeof(value),
                       "%s %s",
                       snapshot->system_name,
                       snapshot->release);
    } else {
        (void)snprintf(value,
                       sizeof(value),
                       "%s",
                       available_text(snapshot,
                                      NB_NIXINFO_SYSTEM_HAS_SYSTEM_NAME,
                                      snapshot->system_name));
    }
    if (!render_row(renderer, content, y, "System", value)) {
        return false;
    }
    y += 18;

    if (!render_row(renderer,
                    content,
                    y,
                    "Host",
                    available_text(snapshot,
                                   NB_NIXINFO_SYSTEM_HAS_HOSTNAME,
                                   snapshot->hostname)) ||
        !render_row(renderer,
                    content,
                    y + 18,
                    "Architecture",
                    available_text(snapshot,
                                   NB_NIXINFO_SYSTEM_HAS_ARCHITECTURE,
                                   snapshot->architecture))) {
        return false;
    }
    y += 36;

    if (state->show_full_version) {
        if (!render_row(renderer,
                        content,
                        y,
                        "Kernel",
                        available_text(snapshot,
                                       NB_NIXINFO_SYSTEM_HAS_VERSION,
                                       snapshot->version))) {
            return false;
        }
        y += 18;
    }

    if (!render_row(renderer,
                    content,
                    y,
                    "CPU",
                    available_text(snapshot,
                                   NB_NIXINFO_SYSTEM_HAS_CPU_MODEL,
                                   snapshot->cpu_model))) {
        return false;
    }
    y += 18;

    if ((snapshot->available & NB_NIXINFO_SYSTEM_HAS_CPU_COUNT) != 0) {
        (void)snprintf(formatted,
                       sizeof(formatted),
                       "%u",
                       snapshot->online_cpu_count);
    } else {
        (void)snprintf(formatted, sizeof(formatted), "Unavailable");
    }
    if (!render_row(renderer, content, y, "Online CPUs", formatted)) {
        return false;
    }
    y += 18;

    if ((snapshot->available & NB_NIXINFO_SYSTEM_HAS_PHYSICAL_MEMORY) == 0 ||
        !nb_nixinfo_format_bytes(formatted,
                                 sizeof(formatted),
                                 snapshot->physical_memory_bytes)) {
        (void)snprintf(formatted, sizeof(formatted), "Unavailable");
    }
    if (!render_row(renderer, content, y, "Memory", formatted)) {
        return false;
    }
    y += 18;

    if ((snapshot->available & NB_NIXINFO_SYSTEM_HAS_UPTIME) == 0 ||
        !nb_nixinfo_format_duration(formatted,
                                    sizeof(formatted),
                                    snapshot->uptime_seconds)) {
        (void)snprintf(formatted, sizeof(formatted), "Unavailable");
    }
    if (!render_row(renderer, content, y, "Uptime", formatted)) {
        return false;
    }
    y += 18;

    if ((snapshot->available & NB_NIXINFO_SYSTEM_HAS_LOAD_AVERAGES) != 0) {
        (void)snprintf(formatted,
                       sizeof(formatted),
                       "%.2f  %.2f  %.2f",
                       snapshot->load_averages[0],
                       snapshot->load_averages[1],
                       snapshot->load_averages[2]);
    } else {
        (void)snprintf(formatted, sizeof(formatted), "Unavailable");
    }
    if (!render_row(renderer, content, y, "Load averages", formatted)) {
        return false;
    }
    y += 18;

    if ((snapshot->available & NB_NIXINFO_SYSTEM_HAS_ROOT_FILESYSTEM) != 0) {
        char available[64];
        char total[64];

        if (nb_nixinfo_format_bytes(available,
                                    sizeof(available),
                                    snapshot->root_available_bytes) &&
            nb_nixinfo_format_bytes(total,
                                    sizeof(total),
                                    snapshot->root_total_bytes)) {
            (void)snprintf(formatted,
                           sizeof(formatted),
                           "%s available of %s",
                           available,
                           total);
        } else {
            (void)snprintf(formatted, sizeof(formatted), "Unavailable");
        }
    } else {
        (void)snprintf(formatted, sizeof(formatted), "Unavailable");
    }
    if (!render_row(renderer, content, y, "Root volume", formatted)) {
        return false;
    }
    y += 18;

    (void)snprintf(formatted,
                   sizeof(formatted),
                   "revision %" PRIu64,
                   state->revision);
    if (!render_row(renderer, content, y, "Snapshot", formatted)) {
        return false;
    }
    y += 18;

    if (state->refresh_failed) {
        if (!render_clipped_text(renderer,
                                 content.x + 18,
                                 y,
                                 content.x + content.width - 12,
                                 "Refresh failed; previous data retained.",
                                 error_text_color)) {
            return false;
        }
    } else if (!render_row(renderer,
                           content,
                           y,
                           "Updated",
                           state->refreshed_at[0] != '\0'
                               ? state->refreshed_at
                               : "Unavailable")) {
        return false;
    }
    y += 26;
    return render_usage_history(
        renderer,
        (struct nb_rect){content.x + 18,
                         y,
                         content.width - 36,
                         content.y + content.height - y - 14},
        nb_nixinfo_usage_history(nixinfo));
}

bool nb_nixinfo_render_content(SDL_Renderer *renderer,
                               nb_window_id id,
                               const struct nb_window *window,
                               struct nb_rect content_rect,
                               void *context)
{
    const struct nb_nixinfo *nixinfo = context;
    const struct nb_nixinfo_window_state *state;
    const struct nb_rect panel = {
        content_rect.x + 8,
        content_rect.y + 8,
        content_rect.width - 16,
        content_rect.height - 16
    };
    const struct nb_rect heading = {
        panel.x + 2,
        panel.y + 2,
        panel.width - 4,
        panel.height > 34 ? 30 : panel.height - 4
    };
    const char *heading_text;

    (void)window;
    if (nixinfo == NULL) {
        return false;
    }
    state = nb_nixinfo_find_window(nixinfo, id);
    if (state == NULL) {
        return render_clipped_text(renderer,
                                   content_rect.x + 18,
                                   content_rect.y + 24,
                                   content_rect.x + content_rect.width - 12,
                                   "NixInfo is starting...",
                                   muted_text_color);
    }

    heading_text = state->kind == NB_NIXINFO_WINDOW_ABOUT
                       ? "About NixInfo"
                       : "NixInfo System Snapshot";
    if (!fill_rect(renderer, panel, panel_color) ||
        !render_bevel(renderer, panel) ||
        !fill_rect(renderer, heading, heading_color) ||
        !render_clipped_text(renderer,
                             heading.x + 10,
                             heading.y + ((heading.height - 8) / 2),
                             heading.x + heading.width - 8,
                             heading_text,
                             heading_text_color)) {
        return false;
    }

    return state->kind == NB_NIXINFO_WINDOW_ABOUT
               ? render_about(renderer, content_rect)
               : render_system(renderer, content_rect, nixinfo, state);
}
