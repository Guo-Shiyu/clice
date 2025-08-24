
#include "Support/GlobPattern.h"
#include "llvm/ADT/DenseMap.h"
#include <memory>
#include "uv.h"

// struct uv_fs_event_t;

namespace clice {

struct FsEvent {
    const char* filename;
};

using WatcherCallback = void (*)(int status);

struct FsEventHook {};

/// If the client doesn't support file watching, the server can use this
/// listener to track file changes.
class ServerSideListener {
    /// https://docs.libuv.org/en/v1.x/guide/filesystem.html#file-change-events
};

class WatchedFileManager {
public:
    WatchedFileManager() = default;

    WatchedFileManager(const WatchedFileManager&) = delete;
    WatchedFileManager& operator= (const WatchedFileManager&) = delete;

    /// Add a file to the watch list.
    void add_watch(llvm::StringRef path) {
        // watched_files.insert(path);
    }

    /// Remove a file from the watch list.
    void remove_watch(llvm::StringRef path) {
        // watched_files.erase(path);
    }

    /// Check if a file is being watched.
    [[nodiscard]] bool is_watched(llvm::StringRef path) const {
        // return watched_files.contains(path);
    }

    void enable_server_side_listen() {
        /// TODO:
    };

private:
    llvm::DenseMap<GlobPattern, FsEventHook> callbacks;

    /// Non-null if client does not supoort file watching.
    std::unique_ptr<ServerSideListener> listener;
};

}  // namespace clice
