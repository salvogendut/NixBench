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

static bool render_system(
    SDL_Renderer *renderer,
    struct nb_rect content,
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
        return render_clipped_text(renderer,
                                   content.x + 18,
                                   y,
                                   content.x + content.width - 12,
                                   "Refresh failed; previous data retained.",
                                   error_text_color);
    }
    return render_row(renderer,
                      content,
                      y,
                      "Updated",
                      state->refreshed_at[0] != '\0'
                          ? state->refreshed_at
                          : "Unavailable");
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
               : render_system(renderer, content_rect, state);
}
