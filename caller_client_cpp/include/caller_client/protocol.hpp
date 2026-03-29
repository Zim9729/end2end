#ifndef CALLER_CLIENT_CPP_INCLUDE_CALLER_CLIENT_PROTOCOL_HPP
#define CALLER_CLIENT_CPP_INCLUDE_CALLER_CLIENT_PROTOCOL_HPP

#include <string>

namespace caller_client
{
	struct SubmitRequest
	{
		std::string cmd;
		std::string protocol_version;
		std::string task_id;
		std::string request_relpath;
		std::string result_relpath;
		std::string reply_ip;
		int reply_port = 0;
	};

	struct TaskReply
	{
		std::string cmd;
		std::string protocol_version;
		std::string task_id;
		std::string status;
		std::string result_relpath;
		std::string error;
	};

	std::string EscapeJsonString(const std::string& value);
	std::string ExtractJsonString(const std::string& json, const std::string& key);
	double ExtractJsonNumber(const std::string& json, const std::string& key, double default_value);
	std::string BuildSubmitJson(const SubmitRequest& request);
	TaskReply ParseTaskReply(const std::string& json);
	std::string UpsertJsonString(const std::string& json, const std::string& key, const std::string& value);
}

#endif
