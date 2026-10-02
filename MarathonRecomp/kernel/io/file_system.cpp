#include "file_system.h"
#include <cpu/guest_thread.h>
#include <cstdio>
#include <mutex>
#include <kernel/xam.h>
#include <kernel/xdm.h>
#include <kernel/function.h>
#include <mod/mod_loader.h>
#include <os/logger.h>
#include <user/config.h>
#include <user/paths.h>
#include <stdafx.h>

#if defined(__SWITCH__) && defined(__GLIBCXX__)
#include <sys/stat.h>
#define SWITCH_FEWER_FILE_QUERIES 1

// [Switch] SwitchFewerFileQueries (set by SwitchPerfInitKernel before any guest code runs). XGetFileSizeA took the
// size of an open file from its path (std::filesystem::file_size: a stat, which on the SD card is an entry-type
// query, an open, a size query, a close and a time-stamp query, each a file-system IPC, two of them walking the
// path), and the loader asks it for every file it reads whole. A read-only handle opened from its own path now
// answers from its open descriptor (fstat: one size query). The same file, so the same size: the game has no way
// to delete or rename a file (NtSetInformationFile is a stub), so while the handle is open its path names the file
// it holds, and a write through another handle changes that same file. Anything else, or an fstat that fails,
// takes the path as before.
bool g_fewerFileQueries = false;

// The descriptor under a libstdc++ filebuf: its __basic_file member is protected, and a pointer to it formed
// through a derived class is the standard way to name it.
struct FilebufDescriptor : std::filebuf
{
    static int Get(std::filebuf& buffer)
    {
        return (buffer.*(&FilebufDescriptor::_M_file)).fd();
    }
};
#endif

struct FileHandle : KernelObject
{
    std::fstream stream;
    std::filesystem::path path;
    // The guest streams assets from a small set of archive handles across several
    // loader threads at once. std::fstream is not thread-safe, so concurrent
    // seek+read on one handle corrupts the filebuf's internal pointers and makes
    // the underlying read write to a wild address. Serialize per handle.
    std::mutex mutex;
#if defined(SWITCH_FEWER_FILE_QUERIES)
    // The open descriptor of a read-only handle opened from `path` itself (SwitchFewerFileQueries), else -1.
    int readOnlyFd = -1;
#endif
};

struct FindHandle : KernelObject
{
    std::error_code ec;
    ankerl::unordered_dense::map<std::u8string, std::pair<size_t, bool>> searchResult; // Relative path, file size, is directory
    decltype(searchResult)::iterator iterator;

    FindHandle(const std::string_view& path)
    {
        auto addDirectory = [&](const std::filesystem::path& directory)
            {
                for (auto& entry : std::filesystem::directory_iterator(directory, ec))
                {
                    std::u8string relativePath = entry.path().lexically_relative(directory).u8string();
                    searchResult.emplace(relativePath, std::make_pair(entry.is_directory(ec) ? 0 : entry.file_size(ec), entry.is_directory(ec)));
                }
            };

        std::string_view pathNoPrefix = path;
        size_t index = pathNoPrefix.find(":\\");
        if (index != std::string_view::npos)
            pathNoPrefix.remove_prefix(index + 2);

        // Force add a work folder to let the game see the files in mods,
        // if by some rare chance the user has no DLC or update files.
        if (pathNoPrefix.empty())
            searchResult.emplace(u8"work", std::make_pair(0, true));

        // Look for only work folder in mod folders, AR files cause issues.
        if (pathNoPrefix.starts_with("work"))
        {
            std::string pathStr(pathNoPrefix);
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

            for (size_t i = 0; ; i++)
            {
                auto* includeDirs = ModLoader::GetIncludeDirectories(i);
                if (includeDirs == nullptr)
                    break;

                for (auto& includeDir : *includeDirs)
                    addDirectory(includeDir / pathStr);
            }
        }

        addDirectory(FileSystem::ResolvePath(path, false));

        iterator = searchResult.begin();
    }

