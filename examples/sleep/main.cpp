#include <cstdlib>

#include <blog.h>

namespace {

auto demo_sleep() -> async::Task<>
{
	log::info("before sleep");

    using namespace std::chrono_literals;
	co_await async::sleep_for(1s);
    
	log::info("after sleep");
}

} // namespace

int main()
{
	async::run(demo_sleep);
	return EXIT_SUCCESS;
}