#include "pamguard/core/SessionManager.h"

#include <stdexcept>
#include <utility>

namespace pamguard::core {

SessionManager::ManagedSession::ManagedSession(AnalysisConfig config)
    : session(std::move(config)) {
}

void SessionManager::create_session(AnalysisConfig config) {
    if (config.session_id.empty()) {
        throw std::invalid_argument("session_id must not be empty");
    }

    auto managed = std::make_shared<ManagedSession>(std::move(config));
    const auto& session_id = managed->session.config().session_id;

    std::lock_guard lock(mutex_);
    const auto [_, inserted] = sessions_.emplace(session_id, std::move(managed));
    if (!inserted) {
        throw std::runtime_error("session already exists: " + session_id);
    }
}

bool SessionManager::remove_session(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    return sessions_.erase(session_id) != 0;
}

bool SessionManager::has_session(const std::string& session_id) const {
    std::lock_guard lock(mutex_);
    return sessions_.find(session_id) != sessions_.end();
}

std::size_t SessionManager::session_count() const {
    std::lock_guard lock(mutex_);
    return sessions_.size();
}

AnalysisResult SessionManager::process_audio(const std::string& session_id, const AudioChunk& chunk) {
    auto managed = find_session(session_id);
    std::lock_guard lock(managed->mutex);
    return managed->session.process(chunk);
}

AnalysisResult SessionManager::flush_session(const std::string& session_id) {
    auto managed = find_session(session_id);
    std::lock_guard lock(managed->mutex);
    return managed->session.flush();
}

std::shared_ptr<SessionManager::ManagedSession> SessionManager::find_session(const std::string& session_id) const {
    std::lock_guard lock(mutex_);
    const auto found = sessions_.find(session_id);
    if (found == sessions_.end()) {
        throw std::runtime_error("unknown session: " + session_id);
    }
    return found->second;
}

} // namespace pamguard::core
