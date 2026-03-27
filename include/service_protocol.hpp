#ifndef END2END_INCLUDE_SERVICE_PROTOCOL_HPP
#define END2END_INCLUDE_SERVICE_PROTOCOL_HPP

#include <string>
#include <vector>

namespace service
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

	struct InputRequest
	{
		std::string device_id;
		std::string id;
		std::string image_path;
		std::string mileage;
		std::string mileage_sign;
		std::string task_id;
		std::string version;
		std::string image_base64;
		double img_physical = 1.0;
		double img_scaling = 1.0;
	};

	struct TaskState
	{
		std::string protocol_version;
		std::string task_id;
		std::string status;
		std::string request_relpath;
		std::string result_relpath;
		std::string reply_ip;
		int reply_port = 0;
		std::string error;
	};

	struct DefectRecord
	{
		std::string id;
		std::string type;
		int xmin = 0;
		int ymin = 0;
		int xmax = 0;
		int ymax = 0;
		int physical_ymin = 0;
		int physical_ymax = 0;
	};

	std::string ReadTextFile(const std::string& path);
	bool WriteTextFile(const std::string& path, const std::string& content);
	bool EnsureParentDirectory(const std::string& file_path);
	std::string ResolveBusinessTypeName(int label);
	int ComputePhysicalCoordinate(int coordinate, double scale);
	std::string ExtractJsonString(const std::string& json, const std::string& key);
	double ExtractJsonNumber(const std::string& json, const std::string& key, double default_value);
	SubmitRequest ParseSubmitRequest(const std::string& json);
	InputRequest ParseInputRequest(const std::string& json);
	TaskState ParseTaskState(const std::string& json);
	std::vector<unsigned char> Base64Decode(const std::string& value);
	std::string EscapeJsonString(const std::string& value);
	std::string BuildResultJson(const std::string& image_path,
		double img_physical,
		double img_scaling,
		int ikoujian_count,
		const std::vector<DefectRecord>& defects);
	std::string BuildTaskReplyJson(const std::string& cmd,
		const std::string& task_id,
		const std::string& status,
		const std::string& result_relpath,
		const std::string& error);
	std::string BuildTaskStateJson(const TaskState& state);
	std::string QuoteArg(const std::string& value);
}

#endif
