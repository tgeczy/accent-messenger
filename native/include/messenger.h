#pragma once
#include <stdint.h>
#ifdef _WIN32
#define MSG_API __declspec(dllexport)
#else
#define MSG_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* All allocations stay in the library. One synthesis thread per instance;
   cancel is the only call permitted concurrently with synthesis. */
MSG_API void* msg_create(const uint8_t* driver, uint32_t length, const double* parameters);
MSG_API void msg_destroy(void* instance);
MSG_API void msg_cancel(void* instance, uint32_t generation);
/* 0 success, 1 cancelled, -1 error. Returned buffers live until the next call. */
MSG_API int msg_synthesize(void* instance, uint32_t generation, const char* text, int rate,
                           int pitch, int volume);
/* Original V/M/S commands; output gain is a fixed percentage, 100..200. */
MSG_API int msg_synthesize_options(void* instance, uint32_t generation, const char* text, int rate,
                                   int pitch, int volume, int voice, int intonation, int spacing,
                                   int outputGain);
MSG_API const int16_t* msg_pcm(void* instance, uint32_t* samples);
MSG_API const uint8_t* msg_frames(void* instance, uint32_t* bytes);
MSG_API const char* msg_error(void* instance);
MSG_API uint32_t msg_api_version(void);
#ifdef __cplusplus
}
#endif
