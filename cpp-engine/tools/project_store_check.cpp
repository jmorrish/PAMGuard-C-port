#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "pamguard/project/ProjectJson.h"
#include "pamguard/project/ProjectStore.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using pamguard::project::CorruptProjectFile;
using pamguard::project::ProjectDocument;
using pamguard::project::ProjectFileConflict;
using pamguard::project::ProjectFileEnvelope;
using pamguard::project::ProjectStore;
using pamguard::project::ProjectStoreError;
using pamguard::project::SavedProjectStatus;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Callback>
void require_throws(
    Callback&& callback,
    const std::string& message) {
    try {
        callback();
    }
    catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

ProjectDocument blank_project(
    std::string project_id,
    std::string name = "Project") {
    ProjectDocument project;
    project.project_id = std::move(project_id);
    project.metadata.name = std::move(name);
    project.metadata.description = "Portable project-store fixture";
    project.descriptor_set.id = "pamguard-2.02.18e";
    project.descriptor_set.version = 1;
    return project;
}

ProjectFileEnvelope saved_envelope(
    ProjectDocument project,
    std::uint64_t authority_revision,
    std::uint64_t saved_revision,
    std::int64_t saved_at_unix_ms) {
    ProjectFileEnvelope envelope;
    envelope.authority_revision = authority_revision;
    envelope.saved_revision = saved_revision;
    envelope.saved_at_unix_ms = saved_at_unix_ms;
    envelope.project = std::move(project);
    envelope.content_hash =
        pamguard::project::project_content_hash(
            envelope.project);
    return envelope;
}

void write_text(
    const std::filesystem::path& path,
    const std::string& text) {
    std::ofstream output(
        path,
        std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "Could not write fixture file");
    }
    output << text;
    output.flush();
    if (!output) {
        throw std::runtime_error(
            "Could not finish fixture file");
    }
}

#ifdef _WIN32

std::wstring quoted_argument(
    const std::filesystem::path& value) {
    return L"\"" + value.wstring() + L"\"";
}

