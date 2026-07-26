#include "pamguard/project/ProjectStore.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <system_error>
#include <unordered_set>

#include <json.hpp>

#include "CanonicalJson.h"
#include "Sha256.h"
#include "pamguard/project/ProjectJson.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pamguard::project {

namespace detail {

class ProjectStoreRootHandle {
public:
    explicit ProjectStoreRootHandle(
        const std::filesystem::path& root) {
#ifdef _WIN32
        handle = CreateFileW(
            root.c_str(),
            FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            // Share delete so the service can diagnose root replacement (and
            // so shutdown/rollback tests can move the directory). The retained
            // handle remains a stable identity reference even after a rename.
            FILE_SHARE_READ | FILE_SHARE_WRITE |
                FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS |
                FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            throw ProjectStoreError(
                "Could not retain configured project storage directory "
                "(Windows error " +
                std::to_string(GetLastError()) + ")");
        }
        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(handle, &information) ||
            (information.dwFileAttributes &
             FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (information.dwFileAttributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            const auto error = GetLastError();
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            throw ProjectStoreError(
                "Configured project storage root is not a stable real "
                "directory (Windows error " +
                std::to_string(error) + ")");
        }
        identity_high = information.dwVolumeSerialNumber;
        identity_low =
            (static_cast<std::uint64_t>(
                 information.nFileIndexHigh)
             << 32U) |
            information.nFileIndexLow;
#else
        int flags = O_RDONLY;
#ifdef O_DIRECTORY
        flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        descriptor = ::open(root.c_str(), flags);
        if (descriptor < 0) {
            throw ProjectStoreError(
                "Could not retain configured project storage directory: " +
                std::error_code(
                    errno,
                    std::generic_category())
                    .message());
        }
        struct stat information {};
        if (::fstat(descriptor, &information) != 0 ||
            !S_ISDIR(information.st_mode)) {
            const auto error = errno;
            ::close(descriptor);
            descriptor = -1;
            throw ProjectStoreError(
                "Configured project storage root is not a stable real "
                "directory: " +
                std::error_code(
                    error,
                    std::generic_category())
                    .message());
        }
        identity_high =
            static_cast<std::uint64_t>(information.st_dev);
        identity_low =
            static_cast<std::uint64_t>(information.st_ino);
#endif
    }

    ProjectStoreRootHandle(const ProjectStoreRootHandle&) = delete;
    ProjectStoreRootHandle& operator=(
        const ProjectStoreRootHandle&) = delete;

    ~ProjectStoreRootHandle() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
#else
        if (descriptor >= 0) {
            ::close(descriptor);
        }
#endif
    }

    std::uint64_t identity_high = 0;
    std::uint64_t identity_low = 0;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int descriptor = -1;
#endif
};

} // namespace detail

