//
// Created by ubuntu on 1/10/23.
//

#ifndef YOLOV8_CSRC_END2END_INCLUDE_UTILS_H
#define YOLOV8_CSRC_END2END_INCLUDE_UTILS_H
#include <algorithm>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <assert.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "NvInfer.h"

#define CHECK(call)                                   \
do                                                    \
{                                                     \
    const cudaError_t error_code = call;              \
    if (error_code != cudaSuccess)                    \
    {                                                 \
        printf("CUDA Error:\n");                      \
        printf("    File:       %s\n", __FILE__);     \
        printf("    Line:       %d\n", __LINE__);     \
        printf("    Error code: %d\n", error_code);   \
        printf("    Error text: %s\n",                \
            cudaGetErrorString(error_code));          \
        exit(1);                                      \
    }                                                 \
} while (0)

class Logger : public nvinfer1::ILogger
{
public:
	nvinfer1::ILogger::Severity reportableSeverity;

	explicit Logger(nvinfer1::ILogger::Severity severity = nvinfer1::ILogger::Severity::kINFO) :
		reportableSeverity(severity)
	{
	}

	void log(nvinfer1::ILogger::Severity severity, const char* msg) noexcept override
	{
		if (severity > reportableSeverity)
		{
			return;
		}
		switch (severity)
		{
		case nvinfer1::ILogger::Severity::kINTERNAL_ERROR:
			std::cerr << "INTERNAL_ERROR: ";
			break;
		case nvinfer1::ILogger::Severity::kERROR:
			std::cerr << "ERROR: ";
			break;
		case nvinfer1::ILogger::Severity::kWARNING:
			std::cerr << "WARNING: ";
			break;
		case nvinfer1::ILogger::Severity::kINFO:
			std::cerr << "INFO: ";
			break;
		default:
			std::cerr << "VERBOSE: ";
			break;
		}
		std::cerr << msg << std::endl;
	}
};

inline int get_size_by_dims(const nvinfer1::Dims& dims)
{
	int size = 1;
	for (int i = 0; i < dims.nbDims; i++)
	{
		size *= dims.d[i];
	}
	return size;
}

inline int DataTypeToSize(const nvinfer1::DataType& dataType)
{
	switch (dataType)
	{
	case nvinfer1::DataType::kFLOAT:
		return sizeof(float);
	case nvinfer1::DataType::kHALF:
		return 2;
	case nvinfer1::DataType::kINT8:
		return sizeof(int8_t);
	case nvinfer1::DataType::kINT32:
		return sizeof(int32_t);
	case nvinfer1::DataType::kBOOL:
		return sizeof(bool);
	default:
		return sizeof(float);
	}
}

inline float clamp(const float val, const float minVal = 0.f, const float maxVal = 1280.f)
{
	assert(minVal <= maxVal);
	return std::min(maxVal, std::max(minVal, val));
}

inline bool IsPathExist(const std::string& path)
{
	if (
#ifdef _WIN32
		_access(path.c_str(), 0) == 0
#else
		access(path.c_str(), 0) == F_OK
#endif
		)
	{
		return true;
	}
	return false;
}

inline bool IsFile(const std::string& path)
{
	if (!IsPathExist(path))
	{
		printf("%s:%d %s not exist\n", __FILE__, __LINE__, path.c_str());
		return false;
	}
	struct stat buffer;
	if (stat(path.c_str(), &buffer) != 0)
	{
		return false;
	}
#ifdef _WIN32
	return (buffer.st_mode & _S_IFREG) != 0;
#else
	return S_ISREG(buffer.st_mode);
#endif
}

inline bool IsFolder(const std::string& path)
{
	if (!IsPathExist(path))
	{
		return false;
	}
	struct stat buffer;
	if (stat(path.c_str(), &buffer) != 0)
	{
		return false;
	}
#ifdef _WIN32
	return (buffer.st_mode & _S_IFDIR) != 0;
#else
	return S_ISDIR(buffer.st_mode);
#endif
}

#endif //YOLOV8_CSRC_END2END_INCLUDE_UTILS_H
