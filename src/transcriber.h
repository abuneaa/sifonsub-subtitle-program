#pragma once

#include <string>
#include <vector>
#include "whisper.h"

struct GpuDeviceInfo {
    int index;           // ggml_backend_dev_get() için index
    std::string name;    // örn. "NVIDIA GeForce RTX 4050 Laptop GPU"
    bool isGpu;           // true: GPU/IGPU, false: CPU
};

std::vector<GpuDeviceInfo> list_available_devices();

bool transcribe(const std::string& model_path,
                 const std::vector<float>& pcmf32,
                 whisper_context*& out_ctx,
                 const std::string& language,
                 bool use_vad,
                 const std::string& vad_model_path,
                 float entropy_thold,
                 float logprob_thold,
                 float no_speech_thold,
                 float vad_threshold,
                 const std::string& model_size,
                 int gpu_device_index,   // -1 = CPU-only, 0+ = seçilen cihaz
                 void (*progress_callback)(int, void*) = nullptr,
                 void* progress_user_data = nullptr);