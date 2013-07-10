#pragma once

#include "location.hpp"

#include <string>
#include <exception>

struct ErrorAtLocation: public std::exception
{
	Location location;
	std::string error;

	ErrorAtLocation(const Location& location, const std::string& error): location(location), error(error)
	{
	}

    ~ErrorAtLocation() throw()
    {
    }

	virtual const char* what() const throw()
	{
		return error.c_str();
	}
};

#ifdef _MSC_VER
__declspec(noreturn)
#endif
void errorf(const Location& location, const char* format, ...);
