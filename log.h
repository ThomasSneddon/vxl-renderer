#pragma once

#include "general_headers.h"

class logger
{
public:
	logger() = default;
	~logger() = default;

	static logger instance;
	static bool initialize();
	static void uninitialize();

	template<typename T>
	static void writelog(const T& arg)
	{
		if (!_logfile)
		{
			return;
		}

		_logfile << arg << std::endl;
	}

	template<typename T>
	logger& operator<<(const T& arg)
	{
		if (_logfile)
		{
			_logfile << arg;
#ifdef _DEBUG
			std::cout << arg;
#endif
		}
		return *this;
	}

private:
	static std::ofstream _logfile;
};

#define LOG(LEVEL) logger::instance<<#LEVEL" : "