#pragma once

#include "Core/CoreTypes.h"
#include "Core/Interfaces/IFileSystem.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace Potato {

/**
 * 文件系統實現
 * 提供跨平台的文件操作功能
 */
class FileSystem : public IFileSystem {
public:
    FileSystem();
    ~FileSystem();
    
    // IFileSystem 接口實現
    bool Initialize() override;
    void Shutdown() override;
    
    bool FileExists(const std::string& path) const override;
    bool DirectoryExists(const std::string& path) const override;
    
    bool ReadFile(const std::string& path, std::vector<uint8>& data) const override;
    bool ReadFileText(const std::string& path, std::string& text) const override;
    bool WriteFile(const std::string& path, const std::vector<uint8>& data) override;
    bool WriteFileText(const std::string& path, const std::string& text) override;
    
    bool CreateFile(const std::string& path) override;
    bool DeleteFile(const std::string& path) override;
    bool CopyFile(const std::string& source, const std::string& destination) override;
    bool MoveFile(const std::string& source, const std::string& destination) override;
    
    bool CreateDirectory(const std::string& path) override;
    bool DeleteDirectory(const std::string& path) override;
    bool CopyDirectory(const std::string& source, const std::string& destination) override;
    
    std::vector<FileInfo> ListFiles(const std::string& path) const override;
    std::vector<FileInfo> ListDirectories(const std::string& path) const override;
    std::vector<FileInfo> ListAll(const std::string& path) const override;
    
    std::string GetAbsolutePath(const std::string& path) const override;
    std::string GetRelativePath(const std::string& path, const std::string& base) const override;
    std::string GetFileName(const std::string& path) const override;
    std::string GetFileExtension(const std::string& path) const override;
    std::string GetDirectoryName(const std::string& path) const override;
    
    FileInfo GetFileInfo(const std::string& path) const override;
    uint64 GetFileSize(const std::string& path) const override;
    uint64 GetLastModifiedTime(const std::string& path) const override;
    
    std::string GetCurrentDirectory() const override;
    bool SetCurrentDirectory(const std::string& path) override;
    
    void AddSearchPath(const std::string& path) override;
    void RemoveSearchPath(const std::string& path) override;
    std::vector<std::string> GetSearchPaths() const override;
    std::string ResolvePath(const std::string& path) const override;
    
    // 文件系統特定方法
    std::string GetExecutablePath() const;
    std::string GetTempDirectory() const;
    std::string GetHomeDirectory() const;
    
    // 路径規範化
    static std::string NormalizePath(const std::string& path);
    static std::string JoinPath(const std::string& path1, const std::string& path2);
    static bool IsAbsolutePath(const std::string& path);
    
private:
    // 平台特定實現
    bool FileExistsPlatform(const std::string& path) const;
    bool DirectoryExistsPlatform(const std::string& path) const;
    bool CreateDirectoryPlatform(const std::string& path) const;
    bool DeleteDirectoryPlatform(const std::string& path) const;
    std::vector<FileInfo> ListDirectoryPlatform(const std::string& path, bool filesOnly, bool dirsOnly) const;
    uint64 GetFileSizePlatform(const std::string& path) const;
    uint64 GetLastModifiedTimePlatform(const std::string& path) const;
    std::string GetCurrentDirectoryPlatform() const;
    bool SetCurrentDirectoryPlatform(const std::string& path) const;
    
    // 路径處理輔助函數
    std::string ConvertSeparators(const std::string& path) const;
    std::string RemoveTrailingSeparator(const std::string& path) const;
    
private:
    std::vector<std::string> searchPaths;
    mutable std::mutex mutex;
    bool initialized;
    
    static constexpr char PATH_SEPARATOR = '/';
#ifdef _WIN32
    static constexpr char NATIVE_SEPARATOR = '\\';
#else
    static constexpr char NATIVE_SEPARATOR = '/';
#endif
};

// 全局文件系統指針
extern FileSystem* gFileSystem;

/**
 * 初始化全局文件系統
 */
bool InitializeFileSystem();

/**
 * 關閉全局文件系統
 */
void ShutdownFileSystem();

/**
 * 獲取全局文件系統
 */
FileSystem* GetFileSystem();

} // namespace Potato