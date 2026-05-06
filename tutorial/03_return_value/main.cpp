#include <blog.h>

namespace {

using namespace std::chrono_literals;

// 一个可能失败的异步操作：尝试解析一个整数，模拟耗时
auto parse_int(std::string_view input)
    -> async::Task<std::expected<int, std::error_code>>
{
    co_await async::sleep_for(50ms);   // 模拟异步耗时
    co_return numeric_cast<int>(input);
}

auto run() -> async::Task<>
{
    // 成功路径
    auto r1 = co_await parse_int("42");
    if (r1)
        log::info("parsed: {}", *r1);

    // 失败路径
    auto r2 = co_await parse_int("abc");
    if (!r2)
        log::warning("error: {}", r2.error());
}

} // namespace

int main()
{
    async::run(run);
}
