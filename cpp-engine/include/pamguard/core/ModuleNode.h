#pragma once

#include <string>

namespace pamguard::core {

enum class ModuleState {
    Created,
    Prepared,
    Running,
    Stopped,
    Error,
};

class ModuleNode {
public:
    virtual ~ModuleNode() = default;

    [[nodiscard]] virtual const std::string& instance_id() const noexcept = 0;
    [[nodiscard]] virtual ModuleState state() const noexcept = 0;
    virtual void prepare() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    /** Publish/finalise buffered results without destroying configuration. */
    virtual void flush() {}
    virtual void reset() = 0;
    /** Explicit graph-lifecycle hooks; implementations may override. */
    virtual void disconnect() { stop(); }
    virtual void destroy() { stop(); }
};

} // namespace pamguard::core
