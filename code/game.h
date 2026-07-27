#include <cmath>
#include <cstdint>

#define Kilobytes(v) ((v) * 1024LL)
#define Megabytes(v) (Kilobytes(v) * 1024LL)
#define Gigabytes(v) (Megabytes(v) * 1024LL)
#define Terabytes(v) (Gigabytes(v) * 1024LL)

#define Pi32 3.14159265359f

struct debug_read_file_result {
    uint32_t contents_size;
    void* contents;
};

#define DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(const char* filename)
typedef DEBUG_PLATFORM_READ_ENTIRE_FILE(debug_platform_read_entire_file);

#define DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(void* memory)
typedef DEBUG_PLATFORM_FREE_FILE_MEMORY(debug_platform_free_file_memory);

#define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) bool name(const char* filename, uint32_t memory_size, void* memory)
typedef DEBUG_PLATFORM_WRITE_ENTIRE_FILE(debug_platform_write_entire_file);

struct game_memory {
    void* permanent_storage;
    uint64_t permanent_storage_size;
    void* transient_storage;
    uint64_t transient_storage_size;
    bool is_initialized;
    debug_platform_read_entire_file* DEBUG_platform_read_entire_file;
    debug_platform_free_file_memory* DEBUG_platform_free_file_memory;
    debug_platform_write_entire_file* DEBUG_platform_write_entire_file;
};

struct game_offscreen_buffer {
    void* memory;
    int width;
    int height;
    int pitch;
};

struct game_sound_output_buffer {
    int samples_per_second;
    int sample_count;
    int16_t* samples;
};

struct game_button_state {
    int half_transition_count;
    bool ended_down;
};

struct game_controller_input {
    game_button_state move_up;
    game_button_state move_down;
    game_button_state move_left;
    game_button_state move_right;
    game_button_state action_up;
    game_button_state action_down;
    game_button_state action_left;
    game_button_state action_right;
    game_button_state left_shoulder;
    game_button_state right_shoulder;
    game_button_state start;
    game_button_state back;
};

struct game_input {
    game_controller_input controller;
    float mouse_x;
    float mouse_y;
    game_button_state mouse_left;
    game_button_state mouse_right;
};

#if defined(_WIN32)
    #define GAME_API extern "C" __declspec(dllexport)
#elif defined(__APPLE__)
    #define GAME_API extern "C"
#else
    #define GAME_API extern "C"
#endif

GAME_API void game_update_and_render(game_memory* memory, game_offscreen_buffer* buffer, game_input* input);
GAME_API void game_get_sound_samples(game_memory* memory, game_sound_output_buffer* sound_buffer);
