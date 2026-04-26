#include "transferstation.h"
#include <windows.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

TransferStation& TransferStation::Instance() {
    static TransferStation instance;
    return instance;
}

void TransferStation::AddFile(const std::wstring& filePath) {
    std::lock_guard<std::mutex> lock(mutex);
    
    FileInfo fileInfo;
    fileInfo.path = filePath;
    fileInfo.name = GetFileName(filePath);
    fileInfo.size = GetFileSize(filePath);
    fileInfo.added_time = std::chrono::system_clock::now();
    
    files.push_back(fileInfo);
    totalSize += fileInfo.size;
}

void TransferStation::RemoveFile(size_t index) {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (index < files.size()) {
        totalSize -= files[index].size;
        files.erase(files.begin() + index);
    }
}

void TransferStation::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    
    files.clear();
    totalSize = 0;
}

std::vector<FileInfo> TransferStation::GetFiles() const {
    std::lock_guard<std::mutex> lock(mutex);
    return files;
}

size_t TransferStation::GetFileCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return files.size();
}

uint64_t TransferStation::GetTotalSize() const {
    std::lock_guard<std::mutex> lock(mutex);
    return totalSize;
}

bool TransferStation::OpenFile(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (index >= files.size()) {
        return false;
    }
    
    const std::wstring& filePath = files[index].path;
    return ShellExecute(nullptr, L"open", filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > (HINSTANCE)32;
}

bool TransferStation::TransferCopyFile(size_t index, const std::wstring& destination) const {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (index >= files.size()) {
        return false;
    }
    
    const std::wstring& sourcePath = files[index].path;
    std::wstring destPath = destination + L"\\" + files[index].name;
    
    return ::CopyFileW(sourcePath.c_str(), destPath.c_str(), FALSE);
}

bool TransferStation::TransferMoveFile(size_t index, const std::wstring& destination) {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (index >= files.size()) {
        return false;
    }
    
    const std::wstring& sourcePath = files[index].path;
    std::wstring destPath = destination + L"\\" + files[index].name;
    
    if (::MoveFileW(sourcePath.c_str(), destPath.c_str())) {
        // 更新文件路径
        files[index].path = destPath;
        return true;
    }
    
    return false;
}

bool TransferStation::TransferDeleteFile(size_t index) {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (index >= files.size()) {
        return false;
    }
    
    const std::wstring& filePath = files[index].path;
    if (::DeleteFileW(filePath.c_str())) {
        totalSize -= files[index].size;
        files.erase(files.begin() + index);
        return true;
    }
    
    return false;
}

FilePreviewInfo TransferStation::GetFilePreviewInfo(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex);
    
    FilePreviewInfo info;
    
    if (index >= files.size()) {
        return info;
    }
    
    const FileInfo& file = files[index];
    const std::wstring& filePath = file.path;
    
    // 获取文件扩展名
    info.file_extension = GetFileExtension(filePath);
    
    // 检查文件类型
    info.is_image = IsImageFile(filePath);
    info.is_text = IsTextFile(filePath);
    
    // 设置文件类型描述
    if (info.is_image) {
        info.file_type = L"Image";
    } else if (info.is_text) {
        info.file_type = L"Text";
    } else {
        info.file_type = L"Other";
    }
    
    // 读取文本文件内容（如果是文本文件）
    if (info.is_text) {
        info.text_content = ReadTextFile(filePath);
    }
    
    // 获取文件时间信息
    WIN32_FILE_ATTRIBUTE_DATA fileAttr;
    if (GetFileAttributesEx(filePath.c_str(), GetFileExInfoStandard, &fileAttr)) {
        info.creation_time = GetFileTimeString(fileAttr.ftCreationTime);
        info.last_modified_time = GetFileTimeString(fileAttr.ftLastWriteTime);
        info.last_access_time = GetFileTimeString(fileAttr.ftLastAccessTime);
    }
    
    return info;
}

std::string TransferStation::ReadTextFile(const std::wstring& filePath) const {
    std::string content;
    
    try {
        // 打开文件
        std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            return content;
        }
        
        // 读取文件内容
        std::stringstream buffer;
        buffer << file.rdbuf();
        content = buffer.str();
        
        // 限制读取大小，防止大文件影响性能
        const size_t MAX_PREVIEW_SIZE = 1024 * 1024; // 1MB
        if (content.size() > MAX_PREVIEW_SIZE) {
            content = content.substr(0, MAX_PREVIEW_SIZE) + "\n... (truncated)";
        }
        
        file.close();
    } catch (...) {
        // 忽略读取错误
    }
    
    return content;
}

bool TransferStation::IsImageFile(const std::wstring& filePath) const {
    std::wstring ext = GetFileExtension(filePath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // 常见图片文件扩展名
    std::vector<std::wstring> imageExts = {
        L"jpg", L"jpeg", L"png", L"bmp", L"gif", L"tiff", L"webp", L"svg"
    };
    
    return std::find(imageExts.begin(), imageExts.end(), ext) != imageExts.end();
}

bool TransferStation::IsTextFile(const std::wstring& filePath) const {
    std::wstring ext = GetFileExtension(filePath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // 常见文本文件扩展名
    std::vector<std::wstring> textExts = {
        L"txt", L"text", L"csv", L"json", L"xml", L"html", L"css", L"js",
        L"ts", L"cpp", L"h", L"hpp", L"c", L"cs", L"py", L"java", L"php",
        L"md", L"rtf", L"log"
    };
    
    return std::find(textExts.begin(), textExts.end(), ext) != textExts.end();
}

uint64_t TransferStation::GetFileSize(const std::wstring& filePath) const {
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesEx(filePath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        return (static_cast<uint64_t>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
    }
    return 0;
}

std::wstring TransferStation::GetFileName(const std::wstring& filePath) const {
    WCHAR fileName[MAX_PATH];
    if (PathFindFileName(filePath.c_str())) {
        wcscpy_s(fileName, MAX_PATH, PathFindFileName(filePath.c_str()));
        return fileName;
    }
    return L"";
}

std::wstring TransferStation::GetFileExtension(const std::wstring& filePath) const {
    WCHAR ext[MAX_PATH];
    if (PathFindExtension(filePath.c_str())) {
        wcscpy_s(ext, MAX_PATH, PathFindExtension(filePath.c_str()));
        // 移除点号
        if (ext[0] == L'.') {
            return ext + 1;
        }
        return ext;
    }
    return L"";
}

std::wstring TransferStation::GetFileTimeString(FILETIME fileTime) const {
    SYSTEMTIME systemTime;
    FileTimeToSystemTime(&fileTime, &systemTime);
    
    WCHAR timeStr[100];
    swprintf_s(timeStr, sizeof(timeStr) / sizeof(WCHAR),
               L"%04d-%02d-%02d %02d:%02d:%02d",
               systemTime.wYear, systemTime.wMonth, systemTime.wDay,
               systemTime.wHour, systemTime.wMinute, systemTime.wSecond);
    
    return timeStr;
}