namespace {

using Json = nlohmann::json;

constexpr std::string_view kFileFormat = "pamguard-project";
constexpr std::string_view kFileSuffix = ".pamguard-project.json";

[[noreturn]] void corrupt(const std::string& message) {
    throw CorruptProjectFile(message);
}

void require_exact_fields(
    const Json& value,
    std::initializer_list<std::string_view> names,
    std::string_view context) {
    if (!value.is_object()) {
        corrupt(std::string(context) + " must be an object");
    }
    std::unordered_set<std::string> expected;
    for (const auto name : names) {
        expected.emplace(name);
        if (!value.contains(std::string(name))) {
            corrupt(
                std::string(context) + " is missing '" +
                std::string(name) + "'");
        }
    }
    for (const auto& [key, ignored] : value.items()) {
        (void)ignored;
        if (!expected.contains(key)) {
            corrupt(
                std::string(context) + " contains unknown field '" +
                key + "'");
        }
    }
}

template <typename Integer>
Integer unsigned_integer(
    const Json& value,
    const char* field,
    std::string_view context) {
    const auto& member = value.at(field);
    if (!member.is_number_unsigned()) {
        corrupt(
            std::string(context) + "." + field +
            " must be an unsigned integer");
    }
    try {
        const auto number = member.get<std::uint64_t>();
        if (number >
            static_cast<std::uint64_t>(
                std::numeric_limits<Integer>::max())) {
            corrupt(
                std::string(context) + "." + field +
                " is outside the supported range");
        }
        return static_cast<Integer>(number);
    }
    catch (const Json::exception&) {
        corrupt(
            std::string(context) + "." + field +
            " is outside the supported range");
    }
}

std::string required_string(
    const Json& value,
    const char* field,
    std::string_view context) {
    const auto& member = value.at(field);
    if (!member.is_string()) {
        corrupt(
            std::string(context) + "." + field +
            " must be a string");
    }
    return member.get<std::string>();
}

struct PathIdentity {
    std::uint64_t high = 0;
    std::uint64_t low = 0;
};

PathIdentity directory_identity(
    const std::filesystem::path& root) {
#ifdef _WIN32
    const auto handle = CreateFileW(
        root.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw ProjectStoreError(
            "Could not open configured project storage directory "
            "(Windows error " +
            std::to_string(GetLastError()) + ")");
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const auto success =
        GetFileInformationByHandle(handle, &information);
    const auto error = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!success ||
        (information.dwFileAttributes &
         FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (information.dwFileAttributes &
         FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        throw ProjectStoreError(
            "Configured project storage root is not a stable real "
            "directory (Windows error " +
            std::to_string(error) + ")");
    }
    return {
        information.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(
             information.nFileIndexHigh)
         << 32U) |
            information.nFileIndexLow,
    };
#else
    struct stat information {};
    if (::lstat(root.c_str(), &information) != 0 ||
        !S_ISDIR(information.st_mode) ||
        S_ISLNK(information.st_mode)) {
        throw ProjectStoreError(
            "Configured project storage root is not a stable real "
            "directory");
    }
    return {
        static_cast<std::uint64_t>(information.st_dev),
        static_cast<std::uint64_t>(information.st_ino),
    };
#endif
}

void verify_store_root(
    const std::filesystem::path& root,
    const detail::ProjectStoreRootHandle& retained,
    std::uint64_t expected_high,
    std::uint64_t expected_low) {
    PathIdentity retained_identity;
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION retained_information{};
    if (!GetFileInformationByHandle(
            retained.handle,
            &retained_information) ||
        (retained_information.dwFileAttributes &
         FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (retained_information.dwFileAttributes &
         FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        throw ProjectStoreError(
            "Retained project storage directory handle is no longer valid");
    }
    retained_identity = {
        retained_information.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(
             retained_information.nFileIndexHigh)
         << 32U) |
            retained_information.nFileIndexLow,
    };
#else
    struct stat retained_information {};
    if (::fstat(
            retained.descriptor,
            &retained_information) != 0 ||
        !S_ISDIR(retained_information.st_mode)) {
        throw ProjectStoreError(
            "Retained project storage directory descriptor is no longer "
            "valid");
    }
    retained_identity = {
        static_cast<std::uint64_t>(
            retained_information.st_dev),
        static_cast<std::uint64_t>(
            retained_information.st_ino),
    };
#endif
    if (retained_identity.high != expected_high ||
        retained_identity.low != expected_low ||
        retained.identity_high != expected_high ||
        retained.identity_low != expected_low) {
        throw ProjectStoreError(
            "Retained project storage directory identity changed");
    }

    std::error_code error;
    const auto status =
        std::filesystem::symlink_status(root, error);
    if (error ||
        std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
        throw ProjectStoreError(
            "Configured project storage directory is no longer a "
            "real directory");
    }
    const auto current =
        std::filesystem::canonical(root, error);
    if (error || current != root) {
        throw ProjectStoreError(
            "Configured project storage directory changed after startup");
    }
    const auto identity = directory_identity(root);
    if (identity.high != expected_high ||
        identity.low != expected_low) {
        throw ProjectStoreError(
            "Configured project storage directory identity changed "
            "after startup");
    }
}

std::string project_filename(
    const std::string& project_id) {
    return project_id + std::string(kFileSuffix);
}

#ifdef _WIN32

HANDLE open_existing_project_handle(
    const std::filesystem::path& root,
    const std::string& filename,
    DWORD desired_access,
    DWORD share_mode) {
    const auto path = root / filename;
    const auto handle = CreateFileW(
        path.c_str(),
        desired_access,
        share_mode,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND ||
            error == ERROR_PATH_NOT_FOUND) {
            throw ProjectStoreError(
                "Saved project does not exist");
        }
        throw ProjectStoreError(
            "Could not open project file '" +
            path.string() + "' (Windows error " +
            std::to_string(error) + ")");
    }
    return handle;
}

void require_regular_project_handle(
    HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(handle, &information)) {
        throw ProjectStoreError(
            "Could not inspect opened project file "
            "(Windows error " +
            std::to_string(GetLastError()) + ")");
    }
    if ((information.dwFileAttributes &
         FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        throw ProjectStoreError(
            "Project file must not be a symbolic or reparse link");
    }
    if ((information.dwFileAttributes &
         FILE_ATTRIBUTE_DIRECTORY) != 0) {
        throw ProjectStoreError(
            "Project path is not a regular file");
    }
}

#endif

bool relative_project_exists(
    const detail::ProjectStoreRootHandle& retained,
    const std::filesystem::path& root,
    const std::string& filename) {
#ifdef _WIN32
    (void)retained;
    const auto path = root / filename;
    const auto handle = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND ||
            error == ERROR_PATH_NOT_FOUND) {
            return false;
        }
        throw ProjectStoreError(
            "Could not inspect project file (Windows error " +
            std::to_string(error) + ")");
    }
    try {
        require_regular_project_handle(handle);
    }
    catch (...) {
        CloseHandle(handle);
        throw;
    }
    CloseHandle(handle);
    return true;
#else
    (void)root;
    struct stat information {};
    if (::fstatat(
            retained.descriptor,
            filename.c_str(),
            &information,
            AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            return false;
        }
        throw ProjectStoreError(
            "Could not inspect project file: " +
            std::error_code(
                errno,
                std::generic_category())
                .message());
    }
    if (S_ISLNK(information.st_mode)) {
        throw ProjectStoreError(
            "Project file must not be a symbolic link");
    }
    if (!S_ISREG(information.st_mode)) {
        throw ProjectStoreError(
            "Project path is not a regular file");
    }
    return true;
#endif
}

