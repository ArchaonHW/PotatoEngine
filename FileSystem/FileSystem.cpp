#include "FileSystem.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// 全局文件系統指針
FileSystem* gFileSystem = nullptr;

// ============================================================================
// FileSystem 實現
// ============================================================================

FileSystem::FileSystem()
    : initialized(false)
{
}

FileSystem::~FileSystem() {
    Shutdown();
}

bool FileSystem::Initialize() {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (initialized) {
        return true;
    }
    
    // 添加默認搜索路徑
    std::string currentDir = GetCurrentDirectoryPlatform();
    AddSearchPath(currentDir);
    
    // 添加可執行文件目錄
    std::string exeDir = GetExecutablePath();
    if (!exeDir.empty()) {
        std::string exeDirectory = GetDirectoryName(exeDir);
        AddSearchPath(exeDirectory);
    }
    
    initialized = true;
    std::cout << "File System initialized" << std::endl;
    
    return true;
}

void FileSystem::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex);
    
    searchPaths.clear();
    initialized = false;
    
    std::cout << "File System shutdown complete" << std::endl;
}

bool FileSystem::FileExists(const std::string& path) const {
    std::string resolvedPath = ResolvePath(path);
    return FileExistsPlatform(resolvedPath);
}

bool FileSystem::DirectoryExists(const std::string& path) const {
    std::string resolvedPath = ResolvePath(path);
    return DirectoryExistsPlatform(resolvedPath);
}

