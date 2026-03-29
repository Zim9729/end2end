#include "caller_client/protocol.hpp"

#include <cctype>
#include <cstdlib>

namespace
{
	std::size_t FindKey(const std::string& json, const std::string& key)
	{
		const std::string pattern = "\"" + key + "\"";
		return json.find(pattern);
	}

	std::size_t FindValueStart(const std::string& json, const std::size_t key_pos)
	{
		if (key_pos == std::string::npos)
		{
			return std::string::npos;
		}
		const std::size_t colon_pos = json.find(':', key_pos);
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

	std::string ParseJsonStringValue(const std::string& json, const std::size_t value_pos)
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

	std::size_t FindStringEnd(const std::string& json, const std::size_t value_pos)
	{
		if (value_pos == std::string::npos || value_pos >= json.size() || json[value_pos] != '"')
		{
			return std::string::npos;
		}
		for (std::size_t i = value_pos + 1; i < json.size(); ++i)
		{
			if (json[i] == '\\')
			{
				++i;
				continue;
			}
			if (json[i] == '"')
			{
				return i;
			}
		}
		return std::string::npos;
	}
}

namespace caller_client
{
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

	std::string BuildSubmitJson(const SubmitRequest& request)
	{
		std::string json = "{";
		json += "\"cmd\":\"" + EscapeJsonString(request.cmd) + "\",";
		json += "\"protocol_version\":\"" + EscapeJsonString(request.protocol_version) + "\",";
		json += "\"task_id\":\"" + EscapeJsonString(request.task_id) + "\",";
		json += "\"request_relpath\":\"" + EscapeJsonString(request.request_relpath) + "\",";
		json += "\"result_relpath\":\"" + EscapeJsonString(request.result_relpath) + "\",";
		json += "\"reply_ip\":\"" + EscapeJsonString(request.reply_ip) + "\",";
		json += "\"reply_port\":" + std::to_string(request.reply_port);
		json += "}";
		return json;
	}

	TaskReply ParseTaskReply(const std::string& json)
	{
		TaskReply reply;
		reply.cmd = ExtractJsonString(json, "cmd");
		reply.protocol_version = ExtractJsonString(json, "protocol_version");
		reply.task_id = ExtractJsonString(json, "task_id");
		reply.status = ExtractJsonString(json, "status");
		reply.result_relpath = ExtractJsonString(json, "result_relpath");
		reply.error = ExtractJsonString(json, "error");
		return reply;
	}

	std::string UpsertJsonString(const std::string& json, const std::string& key, const std::string& value)
	{
		const std::size_t key_pos = FindKey(json, key);
		const std::string escaped = "\"" + EscapeJsonString(value) + "\"";
		if (key_pos != std::string::npos)
		{
			const std::size_t value_pos = FindValueStart(json, key_pos);
			const std::size_t value_end = FindStringEnd(json, value_pos);
			if (value_pos == std::string::npos || value_end == std::string::npos)
			{
				return json;
			}
			std::string updated = json;
			updated.replace(value_pos, value_end - value_pos + 1, escaped);
			return updated;
		}

		const std::size_t insert_pos = json.find_last_of('}');
		const std::size_t object_start = json.find('{');
		if (insert_pos == std::string::npos || object_start == std::string::npos || object_start > insert_pos)
		{
			return json;
		}
		bool has_fields = false;
		for (std::size_t i = object_start + 1; i < insert_pos; ++i)
		{
			if (std::isspace(static_cast<unsigned char>(json[i])) == 0)
			{
				has_fields = true;
				break;
			}
		}
		std::string updated = json;
		updated.insert(insert_pos, std::string(has_fields ? "," : "") + "\"" + key + "\":" + escaped);
		return updated;
	}
}