std::string read_bounded_file(
    const detail::ProjectStoreRootHandle& retained,
    const std::filesystem::path& root,
    const std::string& filename) {
#ifdef _WIN32
    (void)retained;
    const auto handle = open_existing_project_handle(
        root,
        filename,
        GENERIC_READ | FILE_READ_ATTRIBUTES,
        // Deny write/delete while the exact opened file is validated and
        // read, preventing a torn snapshot from an ordinary external writer.
        FILE_SHARE_READ);
    BY_HANDLE_FILE_INFORMATION before{};
    try {
        require_regular_project_handle(handle);
        if (!GetFileInformationByHandle(handle, &before)) {
            throw ProjectStoreError(
                "Could not inspect project file size "
                "(Windows error " +
                std::to_string(GetLastError()) + ")");
        }
        const auto size =
            (static_cast<std::uint64_t>(before.nFileSizeHigh)
             << 32U) |
            before.nFileSizeLow;
        if (size > detail::kMaximumProjectJsonBytes) {
            throw CorruptProjectFile(
                "Project file exceeds " +
                std::to_string(
                    detail::kMaximumProjectJsonBytes) +
                " bytes");
        }
        std::string bytes(
            static_cast<std::size_t>(size),
            '\0');
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto chunk = static_cast<DWORD>(
                std::min<std::size_t>(
                    bytes.size() - offset,
                    std::numeric_limits<DWORD>::max()));
            DWORD read = 0;
            if (!ReadFile(
                    handle,
                    bytes.data() + offset,
                    chunk,
                    &read,
                    nullptr) ||
                read == 0) {
                throw ProjectStoreError(
                    "Could not read complete project file "
                    "(Windows error " +
                    std::to_string(GetLastError()) + ")");
            }
            offset += read;
        }
        char extra = '\0';
        DWORD extra_read = 0;
        if (!ReadFile(
                handle,
                &extra,
                1,
                &extra_read,
                nullptr)) {
            throw ProjectStoreError(
                "Could not verify project file boundary "
                "(Windows error " +
                std::to_string(GetLastError()) + ")");
        }
        BY_HANDLE_FILE_INFORMATION after{};
        if (extra_read != 0 ||
            !GetFileInformationByHandle(handle, &after) ||
            before.dwVolumeSerialNumber !=
                after.dwVolumeSerialNumber ||
            before.nFileIndexHigh != after.nFileIndexHigh ||
            before.nFileIndexLow != after.nFileIndexLow ||
            before.nFileSizeHigh != after.nFileSizeHigh ||
            before.nFileSizeLow != after.nFileSizeLow) {
            throw ProjectStoreError(
                "Project file changed while it was being read");
        }
        CloseHandle(handle);
        return bytes;
    }
    catch (...) {
        CloseHandle(handle);
        throw;
    }
#else
    (void)root;
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const auto descriptor =
        ::openat(
            retained.descriptor,
            filename.c_str(),
            flags);
    if (descriptor < 0) {
        if (errno == ENOENT) {
            throw ProjectStoreError(
                "Saved project does not exist");
        }
        throw ProjectStoreError(
            "Could not open project file: " +
            std::error_code(
                errno,
                std::generic_category())
                .message());
    }
    try {
        struct stat before {};
        if (::fstat(descriptor, &before) != 0) {
            throw ProjectStoreError(
                "Could not inspect opened project file");
        }
        if (!S_ISREG(before.st_mode)) {
            throw ProjectStoreError(
                "Project path is not a regular file");
        }
        if (before.st_size < 0 ||
            static_cast<std::uint64_t>(before.st_size) >
                detail::kMaximumProjectJsonBytes) {
            throw CorruptProjectFile(
                "Project file exceeds " +
                std::to_string(
                    detail::kMaximumProjectJsonBytes) +
                " bytes");
        }
        std::string bytes(
            static_cast<std::size_t>(before.st_size),
            '\0');
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto read_count =
                ::read(
                    descriptor,
                    bytes.data() + offset,
                    bytes.size() - offset);
            if (read_count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw ProjectStoreError(
                    "Could not read project file");
            }
            if (read_count == 0) {
                throw ProjectStoreError(
                    "Could not read complete project file");
            }
            offset +=
                static_cast<std::size_t>(read_count);
        }
        char extra = '\0';
        ssize_t extra_read = 0;
        do {
            extra_read = ::read(descriptor, &extra, 1);
        } while (extra_read < 0 && errno == EINTR);
        struct stat after {};
        if (extra_read < 0 ||
            extra_read != 0 ||
            ::fstat(descriptor, &after) != 0 ||
            before.st_dev != after.st_dev ||
            before.st_ino != after.st_ino ||
            before.st_size != after.st_size) {
            throw ProjectStoreError(
                "Project file changed while it was being read");
        }
        ::close(descriptor);
        return bytes;
    }
    catch (...) {
        ::close(descriptor);
        throw;
    }
#endif
}