int run_windows_lock_holder(
    const std::filesystem::path& lock_path,
    const std::filesystem::path& ready_path,
    const std::filesystem::path& release_path) {
    const auto lock = CreateFileW(
        lock_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_HIDDEN |
            FILE_FLAG_DELETE_ON_CLOSE |
            FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (lock == INVALID_HANDLE_VALUE) {
        return 21;
    }
    try {
        write_text(ready_path, "ready");
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() <
               deadline) {
            std::error_code error;
            if (std::filesystem::exists(
                    release_path,
                    error) &&
                !error) {
                CloseHandle(lock);
                return 0;
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
        CloseHandle(lock);
        return 22;
    }
    catch (...) {
        CloseHandle(lock);
        return 23;
    }
}

void exercise_cross_process_lock(
    ProjectStore& store,
    const ProjectFileEnvelope& replacement,
    const pamguard::project::ProjectFileFingerprint& expected,
    const std::filesystem::path& root) {
    auto lock_path =
        store.path_for(replacement.project.project_id);
    lock_path += ".lock";
    const auto ready_path = root / "external-lock.ready";
    const auto release_path = root / "external-lock.release";

    std::array<wchar_t, 32768> executable{};
    const auto executable_length =
        GetModuleFileNameW(
            nullptr,
            executable.data(),
            static_cast<DWORD>(executable.size()));
    require(
        executable_length != 0 &&
            executable_length < executable.size(),
        "Could not resolve project-store test executable");
    std::wstring command =
        quoted_argument(
            std::filesystem::path(
                std::wstring(
                    executable.data(),
                    executable_length))) +
        L" --hold-project-lock " +
        quoted_argument(lock_path) + L" " +
        quoted_argument(ready_path) + L" " +
        quoted_argument(release_path);
    std::vector<wchar_t> command_buffer(
        command.begin(),
        command.end());
    command_buffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(
        CreateProcessW(
            nullptr,
            command_buffer.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process) != FALSE,
        "Could not launch external project-lock holder");
    CloseHandle(process.hThread);

    bool released = false;
    try {
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() <
               deadline) {
            std::error_code error;
            if (std::filesystem::exists(
                    ready_path,
                    error) &&
                !error) {
                break;
            }
            if (WaitForSingleObject(
                    process.hProcess,
                    0) == WAIT_OBJECT_0) {
                throw std::runtime_error(
                    "External project-lock holder exited before ready");
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
        require(
            std::filesystem::exists(ready_path),
            "External project-lock holder did not become ready");
        require_throws<ProjectFileConflict>(
            [&] {
                (void)store.replace(
                    replacement,
                    expected);
            },
            "A cross-process project lock did not reject a writer");
        write_text(release_path, "release");
        released = true;
        require(
            WaitForSingleObject(
                process.hProcess,
                10000) == WAIT_OBJECT_0,
            "External project-lock holder did not exit");
        DWORD exit_code = 0;
        require(
            GetExitCodeProcess(
                process.hProcess,
                &exit_code) != FALSE &&
                exit_code == 0,
            "External project-lock holder reported a failure");
        CloseHandle(process.hProcess);
    }
    catch (...) {
        if (!released) {
            try {
                write_text(release_path, "release");
            }
            catch (...) {
            }
        }
        (void)WaitForSingleObject(
            process.hProcess,
            10000);
        CloseHandle(process.hProcess);
        throw;
    }
    std::error_code ignored;
    std::filesystem::remove(ready_path, ignored);
    std::filesystem::remove(release_path, ignored);
}

#else

void exercise_cross_process_lock(
    ProjectStore& store,
    const ProjectFileEnvelope& replacement,
    const pamguard::project::ProjectFileFingerprint& expected,
    const std::filesystem::path&) {
    auto lock_path =
        store.path_for(replacement.project.project_id);
    lock_path += ".lock";
    int ready_pipe[2]{};
    int release_pipe[2]{};
    require(
        ::pipe(ready_pipe) == 0 &&
            ::pipe(release_pipe) == 0,
        "Could not create cross-process lock pipes");
    const auto child = ::fork();
    require(
        child >= 0,
        "Could not fork cross-process lock holder");
    if (child == 0) {
        ::close(ready_pipe[0]);
        ::close(release_pipe[1]);
        int flags = O_CREAT | O_RDWR;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        const auto descriptor =
            ::open(
                lock_path.c_str(),
                flags,
                S_IRUSR | S_IWUSR);
        if (descriptor < 0 ||
            ::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
            _exit(31);
        }
        const char ready = 'R';
        if (::write(ready_pipe[1], &ready, 1) != 1) {
            _exit(32);
        }
        char release = '\0';
        if (::read(release_pipe[0], &release, 1) != 1) {
            _exit(33);
        }
        ::flock(descriptor, LOCK_UN);
        ::close(descriptor);
        _exit(0);
    }

    ::close(ready_pipe[1]);
    ::close(release_pipe[0]);
    char ready = '\0';
    require(
        ::read(ready_pipe[0], &ready, 1) == 1 &&
            ready == 'R',
        "Cross-process project-lock holder did not become ready");
    try {
        require_throws<ProjectFileConflict>(
            [&] {
                (void)store.replace(
                    replacement,
                    expected);
            },
            "A cross-process project lock did not reject a writer");
    }
    catch (...) {
        const char release = 'X';
        (void)::write(release_pipe[1], &release, 1);
        int status = 0;
        (void)::waitpid(child, &status, 0);
        ::close(ready_pipe[0]);
        ::close(release_pipe[1]);
        throw;
    }
    const char release = 'X';
    require(
        ::write(release_pipe[1], &release, 1) == 1,
        "Could not release cross-process project lock");
    int status = 0;
    require(
        ::waitpid(child, &status, 0) == child &&
            WIFEXITED(status) &&
            WEXITSTATUS(status) == 0,
        "Cross-process project-lock holder reported a failure");
    ::close(ready_pipe[0]);
    ::close(release_pipe[1]);
}

#endif

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc == 5 &&
        std::string(argv[1]) ==
            "--hold-project-lock") {
        return run_windows_lock_holder(
            std::filesystem::path(argv[2]),
            std::filesystem::path(argv[3]),
            std::filesystem::path(argv[4]));
    }
#else
    (void)argc;
    (void)argv;
#endif
    const auto root =
        std::filesystem::temp_directory_path() /
        (
            "pamguard-project-store-" +
            std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()));
    std::filesystem::create_directories(root);
    auto original_root = root;
    original_root += ".original";
    auto outside_project = root;
    outside_project += ".outside-project";

    try {
        {
        constexpr auto project_id =
            "123e4567-e89b-42d3-a456-426614174000";
        constexpr auto second_id =
            "123e4567-e89b-42d3-b456-426614174001";
        constexpr auto symlink_id =
            "123e4567-e89b-42d4-8456-426614174002";
        ProjectStore store(root);
        require(
            std::filesystem::equivalent(
                store.root(),
                root),
            "Project store did not canonicalize its root");

        auto first = saved_envelope(
            blank_project(project_id),
            1,
            0,
            1785000000000);
        const auto first_fingerprint =
            store.create(first);
        require(
            store.exists(project_id),
            "Created project is not visible");
        const auto loaded = store.load(project_id);
        require(
            loaded.envelope == first &&
                loaded.fingerprint == first_fingerprint,
            "Created project did not round-trip exactly");

        require_throws<ProjectFileConflict>(
            [&] { (void)store.create(first); },
            "Save As overwrote an existing project");

        auto second = first;
        second.project.metadata.name = "Renamed Project";
        second.authority_revision = 3;
        second.saved_revision = 2;
        second.saved_at_unix_ms = 1785000001000;
        second.content_hash =
            pamguard::project::project_content_hash(
                second.project);
        const auto second_fingerprint =
            store.replace(second, first_fingerprint);
        require(
            store.load(project_id).envelope == second,
            "Replacement did not publish the new project");

        // A durable CAS token must cover the complete normalized envelope,
        // not just the project-content tuple. Simulate an uncooperative
        // external writer changing only savedAtUnixMs.
        auto external_rewrite = second;
        external_rewrite.saved_at_unix_ms += 77;
        write_text(
            store.path_for(project_id),
            pamguard::project::
                project_file_envelope_to_json(
                    external_rewrite,
                    false));
        const auto externally_loaded =
            store.load(project_id);
        require(
            externally_loaded.fingerprint.authority_revision ==
                second_fingerprint.authority_revision &&
                externally_loaded.fingerprint.saved_revision ==
                    second_fingerprint.saved_revision &&
                externally_loaded.fingerprint.content_hash ==
                    second_fingerprint.content_hash &&
                externally_loaded.fingerprint.envelope_hash !=
                    second_fingerprint.envelope_hash,
            "Complete-envelope fingerprint did not detect an "
            "external metadata-only rewrite");
        require_throws<ProjectFileConflict>(
            [&] {
                (void)store.replace(
                    second,
                    second_fingerprint);
            },
            "An external complete-envelope rewrite evaded durable CAS");
        const auto restored_fingerprint =
            store.replace(
                second,
                externally_loaded.fingerprint);
        require(
            restored_fingerprint == second_fingerprint,
            "Project was not restored after the external-writer test");

        auto stale_attempt = second;
        stale_attempt.project.metadata.name = "Stale writer";
        stale_attempt.authority_revision = 4;
        stale_attempt.saved_revision = 3;
        stale_attempt.content_hash =
            pamguard::project::project_content_hash(
                stale_attempt.project);
        require_throws<ProjectFileConflict>(
            [&] {
                (void)store.replace(
                    stale_attempt,
                    first_fingerprint);
            },
            "A stale durable fingerprint overwrote the project");
        require(
            store.load(project_id).fingerprint ==
                second_fingerprint,
            "Stale replacement changed the durable project");

        auto locked_attempt = stale_attempt;
        locked_attempt.project.metadata.name =
            "Cross-process lock contender";
        locked_attempt.content_hash =
            pamguard::project::project_content_hash(
                locked_attempt.project);
        exercise_cross_process_lock(
            store,
            locked_attempt,
            second_fingerprint,
            root);
        require(
            store.load(project_id).fingerprint ==
                second_fingerprint,
            "Cross-process lock conflict changed the durable project");

        auto wrong_hash = second;
        wrong_hash.authority_revision = 4;
        wrong_hash.saved_revision = 3;
        wrong_hash.content_hash =
            "sha256:0000000000000000000000000000000000000000000000000000000000000000";
        require_throws<CorruptProjectFile>(
            [&] {
                (void)store.replace(
                    wrong_hash,
                    second_fingerprint);
            },
            "A mismatched content hash was persisted");
        require(
            store.load(project_id).fingerprint ==
                second_fingerprint,
            "Failed validation changed the durable project");

        write_text(outside_project, "not a project");
        std::error_code symlink_error;
        std::filesystem::create_symlink(
            outside_project,
            store.path_for(symlink_id),
            symlink_error);
#ifndef _WIN32
        require(
            !symlink_error,
            "Could not construct the project symlink fixture");
#endif
        if (!symlink_error) {
            require_throws<ProjectStoreError>(
                [&] {
                    (void)store.exists(symlink_id);
                },
                "A project symlink/reparse point was accepted");
            std::filesystem::remove(
                store.path_for(symlink_id));
        }
        std::filesystem::remove(outside_project);

        const auto unsupported_path =
            store.path_for(second_id);
        write_text(
            unsupported_path,
            R"({"authorityRevision":1,"canonicalizationVersion":1,"contentHash":"sha256:0000000000000000000000000000000000000000000000000000000000000000","fileFormat":"pamguard-project","fileFormatVersion":99,"project":{},"savedAtUnixMs":0,"savedRevision":0})");
        const auto summaries = store.list();
        require(
            summaries.size() == 2,
            "Project listing did not include every UUID file");
        require(
            summaries[0].project_id == project_id &&
                summaries[0].status ==
                    SavedProjectStatus::Available &&
                summaries[0].name == "Renamed Project",
            "Available project summary is incorrect");
        require(
            summaries[1].project_id == second_id &&
                summaries[1].status ==
                    SavedProjectStatus::Unsupported,
            "Unsupported project was hidden or misclassified");

        require_throws<pamguard::project::ProjectStoreError>(
            [&] {
                (void)store.path_for("../../escape");
            },
            "A client-controlled project path escaped the store");

        auto overflow_version =
            pamguard::project::project_file_envelope_to_json(
                second,
                false);
        const auto version_token =
            std::string("\"fileFormatVersion\":1");
        const auto version_offset =
            overflow_version.find(version_token);
        require(
            version_offset != std::string::npos,
            "Could not construct overflowed-version fixture");
        overflow_version.replace(
            version_offset,
            version_token.size(),
            "\"fileFormatVersion\":4294967297");
        bool rejected_for_range = false;
        try {
            (void)pamguard::project::
                project_file_envelope_from_json(
                    overflow_version);
        }
        catch (const CorruptProjectFile& error) {
            rejected_for_range =
                std::string(error.what()).find(
                    "outside the supported range") !=
                std::string::npos;
        }
        require(
            rejected_for_range,
            "Overflowed uint32 file version was not rejected at "
            "the numeric range gate");

        std::filesystem::rename(root, original_root);
        std::filesystem::create_directories(root);
        require_throws<ProjectStoreError>(
            [&] { (void)store.path_for(project_id); },
            "Same-path project root replacement evaded identity checking");
        }

        std::filesystem::remove_all(root);
        std::filesystem::remove_all(original_root);
        std::cout
            << "Project store check passed: UUID confinement, "
               "strict envelope/hash validation, atomic create/replace, "
               "complete-envelope CAS, cross-process locking, retained-root "
               "identity, reparse rejection, restart round-trip, and "
               "corrupt/unsupported listing\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::filesystem::remove_all(original_root, ignored);
        std::filesystem::remove(outside_project, ignored);
        std::cerr
            << "Project store check failed: "
            << error.what() << "\n";
        return 1;
    }
}
