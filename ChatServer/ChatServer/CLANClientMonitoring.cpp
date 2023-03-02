#include "CLANClientMonitoring.h"

#include "logger.h"



/* 라이브러리 callback 함수 */
bool CLANClientMonitoring::OnRecv(CPacket& packet)
{
	return true;
}

void CLANClientMonitoring::OnError(const wchar_t* szError, ...)
{
	va_list vaList;
	va_start(vaList, szError);
	LOGGING_VALIST(LOGGING_LEVEL_ERROR, szError, vaList);
	va_end(vaList);
}

void CLANClientMonitoring::OnOutputDebug(const wchar_t* szError, ...)
{
	va_list vaList;
	va_start(vaList, szError);
	LOGGING_VALIST(LOGGING_LEVEL_DEBUG, szError, vaList);
	va_end(vaList);
}

void CLANClientMonitoring::OnOutputSystem(const wchar_t* szError, ...)
{
	va_list vaList;
	va_start(vaList, szError);
	LOGGING_VALIST(LOGGING_LEVEL_INFO, szError, vaList);
	va_end(vaList);
}