#include "Logging.h"
#include <iostream>
namespace CompressTools
{
	static void DefaultDebugLogger(const char* message)
	{
		std::cout << message << std::endl;
	}

	static void DefaultErrorLogger(const char* message)
	{
		std::cout << "Error: " << message << std::endl;
	}

	static void(*DebugLogger)(const char* message) = DefaultDebugLogger;
	static void(*ErrorLogger)(const char* message) = DefaultErrorLogger;

	void DebugLog(std::string message)
	{
		DebugLogger(message.c_str());
	}

	void ErrorLog(std::string message)
	{
		ErrorLogger(message.c_str());
	}

	void SetDebugLogger(void(*newLogger)(const char* message))
	{
		DebugLogger = newLogger;
	}

	void SetErrorLogger(void(*newLogger)(const char* message))
	{
		ErrorLogger = newLogger;
	}
}