class ProjectWriteLock {
public:
    ProjectWriteLock(
        const detail::ProjectStoreRootHandle& retained,
        const std::filesystem::path& root,
        const std::string& project_filename) {
        lock_filename_ = project_filename + ".lock";
#ifdef _WIN32
        (void)retained;
        lock_path_ = root / lock_filename_;
        handle_ = CreateFileW(
            lock_path_.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_HIDDEN |
                FILE_FLAG_DELETE_ON_CLOSE |
                FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            if (error == ERROR_FILE_EXISTS ||
                error == ERROR_ALREADY_EXISTS ||
                error == ERROR_SHARING_VIOLATION) {
                throw ProjectFileConflict(
                    "Another engine process is saving this project");
            }
            throw ProjectStoreError(
                "Could not acquire project save lock (Windows error " +
                std::to_string(error) + ")");
        }
#else
        (void)root;
        int open_flags = O_CREAT | O_RDWR;
#ifdef O_CLOEXEC
        open_flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        open_flags |= O_NOFOLLOW;
#endif
        descriptor_ = ::openat(
            retained.descriptor,
            lock_filename_.c_str(),
            open_flags,
            S_IRUSR | S_IWUSR);
        if (descriptor_ < 0) {
            throw ProjectStoreError(
                "Could not open project save lock: " +
                std::error_code(
                    errno,
                    std::generic_category())
                    .message());
        }
        int lock_result = 0;
        do {
            lock_result =
                ::flock(
                    descriptor_,
                    LOCK_EX | LOCK_NB);
        } while (lock_result != 0 && errno == EINTR);
        if (lock_result != 0) {
            const auto lock_error = errno;
            ::close(descriptor_);
            descriptor_ = -1;
            if (lock_error == EWOULDBLOCK ||
                lock_error == EAGAIN) {
                throw ProjectFileConflict(
                    "Another engine process is saving this project");
            }
            throw ProjectStoreError(
                "Could not lock project save sentinel: " +
                std::error_code(
                    lock_error,
                    std::generic_category())
                    .message());
        }
#endif
    }

    ProjectWriteLock(const ProjectWriteLock&) = delete;
    ProjectWriteLock& operator=(const ProjectWriteLock&) = delete;

    ~ProjectWriteLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
#else
        if (descriptor_ >= 0) {
            ::flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
        }
#endif
    }

private:
    std::string lock_filename_;
#ifdef _WIN32
    std::filesystem::path lock_path_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

std::string temporary_filename_for(
    const std::string& destination_filename) {
    static std::atomic<std::uint64_t> counter{1};
#ifdef _WIN32
    const auto process_id =
        static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const auto process_id =
        static_cast<std::uint64_t>(::getpid());
#endif
    return destination_filename +
        ".tmp." +
        std::to_string(process_id) +
        "." +
        std::to_string(
            counter.fetch_add(1, std::memory_order_relaxed));
}

#ifdef _WIN32
void write_all(HANDLE handle, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<DWORD>(
            std::min<std::size_t>(
                remaining,
                std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(
                handle,
                bytes.data() + offset,
                chunk,
                &written,
                nullptr) ||
            written != chunk) {
            throw ProjectStoreError(
                "Could not write complete temporary project file "
                "(Windows error " +
                std::to_string(GetLastError()) + ")");
        }
        offset += written;
    }
}
#else
void write_all(int descriptor, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto written = ::write(
            descriptor,
            bytes.data() + offset,
            bytes.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw ProjectStoreError(
                "Could not write temporary project file");
        }
        offset += static_cast<std::size_t>(written);
    }
}
#endif

#ifndef _WIN32
void fsync_file(int descriptor) {
    int result = 0;
    do {
        result = ::fsync(descriptor);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        throw ProjectStoreError(
            "Could not flush temporary project file: " +
            std::error_code(
                errno,
                std::generic_category())
                .message());
    }
}

void fsync_published_directory(
    const detail::ProjectStoreRootHandle& retained) {
    int result = 0;
    do {
        result = ::fsync(retained.descriptor);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        throw ProjectDurabilityError(
            "Project file was atomically published, but the project "
            "directory durability barrier failed: " +
            std::error_code(
                errno,
                std::generic_category())
                .message() +
            ". Reload the saved project before retrying.");
    }
}
#endif

