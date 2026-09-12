#include "FileSystem/FileSystem.h"
#include <iostream>

using namespace Potato;

int main() {
    std::cout << "=== Potato Engine File System Test ===" << std::endl;
    
    // 初始化文件系統
    if (!InitializeFileSystem()) {
        std::cerr << "Failed to initialize file system" << std::endl;
        return -1;
    }
    
    FileSystem* fileSystem = GetFileSystem();
    
    // 測試當前目錄
    std::cout << "\n--- Current Directory Tests ---" << std::endl;
    std::string currentDir = fileSystem->GetCurrentDirectory();
    std::cout << "Current Directory: " << currentDir << std::endl;
    
    // 測試路徑操作
    std::cout << "\n--- Path Operation Tests ---" << std::endl;
    std::string testPath = "test/directory/file.txt";
    std::cout << "Test Path: " << testPath << std::endl;
    std::cout << "File Name: " << fileSystem->GetFileName(testPath) << std::endl;
    std::cout << "File Extension: " << fileSystem->GetFileExtension(testPath) << std::endl;
    std::cout << "Directory Name: " << fileSystem->GetDirectoryName(testPath) << std::endl;
    std::cout << "Absolute Path: " << fileSystem->GetAbsolutePath(testPath) << std::endl;
    
    // 測試目錄操作
    std::cout << "\n--- Directory Operation Tests ---" << std::endl;
    std::string testDir = "test_directory";
    
    if (fileSystem->DirectoryExists(testDir)) {
        fileSystem->DeleteDirectory(testDir);
        std::cout << "Removed existing test directory" << std::endl;
    }
    
    if (fileSystem->CreateDirectory(testDir)) {
        std::cout << "Created directory: " << testDir << std::endl;
    } else {
        std::cerr << "Failed to create directory" << std::endl;
    }
    
    std::cout << "Directory exists: " << (fileSystem->DirectoryExists(testDir) ? "Yes" : "No") << std::endl;
    
    // 測試文件操作
    std::cout << "\n--- File Operation Tests ---" << std::endl;
    std::string testFile = testDir + "/test_file.txt";
    std::string testContent = "Hello, Potato Engine!";
    
    if (fileSystem->WriteFileText(testFile, testContent)) {
        std::cout << "Created file: " << testFile << std::endl;
    } else {
        std::cerr << "Failed to create file" << std::endl;
    }
    
    std::cout << "File exists: " << (fileSystem->FileExists(testFile) ? "Yes" : "No") << std::endl;
    
    // 測試文件讀取
    std::string readContent;
    if (fileSystem->ReadFileText(testFile, readContent)) {
        std::cout << "Read content: " << readContent << std::endl;
        std::cout << "Content matches: " << (readContent == testContent ? "Yes" : "No") << std::endl;
    } else {
        std::cerr << "Failed to read file" << std::endl;
    }
    
    // 測試二進制文件操作
    std::cout << "\n--- Binary File Tests ---" << std::endl;
    std::string binaryFile = testDir + "/binary_file.bin";
    std::vector<uint8> binaryData = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
    
    if (fileSystem->WriteFile(binaryFile, binaryData)) {
        std::cout << "Created binary file" << std::endl;
    }
    
    std::vector<uint8> readBinaryData;
    if (fileSystem->ReadFile(binaryFile, readBinaryData)) {
        std::cout << "Read binary data: ";
        for (uint8 byte : readBinaryData) {
            std::cout << std::hex << static_cast<int>(byte) << " ";
        }
        std::cout << std::dec << std::endl;
    }
    
    // 測試文件信息
    std::cout << "\n--- File Info Tests ---" << std::endl;
    FileInfo fileInfo = fileSystem->GetFileInfo(testFile);
    std::cout << "File Info:" << std::endl;
    std::cout << "  Name: " << fileInfo.name << std::endl;
    std::cout << "  Path: " << fileInfo.path << std::endl;
    std::cout << "  Size: " << fileInfo.size << " bytes" << std::endl;
    std::cout << "  Is Directory: " << (fileInfo.isDirectory ? "Yes" : "No") << std::endl;
    std::cout << "  Is Read Only: " << (fileInfo.isReadOnly ? "Yes" : "No") << std::endl;
    
    // 測試目錄列表
    std::cout << "\n--- Directory Listing Tests ---" << std::endl;
    std::vector<FileInfo> files = fileSystem->ListFiles(testDir);
    std::cout << "Files in directory (" << files.size() << "):" << std::endl;
    for (const auto& file : files) {
        std::cout << "  " << file.name << " (" << file.size << " bytes)" << std::endl;
    }
    
    // 測試搜索路徑
    std::cout << "\n--- Search Path Tests ---" << std::endl;
    std::vector<std::string> searchPaths = fileSystem->GetSearchPaths();
    std::cout << "Current search paths (" << searchPaths.size() << "):" << std::endl;
    for (const auto& path : searchPaths) {
        std::cout << "  " << path << std::endl;
    }
    
    // 測試路徑解析
    std::cout << "\n--- Path Resolution Tests ---" << std::endl;
    std::string relativePath = "test_file.txt";
    std::string resolvedPath = fileSystem->ResolvePath(relativePath);
    std::cout << "Resolved '" << relativePath << "' to: " << resolvedPath << std::endl;
    
    // 測試特殊目錄
    std::cout << "\n--- Special Directory Tests ---" << std::endl;
    std::cout << "Executable Path: " << fileSystem->GetExecutablePath() << std::endl;
    std::cout << "Temp Directory: " << fileSystem->GetTempDirectory() << std::endl;
    std::cout << "Home Directory: " << fileSystem->GetHomeDirectory() << std::endl;
    
    // 清理
    std::cout << "\n--- Cleanup ---" << std::endl;
    if (fileSystem->DeleteDirectory(testDir)) {
        std::cout << "Deleted test directory" << std::endl;
    }
    
    // 關閉文件系統
    ShutdownFileSystem();
    
    std::cout << "\n=== File System Test Complete ===" << std::endl;
    
    return 0;
}