#ifndef CALLER_CLIENT_CPP_INCLUDE_CALLER_CLIENT_CLIENT_HPP
#define CALLER_CLIENT_CPP_INCLUDE_CALLER_CLIENT_CLIENT_HPP

#include <string>

namespace caller_client
{
	struct ClientConfig
	{
		std::string server_ip;
		int server_port = 9000;
		std::string reply_ip;
		int reply_port = 9001;
		std::string shared_root;
		std::string source_request_path;
		std::string task_id;
		std::string request_relpath;
		std::string result_relpath;
		int accepted_timeout_ms = 2000;
		int done_timeout_ms = 30000;
		int submit_retry_count = 2;
	};

	struct ClientRunResult
	{
		bool ok = false;
		std::string error;
		std::string task_id;
		std::string request_relpath;
		std::string result_relpath;
		std::string request_file;
		std::string result_file;
		std::string accepted_reply_json;
		std::string done_reply_json;
		std::string result_json;
	};

	bool ParseInt(const std::string& text, int* value);
	ClientRunResult RunClient(const ClientConfig& config);
}

#endif
