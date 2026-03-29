#include "service_protocol.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

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
	void CloseSocket(SocketHandle socket_fd)
	{
#ifdef _WIN32
		closesocket(socket_fd);
#else
		close(socket_fd);
#endif
	}

	bool InitSocketRuntime()
	{
#ifdef _WIN32
		WSADATA data{};
		return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
		return true;
#endif
	}

	void ShutdownSocketRuntime()
	{
#ifdef _WIN32
		WSACleanup();
#endif
	}

	bool ParsePort(const std::string& text, int* port)
	{
		try
		{
			const int value = std::stoi(text);
			if (value <= 0 || value > 65535)
			{
				return false;
			}
			*port = value;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool FillSockaddr(const std::string& ip, int port, sockaddr_in* address)
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

	std::string BuildSubmitJson(const service::SubmitRequest& request)
	{
		std::string json = "{";
		json += "\"cmd\":\"" + service::EscapeJsonString(request.cmd) + "\",";
		json += "\"protocol_version\":\"" + service::EscapeJsonString(request.protocol_version) + "\",";
		json += "\"task_id\":\"" + service::EscapeJsonString(request.task_id) + "\",";
		json += "\"request_relpath\":\"" + service::EscapeJsonString(request.request_relpath) + "\",";
		json += "\"result_relpath\":\"" + service::EscapeJsonString(request.result_relpath) + "\",";
		json += "\"reply_ip\":\"" + service::EscapeJsonString(request.reply_ip) + "\",";
		json += "\"reply_port\":" + std::to_string(request.reply_port);
		json += "}";
		return json;
	}

	std::string GenerateTaskId()
	{
		const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		return "demo-" + std::to_string(now);
	}

	std::string SocketAddressToString(const sockaddr_in& address)
	{
		char ip_buffer[INET_ADDRSTRLEN] = { 0 };
#ifdef _WIN32
		if (InetNtopA(AF_INET, const_cast<in_addr*>(&address.sin_addr), ip_buffer, static_cast<DWORD>(sizeof(ip_buffer))) == nullptr)
		{
			return "unknown";
		}
#else
		if (inet_ntop(AF_INET, &address.sin_addr, ip_buffer, sizeof(ip_buffer)) == nullptr)
		{
			return "unknown";
		}
#endif
		return std::string(ip_buffer) + ":" + std::to_string(ntohs(address.sin_port));
	}

	bool SendAll(SocketHandle socket_fd, const sockaddr_in& server_address, const std::string& payload)
	{
		const int sent = static_cast<int>(sendto(socket_fd,
			payload.c_str(),
			static_cast<int>(payload.size()),
			0,
			reinterpret_cast<const sockaddr*>(&server_address),
			static_cast<int>(sizeof(server_address))));
		return sent == static_cast<int>(payload.size());
	}

	bool ReceiveResponse(SocketHandle socket_fd, std::string* response, sockaddr_in* peer_address)
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(socket_fd, &read_fds);

		timeval timeout{};
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;

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
#ifdef _WIN32
		int peer_length = static_cast<int>(sizeof(*peer_address));
#else
		socklen_t peer_length = static_cast<socklen_t>(sizeof(*peer_address));
#endif
		const int received = static_cast<int>(recvfrom(socket_fd,
			buffer,
			static_cast<int>(sizeof(buffer) - 1),
			0,
			reinterpret_cast<sockaddr*>(peer_address),
			&peer_length));
		if (received <= 0)
		{
			return false;
		}

		response->assign(buffer, buffer + received);
		return true;
	}

	void PrintUsage()
	{
		std::cerr << "usage: udp_client_demo <server_ip> <server_port> <reply_ip> <reply_port> [task_id] [request_relpath] [result_relpath]" << std::endl;
		std::cerr << "example: udp_client_demo 192.168.145.100 9000 192.168.145.1 9001 task-1 requests/inbox/a.json results/outbox/a_result.json" << std::endl;
	}
}

int main(int argc, char** argv)
{
	if (argc < 5)
	{
		PrintUsage();
		return -1;
	}

	if (!InitSocketRuntime())
	{
		std::cerr << "socket runtime initialization failed" << std::endl;
		return -1;
	}

	const std::string server_ip = argv[1];
	int server_port = 0;
	if (!ParsePort(argv[2], &server_port))
	{
		std::cerr << "invalid server port: " << argv[2] << std::endl;
		ShutdownSocketRuntime();
		return -1;
	}

	const std::string reply_ip = argv[3];
	int reply_port = 0;
	if (!ParsePort(argv[4], &reply_port))
	{
		std::cerr << "invalid reply port: " << argv[4] << std::endl;
		ShutdownSocketRuntime();
		return -1;
	}

	service::SubmitRequest request;
	request.cmd = "submit";
	request.protocol_version = "1.0";
	request.task_id = (argc >= 6) ? argv[5] : GenerateTaskId();
	request.request_relpath = (argc >= 7) ? argv[6] : "requests/inbox/demo_request.json";
	request.result_relpath = (argc >= 8) ? argv[7] : "results/outbox/demo_result.json";
	request.reply_ip = reply_ip;
	request.reply_port = reply_port;

	SocketHandle socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_fd == kInvalidSocket)
	{
		std::cerr << "create udp socket failed" << std::endl;
		ShutdownSocketRuntime();
		return -1;
	}

	sockaddr_in local_address{};
	if (!FillSockaddr("0.0.0.0", reply_port, &local_address))
	{
		std::cerr << "invalid local bind address for reply port: " << reply_port << std::endl;
		CloseSocket(socket_fd);
		ShutdownSocketRuntime();
		return -1;
	}
	if (bind(socket_fd, reinterpret_cast<const sockaddr*>(&local_address), static_cast<int>(sizeof(local_address))) < 0)
	{
		std::cerr << "bind failed on local reply port: " << reply_port << std::endl;
		CloseSocket(socket_fd);
		ShutdownSocketRuntime();
		return -1;
	}

	sockaddr_in server_address{};
	if (!FillSockaddr(server_ip, server_port, &server_address))
	{
		std::cerr << "invalid server address: " << server_ip << ":" << server_port << std::endl;
		CloseSocket(socket_fd);
		ShutdownSocketRuntime();
		return -1;
	}

	const std::string payload = BuildSubmitJson(request);
	std::cout << "send request: " << payload << std::endl;
	if (!SendAll(socket_fd, server_address, payload))
	{
		std::cerr << "sendto failed" << std::endl;
		CloseSocket(socket_fd);
		ShutdownSocketRuntime();
		return -1;
	}

	std::string response;
	sockaddr_in peer_address{};
	if (!ReceiveResponse(socket_fd, &response, &peer_address))
	{
		std::cerr << "wait response timeout or recv failed" << std::endl;
		CloseSocket(socket_fd);
		ShutdownSocketRuntime();
		return -1;
	}

	std::cout << "recv response from " << SocketAddressToString(peer_address) << std::endl;
	std::cout << response << std::endl;

	const std::string status = service::ExtractJsonString(response, "status");
	const std::string task_id = service::ExtractJsonString(response, "task_id");
	const std::string result_relpath = service::ExtractJsonString(response, "result_relpath");
	const std::string error = service::ExtractJsonString(response, "error");
	std::cout << "summary: task_id=" << task_id
		<< ", status=" << status
		<< ", result_relpath=" << result_relpath
		<< ", error=" << error << std::endl;

	CloseSocket(socket_fd);
	ShutdownSocketRuntime();
	return 0;
}
