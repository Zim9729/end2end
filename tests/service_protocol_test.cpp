#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "service_protocol.hpp"

int main()
{
	{
		const std::string message = R"({"cmd":"submit","protocol_version":"1.0","task_id":"task-1","request_relpath":"requests/inbox/a.json","result_relpath":"results/outbox/a_result.json","reply_ip":"192.168.145.1","reply_port":9000})";
		const service::SubmitRequest request = service::ParseSubmitRequest(message);
		assert(request.cmd == "submit");
		assert(request.protocol_version == "1.0");
		assert(request.task_id == "task-1");
		assert(request.request_relpath == "requests/inbox/a.json");
		assert(request.result_relpath == "results/outbox/a_result.json");
		assert(request.reply_ip == "192.168.145.1");
		assert(request.reply_port == 9000);
	}

	{
		const std::string input = R"({"device_id":"dev-1","id":"record-1","imagePath":"images/1.jpg","mileage":"1.250","mileageSign":"K","task_id":"task-1","version":"1.0.0","img_physical":1.0,"img_scaling":1.5,"image":"aGVsbG8="})";
		const service::InputRequest request = service::ParseInputRequest(input);
		assert(request.device_id == "dev-1");
		assert(request.id == "record-1");
		assert(request.image_path == "images/1.jpg");
		assert(request.mileage == "1.250");
		assert(request.mileage_sign == "K");
		assert(request.task_id == "task-1");
		assert(request.version == "1.0.0");
		assert(request.img_physical == 1.0);
		assert(request.img_scaling == 1.5);
		assert(request.image_base64 == "aGVsbG8=");
	}

	{
		const std::vector<unsigned char> decoded = service::Base64Decode("aGVsbG8=");
		const std::string value(decoded.begin(), decoded.end());
		assert(value == "hello");
	}

	{
		assert(service::ResolveBusinessTypeName(0) == "XLBH-542");
		assert(service::ResolveBusinessTypeName(999) == "unknown");
		assert(service::ComputePhysicalCoordinate(947, 1.0) == 947);
		assert(service::ComputePhysicalCoordinate(100, 0.5) == 50);
		assert(service::ComputePhysicalCoordinate(100, 0.0) == 100);
	}

	{
		service::DefectRecord defect;
		defect.id = "defect-1";
		defect.type = "XLBH-542";
		defect.xmin = 10;
		defect.ymin = 20;
		defect.xmax = 30;
		defect.ymax = 40;
		defect.physical_ymin = 999;
		defect.physical_ymax = 999;
		const std::string json = service::BuildResultJson("images/1.jpg", 0.5, 1.5, 1, { defect });
		const std::size_t defects_pos = json.find("\"defects\"");
		const std::size_t ikoujian_pos = json.find("\"ikoujian_count\"");
		const std::size_t image_path_pos = json.find("\"imagePath\"");
		const std::size_t img_physical_pos = json.find("\"img_physical\"");
		const std::size_t img_scaling_pos = json.find("\"img_scaling\"");
		const std::size_t id_pos = json.find("\"id\"");
		const std::size_t physical_ymax_pos = json.find("\"physical_ymax\"");
		const std::size_t physical_ymin_pos = json.find("\"physical_ymin\"");
		const std::size_t type_pos = json.find("\"type\"");
		const std::size_t xmax_pos = json.find("\"xmax\"");
		const std::size_t xmin_pos = json.find("\"xmin\"");
		const std::size_t ymax_pos = json.find("\"ymax\"");
		const std::size_t ymin_pos = json.find("\"ymin\"");
		assert(defects_pos != std::string::npos);
		assert(ikoujian_pos != std::string::npos);
		assert(image_path_pos != std::string::npos);
		assert(img_physical_pos != std::string::npos);
		assert(img_scaling_pos != std::string::npos);
		assert(id_pos != std::string::npos);
		assert(physical_ymax_pos != std::string::npos);
		assert(physical_ymin_pos != std::string::npos);
		assert(type_pos != std::string::npos);
		assert(xmax_pos != std::string::npos);
		assert(xmin_pos != std::string::npos);
		assert(ymax_pos != std::string::npos);
		assert(ymin_pos != std::string::npos);
		assert(defects_pos < ikoujian_pos);
		assert(ikoujian_pos < image_path_pos);
		assert(image_path_pos < img_physical_pos);
		assert(img_physical_pos < img_scaling_pos);
		assert(id_pos < physical_ymax_pos);
		assert(physical_ymax_pos < physical_ymin_pos);
		assert(physical_ymin_pos < type_pos);
		assert(type_pos < xmax_pos);
		assert(xmax_pos < xmin_pos);
		assert(xmin_pos < ymax_pos);
		assert(ymax_pos < ymin_pos);
		assert(json.find("\"imagePath\": \"images/1.jpg\"") != std::string::npos);
		assert(json.find("\"ikoujian_count\": 1") != std::string::npos);
		assert(json.find("\"type\": \"XLBH-542\"") != std::string::npos);
		assert(json.find("\"physical_ymax\": 20") != std::string::npos);
		assert(json.find("\"physical_ymin\": 10") != std::string::npos);
	}

	{
		const std::string accepted = service::BuildTaskReplyJson("accepted", "task-1", "accepted", "results/outbox/a_result.json", "");
		assert(accepted.find("\"cmd\":\"accepted\"") != std::string::npos);
		assert(accepted.find("\"status\":\"accepted\"") != std::string::npos);
		assert(accepted.find("\"result_relpath\":\"results/outbox/a_result.json\"") != std::string::npos);

		const std::string done = service::BuildTaskReplyJson("done", "task-1", "failed", "results/outbox/a_result.json", "decode image failed");
		assert(done.find("\"cmd\":\"done\"") != std::string::npos);
		assert(done.find("\"error\":\"decode image failed\"") != std::string::npos);
	}

	{
		service::TaskState state;
		state.protocol_version = "1.0";
		state.task_id = "task-1";
		state.status = "working";
		state.request_relpath = "requests/inbox/a.json";
		state.result_relpath = "results/outbox/a_result.json";
		state.reply_ip = "192.168.145.1";
		state.reply_port = 9000;
		state.error = "";
		const std::string json = service::BuildTaskStateJson(state);
		const service::TaskState parsed = service::ParseTaskState(json);
		assert(parsed.protocol_version == "1.0");
		assert(parsed.task_id == "task-1");
		assert(parsed.status == "working");
		assert(parsed.request_relpath == "requests/inbox/a.json");
		assert(parsed.result_relpath == "results/outbox/a_result.json");
		assert(parsed.reply_ip == "192.168.145.1");
		assert(parsed.reply_port == 9000);
	}

	std::cout << "service_protocol_test passed" << std::endl;
	return 0;
}
