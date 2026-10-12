#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Potato {

/**
 * 文件信息結構
 */
struct FileInfo {
    std::string name;
    std::string path;
    size_t size;
    bool isDirectory;
    bool isReadOnly;
};

/**
 * 文件系統接口
 * 定義文件操作的基本功能
 */
class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    
    // 初始化和關閉
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    
    // 文件存在性檢查
    virtual bool FileExists(const std::string& path) const = 0;
    virtual bool DirectoryExists(const std::string& path) const = 0;
    
    // 文件讀寫
    virtual bool ReadFile(const std::string& path, std::vector<uint8_t>& data) const = 0;
    virtual bool ReadFileText(const std::string& path, std::string& text) const = 0;
    virtual bool WriteFile(const std::string& path, const std::vector<uint8_t>& data) = 0;
    virtual bool WriteFileText(const std::string& path, const std::string& text) = 0;
    
    // 文件操作
    virtual bool CreateFile(const std::string& path) = 0;
    virtual bool DeleteFile(const std::string& path) = 0;
    virtual bool CopyFile(const std::string& source, const std::string& destination) = 0;
    virtual bool MoveFile(const std::string& source, const std::string& destination) = 0;
    
    // 目錄操作
    virtual bool CreateDirectory(const std::string& path) = 0;
    virtual bool DeleteDirectory(const std::string& path) = 0;
    virtual bool CopyDirectory(const std::string& source, const std::string& destination) = 0;
    
    // 文件枚舉
    virtual std::vector<FileInfo> ListFiles(const std::string& path) const = 0;
    virtual std::vector<FileInfo> ListDirectories(const std::string& path) const = 0;
    virtual std::vector<FileInfo> ListAll(const std::string& path) const = 0;
    
    // 路徑操作
    virtual std::string GetAbsolutePath(const std::string& path) const = 0;
    virtual std::string GetRelativePath(const std::string& path, const std::string& base) const = 0;
    virtual std::string GetFileName(const std::string& path) const = 0;
    virtual std::string GetFileExtension(const std::string& path) const = 0;
    virtual std::string GetDirectoryName(const std::string& path) const = 0;
    
    // 文件信息
    virtual FileInfo GetFileInfo(const std::string& path) const = 0;
    virtual uint64_t GetFileSize(const std::string& path) const = 0;
    virtual uint64_t GetLastModifiedTime(const std::string& path) const = 0;
    
    // 工作目錄
    virtual std::string GetCurrentDirectory() const = 0;
    virtual bool SetCurrentDirectory(const std::string& path) = 0;
    
    // 資源路徑管理
    virtual void AddSearchPath(const std::string& path) = 0;
    virtual void RemoveSearchPath(const std::string& path) = 0;
    virtual std::vector<std::string> GetSearchPaths() const = 0;
    virtual std::string ResolvePath(const std::string& path) const = 0;
};

} // namespace Potato