    void fillFindData(WIN32_FIND_DATAA* lpFindFileData)
    {
        if (iterator->second.second)
            lpFindFileData->dwFileAttributes = ByteSwap(FILE_ATTRIBUTE_DIRECTORY);
        else
            lpFindFileData->dwFileAttributes = ByteSwap(FILE_ATTRIBUTE_NORMAL);

        strncpy(lpFindFileData->cFileName, (const char *)(iterator->first.c_str()), sizeof(lpFindFileData->cFileName));
        lpFindFileData->nFileSizeLow = ByteSwap(uint32_t(iterator->second.first >> 32U));
        lpFindFileData->nFileSizeHigh = ByteSwap(uint32_t(iterator->second.first));
        lpFindFileData->ftCreationTime = {};
        lpFindFileData->ftLastAccessTime = {};
        lpFindFileData->ftLastWriteTime = {};
    }
};

FileHandle* XCreateFileA
(
    const char* lpFileName,
    uint32_t dwDesiredAccess,
    uint32_t dwShareMode,
    void* lpSecurityAttributes,
    uint32_t dwCreationDisposition,
    uint32_t dwFlagsAndAttributes
)
{
    assert(((dwDesiredAccess & ~(GENERIC_READ | GENERIC_WRITE | FILE_READ_DATA)) == 0) && "Unknown desired access bits.");
    assert(((dwShareMode & ~(FILE_SHARE_READ | FILE_SHARE_WRITE)) == 0) && "Unknown share mode bits.");
    assert(((dwCreationDisposition & ~(CREATE_NEW | CREATE_ALWAYS)) == 0) && "Unknown creation disposition bits.");

    std::filesystem::path filePath = FileSystem::ResolvePath(lpFileName, true);
    std::fstream fileStream;
    std::ios::openmode fileOpenMode = std::ios::binary;
    if (dwDesiredAccess & (GENERIC_READ | FILE_READ_DATA))
    {
        fileOpenMode |= std::ios::in;
    }

    if (dwDesiredAccess & GENERIC_WRITE)
    {
        fileOpenMode |= std::ios::out;
    }

    fileStream.open(filePath, fileOpenMode);
#if defined(SWITCH_FEWER_FILE_QUERIES)
    const bool openedFromPath = fileStream.is_open();
#endif

    if (!fileStream.is_open()) {
        std::filesystem::path cachedPath = FindInPathCache(filePath.string());
        if (!cachedPath.empty()) {
            fileStream.open(cachedPath, fileOpenMode);
        }
    }

    if (!fileStream.is_open())
    {
#ifdef _WIN32
        GuestThread::SetLastError(GetLastError());
#else
        switch (errno)
        {
        case EACCES:
            GuestThread::SetLastError(ERROR_ACCESS_DENIED);
            break;
        case EEXIST:
            GuestThread::SetLastError(ERROR_FILE_EXISTS);
            break;
        case ENOENT:
        default: // Use ERROR_PATH_NOT_FOUND as a catch-all for other errors.
            GuestThread::SetLastError(ERROR_PATH_NOT_FOUND);
            break;
        }
#endif
        return GetInvalidKernelObject<FileHandle>();
    }

    FileHandle *fileHandle = CreateKernelObject<FileHandle>();
    fileHandle->stream = std::move(fileStream);
    fileHandle->path = std::move(filePath);
#if defined(SWITCH_FEWER_FILE_QUERIES)
    if (g_fewerFileQueries && openedFromPath && (dwDesiredAccess & GENERIC_WRITE) == 0)
        fileHandle->readOnlyFd = FilebufDescriptor::Get(*fileHandle->stream.rdbuf());
#endif
    return fileHandle;
}

#if defined(SWITCH_FEWER_FILE_QUERIES)
// What std::filesystem::file_size(hFile->path) returns, from the open descriptor; false to take the path instead.
static bool TryGetOpenFileSize(FileHandle* hFile, uint64_t& fileSize)
{
    struct stat st;
    if (hFile->readOnlyFd < 0 || fstat(hFile->readOnlyFd, &st) != 0 || !S_ISREG(st.st_mode))
        return false;

    fileSize = uint64_t(st.st_size);
    return true;
}
#endif

