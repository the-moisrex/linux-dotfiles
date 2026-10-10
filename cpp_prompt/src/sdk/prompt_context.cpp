#include "prompt/sdk/prompt_context.hpp"
#include "prompt/core/fs.hpp"
#include "prompt/core/clipboard.hpp"

namespace prompt {

prompt_context make_base_context() noexcept {
    prompt_context ctx;
    ctx.git_root = fs::find_git_root().value_or(std::filesystem::path{});
    return ctx;
}

} // namespace prompt