void atomic_write(
    const detail::ProjectStoreRootHandle& retained,
    const std::filesystem::path& root,
    const std::string& destination_filename,
    std::string_view bytes,
    bool replace_existing) {
    verify_store_root(
        root,
        retained,
        retained.identity_high,
        retained.identity_low);
    std::string temporary_filename;
    bool temporary_exists = false;
    try {
#ifdef _WIN32
        (void)retained;
        HANDLE handle = INVALID_HANDLE_VALUE;
        std::filesystem::path temporary;
        for (int attempt = 0; attempt < 64; ++attempt) {
            temporary_filename =
                temporary_filename_for(
                    destination_filename);
            temporary = root / temporary_filename;
            handle = CreateFileW(
                temporary.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY |
                    FILE_FLAG_WRITE_THROUGH |
                    FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                break;
            }
            const auto error = GetLastError();
            if (error != ERROR_FILE_EXISTS &&
                error != ERROR_ALREADY_EXISTS) {
                throw ProjectStoreError(
                    "Could not create temporary project file "
                    "(Windows error " +
                    std::to_string(error) + ")");
            }
        }
        if (handle == INVALID_HANDLE_VALUE) {
            throw ProjectStoreError(
                "Could not allocate a unique temporary project file");
        }
        temporary_exists = true;
        try {
            verify_store_root(
                root,
                retained,
                retained.identity_high,
                retained.identity_low);
            write_all(handle, bytes);
            if (!FlushFileBuffers(handle)) {
                throw ProjectStoreError(
                    "Could not flush temporary project file "
                    "(Windows error " +
                    std::to_string(GetLastError()) + ")");
            }
        }
        catch (...) {
            CloseHandle(handle);
            throw;
        }
        if (!CloseHandle(handle)) {
            throw ProjectStoreError(
                "Could not close temporary project file");
        }

        verify_store_root(
            root,
            retained,
            retained.identity_high,
            retained.identity_low);
        const auto destination =
            root / destination_filename;
        BOOL published = FALSE;
        if (replace_existing) {
            // ReplaceFile requires an existing destination at the atomic
            // operation itself. Unlike MoveFileEx(REPLACE_EXISTING), an
            // external deletion cannot silently turn Replace into Create.
            published = ReplaceFileW(
                destination.c_str(),
                temporary.c_str(),
                nullptr,
                REPLACEFILE_IGNORE_MERGE_ERRORS,
                nullptr,
                nullptr);
        }
        else {
            published = MoveFileExW(
                temporary.c_str(),
                destination.c_str(),
                MOVEFILE_WRITE_THROUGH);
        }
        if (!published) {
            const auto error = GetLastError();
            if (!replace_existing &&
                (error == ERROR_FILE_EXISTS ||
                 error == ERROR_ALREADY_EXISTS)) {
                throw ProjectFileConflict(
                    "Project file already exists");
            }
            throw ProjectStoreError(
                "Could not atomically publish project file "
                "(Windows error " +
                std::to_string(error) + ")");
        }
        temporary_exists = false;
        try {
            verify_store_root(
                root,
                retained,
                retained.identity_high,
                retained.identity_low);
        }
        catch (const ProjectStoreError& error) {
            throw ProjectDurabilityError(
                "Project file was atomically published, but the "
                "configured project directory changed at the publish "
                "boundary: " +
                std::string(error.what()) +
                ". Reload project storage before retrying.");
        }
#else
        (void)root;
        int descriptor = -1;
        for (int attempt = 0; attempt < 64; ++attempt) {
            temporary_filename =
                temporary_filename_for(
                    destination_filename);
            int open_flags =
                O_CREAT | O_EXCL | O_WRONLY;
#ifdef O_CLOEXEC
            open_flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
            open_flags |= O_NOFOLLOW;
#endif
            descriptor = ::openat(
                retained.descriptor,
                temporary_filename.c_str(),
                open_flags,
                S_IRUSR | S_IWUSR);
            if (descriptor >= 0) {
                break;
            }
            if (errno != EEXIST) {
                throw ProjectStoreError(
                    "Could not create temporary project file: " +
                    std::error_code(
                        errno,
                        std::generic_category())
                        .message());
            }
        }
        if (descriptor < 0) {
            throw ProjectStoreError(
                "Could not allocate a unique temporary project file");
        }
        temporary_exists = true;
        try {
            write_all(descriptor, bytes);
            fsync_file(descriptor);
        }
        catch (...) {
            ::close(descriptor);
            throw;
        }
        if (::close(descriptor) != 0) {
            throw ProjectStoreError(
                "Could not close temporary project file");
        }

        verify_store_root(
            root,
            retained,
            retained.identity_high,
            retained.identity_low);
        if (replace_existing) {
            if (::renameat(
                    retained.descriptor,
                    temporary_filename.c_str(),
                    retained.descriptor,
                    destination_filename.c_str()) != 0) {
                throw ProjectStoreError(
                    "Could not atomically replace project file: " +
                    std::error_code(
                        errno,
                        std::generic_category())
                        .message());
            }
            temporary_exists = false;
        }
        else {
            if (::linkat(
                    retained.descriptor,
                    temporary_filename.c_str(),
                    retained.descriptor,
                    destination_filename.c_str(),
                    0) != 0) {
                if (errno == EEXIST) {
                    throw ProjectFileConflict(
                        "Project file already exists");
                }
                throw ProjectStoreError(
                    "Could not atomically publish new project file");
            }
            // linkat is the no-overwrite commit point. The additional name is
            // invisible to project listing and cleanup is best effort.
            if (::unlinkat(
                    retained.descriptor,
                    temporary_filename.c_str(),
                    0) == 0) {
                temporary_exists = false;
            }
        }
        // A successful inode fsync is insufficient: create/link and rename
        // durability depend on the directory entry. Report failure after the
        // publish point explicitly instead of silently claiming a durable
        // save.
        fsync_published_directory(retained);
        try {
            verify_store_root(
                root,
                retained,
                retained.identity_high,
                retained.identity_low);
        }
        catch (const ProjectStoreError& error) {
            throw ProjectDurabilityError(
                "Project file was atomically published and synced, but "
                "the configured project directory changed at the publish "
                "boundary: " +
                std::string(error.what()) +
                ". Reload project storage before retrying.");
        }
#endif
        temporary_exists = false;
    }
    catch (...) {
        if (temporary_exists) {
#ifdef _WIN32
            std::error_code ignored;
            std::filesystem::remove(
                root / temporary_filename,
                ignored);
#else
            (void)::unlinkat(
                retained.descriptor,
                temporary_filename.c_str(),
                0);
#endif
        }
        throw;
    }
}

