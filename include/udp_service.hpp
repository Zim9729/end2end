#ifndef END2END_INCLUDE_UDP_SERVICE_HPP
#define END2END_INCLUDE_UDP_SERVICE_HPP

#include <string>

int RunBatchInference(const std::string& engine_file_path, const std::string& path);
int RunUdpReceiver(int listen_port, const std::string& queue_dir, const std::string& shared_root);
int RunWorkerLoop(const std::string& engine_file_path, const std::string& queue_dir, const std::string& shared_root);

#endif
