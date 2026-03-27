#include "udp_service.hpp"

#include "service_protocol.hpp"
#include "yolov8.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
	using SocketHandle = SOCKET;
	const SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
	using SocketHandle = int;
	const SocketHandle INVALID_SOCKET_HANDLE = -1;
#endif

	struct SocketRuntime
	{
		SocketRuntime()
		{
#ifdef _WIN32
			WSADATA wsa_data{};
			WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
		}

		~SocketRuntime()
		{
#ifdef _WIN32
			WSACleanup();
#endif
		}
	};

	void CloseSocket(SocketHandle socket_fd)
	{
#ifdef _WIN32
		closesocket(socket_fd);
#else
		close(socket_fd);
#endif
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

	std::string ParentPath(const std::string& path)
	{
		const std::size_t pos = path.find_last_of("\\/");
		if (pos == std::string::npos)
		{
			return "";
		}
		return path.substr(0, pos);
	}

	std::string BaseName(const std::string& path)
	{
		const std::size_t pos = path.find_last_of("\\/");
		if (pos == std::string::npos)
		{
			return path;
		}
		return path.substr(pos + 1);
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

	bool IsAbsolutePath(const std::string& value)
	{
		return (value.size() > 1 && value[1] == ':') || (!value.empty() && (value[0] == '/' || value[0] == '\\'));
	}

	bool ContainsParentTraversal(const std::string& value)
	{
		return value == ".." || value.find("../") != std::string::npos || value.find("/..") != std::string::npos;
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

	std::string ResolveSharedPath(const std::string& shared_root, const std::string& relpath)
	{
		return JoinPath(shared_root, NormalizeSlashes(relpath));
	}

	bool EnsureQueueLayout(const std::string& queue_dir)
	{
		return service::EnsureParentDirectory(JoinPath(queue_dir, "pending/.keep"))
			&& service::EnsureParentDirectory(JoinPath(queue_dir, "working/.keep"))
			&& service::EnsureParentDirectory(JoinPath(queue_dir, "done/.keep"))
			&& service::EnsureParentDirectory(JoinPath(queue_dir, "failed/.keep"))
			&& service::EnsureParentDirectory(JoinPath(queue_dir, "state/.keep"));
	}

	bool RenameFileAtomically(const std::string& from, const std::string& to)
	{
		service::EnsureParentDirectory(to);
		return std::rename(from.c_str(), to.c_str()) == 0;
	}

	std::string BuildSubmitJobJson(const service::SubmitRequest& request)
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

	bool SendUdpMessage(const std::string& ip, const int port, const std::string& payload)
	{
		if (ip.empty() || port <= 0)
		{
			return false;
		}
		SocketRuntime runtime;
		const SocketHandle socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (socket_fd == INVALID_SOCKET_HANDLE)
		{
			return false;
		}
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(static_cast<unsigned short>(port));
		if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) <= 0)
		{
			CloseSocket(socket_fd);
			return false;
		}
		const int send_result = static_cast<int>(sendto(socket_fd,
			payload.c_str(),
			static_cast<int>(payload.size()),
			0,
			reinterpret_cast<const sockaddr*>(&address),
			static_cast<int>(sizeof(address))));
		CloseSocket(socket_fd);
		return send_result >= 0;
	}

	std::string SocketIp(const sockaddr_in& address)
	{
		char buffer[INET_ADDRSTRLEN] = { 0 };
		if (inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer)) == nullptr)
		{
			return "";
		}
		return buffer;
	}

	std::string ResolveImagePath(const std::string& image_path, const std::string& request_file)
	{
		if (image_path.empty())
		{
			return "";
		}
		if (IsFile(image_path))
		{
			return image_path;
		}
		if (IsAbsolutePath(image_path))
		{
			return image_path;
		}
		return JoinPath(ParentPath(request_file), NormalizeSlashes(image_path));
	}

	bool DecodeImage(const service::InputRequest& input, const std::string& request_file, cv::Mat& image, std::string& error)
	{
		const std::string resolved_image_path = ResolveImagePath(input.image_path, request_file);
		if (!resolved_image_path.empty() && IsFile(resolved_image_path))
		{
			image = cv::imread(resolved_image_path);
			if (!image.empty())
			{
				return true;
			}
		}
		if (input.image_base64.empty())
		{
			error = "image data is empty";
			return false;
		}
		const std::vector<unsigned char> decoded = service::Base64Decode(input.image_base64);
		if (decoded.empty())
		{
			error = "decode image failed";
			return false;
		}
		image = cv::imdecode(decoded, cv::IMREAD_COLOR);
		if (image.empty())
		{
			error = "imdecode image failed";
			return false;
		}
		return true;
	}

	std::string GenerateDefectId(const std::string& task_id, const int index)
	{
		const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		return SanitizeFileName(task_id) + "_" + std::to_string(now) + "_" + std::to_string(index);
	}

	std::vector<service::DefectRecord> BuildDefects(const std::vector<Object>& objects,
		const std::string& task_id,
		double img_physical)
	{
		std::vector<service::DefectRecord> defects;
		defects.reserve(objects.size());
		for (std::size_t index = 0; index < objects.size(); ++index)
		{
			const Object& object = objects[index];
			service::DefectRecord defect;
			defect.id = GenerateDefectId(task_id, static_cast<int>(index));
			defect.type = service::ResolveBusinessTypeName(object.label);
			defect.xmin = static_cast<int>(std::round(object.rect.x));
			defect.ymin = static_cast<int>(std::round(object.rect.y));
			defect.xmax = static_cast<int>(std::round(object.rect.x + object.rect.width));
			defect.ymax = static_cast<int>(std::round(object.rect.y + object.rect.height));
			defect.physical_ymin = service::ComputePhysicalCoordinate(defect.ymin, img_physical);
			defect.physical_ymax = service::ComputePhysicalCoordinate(defect.ymax, img_physical);
			defects.push_back(defect);
		}
		return defects;
	}

	std::string BuildPendingPattern(const std::string& queue_dir)
	{
		return JoinPath(queue_dir, "pending/*.json");
	}

	std::string BuildStateFilePath(const std::string& queue_dir, const std::string& task_id)
	{
		return JoinPath(queue_dir, JoinPath("state", SanitizeFileName(task_id) + ".json"));
	}

	bool ReadTaskState(const std::string& queue_dir, const std::string& task_id, service::TaskState& state)
	{
		const std::string json = service::ReadTextFile(BuildStateFilePath(queue_dir, task_id));
		if (json.empty())
		{
			return false;
		}
		state = service::ParseTaskState(json);
		return !state.task_id.empty();
	}

	bool WriteTaskState(const std::string& queue_dir, const service::TaskState& state)
	{
		return service::WriteTextFile(BuildStateFilePath(queue_dir, state.task_id), service::BuildTaskStateJson(state));
	}

	service::TaskState MakeStateFromRequest(const service::SubmitRequest& request, const std::string& status, const std::string& error)
	{
		service::TaskState state;
		state.protocol_version = request.protocol_version.empty() ? "1.0" : request.protocol_version;
		state.task_id = request.task_id;
		state.status = status;
		state.request_relpath = request.request_relpath;
		state.result_relpath = request.result_relpath;
		state.reply_ip = request.reply_ip;
		state.reply_port = request.reply_port;
		state.error = error;
		return state;
	}

	std::string BuildReplyForState(const service::TaskState& state)
	{
		if (state.status == "success" || state.status == "failed")
		{
			return service::BuildTaskReplyJson("done", state.task_id, state.status, state.result_relpath, state.error);
		}
		return service::BuildTaskReplyJson("accepted", state.task_id, "accepted", state.result_relpath, "");
	}

	bool ProcessTask(YOLOv8& yolov8,
		const service::SubmitRequest& submit_request,
		const std::string& shared_root,
		std::string& error)
	{
		const std::string request_file = ResolveSharedPath(shared_root, submit_request.request_relpath);
		const std::string result_file = ResolveSharedPath(shared_root, submit_request.result_relpath);
		const std::string input_json = service::ReadTextFile(request_file);
		if (input_json.empty())
		{
			error = "request file not found or empty";
			return false;
		}
		const service::InputRequest input_request = service::ParseInputRequest(input_json);
		cv::Mat image;
		if (!DecodeImage(input_request, request_file, image, error))
		{
			return false;
		}
		yolov8.copy_from_Mat(image);
		yolov8.infer();
		std::vector<Object> objects;
		yolov8.postprocess(objects);
		const std::vector<service::DefectRecord> defects = BuildDefects(objects,
			submit_request.task_id,
			input_request.img_physical);
		const std::string result_image_path = !input_request.image_path.empty() ? input_request.image_path : submit_request.request_relpath;
		const std::string result_json = service::BuildResultJson(result_image_path,
			input_request.img_physical,
			input_request.img_scaling,
			static_cast<int>(defects.size()),
			defects);
		if (!service::WriteTextFile(result_file, result_json))
		{
			error = "write result file failed";
			return false;
		}
		return true;
	}
}

