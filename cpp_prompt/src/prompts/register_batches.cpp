#include "prompt/prompts/prompt_entry.hpp"

// Batch registrars. Each batch is a self-contained translation unit so
// parallel sessions can add prompt files without touching shared files.
namespace prompt {

void register_batch_text1() noexcept;
void register_batch_text2() noexcept;
void register_batch_tools() noexcept;
void register_batch_git() noexcept;
void register_batch_stdin() noexcept;
void register_batch_complex() noexcept;
void register_batch_tse() noexcept;

void register_batches() noexcept {
    register_batch_text1();
    register_batch_text2();
    register_batch_tools();
    register_batch_git();
    register_batch_stdin();
    register_batch_complex();
    register_batch_tse();
}

} // namespace prompt
