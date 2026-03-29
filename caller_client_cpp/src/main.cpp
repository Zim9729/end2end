#include "caller_client/client.hpp"

#include <iostream>
#include <string>

namespace
{
	void PrintUsage()
	{
		std::cout << "usage: caller_client_demo --server-ip <ip> --server-port <port> --reply-ip <ip> --reply-port <port> --shared-root <path> --source-request <file> [--task-id <id>] [--request-relpath <relpath>] [--result-relpath <relpath>] [--accepted-timeout-ms <ms>] [--done-timeout-ms <ms>] [--retry-count <n>]" << std::endl;
		std::cout << "example: caller_client_demo --server-ip 192.168.145.100 --server-port 9000 --reply-ip 192.168.145.1 --reply-port 9001 --shared-root Z:\\rail_share --source-request .\\request_template.json" << std::endl;
	}

	bool RequireValue(const int argc, char** argv, int* index, std::string* value)
	{
		if (*index + 1 >= argc)
		{
			return false;
		}
		*value = argv[++(*index)];
		return true;
	}
}

int main(int argc, char** argv)
{
	caller_client::ClientConfig config;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		std::string value;
		if (arg == "--help" || arg == "-h")
		{
			PrintUsage();
			return 0;
		}
		if (!RequireValue(argc, argv, &i, &value))
		{
			std::cerr << "missing value for argument: " << arg << std::endl;
			PrintUsage();
			return -1;
		}
		if (arg == "--server-ip")
		{
			config.server_ip = value;
		}
		else if (arg == "--server-port")
		{
			if (!caller_client::ParseInt(value, &config.server_port))
			{
				std::cerr << "invalid server port: " << value << std::endl;
				return -1;
			}
		}
		else if (arg == "--reply-ip")
		{
			config.reply_ip = value;
		}
		else if (arg == "--reply-port")
		{
			if (!caller_client::ParseInt(value, &config.reply_port))
			{
				std::cerr << "invalid reply port: " << value << std::endl;
				return -1;
			}
		}
		else if (arg == "--shared-root")
		{
			config.shared_root = value;
		}
		else if (arg == "--source-request")
		{
			config.source_request_path = value;
		}
		else if (arg == "--task-id")
		{
			config.task_id = value;
		}
		else if (arg == "--request-relpath")
		{
			config.request_relpath = value;
		}
		else if (arg == "--result-relpath")
		{
			config.result_relpath = value;
		}
		else if (arg == "--accepted-timeout-ms")
		{
			if (!caller_client::ParseInt(value, &config.accepted_timeout_ms))
			{
				std::cerr << "invalid accepted timeout: " << value << std::endl;
				return -1;
			}
		}
		else if (arg == "--done-timeout-ms")
		{
			if (!caller_client::ParseInt(value, &config.done_timeout_ms))
			{
				std::cerr << "invalid done timeout: " << value << std::endl;
				return -1;
			}
		}
		else if (arg == "--retry-count")
		{
			if (!caller_client::ParseInt(value, &config.submit_retry_count))
			{
				std::cerr << "invalid retry count: " << value << std::endl;
				return -1;
			}
		}
		else
		{
			std::cerr << "unknown argument: " << arg << std::endl;
			PrintUsage();
			return -1;
		}
	}

	if (config.server_ip.empty() || config.reply_ip.empty() || config.shared_root.empty() || config.source_request_path.empty())
	{
		PrintUsage();
		return -1;
	}

	const caller_client::ClientRunResult result = caller_client::RunClient(config);
	std::cout << "task_id: " << result.task_id << std::endl;
	std::cout << "request_relpath: " << result.request_relpath << std::endl;
	std::cout << "result_relpath: " << result.result_relpath << std::endl;
	std::cout << "request_file: " << result.request_file << std::endl;
	if (!result.accepted_reply_json.empty())
	{
		std::cout << "accepted_reply: " << result.accepted_reply_json << std::endl;
	}
	if (!result.done_reply_json.empty())
	{
		std::cout << "done_reply: " << result.done_reply_json << std::endl;
	}
	if (!result.error.empty())
	{
		std::cerr << "error: " << result.error << std::endl;
		return -1;
	}
	std::cout << "result_file: " << result.result_file << std::endl;
	std::cout << result.result_json << std::endl;
	return 0;
}
