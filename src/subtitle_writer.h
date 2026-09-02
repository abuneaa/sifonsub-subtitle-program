#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "whisper.h"

struct Segment {
    int64_t t0;
    int64_t t1;
    std::string srtText;
    std::string assText;
    std::string originalSrtText;
    bool uncertain;
};

std::vector<Segment> extract_segments(whisper_context* ctx, float thold);
bool write_subtitles(const std::vector<Segment>& segments, const std::string& cikti_adi);

std::string rebuild_karaoke(const std::string& originalAssText, const std::string& newPlainText);
std::string clean_text(const char* text);
std::string msToSrtTime(int64_t ms);
std::string csToAssTime(int64_t cs);