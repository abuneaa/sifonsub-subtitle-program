#include "subtitle_writer.h"
#include <sstream>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <cmath>
#include <regex>

std::string clean_text(const char* text) {
    if (!text) return "";
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    return std::string(text);
}

std::string msToSrtTime(int64_t ms) {
    int millisecond = ms % 1000;
    int total_second = ms / 1000;
    int second = total_second % 60;
    int total_minute = total_second / 60;
    int minute = total_minute % 60;
    int hour = total_minute / 60;

    std::ostringstream oss;
    oss << std::setfill('0');
    oss << std::setw(2) << hour << ":" << std::setw(2) << minute << ":" << std::setw(2) << second << "," << std::setw(3) << millisecond;
    return oss.str();
}

std::string csToAssTime(int64_t cs) {
    int centisecond = cs % 100;
    int total_second = cs / 100;
    int second = total_second % 60;
    int total_minute = total_second / 60;
    int minute = total_minute % 60;
    int hour = total_minute / 60;

    std::ostringstream oss;
    oss << std::setfill('0');
    oss << std::setw(1) << hour << ":" << std::setw(2) << minute << ":" << std::setw(2) << second << "." << std::setw(2) << centisecond;
    return oss.str();
}

std::vector<Segment> extract_segments(whisper_context* ctx, float thold) {
    std::vector<Segment> segments;

    const int64_t OFFSET_CS = 11; //whisper mali icin telafi kayiyo cunku

    int n_segments = whisper_full_n_segments(ctx);
    for (int i = 0; i < n_segments; i++) {

        float token_avg = 0;
        int filler_token_num = 0;
        const char* text = whisper_full_get_segment_text(ctx, i);
        int64_t t0 = whisper_full_get_segment_t0(ctx, i);
        int64_t t1 = whisper_full_get_segment_t1(ctx, i);

        int n_tokens = whisper_full_n_tokens(ctx, i);
        std::string cleanass;
        std::string cleansrt;

        for (int j = 0; j < n_tokens; j++) {
            whisper_token_data td = whisper_full_get_token_data(ctx, i, j);
            const char* ttstr = whisper_token_to_str(ctx, td.id);
            if (*ttstr == '[') { filler_token_num++; continue; }
            token_avg += td.p;
        }
        token_avg = token_avg / (float)(n_tokens - filler_token_num);
        if (std::isnan(token_avg)) {
            std::cerr << "avg token is NaN" << std::endl;
            continue;
        }

        bool uncertain = false;

        if (n_tokens > 0) {
            std::string sepet = "";
            int64_t kelime_baslangic = 0;
            int64_t kelime_bitis = 0;
            std::string karaoke_satiri = "";

            whisper_token_data first_token;
            whisper_token_data last_token;
            int first_j = -1;
            int last_j = -1;

            for (int j = 0; j < n_tokens; j++) {
                whisper_token_data td = whisper_full_get_token_data(ctx, i, j);
                const char* ttstr = whisper_token_to_str(ctx, td.id);
                if (*ttstr == '[') { continue; }
                first_token = td;
                first_j = j;
                break;
            }

            for (int j = n_tokens - 1; j >= 0; j--) {
                whisper_token_data td = whisper_full_get_token_data(ctx, i, j);
                const char* ttstr = whisper_token_to_str(ctx, td.id);
                if (*ttstr == '[') { continue; }
                last_token = td;
                last_j = j;
                break;
            }

            for (int j = 0; j < n_tokens; j++) {
                whisper_token_data td = whisper_full_get_token_data(ctx, i, j);
                const char* ttstr = whisper_token_to_str(ctx, td.id);
                if (*ttstr == '[') { continue; }

                if (ttstr[0] == ' ') {
                    if (!sepet.empty()) {
                        int64_t sure_cs = kelime_bitis - kelime_baslangic;
                        if (sure_cs <= 5) {
                            sure_cs = td.t0 - kelime_baslangic;
                        }
                        if (sure_cs <= 10) {
                            sure_cs = 10;
                        }
                        karaoke_satiri += "{\\k" + std::to_string(sure_cs) + "}" + clean_text(sepet.c_str()) + " ";
                    }
                    sepet = ttstr;
                    kelime_baslangic = td.t0;
                    kelime_bitis = td.t1;
                } else {
                    sepet += ttstr;
                    kelime_bitis = td.t1;
                }
            }
            if (!sepet.empty()) {
                int64_t sure_cs = kelime_bitis - kelime_baslangic;
                if (sure_cs <= 10) sure_cs = 10;
                karaoke_satiri += "{\\k" + std::to_string(sure_cs) + "}" + clean_text(sepet.c_str()) + " ";
            }

            if (first_j >= 0) { t0 = whisper_full_get_token_t0(ctx, i, first_j); }
            if (last_j >= 0) { t1 = whisper_full_get_token_t1(ctx, i, last_j); }

            t0 += OFFSET_CS;
            t1 += OFFSET_CS;

            cleanass = karaoke_satiri;
            if (token_avg < thold) {
                cleanass = "[belirsiz]";
                uncertain = true;
            }
            cleansrt = clean_text(text);
            if (token_avg < thold) {
                cleansrt = "[belirsiz]";
            }
        }

        Segment seg;
        seg.t0 = t0;
        seg.t1 = t1;
        seg.srtText = cleansrt;
        seg.originalSrtText = cleansrt;
        seg.assText = cleanass;
        seg.uncertain = uncertain;
        segments.push_back(seg);
    }

    return segments;
}
std::string rebuild_karaoke(const std::string& originalAssText, const std::string& newPlainText) {
    // Orijinal karaoke satırından (sure, kelime) çiftlerini çıkar
    std::vector<std::pair<std::string, std::string>> original;
    std::regex tagRegex(R"(\{\\k(\d+)\}([^\{]*))");
    auto begin = std::sregex_iterator(originalAssText.begin(), originalAssText.end(), tagRegex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        original.push_back({(*it)[1].str(), (*it)[2].str()});
    }

    // Yeni metni kelimelere ayır
    std::vector<std::string> newWords;
    std::istringstream iss(newPlainText);
    std::string w;
    while (iss >> w) newWords.push_back(w);

    if (original.empty() || newWords.empty()) {
        return newPlainText; // korunacak karaoke bilgisi yoksa düz metne düş
    }

    const std::string DEFAULT_DURATION = "20"; // fazladan kelime gelirse kullanılacak sabit süre

    std::ostringstream result;
    for (size_t i = 0; i < newWords.size(); i++) {
        std::string duration = (i < original.size()) ? original[i].first : DEFAULT_DURATION;
        result << "{\\k" << duration << "}" << newWords[i] << " ";
    }

    return result.str();
}

