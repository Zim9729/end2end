#include "caller_client/client.hpp"

#include "caller_client/file_utils.hpp"
#include "caller_client/protocol.hpp"

#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace
{
	struct SocketRuntime
	{
		bool ok = true;

		SocketRuntime()
		{
#ifdef _WIN32
			WSADATA data{};
			ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
		}

		~SocketRuntime()
		{
#ifdef _WIN32
			if (ok)
			{
				WSACleanup();
			}
#endif
		}
	};

	void CloseSocket(const SocketHandle socket_fd)
	{
#ifdef _WIN32
		closesocket(socket_fd);
#else
		close(socket_fd);
#endif
	}

	bool FillSockaddr(const std::string& ip, const int port, sockaddr_in* address)
	{
		std::memset(address, 0, sizeof(*address));
		address->sin_family = AF_INET;
		address->sin_port = htons(static_cast<unsigned short>(port));
#ifdef _WIN32
		return InetPtonA(AF_INET, ip.c_str(), &address->sin_addr) == 1;
#else
		return inet_pton(AF_INET, ip.c_str(), &address->sin_addr) == 1;
#endif
	}

	bool BindReplyPort(const SocketHandle socket_fd, const int reply_port)
	{
		sockaddr_in local_address{};
		local_address.sin_family = AF_INET;
		local_address.sin_addr.s_addr = htonl(INADDR_ANY);
		local_address.sin_port = htons(static_cast<unsigned short>(reply_port));
		return bind(socket_fd, reinterpret_cast<const sockaddr*>(&local_address), static_cast<int>(sizeof(local_address))) == 0;
	}

	bool SendAll(const SocketHandle socket_fd, const sockaddr_in& server_address, const std::string& payload)
	{
		const int sent = static_cast<int>(sendto(socket_fd,
			payload.c_str(),
			static_cast<int>(payload.size()),
			0,
			reinterpret_cast<const sockaddr*>(&server_address),
			static_cast<int>(sizeof(server_address))));
		return sent == static_cast<int>(payload.size());
	}

	bool ReceiveResponse(const SocketHandle socket_fd, const int timeout_ms, std::string* response)
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(socket_fd, &read_fds);

		timeval timeout{};
		timeout.tv_sec = timeout_ms / 1000;
		timeout.tv_usec = (timeout_ms % 1000) * 1000;

#ifdef _WIN32
		const int ready = select(0, &read_fds, nullptr, nullptr, &timeout);
#else
		const int ready = select(static_cast<int>(socket_fd + 1), &read_fds, nullptr, nullptr, &timeout);
#endif
		if (ready <= 0)
		{
			return false;
		}

		char buffer[65535] = { 0 };
		const int received = static_cast<int>(recvfrom(socket_fd,
			buffer,
			static_cast<int>(sizeof(buffer) - 1),
			0,
			nullptr,
			nullptr));
		if (received <= 0)
		{
			return false;
		}

		response->assign(buffer, buffer + received);
		return true;
	}

	bool WaitForReply(const SocketHandle socket_fd,
		const std::string& task_id,
		const int timeout_ms,
		std::string* raw_json,
		caller_client::TaskReply* reply)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
		while (std::chrono::steady_clock::now() < deadline)
		{
			const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
			if (remaining <= 0)
			{
				break;
			}
			std::string json;
			if (!ReceiveResponse(socket_fd, static_cast<int>(remaining), &json))
			{
				return false;
			}
			caller_client::TaskReply current = caller_client::ParseTaskReply(json);
			if (!current.task_id.empty() && current.task_id != task_id)
			{
				continue;
			}
			*raw_json = json;
			*reply = current;
			return true;
		}
		return false;
	}

	bool WaitForDone(const SocketHandle socket_fd,
		const std::string& task_id,
		const int timeout_ms,
		std::string* raw_json,
		caller_client::TaskReply* reply)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
		while (std::chrono::steady_clock::now() < deadline)
		{
			const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
			if (remaining <= 0)
			{
				break;
			}
			std::string json;
			caller_client::TaskReply current;
			if (!WaitForReply(socket_fd, task_id, static_cast<int>(remaining), &json, &current))
			{
				return false;
			}
			if (current.status == "accepted")
			{
				continue;
			}
			*raw_json = json;
			*reply = current;
			return true;
		}
		return false;
	}
}

