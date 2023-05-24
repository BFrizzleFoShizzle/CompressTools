#include <string>

void DebugLog(std::string message);
void ErrorLog(std::string message);

void SetDebugLogger(void(*newLogger)(const char* message));
void SetErrorLogger(void(*newLogger)(const char* message));