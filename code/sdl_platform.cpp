#include "SDL3/SDL_loadso.h"
#include "game.h"
#include "SDL3/SDL_scancode.h"
#include "SDL3/SDL_audio.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_keyboard.h"
#include "SDL3/SDL_oldnames.h"
#include "SDL3/SDL_pixels.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_stdinc.h"
#include "SDL3/SDL_timer.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstring>
#include <cstdio>
#include <stdlib.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

// Memory is the only place a real platform branch survives: the fixed
// base-address hint in debug builds has no portable spelling. Everything else
// (loading the game lib, debug file I/O) goes through SDL.
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #define GAME_LIB      "libgame.dll"
    #define GAME_LIB_TEMP "libgame_temp_%d.dll"
    #define GAME_LIB_GLOB "libgame_temp_*.dll"
#elif defined(__APPLE__)
    #include <sys/mman.h>
    #include <cerrno>
    #define GAME_LIB      "libgame.dylib"
    #define GAME_LIB_TEMP "libgame_temp_%d.dylib"
    #define GAME_LIB_GLOB "libgame_temp_*.dylib"
#else
    #include <sys/mman.h>
    #include <cerrno>
    #define GAME_LIB      "libgame.so"
    #define GAME_LIB_TEMP "libgame_temp_%d.so"
    #define GAME_LIB_GLOB "libgame_temp_*.so"
#endif

constexpr int screen_width{1280};
constexpr int screen_height{720};
constexpr int g_game_update_hz{60};
constexpr float g_target_seconds_per_frame{1.0f / g_game_update_hz};

SDL_Window* g_window{nullptr};
SDL_Renderer* g_renderer{nullptr};
SDL_Texture* g_texture{nullptr};
SDL_AudioStream* g_audio_stream{nullptr};
Uint64 g_perf_freq{0};
FILE* g_replay_file{nullptr};
const char* g_base_path{nullptr};

enum class replay_state { idle, recording, playing };
replay_state g_replay_state = replay_state::idle;
bool l_was_down = false;

typedef void game_update_and_render_fn(game_memory* memory, game_offscreen_buffer* buffer, game_input* input);
typedef void game_get_sound_samples_fn(game_memory* memory, game_sound_output_buffer* sound_buffer);

void game_update_and_render_stub(game_memory*, game_offscreen_buffer*, game_input*) {}
void game_get_sound_samples_stub(game_memory*, game_sound_output_buffer*) {}

struct sdl_game_code {
    SDL_SharedObject* game_dylib;
    game_update_and_render_fn* update_and_render;
    game_get_sound_samples_fn* get_sound_samples;
    bool is_valid;
    SDL_Time last_write_time;
};

sdl_game_code sdl_load_game_code() {
    sdl_game_code result{};
    result.update_and_render = game_update_and_render_stub;
    result.get_sound_samples = game_get_sound_samples_stub;

    char dylib_path[512];
    snprintf(dylib_path, sizeof(dylib_path), "%s" GAME_LIB, g_base_path);

    static int reload_counter = 0;
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s" GAME_LIB_TEMP, g_base_path, reload_counter++);

    if(SDL_CopyFile(dylib_path, temp_path)) {
        result.game_dylib = SDL_LoadObject(temp_path);
        if(result.game_dylib) {
            game_update_and_render_fn* update = (game_update_and_render_fn*)SDL_LoadFunction(result.game_dylib, "game_update_and_render");
            game_get_sound_samples_fn* samples = (game_get_sound_samples_fn*)SDL_LoadFunction(result.game_dylib, "game_get_sound_samples");
            if(update && samples) {
                result.update_and_render = update;
                result.get_sound_samples = samples;
                result.is_valid = true;
            }
        }
    } else {
        SDL_Log("game code copy failed: %s\n", SDL_GetError());
    }

    if(result.is_valid) {
        // Recorded only on success: a half-written dylib leaves last_write_time
        // at 0, so the mtime check retries the load next frame instead of
        // wedging on the stubs until the next rebuild.
        SDL_PathInfo info;
        if(SDL_GetPathInfo(dylib_path, &info)) {
            result.last_write_time = info.modify_time;
        }
    } else if(result.game_dylib) {
        SDL_UnloadObject(result.game_dylib);
        result.game_dylib = nullptr;
    }

    return result;
}

