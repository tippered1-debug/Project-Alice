#define CATCH_CONFIG_ENABLE_BENCHMARKING 1
#define CATCH_CONFIG_MAIN 1
#define CATCH_CONFIG_DISABLE_EXCEPTIONS 1
#include "catch2/catch.hpp"

#pragma comment(lib, "icu.lib")

TEST_CASE("Dummy test", "[dummy test instance]") {
	REQUIRE(1 + 1 == 2);
}
