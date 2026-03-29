#include <cassert>
#include <iostream>
#include <string>

#include "caller_client/file_utils.hpp"
#include "caller_client/protocol.hpp"

#ifdef _WIN32
#define EXPECTED_SHARED_PATH "Z:\\rail_share\\results\\outbox\\a.json"
#else
#define EXPECTED_SHARED_PATH "Z:/rail_share/results/outbox/a.json"
#endif

int main()
{
	caller_client::SubmitRequest request;
	request.cmd = "submit";
	request.protocol_version = "1.0";
	request.task_id = "task-1";
	request.request_relpath = "requests/2026/03/30/task-1.json";
	request.result_relpath = "results/2026/03/30/task-1_result.json";
	request.reply_ip = "192.168.145.1";
	request.reply_port = 9001;
	const std::string submit_json = caller_client::BuildSubmitJson(request);
	assert(submit_json.find("\"cmd\":\"submit\"") != std::string::npos);
	assert(submit_json.find("\"task_id\":\"task-1\"") != std::string::npos);
	assert(submit_json.find("\"reply_port\":9001") != std::string::npos);

	const std::string done_json = "{\"cmd\":\"done\",\"protocol_version\":\"1.0\",\"task_id\":\"task-1\",\"status\":\"success\",\"result_relpath\":\"results/2026/03/30/task-1_result.json\"}";
	const caller_client::TaskReply reply = caller_client::ParseTaskReply(done_json);
	assert(reply.cmd == "done");
	assert(reply.task_id == "task-1");
	assert(reply.status == "success");
	assert(reply.result_relpath == "results/2026/03/30/task-1_result.json");

	const std::string updated_json = caller_client::UpsertJsonString("{\"device_id\":\"dev-1\"}", "task_id", "task-1");
	assert(updated_json.find("\"task_id\":\"task-1\"") != std::string::npos);

	const std::string replaced_json = caller_client::UpsertJsonString("{\"task_id\":\"old\",\"device_id\":\"dev-1\"}", "task_id", "task-1");
	assert(replaced_json.find("\"task_id\":\"task-1\"") != std::string::npos);
	assert(replaced_json.find("\"task_id\":\"old\"") == std::string::npos);

	assert(caller_client::NormalizeRelativePath("requests\\inbox\\a.json") == "requests/inbox/a.json");
	assert(caller_client::NormalizeRelativePath("../bad.json").empty());
	assert(caller_client::ResolveSharedPath("Z:/rail_share", "results/outbox/a.json") == EXPECTED_SHARED_PATH);
	assert(caller_client::BuildRequestRelPath("task:1").find("requests/") == 0);
	assert(caller_client::BuildResultRelPath("task:1").find("results/") == 0);

	std::cout << "protocol_test passed" << std::endl;
	return 0;
}