bool write_subtitles(const std::vector<Segment>& segments, const std::string& cikti_adi) {

    std::ofstream ofile(cikti_adi + ".srt");
    if (!ofile.is_open()) {
        std::cerr << "Yazilacak dosya acilamadi." << std::endl;
        return false;
    }

    std::ofstream ofile2(cikti_adi + ".ass");
    if (!ofile2.is_open()) {
        std::cerr << "Yazilacak dosya acilamadi." << std::endl;
        return false;
    }

    std::ostringstream ossass;
    ossass << "[Script Info]" << '\n' << "Title: " << cikti_adi << '\n' << "ScriptType: v4.00+" << '\n' << '\n'
    << "[V4+ Styles]" << '\n' << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
    << "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
    << "Alignment, MarginL, MarginR, MarginV, Encoding" << '\n' << "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1" << '\n' << '\n'
    << "[Events]" << '\n' << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text" << '\n';

    ofile2 << ossass.str();

    for (size_t i = 0; i < segments.size(); i++) {
        const Segment& seg = segments[i];

        std::string left = msToSrtTime(seg.t0 * 10);
        std::string right = msToSrtTime(seg.t1 * 10);

        ofile << i + 1 << '\n'
              << left << " --> " << right << '\n'
              << seg.srtText << '\n' << std::endl;

        ofile2 << "Dialogue: " << "0" << "," << csToAssTime(seg.t0) << "," << csToAssTime(seg.t1) << "," << "Default"
        "," << "" << "," << "0" << "," << "0" << "," << "0" << "," << "" << "," << seg.assText << '\n';
    }

    return true;
}