std::string project_id_from_filename(
    const std::filesystem::path& path) {
    const auto filename = path.filename().string();
    if (filename.size() <= kFileSuffix.size() ||
        !filename.ends_with(kFileSuffix)) {
        return {};
    }
    return filename.substr(
        0,
        filename.size() - kFileSuffix.size());
}

std::vector<std::filesystem::path> project_entry_names(
    const detail::ProjectStoreRootHandle& retained,
    const std::filesystem::path& root) {
    std::vector<std::filesystem::path> names;
#ifdef _WIN32
    (void)retained;
    std::error_code iteration_error;
    for (std::filesystem::directory_iterator iterator(
             root,
             std::filesystem::directory_options::skip_permission_denied,
             iteration_error),
         end;
         !iteration_error && iterator != end;
         iterator.increment(iteration_error)) {
        names.push_back(iterator->path().filename());
    }
    if (iteration_error) {
        throw ProjectStoreError(
            "Could not list project storage directory: " +
            iteration_error.message());
    }
#else
    (void)root;
    const auto duplicate = ::dup(retained.descriptor);
    if (duplicate < 0) {
        throw ProjectStoreError(
            "Could not duplicate project directory descriptor for "
            "listing: " +
            std::error_code(
                errno,
                std::generic_category())
                .message());
    }
    auto* directory = ::fdopendir(duplicate);
    if (directory == nullptr) {
        const auto error = errno;
        ::close(duplicate);
        throw ProjectStoreError(
            "Could not enumerate project storage directory: " +
            std::error_code(
                error,
                std::generic_category())
                .message());
    }
    errno = 0;
    while (const auto* entry = ::readdir(directory)) {
        names.emplace_back(entry->d_name);
        errno = 0;
    }
    const auto iteration_error = errno;
    ::closedir(directory);
    if (iteration_error != 0) {
        throw ProjectStoreError(
            "Could not list project storage directory: " +
            std::error_code(
                iteration_error,
                std::generic_category())
                .message());
    }
#endif
    return names;
}

} // namespace

std::string project_file_envelope_to_json(
    const ProjectFileEnvelope& envelope,
    bool pretty) {
    if (envelope.file_format_version !=
        kProjectFileFormatVersion) {
        throw UnsupportedProjectFile(
            "Unsupported project file format version " +
            std::to_string(envelope.file_format_version));
    }
    if (envelope.canonicalization_version !=
        kProjectCanonicalizationVersion) {
        throw UnsupportedProjectFile(
            "Unsupported project canonicalization version " +
            std::to_string(envelope.canonicalization_version));
    }
    const auto expected_hash =
        project_content_hash(envelope.project);
    if (envelope.content_hash != expected_hash) {
        throw CorruptProjectFile(
            "Project envelope contentHash does not match project content");
    }
    if (envelope.authority_revision <
        envelope.saved_revision) {
        throw CorruptProjectFile(
            "Project authorityRevision cannot precede savedRevision");
    }
    if (envelope.saved_at_unix_ms < 0) {
        throw CorruptProjectFile(
            "Project savedAtUnixMs cannot be negative");
    }

    Json root = {
        {"authorityRevision", envelope.authority_revision},
        {
            "canonicalizationVersion",
            envelope.canonicalization_version,
        },
        {"contentHash", envelope.content_hash},
        {"fileFormat", kFileFormat},
        {"fileFormatVersion", envelope.file_format_version},
        {
            "project",
            Json::parse(project_document_to_canonical_json(
                envelope.project)),
        },
        {"savedAtUnixMs", envelope.saved_at_unix_ms},
        {"savedRevision", envelope.saved_revision},
    };
    return root.dump(pretty ? 2 : -1);
}