static uint32_t XGetFileSizeA(FileHandle* hFile, be<uint32_t>* lpFileSizeHigh)
{
#if defined(SWITCH_FEWER_FILE_QUERIES)
    uint64_t openFileSize;
    if (TryGetOpenFileSize(hFile, openFileSize))
    {
        if (lpFileSizeHigh != nullptr)
            *lpFileSizeHigh = uint32_t(openFileSize >> 32U);

        return (uint32_t)(openFileSize);
    }
#endif

    std::error_code ec;
    auto fileSize = std::filesystem::file_size(hFile->path, ec);
    if (!ec)
    {
        if (lpFileSizeHigh != nullptr)
        {
            *lpFileSizeHigh = uint32_t(fileSize >> 32U);
        }
    
        return (uint32_t)(fileSize);
    }

    return INVALID_FILE_SIZE;
}

uint32_t XGetFileSizeExA(FileHandle* hFile, LARGE_INTEGER* lpFileSize)
{
#if defined(SWITCH_FEWER_FILE_QUERIES)
    uint64_t openFileSize;
    if (TryGetOpenFileSize(hFile, openFileSize))
    {
        if (lpFileSize != nullptr)
            lpFileSize->QuadPart = ByteSwap(openFileSize);

        return TRUE;
    }
#endif

    std::error_code ec;
    auto fileSize = std::filesystem::file_size(hFile->path, ec);
    if (!ec)
    {
        if (lpFileSize != nullptr)
        {
            lpFileSize->QuadPart = ByteSwap(fileSize);
        }

        return TRUE;
    }

    return FALSE;
}

uint32_t XReadFile
(
    FileHandle* hFile,
    void* lpBuffer,
    uint32_t nNumberOfBytesToRead,
    be<uint32_t>* lpNumberOfBytesRead,
    XOVERLAPPED* lpOverlapped
)
{
    uint32_t result = FALSE;
    std::lock_guard<std::mutex> lock(hFile->mutex);
    if (lpOverlapped != nullptr)
    {
        std::streamoff streamOffset = lpOverlapped->Offset + (std::streamoff(lpOverlapped->OffsetHigh.get()) << 32U);
        hFile->stream.clear();
        hFile->stream.seekg(streamOffset, std::ios::beg);
        if (hFile->stream.bad())
        {
            return FALSE;
        }
    }

    uint32_t numberOfBytesRead;
    hFile->stream.read((char *)(lpBuffer), nNumberOfBytesToRead);
    if (!hFile->stream.bad())
    {
        numberOfBytesRead = uint32_t(hFile->stream.gcount());
        result = TRUE;
    }

    if (result)
    {
        if (lpOverlapped != nullptr)
        {
            lpOverlapped->Internal = 0;
            lpOverlapped->InternalHigh = numberOfBytesRead;
        }
        else if (lpNumberOfBytesRead != nullptr)
        {
            *lpNumberOfBytesRead = numberOfBytesRead;
        }
    }

    return result;
}

uint32_t XSetFilePointer(FileHandle* hFile, int32_t lDistanceToMove, be<int32_t>* lpDistanceToMoveHigh, uint32_t dwMoveMethod)
{
    int32_t distanceToMoveHigh = lpDistanceToMoveHigh ? lpDistanceToMoveHigh->get() : 0;
    std::streamoff streamOffset = lDistanceToMove + (std::streamoff(distanceToMoveHigh) << 32U);
    std::fstream::seekdir streamSeekDir = {};
    switch (dwMoveMethod)
    {
    case FILE_BEGIN:
        streamSeekDir = std::ios::beg;
        break;
    case FILE_CURRENT:
        streamSeekDir = std::ios::cur;
        break;
    case FILE_END:
        streamSeekDir = std::ios::end;
        break;
    default:
        assert(false && "Unknown move method.");
        break;
    }

    std::lock_guard<std::mutex> lock(hFile->mutex);
    hFile->stream.clear();
    hFile->stream.seekg(streamOffset, streamSeekDir);
    if (hFile->stream.bad())
    {
        return INVALID_SET_FILE_POINTER;
    }

    std::streampos streamPos = hFile->stream.tellg();
    if (lpDistanceToMoveHigh != nullptr)
        *lpDistanceToMoveHigh = int32_t(streamPos >> 32U);

    return uint32_t(streamPos);
}

