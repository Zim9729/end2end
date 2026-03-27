#include "udp_service.hpp"

#include <iostream>

int main(int argc, char** argv)
{
	if (argc >= 2)
	{
		const std::string mode{ argv[1] };
		if (mode == "--serve")
		{
			if (argc < 5)
			{
				std::cerr << "usage: end2end --serve <listen_port> <queue_dir> <shared_root>" << std::endl;
				return -1;
			}
			return RunUdpReceiver(std::stoi(argv[2]), argv[3], argv[4]);
		}
		if (mode == "--worker")
		{
			if (argc < 5)
			{
				std::cerr << "usage: end2end --worker <engine_file> <queue_dir> <shared_root>" << std::endl;
				return -1;
			}
			return RunWorkerLoop(argv[2], argv[3], argv[4]);
		}
	}
	if (argc < 3)
	{
		std::cerr << "usage: end2end <engine_file> <image_or_dir>" << std::endl;
		std::cerr << "   or: end2end --serve <listen_port> <queue_dir> <shared_root>" << std::endl;
		std::cerr << "   or: end2end --worker <engine_file> <queue_dir> <shared_root>" << std::endl;
		return -1;
	}
	return RunBatchInference(argv[1], argv[2]);
}
