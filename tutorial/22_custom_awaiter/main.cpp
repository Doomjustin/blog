#include <string>
#include <thread>

#include <blog.h>

using namespace std::chrono_literals;
using async::run_on_thread;

namespace {

// ----------------------------------------------------------------------------
// 模拟 CPU 密集型操作（此处以 bcrypt 风格的密码校验为例）
// ----------------------------------------------------------------------------
auto bcrypt_verify(std::string_view password, std::string_view stored_hash) -> bool
{
    log::info("[工作线程 {}] 正在进行 bcrypt 校验...", std::this_thread::get_id());
    std::this_thread::sleep_for(200ms); // 模拟真实的 bcrypt 耗时
    return ("$2b$" + std::string(password)) == stored_hash;
}

// ----------------------------------------------------------------------------
// 业务层：处理登录请求
// ----------------------------------------------------------------------------
auto handle_login(std::string username, std::string password) -> async::Task<>
{
    log::info("[IO 线程 {}] 收到登录请求: user={}", std::this_thread::get_id(), username);

    // 数据库里存的哈希（真实场景来自 co_await db.query(...)）
    std::string stored_hash = "$2b$" + password;

    // co_await 期间 IO 线程不阻塞，可继续服务其他连接
    auto result = co_await run_on_thread([pwd = password, hash = stored_hash]() {
        return bcrypt_verify(pwd, hash);
    });

    // 自动回到同一个 IO 线程
    if (!result)
        log::info("[IO 线程 {}] 操作取消: {}", std::this_thread::get_id(), result.error().message());
    else
        log::info("[IO 线程 {}] 验证结果: {}", std::this_thread::get_id(), *result ? "成功" : "失败");
}

} // namespace

int main()
{
    log::info("=== Demo: 将 CPU 密集型任务卸载到工作线程 ===");

    async::run(handle_login, std::string("alice"), std::string("secret123"));

    return EXIT_SUCCESS;
}