uint32_t XSetFilePointerEx(FileHandle* hFile, int32_t lDistanceToMove, LARGE_INTEGER* lpNewFilePointer, uint32_t dwMoveMethod)
{
    std::fstream::seekdir streamSeekDir = {};
    switch (dwMoveMethod)
    {
    case FILE_BEGIN:
        streamSeekDir = std::ios::beg;
        break;
    case FILE_CURRENT:
        streamSeekDir = std::ios::cur;
        break;
    case FILE_END:
        streamSeekDir = std::ios::end;
        break;
    default:
        assert(false && "Unknown move method.");
        break;
    }

    std::lock_guard<std::mutex> lock(hFile->mutex);
    hFile->stream.clear();
    hFile->stream.seekg(lDistanceToMove, streamSeekDir);
    if (hFile->stream.bad())
    {
        return FALSE;
    }

    if (lpNewFilePointer != nullptr)
    {
        lpNewFilePointer->QuadPart = ByteSwap(int64_t(hFile->stream.tellg()));
    }

    return TRUE;
}

FindHandle* XFindFirstFileA(const char* lpFileName, WIN32_FIND_DATAA* lpFindFileData)
{
    std::string_view path = lpFileName;
    if (path.find("\\*") == (path.size() - 2) || path.find("/*") == (path.size() - 2))
    {
        path.remove_suffix(1);
    }
    else if (path.find("\\*.*") == (path.size() - 4) || path.find("/*.*") == (path.size() - 4))
    {
        path.remove_suffix(3);
    }
    else
    {
        assert(!std::filesystem::path(path).has_extension() && "Unknown search pattern.");
    }

    FindHandle findHandle(path);

    if (findHandle.searchResult.empty())
        return GetInvalidKernelObject<FindHandle>();

    findHandle.fillFindData(lpFindFileData);

    return CreateKernelObject<FindHandle>(std::move(findHandle));
}

uint32_t XFindNextFileA(FindHandle* Handle, WIN32_FIND_DATAA* lpFindFileData)
{
    Handle->iterator++;

    if (Handle->iterator == Handle->searchResult.end())
    {
        return FALSE;
    }
    else
    {
        Handle->fillFindData(lpFindFileData);
        return TRUE;
    }
}

uint32_t XReadFileEx(FileHandle* hFile, void* lpBuffer, uint32_t nNumberOfBytesToRead, XOVERLAPPED* lpOverlapped, uint32_t lpCompletionRoutine)
{
    uint32_t result = FALSE;
    uint32_t numberOfBytesRead;
    std::lock_guard<std::mutex> lock(hFile->mutex);
    std::streamoff streamOffset = lpOverlapped->Offset + (std::streamoff(lpOverlapped->OffsetHigh.get()) << 32U);
    hFile->stream.clear();
    hFile->stream.seekg(streamOffset, std::ios::beg);
    if (hFile->stream.bad())
        return FALSE;

    hFile->stream.read((char *)(lpBuffer), nNumberOfBytesToRead);
    if (!hFile->stream.bad())
    {
        numberOfBytesRead = uint32_t(hFile->stream.gcount());
        result = TRUE;
    }

    if (result)
    {
        lpOverlapped->Internal = 0;
        lpOverlapped->InternalHigh = numberOfBytesRead;
    }

    return result;
}

