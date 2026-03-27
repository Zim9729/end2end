#include "service_protocol.hpp"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

namespace
{
	std::size_t FindKey(const std::string& json, const std::string& key)
	{
		const std::string pattern = "\"" + key + "\"";
		return json.find(pattern);
	}

	std::size_t FindValueStart(const std::string& json, std::size_t key_pos)
	{
		if (key_pos == std::string::npos)
		{
			return std::string::npos;
		}
		std::size_t colon_pos = json.find(':', key_pos);
		if (colon_pos == std::string::npos)
		{
			return std::string::npos;
		}
		std::size_t value_pos = colon_pos + 1;
		while (value_pos < json.size() && std::isspace(static_cast<unsigned char>(json[value_pos])) != 0)
		{
			++value_pos;
		}
		return value_pos;
	}

	std::string ParseJsonStringValue(const std::string& json, std::size_t value_pos)
	{
		if (value_pos == std::string::npos || value_pos >= json.size() || json[value_pos] != '"')
		{
			return "";
		}
		std::string result;
		for (std::size_t i = value_pos + 1; i < json.size(); ++i)
		{
			const char ch = json[i];
			if (ch == '\\')
			{
				if (i + 1 >= json.size())
				{
					break;
				}
				const char next = json[++i];
				switch (next)
				{
				case '"': result.push_back('"'); break;
				case '\\': result.push_back('\\'); break;
				case '/': result.push_back('/'); break;
				case 'b': result.push_back('\b'); break;
				case 'f': result.push_back('\f'); break;
				case 'n': result.push_back('\n'); break;
				case 'r': result.push_back('\r'); break;
				case 't': result.push_back('\t'); break;
				default: result.push_back(next); break;
				}
				continue;
			}
			if (ch == '"')
			{
				return result;
			}
			result.push_back(ch);
		}
		return result;
	}

	int MakeDir(const std::string& path)
	{
#ifdef _WIN32
		return _mkdir(path.c_str());
#else
		return mkdir(path.c_str(), 0755);
#endif
	}
}

namespace service
{
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

	std::string ResolveBusinessTypeName(const int label)
	{
		switch (label)
		{
		case 0:
			return "XLBH-542";
		default:
			return "unknown";
		}
	}

	int ComputePhysicalCoordinate(const int coordinate, const double scale)
	{
		if (!(scale > 0.0))
		{
			return coordinate;
		}
		return static_cast<int>(std::lround(static_cast<double>(coordinate) * scale));
	}

	std::string ExtractJsonString(const std::string& json, const std::string& key)
	{
		return ParseJsonStringValue(json, FindValueStart(json, FindKey(json, key)));
	}

	double ExtractJsonNumber(const std::string& json, const std::string& key, const double default_value)
	{
		const std::size_t value_pos = FindValueStart(json, FindKey(json, key));
		if (value_pos == std::string::npos || value_pos >= json.size())
		{
			return default_value;
		}
		std::size_t end_pos = value_pos;
		while (end_pos < json.size())
		{
			const char ch = json[end_pos];
			if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E')
			{
				++end_pos;
				continue;
			}
			break;
		}
		if (end_pos == value_pos)
		{
			return default_value;
		}
		return std::atof(json.substr(value_pos, end_pos - value_pos).c_str());
	}

	SubmitRequest ParseSubmitRequest(const std::string& json)
	{
		SubmitRequest request;
		request.cmd = ExtractJsonString(json, "cmd");
		request.protocol_version = ExtractJsonString(json, "protocol_version");
		request.task_id = ExtractJsonString(json, "task_id");
		request.request_relpath = ExtractJsonString(json, "request_relpath");
		request.result_relpath = ExtractJsonString(json, "result_relpath");
		if (request.request_relpath.empty())
		{
			request.request_relpath = ExtractJsonString(json, "request_file");
		}
		if (request.result_relpath.empty())
		{
			request.result_relpath = ExtractJsonString(json, "result_file");
		}
		request.reply_ip = ExtractJsonString(json, "reply_ip");
		request.reply_port = static_cast<int>(ExtractJsonNumber(json, "reply_port", 0));
		return request;
	}

	InputRequest ParseInputRequest(const std::string& json)
	{
		InputRequest request;
		request.device_id = ExtractJsonString(json, "device_id");
		request.id = ExtractJsonString(json, "id");
		request.image_path = ExtractJsonString(json, "imagePath");
		request.mileage = ExtractJsonString(json, "mileage");
		request.mileage_sign = ExtractJsonString(json, "mileageSign");
		request.task_id = ExtractJsonString(json, "task_id");
		request.version = ExtractJsonString(json, "version");
		request.image_base64 = ExtractJsonString(json, "image");
		request.img_physical = ExtractJsonNumber(json, "img_physical", 1.0);
		request.img_scaling = ExtractJsonNumber(json, "img_scaling", 1.0);
		return request;
	}

	TaskState ParseTaskState(const std::string& json)
	{
		TaskState state;
		state.protocol_version = ExtractJsonString(json, "protocol_version");
		state.task_id = ExtractJsonString(json, "task_id");
		state.status = ExtractJsonString(json, "status");
		state.request_relpath = ExtractJsonString(json, "request_relpath");
		state.result_relpath = ExtractJsonString(json, "result_relpath");
		state.reply_ip = ExtractJsonString(json, "reply_ip");
		state.reply_port = static_cast<int>(ExtractJsonNumber(json, "reply_port", 0));
		state.error = ExtractJsonString(json, "error");
		return state;
	}