int RunBatchInference(const std::string& engine_file_path, const std::string& path)
{
	cudaSetDevice(DEVICE);
	std::vector<cv::String> image_path_list;
	bool is_video = false;
	if (IsFile(path))
	{
		const std::string suffix = path.substr(path.find_last_of('.') + 1);
		if (suffix == "jpg" || suffix == "png" || suffix == "jpeg")
		{
			image_path_list.push_back(path);
		}
		else if (suffix == "mp4")
		{
			is_video = true;
		}
	}
	else if (IsFolder(path))
	{
		cv::glob(path + "/*.jpg", image_path_list);
		std::vector<cv::String> png_list;
		cv::glob(path + "/*.png", png_list);
		image_path_list.insert(image_path_list.end(), png_list.begin(), png_list.end());
	}
	auto* yolov8 = new YOLOv8(engine_file_path);
	yolov8->make_pipe(true);
	cv::Mat res;
	if (is_video)
	{
		cv::VideoCapture cap(path);
		cv::Mat image;
		if (!cap.isOpened())
		{
			delete yolov8;
			return -1;
		}
		const double fp = cap.get(cv::CAP_PROP_FPS);
		const int fps = static_cast<int>(std::round(1000.0 / fp));
		while (cap.read(image))
		{
			auto start = std::chrono::system_clock::now();
			yolov8->copy_from_Mat(image);
			yolov8->infer();
			std::vector<Object> objects;
			yolov8->postprocess(objects);
			draw_objects(image, res, objects);
			auto end = std::chrono::system_clock::now();
			auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.f;
			cv::imshow("result", res);
			printf("cost %2.4f ms\n", cost);
			if (cv::waitKey(fps) == 'q')
			{
				break;
			}
		}
	}
	else
	{
		for (const auto& image_path : image_path_list)
		{
			const cv::Mat image = cv::imread(image_path);
			yolov8->copy_from_Mat(image);
			auto start = std::chrono::system_clock::now();
			yolov8->infer();
			auto end = std::chrono::system_clock::now();
			auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.f;
			printf("infer %-20s\tcost %2.4f ms\n", image_path.c_str(), cost);
			std::vector<Object> objects;
			yolov8->postprocess(objects);
			draw_objects(image, res, objects);
			cv::imwrite("result.jpg", res);
		}
	}
	cv::destroyAllWindows();
	delete yolov8;
	return 0;
}

