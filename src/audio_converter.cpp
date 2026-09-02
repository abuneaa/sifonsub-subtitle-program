#include "audio_converter.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sstream>

bool read_wav(const std::string& path, std::vector<float>& out_samples) {
    //&sız açsaydık her path çağırdığımızda kopyasını çağırıcaktık, performance issue
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Dosya acilamadi: " << path << std::endl;
        return false;
    }

    char riff_header[4]; // RIFF imzasını kontrol et
    file.read(riff_header, 4);
    if (std::strncmp(riff_header, "RIFF", 4) != 0) {
        std::cerr << "Gecerli bir WAV dosyasi degil (RIFF bulunamadi)" << std::endl;
        std::cout << "Okunan header: " << std::string(riff_header, 4) << std::endl;
        std::cout << "Okunan bayt sayisi: " << file.gcount() << std::endl;
        return false;
    }

    file.seekg(8); // boyut alanını atla, "WAVE" imzasına git.
    char wave_header[4];
    file.read(wave_header, 4);
    if (std::strncmp(wave_header, "WAVE", 4) != 0) {
        std::cerr << "Gecerli bir WAV dosyasi degil (WAVE bulunamadi)" << std::endl;
        return false;
    }

    // "data" chunk'ini bulana kadar chunk'lari gez
    char chunk_id[4]; //okunan chunk'in ismi
    uint32_t chunk_size = 0; //o chunk/bolum kac byte
    std::vector<int16_t> pcm_data; // ses verisini dolduracagimiz bos vektor

    while (file.read(chunk_id, 4)) {// dosyadan 4 byte okuyabilirsem devam et, okuyamazsam dur.
        file.read(reinterpret_cast<char*>(&chunk_size), 4);

        if (std::strncmp(chunk_id, "data", 4) == 0) {
            pcm_data.resize(chunk_size / sizeof(int16_t));
            file.read(reinterpret_cast<char*>(pcm_data.data()), chunk_size);
            break;
        } else {
            file.seekg(chunk_size, std::ios::cur); // ilgilenmedigimiz chunk'i atla
        }
    }

    if (pcm_data.empty()) {
        std::cerr << "Ses verisi (data chunk) bulunamadi" << std::endl;
        return false;
    }

    // int16 ornekleri float'a cevir (-1.0 ile 1.0 arasi), whisper bunu bekliyor
    out_samples.resize(pcm_data.size());//pcm_data.size() bize eleman sayısını size_t tipinde döndürür. ELEMAN SAYISINI BYTE DEGİL!
    for (size_t i = 0; i < pcm_data.size(); i++) {
        out_samples[i] = pcm_data[i] / 32768.0f;//burdaki f float demek, 32768 ise 16bit int i bölüp -1 ile 1 aralığında float sayılar elde etmek demek.
    }

    return true;
}

bool ffmpeg_cmd(const std::string& audio_path, std::string& ffmpegpath){
    ffmpegpath = "gecici_ses.wav";

    std::ostringstream o_syscom;
    o_syscom << "ffmpeg -i \"" << audio_path << "\" -ar 16000 -ac 1 -c:a pcm_s16le \"" << ffmpegpath << "\" -y";
    std::string syscom = o_syscom.str();

    if(std::system(syscom.c_str()) != 0){
        std::cerr << "ffmpeg donusumu tamamlanamadi." << std::endl;

        return false;
    }

    return true;
}