// Temp copies stay on disk while the engine runs so lldb can keep resolving
// symbols for loaded images; leftovers from previous runs are swept here.
void sdl_delete_stale_temp_dylibs() {
    int count = 0;
    char** entries = SDL_GlobDirectory(g_base_path, GAME_LIB_GLOB, 0, &count);
    if(entries) {
        for(int i = 0; i < count; ++i) {
            char stale_path[512];
            snprintf(stale_path, sizeof(stale_path), "%s%s", g_base_path, entries[i]);
            SDL_RemovePath(stale_path);
        }
        SDL_free(entries);
    }
}

void sdl_unload_game_code(sdl_game_code* game_code) {
    if(game_code->game_dylib) {
        SDL_UnloadObject(game_code->game_dylib);
        game_code->game_dylib = nullptr;
    }
    game_code->is_valid = false;
    game_code->update_and_render = game_update_and_render_stub;
    game_code->get_sound_samples = game_get_sound_samples_stub;
}

struct sdl_audio_context {
    sdl_game_code* game_code;
    game_memory* memory;
};
sdl_audio_context g_audio_ctx{};

// Runs on the audio thread whenever the device needs more samples than are
// queued, so audio survives main-thread stalls (window drags, slow frames).
// The main thread must hold SDL_LockAudioStream while swapping game code or
// overwriting game memory — the lock blocks this callback.
void SDLCALL sdl_audio_stream_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int /*total_amount*/) {
    sdl_audio_context* ctx = (sdl_audio_context*)userdata;

    while(additional_amount > 0) {
        int16_t scratch[2048] = {};  // zeroed: the stub writes nothing, and silence must not be garbage
        int max_sample_count = (int)(sizeof(scratch) / (2 * sizeof(int16_t)));
        int sample_count = (additional_amount + 3) / 4;
        if(sample_count > max_sample_count) sample_count = max_sample_count;

        game_sound_output_buffer sound_buffer;
        sound_buffer.samples_per_second = 48000;
        sound_buffer.sample_count = sample_count;
        sound_buffer.samples = scratch;

        ctx->game_code->get_sound_samples(ctx->memory, &sound_buffer);
        SDL_PutAudioStreamData(stream, scratch, sample_count * 4);
        additional_amount -= sample_count * 4;
    }
}

DEBUG_PLATFORM_READ_ENTIRE_FILE(sdl_debug_platform_read_entire_file) {
    debug_read_file_result result{};

    // SDL_LoadFile allocates size+1 and null-terminates; it reports the true
    // size, so contents_size stays honest. The allocation is SDL_malloc's,
    // which is why the matching free below must be SDL_free.
    size_t file_size = 0;
    void* contents = SDL_LoadFile(filename, &file_size);
    if (contents) {
        result.contents = contents;
        result.contents_size = (uint32_t)file_size;
    }

    return result;
}

DEBUG_PLATFORM_FREE_FILE_MEMORY(sdl_debug_platform_free_file_memory) {
    SDL_free(memory);
}

DEBUG_PLATFORM_WRITE_ENTIRE_FILE(sdl_debug_platform_write_entire_file) {
    return SDL_SaveFile(filename, memory, memory_size);
}