namespace caller_client
{
	bool ParseInt(const std::string& text, int* value)
	{
		try
		{
			const int parsed = std::stoi(text);
			*value = parsed;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	ClientRunResult RunClient(const ClientConfig& config)
	{
		ClientRunResult result;
		result.task_id = config.task_id.empty() ? GenerateTaskId() : config.task_id;
		result.request_relpath = config.request_relpath.empty() ? BuildRequestRelPath(result.task_id) : NormalizeRelativePath(config.request_relpath);
		result.result_relpath = config.result_relpath.empty() ? BuildResultRelPath(result.task_id) : NormalizeRelativePath(config.result_relpath);
		if (config.server_ip.empty())
		{
			result.error = "server_ip is empty";
			return result;
		}
		if (config.reply_ip.empty())
		{
			result.error = "reply_ip is empty";
			return result;
		}
		if (config.shared_root.empty())
		{
			result.error = "shared_root is empty";
			return result;
		}
		if (config.source_request_path.empty())
		{
			result.error = "source_request_path is empty";
			return result;
		}
		if (result.request_relpath.empty())
		{
			result.error = "request_relpath is empty or invalid";
			return result;
		}
		if (result.result_relpath.empty())
		{
			result.error = "result_relpath is empty or invalid";
			return result;
		}

		const std::string source_request_json = ReadTextFile(config.source_request_path);
		if (source_request_json.empty())
		{
			result.error = "source request file not found or empty";
			return result;
		}
		const std::string request_json = UpsertJsonString(source_request_json, "task_id", result.task_id);
		result.request_file = ResolveSharedPath(config.shared_root, result.request_relpath);
		result.result_file = ResolveSharedPath(config.shared_root, result.result_relpath);
		if (!WriteTextFile(result.request_file, request_json))
		{
			result.error = "write request file failed";
			return result;
		}

		SocketRuntime runtime;
		if (!runtime.ok)
		{
			result.error = "socket runtime initialization failed";
			return result;
		}

		const SocketHandle socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (socket_fd == kInvalidSocket)
		{
			result.error = "create udp socket failed";
			return result;
		}

		if (!BindReplyPort(socket_fd, config.reply_port))
		{
			result.error = "bind reply port failed";
			CloseSocket(socket_fd);
			return result;
		}

		sockaddr_in server_address{};
		if (!FillSockaddr(config.server_ip, config.server_port, &server_address))
		{
			result.error = "invalid server address";
			CloseSocket(socket_fd);
			return result;
		}

		SubmitRequest submit;
		submit.cmd = "submit";
		submit.protocol_version = "1.0";
		submit.task_id = result.task_id;
		submit.request_relpath = result.request_relpath;
		submit.result_relpath = result.result_relpath;
		submit.reply_ip = config.reply_ip;
		submit.reply_port = config.reply_port;
		const std::string submit_json = BuildSubmitJson(submit);
		const int retry_count = config.submit_retry_count > 0 ? config.submit_retry_count : 0;

		TaskReply terminal_reply;
		bool have_terminal_reply = false;
		bool accepted_received = false;
		for (int attempt = 0; attempt <= retry_count; ++attempt)
		{
			if (!SendAll(socket_fd, server_address, submit_json))
			{
				result.error = "sendto failed";
				CloseSocket(socket_fd);
				return result;
			}

			std::string raw_reply;
			TaskReply reply;
			if (!WaitForReply(socket_fd, result.task_id, config.accepted_timeout_ms, &raw_reply, &reply))
			{
				continue;
			}

			if (!reply.result_relpath.empty())
			{
				result.result_relpath = reply.result_relpath;
				result.result_file = ResolveSharedPath(config.shared_root, result.result_relpath);
			}
			if (reply.status == "accepted")
			{
				result.accepted_reply_json = raw_reply;
				accepted_received = true;
				break;
			}
			if (reply.status == "success" || reply.status == "failed")
			{
				result.done_reply_json = raw_reply;
				terminal_reply = reply;
				have_terminal_reply = true;
				break;
			}
		}

		if (!accepted_received && !have_terminal_reply)
		{
			result.error = "wait accepted timeout";
			CloseSocket(socket_fd);
			return result;
		}

		if (!have_terminal_reply)
		{
			std::string raw_done;
			if (!WaitForDone(socket_fd, result.task_id, config.done_timeout_ms, &raw_done, &terminal_reply))
			{
				result.error = "wait done timeout";
				CloseSocket(socket_fd);
				return result;
			}
			result.done_reply_json = raw_done;
		}

		CloseSocket(socket_fd);

		if (!terminal_reply.result_relpath.empty())
		{
			result.result_relpath = terminal_reply.result_relpath;
			result.result_file = ResolveSharedPath(config.shared_root, result.result_relpath);
		}
		if (terminal_reply.status == "failed")
		{
			result.error = terminal_reply.error.empty() ? "task failed" : terminal_reply.error;
			return result;
		}
		if (terminal_reply.status != "success")
		{
			result.error = "unexpected done status";
			return result;
		}

		result.result_json = ReadTextFile(result.result_file);
		if (result.result_json.empty())
		{
			result.error = "result file not found or empty";
			return result;
		}
		result.ok = true;
		return result;
	}
}