	std::vector<unsigned char> Base64Decode(const std::string& value)
	{
		static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::vector<int> inverse(256, -1);
		for (std::size_t i = 0; i < alphabet.size(); ++i)
		{
			inverse[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
		}
		std::vector<unsigned char> output;
		int buffer = 0;
		int bits_collected = 0;
		for (const char ch : value)
		{
			if (std::isspace(static_cast<unsigned char>(ch)) != 0)
			{
				continue;
			}
			if (ch == '=')
			{
				break;
			}
			const int decoded = inverse[static_cast<unsigned char>(ch)];
			if (decoded < 0)
			{
				continue;
			}
			buffer = (buffer << 6) | decoded;
			bits_collected += 6;
			if (bits_collected >= 8)
			{
				bits_collected -= 8;
				output.push_back(static_cast<unsigned char>((buffer >> bits_collected) & 0xFF));
			}
		}
		return output;
	}

	std::string EscapeJsonString(const std::string& value)
	{
		std::string result;
		result.reserve(value.size());
		for (const char ch : value)
		{
			switch (ch)
			{
			case '"': result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\b': result += "\\b"; break;
			case '\f': result += "\\f"; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default: result.push_back(ch); break;
			}
		}
		return result;
	}

	std::string BuildResultJson(const std::string& image_path,
		double img_physical,
		double img_scaling,
		int ikoujian_count,
		const std::vector<DefectRecord>& defects)
	{
		std::ostringstream output;
		output << "{\n";
		output << "  \"defects\": [\n";
		for (std::size_t i = 0; i < defects.size(); ++i)
		{
			const DefectRecord& defect = defects[i];
			const int physical_ymax = ComputePhysicalCoordinate(defect.ymax, img_physical);
			const int physical_ymin = ComputePhysicalCoordinate(defect.ymin, img_physical);
			output << "    {\n";
			output << "      \"id\": \"" << EscapeJsonString(defect.id) << "\",\n";
			output << "      \"physical_ymax\": " << physical_ymax << ",\n";
			output << "      \"physical_ymin\": " << physical_ymin << ",\n";
			output << "      \"type\": \"" << EscapeJsonString(defect.type) << "\",\n";
			output << "      \"xmax\": " << defect.xmax << ",\n";
			output << "      \"xmin\": " << defect.xmin << ",\n";
			output << "      \"ymax\": " << defect.ymax << ",\n";
			output << "      \"ymin\": " << defect.ymin << "\n";
			output << "    }";
			if (i + 1 != defects.size())
			{
				output << ',';
			}
			output << "\n";
		}
		output << "  ],\n";
		output << "  \"ikoujian_count\": " << ikoujian_count << ",\n";
		output << "  \"imagePath\": \"" << EscapeJsonString(image_path) << "\",\n";
		output << "  \"img_physical\": " << img_physical << ",\n";
		output << "  \"img_scaling\": " << img_scaling << "\n";
		output << "}";
		return output.str();
	}

	std::string BuildTaskReplyJson(const std::string& cmd,
		const std::string& task_id,
		const std::string& status,
		const std::string& result_relpath,
		const std::string& error)
	{
		std::string json = "{";
		json += "\"cmd\":\"" + EscapeJsonString(cmd) + "\",";
		json += "\"protocol_version\":\"1.0\",";
		json += "\"task_id\":\"" + EscapeJsonString(task_id) + "\",";
		json += "\"status\":\"" + EscapeJsonString(status) + "\"";
		if (!result_relpath.empty())
		{
			json += ",\"result_relpath\":\"" + EscapeJsonString(result_relpath) + "\"";
		}
		if (!error.empty())
		{
			json += ",\"error\":\"" + EscapeJsonString(error) + "\"";
		}
		json += "}";
		return json;
	}

	std::string BuildTaskStateJson(const TaskState& state)
	{
		std::string json = "{";
		json += "\"protocol_version\":\"" + EscapeJsonString(state.protocol_version) + "\",";
		json += "\"task_id\":\"" + EscapeJsonString(state.task_id) + "\",";
		json += "\"status\":\"" + EscapeJsonString(state.status) + "\",";
		json += "\"request_relpath\":\"" + EscapeJsonString(state.request_relpath) + "\",";
		json += "\"result_relpath\":\"" + EscapeJsonString(state.result_relpath) + "\",";
		json += "\"reply_ip\":\"" + EscapeJsonString(state.reply_ip) + "\",";
		json += "\"reply_port\":" + std::to_string(state.reply_port);
		if (!state.error.empty())
		{
			json += ",\"error\":\"" + EscapeJsonString(state.error) + "\"";
		}
		json += "}";
		return json;
	}

	std::string QuoteArg(const std::string& value)
	{
		std::string result = "\"";
		for (const char ch : value)
		{
			if (ch == '"')
			{
				result += '\\';
			}
			result += ch;
		}
		result += "\"";
		return result;
	}
}
