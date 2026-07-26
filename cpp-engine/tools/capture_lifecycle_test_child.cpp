#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv) {
    std::size_t channel_count = 0;
    bool f32le_output = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "-ac" && index + 1 < argc) {
            channel_count = static_cast<std::size_t>(
                std::stoull(argv[++index]));
        }
        else if (argument == "-f" && index + 1 < argc) {
            f32le_output =
                std::string(argv[++index]) == "f32le";
        }
    }

    // When used as deterministic fake FFmpeg, publish two non-zero 2400-frame
    // chunks and keep the pipe open. This exercises the real ingest bridge
    // while leaving capture lifecycle ownership with the service Job Object.
    if (f32le_output && channel_count > 0) {
#ifdef _WIN32
        if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
            return 2;
        }
#endif
        constexpr std::size_t frame_count = 4800;
        std::vector<float> pcm(frame_count * channel_count);
        for (std::size_t index = 0; index < pcm.size(); ++index) {
            pcm[index] =
                static_cast<float>(
                    (static_cast<int>(index % 17) - 8) *
                    0.0125);
        }
        const auto written = std::fwrite(
            pcm.data(),
            sizeof(float),
            pcm.size(),
            stdout);
        std::fflush(stdout);
        if (written != pcm.size()) {
            return 3;
        }
    }

    // The capture service supplies the real ingest command line. This
    // deterministic child remains alive until the service's capture Job
    // Object stops it.
    std::this_thread::sleep_for(std::chrono::minutes(10));
    return 0;
}
