#ifndef COMPILER_GENERATE_CODE_C_CODE_H_
#define COMPILER_GENERATE_CODE_C_CODE_H_

#include <string>

namespace tree_sitter {

class LexTable;
class LexicalGrammar;
class ParseTable;
class SyntaxGrammar;

namespace generate_code {

std::string c_code(std::string, const ParseTable &, const LexTable &,
                   const SyntaxGrammar &, const LexicalGrammar &);

}  // namespace generate_code
}  // namespace tree_sitter

#endif  // COMPILER_GENERATE_CODE_C_CODE_H_