uint32_t XGetFileAttributesA(const char* lpFileName)
{
    std::filesystem::path filePath = FileSystem::ResolvePath(lpFileName, true);
#if defined(SWITCH_FEWER_FILE_QUERIES)
    if (g_fewerFileQueries)
    {
        // One stat for both questions: is_directory(path) and is_regular_file(path) are each a status(path).
        const std::filesystem::file_status status = std::filesystem::status(filePath);
        if (std::filesystem::is_directory(status))
            return FILE_ATTRIBUTE_DIRECTORY;
        else if (std::filesystem::is_regular_file(status))
            return FILE_ATTRIBUTE_NORMAL;
        else
            return INVALID_FILE_ATTRIBUTES;
    }
#endif
    if (std::filesystem::is_directory(filePath))
        return FILE_ATTRIBUTE_DIRECTORY;
    else if (std::filesystem::is_regular_file(filePath))
        return FILE_ATTRIBUTE_NORMAL;
    else
        return INVALID_FILE_ATTRIBUTES;
}

uint32_t XWriteFile(FileHandle* hFile, const void* lpBuffer, uint32_t nNumberOfBytesToWrite, be<uint32_t>* lpNumberOfBytesWritten, void* lpOverlapped)
{
    assert(lpOverlapped == nullptr && "Overlapped not implemented.");

    std::lock_guard<std::mutex> lock(hFile->mutex);
    hFile->stream.write((const char *)(lpBuffer), nNumberOfBytesToWrite);
    if (hFile->stream.bad())
        return FALSE;

    if (lpNumberOfBytesWritten != nullptr)
        *lpNumberOfBytesWritten = uint32_t(hFile->stream.gcount());

    return TRUE;
}

std::filesystem::path FileSystem::ResolvePath(const std::string_view& path, bool checkForMods)
{
    LOGF_IMPL(Utility, "Game", "Loading file: \"{}\"", path.data());
    if (checkForMods)
    {
        std::filesystem::path resolvedPath = ModLoader::ResolvePath(path);

        if (!resolvedPath.empty())
        {
            if (ModLoader::s_isLogTypeConsole)
                LOGF_IMPL(Utility, "Mod Loader", "Loading file: \"{}\"", reinterpret_cast<const char*>(resolvedPath.u8string().c_str()));

            return resolvedPath;
        }
    }

    thread_local std::string builtPath;
    builtPath.clear();

    size_t index = path.find(":\\");
    if (index != std::string::npos)
    {
        // rooted folder, handle direction
        const std::string_view root = path.substr(0, index);
        const auto newRoot = XamGetRootPath(root);

        if (!newRoot.empty())
        {
            builtPath += newRoot;
            builtPath += '/';
        }
        
        builtPath += path.substr(index + 2);
    }
    else
    {
        builtPath += path;
    }

    std::replace(builtPath.begin(), builtPath.end(), '\\', '/');

    return std::u8string_view((const char8_t*)builtPath.c_str());
}

GUEST_FUNCTION_HOOK(sub_82537400, XCreateFileA); // replaced
GUEST_FUNCTION_HOOK(sub_826FD090, XGetFileSizeA); // replaced
GUEST_FUNCTION_HOOK(sub_826FDC88, XGetFileSizeExA); // replaced
GUEST_FUNCTION_HOOK(sub_82537118, XReadFile); // replaced
GUEST_FUNCTION_HOOK(sub_825372B8, XSetFilePointer); // replaced
// GUEST_FUNCTION_HOOK(sub_831CE888, XSetFilePointerEx);
GUEST_FUNCTION_HOOK(sub_826F2570, XFindFirstFileA); // replaced
GUEST_FUNCTION_HOOK(sub_826FD2B8, XFindNextFileA); // replaced
// GUEST_FUNCTION_HOOK(sub_831CDF40, XReadFileEx);
GUEST_FUNCTION_HOOK(sub_826FD250, XGetFileAttributesA); // replaced
// GUEST_FUNCTION_HOOK(sub_831CE3F8, XCreateFileA);
GUEST_FUNCTION_HOOK(sub_826FCBD0, XWriteFile); // replaced
