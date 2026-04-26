#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <windows.h>

struct FileInfo {
    std::wstring path;
    std::wstring name;
    uint64_t size;
    std::chrono::system_clock::time_point added_time;
};

struct FilePreviewInfo {
    std::wstring file_type;
    std::wstring file_extension;
    std::wstring creation_time;
    std::wstring last_modified_time;
    std::wstring last_access_time;
    std::string text_content;
    bool is_image = false;
    bool is_text = false;
};

class TransferStation {
public:
    static TransferStation& Instance();
    
    // 添加文件到中转站
    void AddFile(const std::wstring& filePath);
    
    // 从中转站移除文件
    void RemoveFile(size_t index);
    
    // 清空中转站
    void Clear();
    
    // 获取文件列表
    std::vector<FileInfo> GetFiles() const;
    
    // 获取文件数量
    size_t GetFileCount() const;
    
    // 获取总文件大小
    uint64_t GetTotalSize() const;
    
    // 打开文件
    bool OpenFile(size_t index) const;
    
    // 复制文件
    bool TransferCopyFile(size_t index, const std::wstring& destination) const;
    
    // 移动文件
    bool TransferMoveFile(size_t index, const std::wstring& destination);
    
    // 删除文件
    bool TransferDeleteFile(size_t index);
    
    // 获取文件详细信息
    FilePreviewInfo GetFilePreviewInfo(size_t index) const;
    
    // 读取文本文件内容
    std::string ReadTextFile(const std::wstring& filePath) const;
    
    // 检查文件是否为图片
    bool IsImageFile(const std::wstring& filePath) const;
    
    // 检查文件是否为文本文件
    bool IsTextFile(const std::wstring& filePath) const;
    
private:
    TransferStation() = default;
    ~TransferStation() = default;
    TransferStation(const TransferStation&) = delete;
    TransferStation& operator=(const TransferStation&) = delete;
    
    mutable std::mutex mutex;
    std::vector<FileInfo> files;
    uint64_t totalSize = 0;
    
    // 获取文件大小
    uint64_t GetFileSize(const std::wstring& filePath) const;
    
    // 获取文件名
    std::wstring GetFileName(const std::wstring& filePath) const;
    
    // 获取文件扩展名
    std::wstring GetFileExtension(const std::wstring& filePath) const;
    
    // 获取文件时间信息
    std::wstring GetFileTimeString(FILETIME fileTime) const;
};

// 全局访问宏
#define g_transferstation TransferStation::Instance()