namespace {

struct ParsedProjectFileEnvelope {
    ProjectFileEnvelope envelope;
    ProjectMigrationReport migration;
};

ParsedProjectFileEnvelope parse_project_file_envelope(
    std::string_view text) {
    try {
        auto root = detail::parse_strict_json(
            text,
            "Project file envelope",
            detail::kMaximumProjectJsonBytes,
            detail::kMaximumJsonDepth);
        require_exact_fields(
            root,
            {
                "fileFormat",
                "fileFormatVersion",
                "canonicalizationVersion",
                "authorityRevision",
                "savedRevision",
                "contentHash",
                "savedAtUnixMs",
                "project",
            },
            "Project file envelope");

        const auto file_format =
            required_string(
                root,
                "fileFormat",
                "Project file envelope");
        if (file_format != kFileFormat) {
            throw UnsupportedProjectFile(
                "Unsupported project file format '" +
                file_format + "'");
        }
        const auto file_version =
            unsigned_integer<std::uint32_t>(
                root,
                "fileFormatVersion",
                "Project file envelope");
        if (file_version != kProjectFileFormatVersion) {
            throw UnsupportedProjectFile(
                "Unsupported project file format version " +
                std::to_string(file_version));
        }
        const auto canonicalization_version =
            unsigned_integer<std::uint32_t>(
                root,
                "canonicalizationVersion",
                "Project file envelope");
        if (canonicalization_version !=
            kProjectCanonicalizationVersion) {
            throw UnsupportedProjectFile(
                "Unsupported project canonicalization version " +
                std::to_string(canonicalization_version));
        }
        if (!root.at("savedAtUnixMs").is_number_integer() ||
            root.at("savedAtUnixMs").get<std::int64_t>() < 0) {
            corrupt(
                "Project file envelope.savedAtUnixMs must be a "
                "non-negative integer");
        }
        if (!root.at("project").is_object()) {
            corrupt(
                "Project file envelope.project must be an object");
        }

        ProjectFileEnvelope envelope;
        envelope.file_format_version = file_version;
        envelope.canonicalization_version =
            canonicalization_version;
        envelope.authority_revision =
            unsigned_integer<std::uint64_t>(
                root,
                "authorityRevision",
                "Project file envelope");
        envelope.saved_revision =
            unsigned_integer<std::uint64_t>(
                root,
                "savedRevision",
                "Project file envelope");
        const auto stored_content_hash =
            required_string(
                root,
                "contentHash",
                "Project file envelope");
        envelope.saved_at_unix_ms =
            root.at("savedAtUnixMs").get<std::int64_t>();
        auto migrated =
            migrate_persisted_project_document_from_json(
                root.at("project").dump());
        if (stored_content_hash !=
            migrated.source_content_hash) {
            throw CorruptProjectFile(
                "Project envelope contentHash does not match "
                "the persisted project content");
        }
        envelope.project = std::move(migrated.project);
        envelope.content_hash =
            project_content_hash(envelope.project);

        const auto normalized =
            project_file_envelope_to_json(envelope, false);
        (void)normalized;
        return {
            std::move(envelope),
            std::move(migrated.report),
        };
    }
    catch (const UnsupportedProjectSchema& error) {
        throw UnsupportedProjectFile(error.what());
    }
    catch (const UnsupportedProjectFile&) {
        throw;
    }
    catch (const CorruptProjectFile&) {
        throw;
    }
    catch (const ProjectJsonError& error) {
        throw CorruptProjectFile(error.what());
    }
    catch (const Json::exception& error) {
        throw CorruptProjectFile(error.what());
    }
}

} // namespace

ProjectFileEnvelope project_file_envelope_from_json(
    std::string_view text) {
    return parse_project_file_envelope(text).envelope;
}

ProjectFileFingerprint project_file_fingerprint(
    const ProjectFileEnvelope& envelope) {
    const auto normalized_envelope =
        project_file_envelope_to_json(
            envelope,
            false);
    return {
        envelope.authority_revision,
        envelope.saved_revision,
        envelope.content_hash,
        "sha256:" +
            detail::sha256_hex_digest(
                normalized_envelope),
    };
}

ProjectStore::ProjectStore(std::filesystem::path root) {
    if (root.empty()) {
        throw ProjectStoreError(
            "Project storage directory must not be empty");
    }
    std::error_code error;
    auto absolute = std::filesystem::absolute(root, error);
    if (error) {
        throw ProjectStoreError(
            "Could not resolve project storage directory: " +
            error.message());
    }
    std::filesystem::create_directories(absolute, error);
    if (error) {
        throw ProjectStoreError(
            "Could not create project storage directory: " +
            error.message());
    }
    if (!std::filesystem::is_directory(absolute, error) ||
        error) {
        throw ProjectStoreError(
            "Project storage path is not a directory");
    }
    root_ = std::filesystem::canonical(absolute, error);
    if (error) {
        throw ProjectStoreError(
            "Could not canonicalize project storage directory: " +
            error.message());
    }
    root_handle_ =
        std::make_shared<detail::ProjectStoreRootHandle>(
            root_);
    root_identity_high_ =
        root_handle_->identity_high;
    root_identity_low_ =
        root_handle_->identity_low;
    verify_store_root(
        root_,
        *root_handle_,
        root_identity_high_,
        root_identity_low_);
}

const std::filesystem::path& ProjectStore::root() const noexcept {
    return root_;
}

std::filesystem::path ProjectStore::path_for(
    const std::string& project_id) const {
    verify_store_root(
        root_,
        *root_handle_,
        root_identity_high_,
        root_identity_low_);
    if (!is_uuid_v4(project_id)) {
        throw ProjectStoreError(
            "Project ID must be a lowercase UUIDv4");
    }
    return root_ /
        (project_id + std::string(kFileSuffix));
}

bool ProjectStore::exists(
    const std::string& project_id) const {
    (void)path_for(project_id);
    const auto present =
        relative_project_exists(
            *root_handle_,
            root_,
            project_filename(project_id));
    verify_store_root(
        root_,
        *root_handle_,
        root_identity_high_,
        root_identity_low_);
    return present;
}

