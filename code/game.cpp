#include "game.h"

struct game_state {
    int xoffset;
    int yoffset;
    int tone_hz;
    float tone_sine;
};

void game_get_sound_samples(game_memory* memory, game_sound_output_buffer* sound_buffer) {
    game_state* state = (game_state*)memory->permanent_storage;

    int tone_hz = state->tone_hz;
    if(tone_hz == 0) tone_hz = 256;  // audio can run before the first frame initializes state

    int tone_volume = 3000;
    int samples_per_period = sound_buffer->samples_per_second / tone_hz;
    int16_t* sample_out = sound_buffer->samples;

    for(int i = 0; i < sound_buffer->sample_count; ++i) {
        int16_t value = (int16_t)(sinf(state->tone_sine) * tone_volume);
        *sample_out++ = value;
        *sample_out++ = value;
        state->tone_sine += 2.0f * (float)M_PI / (float)samples_per_period;
        if (state->tone_sine > 2.0f * (float)M_PI) state->tone_sine -= 2.0f * (float)M_PI;
    }
}

void game_update_and_render(game_memory* memory, game_offscreen_buffer* buffer, game_input* input) {
    game_state* state = (game_state*)memory->permanent_storage;

    if(!memory->is_initialized) {
        state->xoffset = 0;
        state->yoffset = 0;
        // tone_hz/tone_sine deliberately not initialized here: the audio
        // thread may already be advancing them, and resetting the phase
        // mid-stream is an audible click. Zeroed memory is their initializer.
        memory->is_initialized = true;
        debug_read_file_result file = memory->DEBUG_platform_read_entire_file("code/sdl_platform.cpp");
        if(file.contents) {
            memory->DEBUG_platform_write_entire_file("test_copy.cpp", file.contents_size, file.contents);
            memory->DEBUG_platform_free_file_memory(file.contents);
        }
    }

    if (input->controller.move_right.ended_down) state->xoffset += 1;
    if (input->controller.move_left.ended_down)  state->xoffset -= 1;
    if (input->controller.move_down.ended_down)  state->yoffset += 1;
    if (input->controller.move_up.ended_down)    state->yoffset -= 1;

    state->tone_hz = input->controller.action_down.ended_down ? 512 : 256;

    int width = buffer->width;
    int height = buffer->height;
    int pitch = buffer->pitch;
    uint8_t* row = (uint8_t*) buffer->memory;

    for(int y = 0; y < height; ++y) {
        uint32_t* pixel = (uint32_t*) row;
        for(int x = 0; x < width; ++x) {
            uint8_t blue = (x + state->xoffset);
            uint8_t green = (y + state->yoffset);
            *pixel++ = ((green << 8) | blue);
        }
        row += pitch;
    }

    int mx = (int)input->mouse_x;
    int my = (int)input->mouse_y;
    int marker_size = 10;
    for (int y = my - marker_size; y < my + marker_size; ++y) {
        if (y < 0 || y >= height) continue;
        uint32_t* marker_pixel = (uint32_t*)((uint8_t*)buffer->memory + y * pitch);
        for (int x = mx - marker_size; x < mx + marker_size; ++x) {
            if (x < 0 || x >= width) continue;
            marker_pixel[x] = 0x00FFFFFF; // white
        }
    }
}
