#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "app.h"

int main()
{
	App app;
	try
	{
		app.run();
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "Fatal error: %s\n", e.what());
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
