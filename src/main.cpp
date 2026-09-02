#include "audio_converter.h"
#include "transcriber.h"
#include "subtitle_writer.h"
#include "whisper.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char** argv) {

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc < 3) {
        std::cerr << "Kullanim: " << argv[0] << " <model_yolu> <ses_dosyasi>" << std::endl;
        return 1;
    }

    std::string model_path = argv[1];
    std::string audio_path = argv[2];

    // 1. Adim: ffmpeg ile donustur
    std::string ffmpegpath;
    if (!ffmpeg_cmd(audio_path, ffmpegpath)) {
        return 1;
    }

    // 2. Adim: WAV'i oku
    std::vector<float> pcmf32;
    if (!read_wav(ffmpegpath, pcmf32)) {
        std::remove(ffmpegpath.c_str());
        return 1;
    }
    std::cout << "Ses dosyasi okundu: " << pcmf32.size() << " ornek." << std::endl;
    std::remove(ffmpegpath.c_str());



    // 3. Adim: Transkript calistir
    whisper_context* ctx = nullptr;
    namespace fs = std::filesystem;
    fs::path vad_model_path = fs::path(model_path).parent_path() / "ggml-silero-v6.2.0.bin";

    if (!transcribe(model_path, pcmf32, ctx, "tr", true,
                vad_model_path.string(),
                3.131f, -0.3f, 0.7f, 0.7f, "large-v3-turbo", -1, nullptr, nullptr)) {
    return 1;
}

    // 4. Adim: cikti dosya adini hesapla
    size_t a = audio_path.find_last_of(".");
    if (a == std::string::npos) {
        std::cerr << "hatali input" << std::endl;
        whisper_free(ctx);
        return 1;
    }
    std::string cikti_adi = audio_path.substr(0, a);

    // 5. Adim: altyazilari yaz
    const float thold = 0.5f;
    std::vector<Segment> segments = extract_segments(ctx, thold);
    if (!write_subtitles(segments, cikti_adi)) {
        whisper_free(ctx);
        return 1;
    }

    whisper_free(ctx);
    return 0;
}