// One big zeroed allocation, failure normalized to nullptr on both platforms.
// `base` is a hint: mmap silently relocates if the range is taken, VirtualAlloc
// refuses and returns null — see the caller's handling of the debug fixed base.
void* sdl_platform_alloc(void* base, size_t size) {
#if defined(_WIN32)
    return VirtualAlloc(base, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void* result = mmap(base, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return (result == MAP_FAILED) ? nullptr : result;
#endif
}

void sdl_platform_alloc_log_failure(const char* what) {
#if defined(_WIN32)
    SDL_Log("%s failed: GetLastError() = %lu\n", what, GetLastError());
#else
    SDL_Log("%s failed: %s\n", what, strerror(errno));
#endif
}

bool init() {
    bool success{true};

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO) == false) {
        SDL_Log("SDL could not initialize. SDL error: %s\n", SDL_GetError());
        success = false;
    }

    g_base_path = SDL_GetBasePath();

    g_perf_freq = SDL_GetPerformanceFrequency();

    g_window = SDL_CreateWindow("SDL tutorial", screen_width, screen_height, SDL_WINDOW_RESIZABLE);

    if(g_window == nullptr) {
        SDL_Log("Window could not be created. SDL error: %s\n", SDL_GetError());
        success = false;
    }

    g_renderer = SDL_CreateRenderer(g_window, nullptr);

    if(g_renderer == nullptr) {
       SDL_Log("Renderer could not be created. SDL error: %s\n", SDL_GetError());
       success = false;
    }

    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 1280, 720);

    SDL_SetTextureBlendMode(g_texture, SDL_BLENDMODE_NONE);

    if(g_texture == nullptr) {
       SDL_Log("Texture could not be created. SDL error: %s\n", SDL_GetError());
       success = false;
    }

    SDL_AudioSpec audio_spec = {SDL_AUDIO_S16, 2, 48000};

    g_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);

    if(g_audio_stream == nullptr) {
        SDL_Log("Audio stream could not be created. SDL error: %s\n", SDL_GetError());
        success = false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL3_InitForSDLRenderer(g_window, g_renderer);
    ImGui_ImplSDLRenderer3_Init(g_renderer);

    // Resumed in main() once the get-callback is installed — the device must
    // not start pulling before the callback can supply samples.

    return success;
}

void platform_close() {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyWindow(g_window);
    g_window = nullptr;
    SDL_Quit();

}

void process_keyboard_button(game_button_state* new_state, bool is_down) {
    if(new_state->ended_down != is_down) {
        new_state->half_transition_count += 1;
    }
    new_state->ended_down = is_down;
}