int RunUdpReceiver(const int listen_port, const std::string& queue_dir, const std::string& shared_root)
{
	SocketRuntime runtime;
	if (!EnsureQueueLayout(queue_dir))
	{
		std::cerr << "queue dir create failed: " << queue_dir << std::endl;
		return -1;
	}
	if (!IsFolder(shared_root))
	{
		std::cerr << "shared root not found: " << shared_root << std::endl;
		return -1;
	}
	const SocketHandle socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_fd == INVALID_SOCKET_HANDLE)
	{
		std::cerr << "create udp socket failed" << std::endl;
		return -1;
	}
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(static_cast<unsigned short>(listen_port));
	if (bind(socket_fd, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) < 0)
	{
		std::cerr << "bind udp socket failed" << std::endl;
		CloseSocket(socket_fd);
		return -1;
	}
	std::cout << "udp receiver listen on port " << listen_port << std::endl;
	while (true)
	{
		char buffer[65535] = { 0 };
		sockaddr_in peer{};
#ifdef _WIN32
		int peer_length = sizeof(peer);
#else
		socklen_t peer_length = sizeof(peer);
#endif
		const int recv_length = static_cast<int>(recvfrom(socket_fd,
			buffer,
			static_cast<int>(sizeof(buffer) - 1),
			0,
			reinterpret_cast<sockaddr*>(&peer),
			&peer_length));
		if (recv_length <= 0)
		{
			continue;
		}

		service::SubmitRequest request = service::ParseSubmitRequest(std::string(buffer, buffer + recv_length));
		if (request.task_id.empty())
		{
			request.task_id = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
		}
		if (request.cmd.empty())
		{
			request.cmd = "submit";
		}
		if (request.protocol_version.empty())
		{
			request.protocol_version = "1.0";
		}
		if (request.reply_ip.empty())
		{
			request.reply_ip = SocketIp(peer);
		}
		if (request.reply_port <= 0)
		{
			request.reply_port = ntohs(peer.sin_port);
		}
		request.request_relpath = NormalizeRelativePath(request.request_relpath);
		request.result_relpath = NormalizeRelativePath(request.result_relpath);
		if (request.result_relpath.empty())
		{
			request.result_relpath = "results/" + SanitizeFileName(request.task_id) + "_result.json";
		}

		if (request.request_relpath.empty())
		{
			SendUdpMessage(request.reply_ip,
				request.reply_port,
				service::BuildTaskReplyJson("done", request.task_id, "failed", request.result_relpath, "request_relpath is empty or invalid"));
			continue;
		}

		service::TaskState existing_state;
		if (ReadTaskState(queue_dir, request.task_id, existing_state))
		{
			SendUdpMessage(request.reply_ip, request.reply_port, BuildReplyForState(existing_state));
			continue;
		}

		service::TaskState state = MakeStateFromRequest(request, "accepted", "");
		if (!WriteTaskState(queue_dir, state))
		{
			SendUdpMessage(request.reply_ip,
				request.reply_port,
				service::BuildTaskReplyJson("done", request.task_id, "failed", request.result_relpath, "write state file failed"));
			continue;
		}

		const std::string file_name = SanitizeFileName(request.task_id) + "_" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ".json";
		const std::string job_file = JoinPath(queue_dir, JoinPath("pending", file_name));
		if (!service::WriteTextFile(job_file, BuildSubmitJobJson(request)))
		{
			state.status = "failed";
			state.error = "write job file failed";
			WriteTaskState(queue_dir, state);
			SendUdpMessage(request.reply_ip,
				request.reply_port,
				service::BuildTaskReplyJson("done", request.task_id, "failed", request.result_relpath, state.error));
			continue;
		}

		state.status = "pending";
		WriteTaskState(queue_dir, state);
		SendUdpMessage(request.reply_ip,
			request.reply_port,
			service::BuildTaskReplyJson("accepted", request.task_id, "accepted", request.result_relpath, ""));
		std::cout << "accepted task " << request.task_id << " -> " << job_file << std::endl;
	}
}

