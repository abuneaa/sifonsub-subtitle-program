#include "transcriber.h"
#include "ggml-backend.h"
#include <iostream>

struct ProgressBridge {
    void (*user_callback)(int, void*);
    void* user_data;
};

static void whisper_progress_trampoline(struct whisper_context* /*ctx*/,
                                          struct whisper_state* /*state*/,
                                          int progress,
                                          void* user_data) {
    auto* bridge = static_cast<ProgressBridge*>(user_data);
    if (bridge && bridge->user_callback) {
        bridge->user_callback(progress, bridge->user_data);
    }
}

std::vector<GpuDeviceInfo> list_available_devices() {
    std::vector<GpuDeviceInfo> result;

    size_t count = ggml_backend_dev_count();
    for (size_t i = 0; i < count; i++) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        enum ggml_backend_dev_type devType = ggml_backend_dev_type(dev);

        GpuDeviceInfo info;
        info.index = static_cast<int>(i);
        info.name = ggml_backend_dev_description(dev);
        info.isGpu = (devType == GGML_BACKEND_DEVICE_TYPE_GPU || devType == GGML_BACKEND_DEVICE_TYPE_IGPU);

        result.push_back(info);
    }

    return result;
}

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
                 int gpu_device_index,
                 void (*progress_callback)(int, void*),
                 void* progress_user_data) {

    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.flash_attn = false;
    cparams.dtw_token_timestamps = true;

    if (gpu_device_index >= 0) {
        cparams.use_gpu = true;
        cparams.gpu_device = gpu_device_index;
    } else {
        cparams.use_gpu = false;
    }

    if (model_size == "tiny") {
        cparams.dtw_aheads_preset = WHISPER_AHEADS_TINY;
    } else if (model_size == "base") {
        cparams.dtw_aheads_preset = WHISPER_AHEADS_BASE;
    } else if (model_size == "small") {
        cparams.dtw_aheads_preset = WHISPER_AHEADS_SMALL;
    } else if (model_size == "medium") {
        cparams.dtw_aheads_preset = WHISPER_AHEADS_MEDIUM;
    } else if (model_size == "large-v3") {
        cparams.dtw_aheads_preset = WHISPER_AHEADS_LARGE_V3;
    } else if (model_size == "large-v3-turbo") {
        cparams.dtw_aheads_preset = WHISPER_AHEADS_LARGE_V3_TURBO;
    } else {
        cparams.dtw_aheads_preset = WHISPER_AHEADS_NONE;
        std::cerr << "Uyari: bilinmeyen model boyutu '" << model_size << "', DTW alignment kapatildi." << std::endl;
    }
    //cparams.use_gpu = true;

    out_ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);

    if (out_ctx == nullptr) {
        std::cerr << "Model yuklenemedi: " << model_path << std::endl;
        return false;
    }
    std::cout << "Model basariyla yuklendi." << std::endl;

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    wparams.language = language.c_str();
    wparams.n_threads = 6;
    wparams.print_progress = false;
    wparams.print_realtime = false;

    wparams.vad = use_vad;
    if (use_vad) {
        wparams.vad_model_path = vad_model_path.c_str();
        wparams.vad_params.threshold = vad_threshold;
        wparams.vad_params.speech_pad_ms = 50.55f;
        wparams.vad_params.min_silence_duration_ms = 100.5f;
    }

    wparams.no_context = true;
    wparams.token_timestamps = true;
    wparams.max_len = 20;
    wparams.split_on_word = true;
    wparams.no_speech_thold = no_speech_thold;
    wparams.entropy_thold = entropy_thold;
    wparams.logprob_thold = logprob_thold;

    ProgressBridge bridge{progress_callback, progress_user_data};
    if (progress_callback) {
        wparams.progress_callback = whisper_progress_trampoline;
        wparams.progress_callback_user_data = &bridge;
    }

    if (whisper_full(out_ctx, wparams, pcmf32.data(), pcmf32.size()) != 0) {
        std::cerr << "Transkript basarisiz oldu." << std::endl;
        whisper_free(out_ctx);
        return false;
    }

    return true;
}