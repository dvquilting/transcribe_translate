// translate_contract_main.cpp - "tt_engine translate" as a program of its own, with
// a stand-in model (llm_stub.cpp): for test_cli_contract.cmake.
#include <string>
#include <vector>

int translate_main(const std::vector<std::string>& av);  // ../src/translate/cli_main.cpp

int main(int argc, char** argv) {
    std::vector<std::string> av{"translate"};
    for (int i = 1; i < argc; ++i) av.emplace_back(argv[i]);
    return translate_main(av);
}
