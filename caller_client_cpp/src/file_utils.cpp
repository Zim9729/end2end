#include "caller_client/file_utils.hpp"

#include <cerrno>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

namespace
{
	int MakeDir(const std::string& path)
	{
#ifdef _WIN32
		return _mkdir(path.c_str());
#else
		return mkdir(path.c_str(), 0755);
#endif
	}

	bool IsAbsolutePath(const std::string& value)
	{
		return (value.size() > 1 && value[1] == ':') || (!value.empty() && (value[0] == '/' || value[0] == '\\'));
	}

	bool ContainsParentTraversal(const std::string& value)
	{
		return value == ".." || value.find("../") != std::string::npos || value.find("/..") != std::string::npos;
	}
}

namespace caller_client
{
	std::string NormalizeSlashes(std::string value)
	{
		for (char& ch : value)
		{
			if (ch == '\\')
			{
				ch = '/';
			}
		}
		return value;
	}

	std::string NormalizeRelativePath(std::string value)
	{
		value = NormalizeSlashes(value);
		while (value.find("./") == 0)
		{
			value.erase(0, 2);
		}
		while (!value.empty() && value[0] == '/')
		{
			value.erase(0, 1);
		}
		if (value.empty() || IsAbsolutePath(value) || ContainsParentTraversal(value))
		{
			return "";
		}
		return value;
	}

	std::string JoinPath(const std::string& left, const std::string& right)
	{
		if (left.empty())
		{
			return right;
		}
		if (right.empty())
		{
			return left;
		}
		if (left.back() == '/' || left.back() == '\\')
		{
			return left + right;
		}
		return left + "/" + right;
	}

	std::string ResolveSharedPath(const std::string& shared_root, const std::string& relpath)
	{
		return ToNativePath(JoinPath(shared_root, NormalizeSlashes(relpath)));
	}

	std::string ToNativePath(std::string value)
	{
#ifdef _WIN32
		for (char& ch : value)
		{
			if (ch == '/')
			{
				ch = '\\';
			}
		}
#else
		for (char& ch : value)
		{
			if (ch == '\\')
			{
				ch = '/';
			}
		}
#endif
		return value;
	}

	std::string SanitizeFileName(std::string value)
	{
		for (char& ch : value)
		{
			if (!(std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '_' || ch == '.'))
			{
				ch = '_';
			}
		}
		if (value.empty())
		{
			value = "task";
		}
		return value;
	}

	std::string GenerateTaskId()
	{
		const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		return "task-" + std::to_string(now);
	}

	std::string BuildDatePath()
	{
		std::time_t now = std::time(nullptr);
		std::tm local_time{};
#ifdef _WIN32
		localtime_s(&local_time, &now);
#else
		localtime_r(&now, &local_time);
#endif
		char buffer[16] = { 0 };
		std::strftime(buffer, sizeof(buffer), "%Y/%m/%d", &local_time);
		return buffer;
	}

	std::string BuildRequestRelPath(const std::string& task_id)
	{
		return "requests/" + BuildDatePath() + "/" + SanitizeFileName(task_id) + ".json";
	}

	std::string BuildResultRelPath(const std::string& task_id)
	{
		return "results/" + BuildDatePath() + "/" + SanitizeFileName(task_id) + "_result.json";
	}

	std::string ReadTextFile(const std::string& path)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input.good())
		{
			return "";
		}
		std::ostringstream buffer;
		buffer << input.rdbuf();
		return buffer.str();
	}

	bool EnsureParentDirectory(const std::string& file_path)
	{
		const std::size_t pos = file_path.find_last_of("\\/");
		if (pos == std::string::npos)
		{
			return true;
		}
		const std::string directory = file_path.substr(0, pos);
		if (directory.empty())
		{
			return true;
		}
		std::string current;
		if (directory.size() > 1 && directory[1] == ':')
		{
			current = directory.substr(0, 2);
		}
		for (std::size_t i = current.empty() ? 0 : 2; i < directory.size(); ++i)
		{
			const char ch = directory[i];
			current.push_back(ch);
			if (ch != '/' && ch != '\\')
			{
				continue;
			}
			if (current.size() <= 1)
			{
				continue;
			}
			MakeDir(current.c_str());
		}
		if (MakeDir(directory.c_str()) == 0)
		{
			return true;
		}
		return errno == EEXIST;
	}

	bool WriteTextFile(const std::string& path, const std::string& content)
	{
		if (!EnsureParentDirectory(path))
		{
			return false;
		}
		std::ofstream output(path, std::ios::binary);
		if (!output.good())
		{
			return false;
		}
		output << content;
		return output.good();
	}
}
