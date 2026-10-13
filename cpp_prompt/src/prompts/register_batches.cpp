#include "prompt_entry.hpp"
#include "prompts.hpp"

// Batch registrars. Each batch is a self-contained translation unit so
// parallel sessions can add prompt files without touching shared files.
namespace prompt {

void register_batch_text1();
void register_batch_text2();
void register_batch_tools();
void register_batch_git();
void register_batch_stdin();
void register_batch_complex();
void register_batch_tse();

void register_batches() {
    register_batch_text1();
    register_batch_text2();
    register_batch_tools();
    register_batch_git();
    register_batch_stdin();
    register_batch_complex();
    register_batch_tse();
}

} // namespace prompt
