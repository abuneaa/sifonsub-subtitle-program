#pragma once

#include <vector>
#include <string>

bool read_wav(const std::string& path, std::vector<float>& out_samples);
bool ffmpeg_cmd(const std::string& audio_path, std::string& ffmpegpath);