bool FileSystem::ReadFile(const std::string& path, std::vector<uint8>& data) const {
    std::string resolvedPath = ResolvePath(path);
    
    std::ifstream file(resolvedPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << resolvedPath << std::endl;
        return false;
    }
    
    // 獲取文件大小
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // 讀取文件內容
    data.resize(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    
    return !file.fail();
}

bool FileSystem::ReadFileText(const std::string& path, std::string& text) const {
    std::string resolvedPath = ResolvePath(path);
    
    std::ifstream file(resolvedPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for reading: " << resolvedPath << std::endl;
        return false;
    }
    
    // 讀取文件內容
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    text = content;
    
    return !file.fail();
}

bool FileSystem::WriteFile(const std::string& path, const std::vector<uint8>& data) {
    std::string resolvedPath = ResolvePath(path);
    
    // 確保目錄存在
    std::string directory = GetDirectoryName(resolvedPath);
    if (!directory.empty() && !DirectoryExistsPlatform(directory)) {
        CreateDirectoryPlatform(directory);
    }
    
    std::ofstream file(resolvedPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << resolvedPath << std::endl;
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    return !file.fail();
}

bool FileSystem::WriteFileText(const std::string& path, const std::string& text) {
    std::string resolvedPath = ResolvePath(path);
    
    // 確保目錄存在
    std::string directory = GetDirectoryName(resolvedPath);
    if (!directory.empty() && !DirectoryExistsPlatform(directory)) {
        CreateDirectoryPlatform(directory);
    }
    
    std::ofstream file(resolvedPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for writing: " << resolvedPath << std::endl;
        return false;
    }
    
    file << text;
    
    return !file.fail();
}

bool FileSystem::CreateFile(const std::string& path) {
    std::string resolvedPath = ResolvePath(path);
    
    // 確保目錄存在
    std::string directory = GetDirectoryName(resolvedPath);
    if (!directory.empty() && !DirectoryExistsPlatform(directory)) {
        CreateDirectoryPlatform(directory);
    }
    
    std::ofstream file(resolvedPath);
    return file.is_open();
}

bool FileSystem::DeleteFile(const std::string& path) {
    std::string resolvedPath = ResolvePath(path);
    
    try {
        return fs::remove(resolvedPath);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to delete file: " << e.what() << std::endl;
        return false;
    }
}

bool FileSystem::CopyFile(const std::string& source, const std::string& destination) {
    std::string resolvedSource = ResolvePath(source);
    std::string resolvedDest = ResolvePath(destination);
    
    // 確保目標目錄存在
    std::string destDirectory = GetDirectoryName(resolvedDest);
    if (!destDirectory.empty() && !DirectoryExistsPlatform(destDirectory)) {
        CreateDirectoryPlatform(destDirectory);
    }
    
    try {
        fs::copy_file(resolvedSource, resolvedDest, fs::copy_options::overwrite_existing);
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to copy file: " << e.what() << std::endl;
        return false;
    }
}

bool FileSystem::MoveFile(const std::string& source, const std::string& destination) {
    std::string resolvedSource = ResolvePath(source);
    std::string resolvedDest = ResolvePath(destination);
    
    // 確保目標目錄存在
    std::string destDirectory = GetDirectoryName(resolvedDest);
    if (!destDirectory.empty() && !DirectoryExistsPlatform(destDirectory)) {
        CreateDirectoryPlatform(destDirectory);
    }
    
    try {
        fs::rename(resolvedSource, resolvedDest);
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to move file: " << e.what() << std::endl;
        return false;
    }
}

bool FileSystem::CreateDirectory(const std::string& path) {
    std::string resolvedPath = ResolvePath(path);
    return CreateDirectoryPlatform(resolvedPath);
}

bool FileSystem::DeleteDirectory(const std::string& path) {
    std::string resolvedPath = ResolvePath(path);
    return DeleteDirectoryPlatform(resolvedPath);
}

bool FileSystem::CopyDirectory(const std::string& source, const std::string& destination) {
    std::string resolvedSource = ResolvePath(source);
    std::string resolvedDest = ResolvePath(destination);
    
    try {
        fs::copy(resolvedSource, resolvedDest, 
                fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to copy directory: " << e.what() << std::endl;
        return false;
    }
}

std::vector<FileInfo> FileSystem::ListFiles(const std::string& path) const {
    return ListDirectoryPlatform(path, true, false);
}

std::vector<FileInfo> FileSystem::ListDirectories(const std::string& path) const {
    return ListDirectoryPlatform(path, false, true);
}

std::vector<FileInfo> FileSystem::ListAll(const std::string& path) const {
    return ListDirectoryPlatform(path, false, false);
}

std::string FileSystem::GetAbsolutePath(const std::string& path) const {
    try {
        return fs::absolute(path).string();
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to get absolute path: " << e.what() << std::endl;
        return path;
    }
}

std::string FileSystem::GetRelativePath(const std::string& path, const std::string& base) const {
    try {
        return fs::relative(path, base).string();
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to get relative path: " << e.what() << std::endl;
        return path;
    }
}

std::string FileSystem::GetFileName(const std::string& path) const {
    try {
        return fs::path(path).filename().string();
    } catch (const fs::filesystem_error& e) {
        return path;
    }
}

std::string FileSystem::GetFileExtension(const std::string& path) const {
    try {
        return fs::path(path).extension().string();
    } catch (const fs::filesystem_error& e) {
        return "";
    }
}

std::string FileSystem::GetDirectoryName(const std::string& path) const {
    try {
        return fs::path(path).parent_path().string();
    } catch (const fs::filesystem_error& e) {
        return "";
    }
}

FileInfo FileSystem::GetFileInfo(const std::string& path) const {
    std::string resolvedPath = ResolvePath(path);
    FileInfo info;
    
    try {
        fs::path filePath(resolvedPath);
        info.name = filePath.filename().string();
        info.path = resolvedPath;
        info.size = GetFileSizePlatform(resolvedPath);
        info.isDirectory = fs::is_directory(resolvedPath);
        info.isReadOnly = !(fs::status(resolvedPath).permissions() & fs::perms::owner_write);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to get file info: " << e.what() << std::endl;
    }
    
    return info;
}

uint64 FileSystem::GetFileSize(const std::string& path) const {
    std::string resolvedPath = ResolvePath(path);
    return GetFileSizePlatform(resolvedPath);
}

uint64 FileSystem::GetLastModifiedTime(const std::string& path) const {
    std::string resolvedPath = ResolvePath(path);
    return GetLastModifiedTimePlatform(resolvedPath);
}

std::string FileSystem::GetCurrentDirectory() const {
    return GetCurrentDirectoryPlatform();
}

bool FileSystem::SetCurrentDirectory(const std::string& path) {
    std::string resolvedPath = ResolvePath(path);
    return SetCurrentDirectoryPlatform(resolvedPath);
}

void FileSystem::AddSearchPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::string normalizedPath = NormalizePath(path);
    normalizedPath = RemoveTrailingSeparator(normalizedPath);
    
    // 檢查是否已存在
    if (std::find(searchPaths.begin(), searchPaths.end(), normalizedPath) == searchPaths.end()) {
        searchPaths.push_back(normalizedPath);
        std::cout << "Added search path: " << normalizedPath << std::endl;
    }
}

void FileSystem::RemoveSearchPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::string normalizedPath = NormalizePath(path);
    normalizedPath = RemoveTrailingSeparator(normalizedPath);
    
    auto it = std::remove(searchPaths.begin(), searchPaths.end(), normalizedPath);
    if (it != searchPaths.end()) {
        searchPaths.erase(it, searchPaths.end());
        std::cout << "Removed search path: " << normalizedPath << std::endl;
    }
}

std::vector<std::string> FileSystem::GetSearchPaths() const {
    std::lock_guard<std::mutex> lock(mutex);
    return searchPaths;
}

std::string FileSystem::ResolvePath(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex);
    
    // 如果是絕對路徑，直接返回
    if (IsAbsolutePath(path)) {
        return NormalizePath(path);
    }
    
    // 在搜索路徑中查找
    for (const auto& searchPath : searchPaths) {
        std::string fullPath = JoinPath(searchPath, path);
        if (FileExistsPlatform(fullPath) || DirectoryExistsPlatform(fullPath)) {
            return fullPath;
        }
    }
    
    // 如果找不到，返回當前目錄下的路徑
    std::string currentDir = GetCurrentDirectoryPlatform();
    return JoinPath(currentDir, path);
}