LoadedProjectFile ProjectStore::load(
    const std::string& project_id) const {
    (void)path_for(project_id);
    auto parsed =
        parse_project_file_envelope(
            read_bounded_file(
                *root_handle_,
                root_,
                project_filename(project_id)));
    verify_store_root(
        root_,
        *root_handle_,
        root_identity_high_,
        root_identity_low_);
    if (parsed.envelope.project.project_id != project_id) {
        throw CorruptProjectFile(
            "Project ID does not match its UUID filename");
    }
    auto fingerprint =
        project_file_fingerprint(parsed.envelope);
    return {
        std::move(parsed.envelope),
        std::move(fingerprint),
        std::move(parsed.migration),
    };
}

std::vector<SavedProjectSummary> ProjectStore::list() const {
    verify_store_root(
        root_,
        *root_handle_,
        root_identity_high_,
        root_identity_low_);
    std::vector<SavedProjectSummary> result;
    for (const auto& entry :
         project_entry_names(
             *root_handle_,
             root_)) {
        const auto id = project_id_from_filename(
            entry);
        if (!is_uuid_v4(id)) {
            continue;
        }
        SavedProjectSummary summary;
        summary.project_id = id;
        try {
            const auto loaded = load(id);
            summary.name =
                loaded.envelope.project.metadata.name;
            summary.description =
                loaded.envelope.project.metadata.description;
            summary.saved_revision =
                loaded.envelope.saved_revision;
            summary.saved_at_unix_ms =
                loaded.envelope.saved_at_unix_ms;
        }
        catch (const UnsupportedProjectFile& error) {
            summary.status = SavedProjectStatus::Unsupported;
            summary.issue = error.what();
        }
        catch (const CorruptProjectFile& error) {
            summary.status = SavedProjectStatus::Corrupt;
            summary.issue = error.what();
        }
        result.push_back(std::move(summary));
    }
    verify_store_root(
        root_,
        *root_handle_,
        root_identity_high_,
        root_identity_low_);
    std::sort(
        result.begin(),
        result.end(),
        [](const auto& left, const auto& right) {
            return left.project_id < right.project_id;
        });
    return result;
}

ProjectFileFingerprint ProjectStore::create(
    const ProjectFileEnvelope& envelope) {
    (void)path_for(envelope.project.project_id);
    const auto filename =
        project_filename(
            envelope.project.project_id);
    ProjectWriteLock lock(
        *root_handle_,
        root_,
        filename);
    if (relative_project_exists(
            *root_handle_,
            root_,
            filename)) {
        throw ProjectFileConflict(
            "Project file already exists");
    }
    const auto bytes =
        project_file_envelope_to_json(envelope, false);
    if (bytes.size() > detail::kMaximumProjectJsonBytes) {
        throw ProjectStoreError(
            "Serialized project file exceeds " +
            std::to_string(detail::kMaximumProjectJsonBytes) +
            " bytes");
    }
    verify_store_root(
        root_,
        *root_handle_,
        root_identity_high_,
        root_identity_low_);
    atomic_write(
        *root_handle_,
        root_,
        filename,
        bytes,
        false);
    const auto expected =
        project_file_fingerprint(envelope);
    const auto observed =
        load(envelope.project.project_id);
    if (observed.fingerprint != expected ||
        observed.envelope != envelope) {
        throw ProjectFileConflict(
            "Project file changed immediately after it was created");
    }
    return observed.fingerprint;
}

ProjectFileFingerprint ProjectStore::replace(
    const ProjectFileEnvelope& envelope,
    const ProjectFileFingerprint& expected) {
    (void)path_for(envelope.project.project_id);
    const auto filename =
        project_filename(
            envelope.project.project_id);
    ProjectWriteLock lock(
        *root_handle_,
        root_,
        filename);
    const auto current = load(envelope.project.project_id);
    if (current.fingerprint != expected) {
        throw ProjectFileConflict(
            "Saved project changed since it was loaded");
    }
    const auto bytes =
        project_file_envelope_to_json(envelope, false);
    if (bytes.size() > detail::kMaximumProjectJsonBytes) {
        throw ProjectStoreError(
            "Serialized project file exceeds " +
            std::to_string(detail::kMaximumProjectJsonBytes) +
            " bytes");
    }
    verify_store_root(
        root_,
        *root_handle_,
        root_identity_high_,
        root_identity_low_);
    // Re-read immediately before publication. This does not turn an
    // uncooperative writer into a participant in our lock protocol, but it
    // narrows the unavoidable portable check/rename window and catches all
    // changes made while serialization was in progress.
    const auto precommit =
        load(envelope.project.project_id);
    if (precommit.fingerprint != expected) {
        throw ProjectFileConflict(
            "Saved project changed while the replacement was prepared");
    }
    atomic_write(
        *root_handle_,
        root_,
        filename,
        bytes,
        true);
    const auto replacement =
        project_file_fingerprint(envelope);
    const auto observed =
        load(envelope.project.project_id);
    if (observed.fingerprint != replacement ||
        observed.envelope != envelope) {
        throw ProjectFileConflict(
            "Project file changed immediately after it was replaced");
    }
    return observed.fingerprint;
}

} // namespace pamguard::project
