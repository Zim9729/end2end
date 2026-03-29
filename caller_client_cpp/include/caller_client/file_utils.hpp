#ifndef CALLER_CLIENT_CPP_INCLUDE_CALLER_CLIENT_FILE_UTILS_HPP
#define CALLER_CLIENT_CPP_INCLUDE_CALLER_CLIENT_FILE_UTILS_HPP

#include <string>

namespace caller_client
{
	std::string NormalizeSlashes(std::string value);
	std::string NormalizeRelativePath(std::string value);
	std::string JoinPath(const std::string& left, const std::string& right);
	std::string ResolveSharedPath(const std::string& shared_root, const std::string& relpath);
	std::string ToNativePath(std::string value);
	std::string SanitizeFileName(std::string value);
	std::string GenerateTaskId();
	std::string BuildDatePath();
	std::string BuildRequestRelPath(const std::string& task_id);
	std::string BuildResultRelPath(const std::string& task_id);
	std::string ReadTextFile(const std::string& path);
	bool WriteTextFile(const std::string& path, const std::string& content);
	bool EnsureParentDirectory(const std::string& file_path);
}

#endif