// Parameters unnamed: SDL_main.h #defines main to SDL_main, so this is no
// longer the special main() that is exempt from -Wunused-parameter.
int main(int /*argc*/, char* /*argv*/[]) {
    int exit_code{0};

    if(init() == false) {
        SDL_Log("Unable to initialize program\n");
        exit_code = 1;
    }
    else {
        bool quit{false};
        SDL_Event event;

        SDL_zero(event);

        game_offscreen_buffer pixel_backbuffer{};
        pixel_backbuffer.width = 1280;
        pixel_backbuffer.height = 720;
        pixel_backbuffer.pitch = 4 * pixel_backbuffer.width;
        pixel_backbuffer.memory = sdl_platform_alloc(
            0, (size_t)pixel_backbuffer.pitch * pixel_backbuffer.height);

        game_memory memory{};
        memory.permanent_storage_size = Megabytes(64);
        memory.transient_storage_size = Megabytes(256);
        uint64_t total_size = memory.permanent_storage_size + memory.transient_storage_size;

#if BUILD_INTERNAL
        // Fixed base in dev builds: pointers stored in game state then have
        // identical values across runs, which snapshot replay depends on.
        void* base_address = (void*)Terabytes(2);
#else
        void* base_address = nullptr;
#endif
        memory.permanent_storage = sdl_platform_alloc(base_address, total_size);
#if BUILD_INTERNAL
        // VirtualAlloc refuses a taken base rather than relocating the way mmap
        // does, so a failure here may just mean "2 TB is occupied". Retry
        // wherever the OS likes and let the check below report the lost hint —
        // a debug convenience must not be the reason the engine won't boot.
        if(memory.permanent_storage == nullptr) {
            memory.permanent_storage = sdl_platform_alloc(nullptr, total_size);
        }
#endif
        memory.transient_storage = (uint8_t*)memory.permanent_storage + memory.permanent_storage_size;

        memory.DEBUG_platform_read_entire_file = sdl_debug_platform_read_entire_file;
        memory.DEBUG_platform_free_file_memory = sdl_debug_platform_free_file_memory;
        memory.DEBUG_platform_write_entire_file = sdl_debug_platform_write_entire_file;

        void* replay_memory_block = sdl_platform_alloc(0, total_size);

        if(pixel_backbuffer.memory == nullptr || memory.permanent_storage == nullptr ||
           replay_memory_block == nullptr) {
            sdl_platform_alloc_log_failure("game memory allocation");
            platform_close();
            return 1;
        }
#if BUILD_INTERNAL
        if(memory.permanent_storage != base_address) {
            SDL_Log("warning: game memory at %p, not the requested base %p — replay breaks for pointer-holding state\n",
                    memory.permanent_storage, base_address);
        }
#endif

        sdl_delete_stale_temp_dylibs();
        sdl_game_code game_code = sdl_load_game_code();

        g_audio_ctx.game_code = &game_code;
        g_audio_ctx.memory = &memory;
        SDL_SetAudioStreamGetCallback(g_audio_stream, sdl_audio_stream_callback, &g_audio_ctx);
        SDL_ResumeAudioStreamDevice(g_audio_stream);

        SDL_AudioSpec device_spec;
        int device_period_frames = 0;
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(g_audio_stream), &device_spec, &device_period_frames);
        SDL_Log("audio: device period %d sample frames (%.1f ms) — this is the latency\n",
                device_period_frames, 1000.0 * device_period_frames / (double)device_spec.freq);

        while(quit == false) {
            Uint64 frame_start = SDL_GetPerformanceCounter();

            char dylib_path[512];
            snprintf(dylib_path, sizeof(dylib_path), "%s" GAME_LIB, g_base_path);

            SDL_PathInfo current_info;
            if(SDL_GetPathInfo(dylib_path, &current_info) &&
               current_info.modify_time != game_code.last_write_time) {
                SDL_LockAudioStream(g_audio_stream);
                sdl_unload_game_code(&game_code);
                game_code = sdl_load_game_code();
                SDL_UnlockAudioStream(g_audio_stream);
            }

            while(SDL_PollEvent(&event) == true) {
                ImGui_ImplSDL3_ProcessEvent(&event);

                if(event.type == SDL_EVENT_QUIT) {
                    quit = true;
                }
            }

            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            static game_input input{};
            input.controller.move_up.half_transition_count = 0;
            input.controller.move_down.half_transition_count = 0;
            input.controller.move_left.half_transition_count = 0;
            input.controller.move_right.half_transition_count = 0;
            input.controller.action_up.half_transition_count = 0;
            input.controller.action_down.half_transition_count = 0;
            input.controller.action_left.half_transition_count = 0;
            input.controller.action_right.half_transition_count = 0;
            input.controller.left_shoulder.half_transition_count = 0;
            input.controller.right_shoulder.half_transition_count = 0;
            input.controller.start.half_transition_count = 0;
            input.controller.back.half_transition_count = 0;
            input.mouse_left.half_transition_count = 0;
            input.mouse_right.half_transition_count = 0;

            float mouse_x, mouse_y;
            Uint32 mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);

            int window_w, window_h;
            SDL_GetWindowSize(g_window, &window_w, &window_h);

            input.mouse_x = mouse_x * ((float)pixel_backbuffer.width / (float)window_w);
            input.mouse_y = mouse_y * ((float)pixel_backbuffer.height / (float)window_h);

            const bool* keys = SDL_GetKeyboardState(NULL);

            bool l_down = keys[SDL_SCANCODE_L];
            if(l_down && !l_was_down) {
                if(g_replay_state == replay_state::idle) {
                    g_replay_state = replay_state::recording;
                    SDL_LockAudioStream(g_audio_stream);
                    memcpy(replay_memory_block, memory.permanent_storage, total_size);
                    SDL_UnlockAudioStream(g_audio_stream);
                    char replay_path[512];
                    snprintf(replay_path, sizeof(replay_path), "%sreplay.hmi", g_base_path);
                    g_replay_file = fopen(replay_path, "wb");
                    SDL_Log("REPLAY: recording started\n");
                }
                else if(g_replay_state == replay_state::recording) {
                    g_replay_state = replay_state::playing;
                    if(g_replay_file) {
                        fclose(g_replay_file);  // flushes the buffered tail of the recording
                        g_replay_file = nullptr;
                    }
                    SDL_LockAudioStream(g_audio_stream);
                    memcpy(memory.permanent_storage, replay_memory_block, total_size);
                    SDL_UnlockAudioStream(g_audio_stream);
                    char replay_path[512];
                    snprintf(replay_path, sizeof(replay_path), "%sreplay.hmi", g_base_path);
                    g_replay_file = fopen(replay_path, "rb");
                    SDL_Log("REPLAY: playback started (memory restored)\n");
                }
                else {
                    g_replay_state = replay_state::idle;
                    if(g_replay_file) {
                        fclose(g_replay_file);
                        g_replay_file = nullptr;
                    }
                    SDL_Log("REPLAY: idle\n");
                }
            }
            l_was_down = l_down;

            process_keyboard_button(&input.controller.move_up,        keys[SDL_SCANCODE_W]);
            process_keyboard_button(&input.controller.move_down,      keys[SDL_SCANCODE_S]);
            process_keyboard_button(&input.controller.move_left,      keys[SDL_SCANCODE_A]);
            process_keyboard_button(&input.controller.move_right,     keys[SDL_SCANCODE_D]);
            process_keyboard_button(&input.controller.action_up,      keys[SDL_SCANCODE_K]);
            process_keyboard_button(&input.controller.action_down,    keys[SDL_SCANCODE_J]);
            process_keyboard_button(&input.controller.action_left,    keys[SDL_SCANCODE_H]);
            process_keyboard_button(&input.controller.action_right,   keys[SDL_SCANCODE_SEMICOLON]);
            process_keyboard_button(&input.controller.right_shoulder, keys[SDL_SCANCODE_E]);
            process_keyboard_button(&input.controller.left_shoulder,  keys[SDL_SCANCODE_Q]);
            process_keyboard_button(&input.controller.start,          keys[SDL_SCANCODE_ESCAPE]);
            process_keyboard_button(&input.controller.back,           keys[SDL_SCANCODE_BACKSPACE]);
            process_keyboard_button(&input.mouse_left, (mouse_buttons & SDL_BUTTON_LMASK) != 0);
            process_keyboard_button(&input.mouse_right, (mouse_buttons & SDL_BUTTON_RMASK) != 0);

            if(g_replay_state == replay_state::recording && g_replay_file) {
                fwrite(&input, sizeof(input), 1, g_replay_file);
            }
            else if(g_replay_state == replay_state::playing && g_replay_file) {
                game_input recorded_input{};
                if(fread(&recorded_input, sizeof(recorded_input), 1, g_replay_file) != 1) {
                    fseek(g_replay_file, 0, SEEK_SET);
                    SDL_LockAudioStream(g_audio_stream);
                    memcpy(memory.permanent_storage, replay_memory_block, total_size);
                    SDL_UnlockAudioStream(g_audio_stream);
                    fread(&recorded_input, sizeof(recorded_input), 1, g_replay_file);
                }
                input = recorded_input;
            }

            game_code.update_and_render(&memory, &pixel_backbuffer, &input);

            ImGui::Render();

            SDL_UpdateTexture(g_texture, NULL, pixel_backbuffer.memory, pixel_backbuffer.pitch);
            SDL_RenderClear(g_renderer);
            SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);

            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);

            SDL_RenderPresent(g_renderer);

            Uint64 work_end = SDL_GetPerformanceCounter();
            double seconds_elapsed = (double)(work_end - frame_start) / (double)g_perf_freq;
            if (seconds_elapsed < g_target_seconds_per_frame) {
                Uint64 delay_ns = (Uint64)((g_target_seconds_per_frame - seconds_elapsed) * 1e9);
                SDL_DelayPrecise(delay_ns);
            } else {
                SDL_Log("Missed frame budget: %.2f ms\n", seconds_elapsed * 1000.0);
            }

            Uint64 frame_end = SDL_GetPerformanceCounter();
            double ms_per_frame = 1000.0 * (double)(frame_end - frame_start) / (double)g_perf_freq;

            printf("%.2f ms/frame, %.1f fps\n", ms_per_frame, 1000.0 / ms_per_frame);
        }
    }

    platform_close();

    return exit_code;
}