std::string FileSystem::GetExecutablePath() const {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::string(buffer);
#else
    char buffer[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
    if (count != -1) {
        buffer[count] = '\0';
        return std::string(buffer);
    }
    return "";
#endif
}

std::string FileSystem::GetTempDirectory() const {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetTempPathA(MAX_PATH, buffer);
    return std::string(buffer);
#else
    return "/tmp";
#endif
}

std::string FileSystem::GetHomeDirectory() const {
#ifdef _WIN32
    const char* homeDir = std::getenv("USERPROFILE");
    if (homeDir) return std::string(homeDir);
    
    const char* homeDrive = std::getenv("HOMEDRIVE");
    const char* homePath = std::getenv("HOMEPATH");
    if (homeDrive && homePath) {
        return std::string(homeDrive) + std::string(homePath);
    }
    return "";
#else
    const char* homeDir = std::getenv("HOME");
    if (homeDir) return std::string(homeDir);
    return "";
#endif
}

std::string FileSystem::NormalizePath(const std::string& path) {
    std::string normalized = path;
    
    // 替換所有分隔符為統一分隔符
    for (auto& c : normalized) {
        if (c == '\\' || c == '/') {
            c = PATH_SEPARATOR;
        }
    }
    
    // 移除重複的分隔符
    size_t pos = 0;
    while ((pos = normalized.find(std::string(2, PATH_SEPARATOR), pos)) != std::string::npos) {
        normalized.replace(pos, 2, 1, PATH_SEPARATOR);
    }
    
    return normalized;
}

std::string FileSystem::JoinPath(const std::string& path1, const std::string& path2) {
    std::string result = path1;
    
    if (!result.empty() && result.back() != PATH_SEPARATOR) {
        result += PATH_SEPARATOR;
    }
    
    result += path2;
    return NormalizePath(result);
}

bool FileSystem::IsAbsolutePath(const std::string& path) {
#ifdef _WIN32
    return (path.length() >= 2 && path[1] == ':') || 
           (path.length() >= 1 && (path[0] == '/' || path[0] == '\\'));
#else
    return path.length() >= 1 && path[0] == '/';
#endif
}

// ============================================================================
// 平台特定實現
// ============================================================================

bool FileSystem::FileExistsPlatform(const std::string& path) const {
    try {
        return fs::exists(path) && !fs::is_directory(path);
    } catch (const fs::filesystem_error&) {
        return false;
    }
}

bool FileSystem::DirectoryExistsPlatform(const std::string& path) const {
    try {
        return fs::exists(path) && fs::is_directory(path);
    } catch (const fs::filesystem_error&) {
        return false;
    }
}

bool FileSystem::CreateDirectoryPlatform(const std::string& path) const {
    try {
        return fs::create_directories(path);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to create directory: " << e.what() << std::endl;
        return false;
    }
}

bool FileSystem::DeleteDirectoryPlatform(const std::string& path) const {
    try {
        return fs::remove_all(path) > 0;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to delete directory: " << e.what() << std::endl;
        return false;
    }
}

std::vector<FileInfo> FileSystem::ListDirectoryPlatform(const std::string& path, bool filesOnly, bool dirsOnly) const {
    std::vector<FileInfo> result;
    
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            FileInfo info;
            info.name = entry.path().filename().string();
            info.path = entry.path().string();
            info.size = entry.file_size();
            info.isDirectory = entry.is_directory();
            info.isReadOnly = !(entry.status().permissions() & fs::perms::owner_write);
            
            if (filesOnly && info.isDirectory) continue;
            if (dirsOnly && !info.isDirectory) continue;
            
            result.push_back(info);
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to list directory: " << e.what() << std::endl;
    }
    
    return result;
}

uint64 FileSystem::GetFileSizePlatform(const std::string& path) const {
    try {
        return fs::file_size(path);
    } catch (const fs::filesystem_error&) {
        return 0;
    }
}

uint64 FileSystem::GetLastModifiedTimePlatform(const std::string& path) const {
    try {
        auto ftime = fs::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        return std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
    } catch (const fs::filesystem_error&) {
        return 0;
    }
}

std::string FileSystem::GetCurrentDirectoryPlatform() const {
    try {
        return fs::current_path().string();
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to get current directory: " << e.what() << std::endl;
        return "";
    }
}

bool FileSystem::SetCurrentDirectoryPlatform(const std::string& path) const {
    try {
        fs::current_path(path);
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to set current directory: " << e.what() << std::endl;
        return false;
    }
}

std::string FileSystem::ConvertSeparators(const std::string& path) const {
    return NormalizePath(path);
}

std::string FileSystem::RemoveTrailingSeparator(const std::string& path) const {
    if (path.empty()) return path;
    
    std::string result = path;
    while (!result.empty() && (result.back() == PATH_SEPARATOR || result.back() == NATIVE_SEPARATOR)) {
        result.pop_back();
    }
    
    return result;
}

// ============================================================================
// 全局函數實現
// ============================================================================

bool InitializeFileSystem() {
    if (gFileSystem) {
        std::cerr << "File system already initialized" << std::endl;
        return false;
    }
    
    gFileSystem = new FileSystem();
    return gFileSystem->Initialize();
}

void ShutdownFileSystem() {
    if (gFileSystem) {
        delete gFileSystem;
        gFileSystem = nullptr;
    }
}

FileSystem* GetFileSystem() {
    return gFileSystem;
}

} // namespace Potato