int RunWorkerLoop(const std::string& engine_file_path, const std::string& queue_dir, const std::string& shared_root)
{
	if (!EnsureQueueLayout(queue_dir))
	{
		std::cerr << "queue dir create failed: " << queue_dir << std::endl;
		return -1;
	}
	if (!IsFolder(shared_root))
	{
		std::cerr << "shared root not found: " << shared_root << std::endl;
		return -1;
	}
	cudaSetDevice(DEVICE);
	YOLOv8 yolov8(engine_file_path);
	yolov8.make_pipe(true);
	std::cout << "worker started, queue dir: " << queue_dir << std::endl;
	while (true)
	{
		std::vector<cv::String> pending_files;
		cv::glob(BuildPendingPattern(queue_dir), pending_files, false);
		if (pending_files.empty())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}
		std::sort(pending_files.begin(), pending_files.end());
		for (const auto& pending_file : pending_files)
		{
			const std::string working_file = JoinPath(queue_dir, JoinPath("working", BaseName(pending_file)));
			if (!RenameFileAtomically(pending_file, working_file))
			{
				continue;
			}
			const service::SubmitRequest request = service::ParseSubmitRequest(service::ReadTextFile(working_file));
			service::TaskState state;
			if (!ReadTaskState(queue_dir, request.task_id, state))
			{
				state = MakeStateFromRequest(request, "working", "");
			}
			state.status = "working";
			state.error.clear();
			WriteTaskState(queue_dir, state);

			std::string error;
			const bool ok = ProcessTask(yolov8, request, shared_root, error);
			const std::string target_file = JoinPath(queue_dir, JoinPath(ok ? "done" : "failed", BaseName(working_file)));
			RenameFileAtomically(working_file, target_file);

			state.status = ok ? "success" : "failed";
			state.error = ok ? "" : error;
			WriteTaskState(queue_dir, state);

			SendUdpMessage(state.reply_ip,
				state.reply_port,
				service::BuildTaskReplyJson("done", request.task_id, state.status, request.result_relpath, state.error));
			std::cout << (ok ? "processed task " : "failed task ") << request.task_id;
			if (!ok)
			{
				std::cout << " error: " << error;
			}
			std::cout << std::endl;
		}
	}
}
