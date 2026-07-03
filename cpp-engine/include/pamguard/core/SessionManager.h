#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/core/AnalysisSession.h"
#include "pamguard/core/AudioFrame.h"

namespace pamguard::core {

class SessionManager {
public:
    void create_session(AnalysisConfig config);
    bool remove_session(const std::string& session_id);
    [[nodiscard]] bool has_session(const std::string& session_id) const;
    [[nodiscard]] std::size_t session_count() const;

    AnalysisResult process_audio(const std::string& session_id, const AudioChunk& chunk);
    AnalysisResult flush_session(const std::string& session_id);

private:
    struct ManagedSession {
        explicit ManagedSession(AnalysisConfig config);

        AnalysisSession session;
        mutable std::mutex mutex;
    };

    std::shared_ptr<ManagedSession> find_session(const std::string& session_id) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ManagedSession>> sessions_;
};

} // namespace pamguard::core
