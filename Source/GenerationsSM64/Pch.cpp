#include "Pch.h"

#ifndef _DEBUG 
namespace boost
{
	void throw_exception(class std::exception const&) {}
}
#endif