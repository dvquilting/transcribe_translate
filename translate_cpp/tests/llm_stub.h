#pragma once
#include "llm.h"
namespace tl {
void reset_stub(const std::vector<Generation>& answers = {});
const std::vector<Prompt>& stub_prompts();
}
