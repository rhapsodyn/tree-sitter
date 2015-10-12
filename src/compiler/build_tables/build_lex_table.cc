#include "compiler/build_tables/build_lex_table.h"
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "compiler/build_tables/lex_conflict_manager.h"
#include "compiler/build_tables/get_completion_status.h"
#include "compiler/build_tables/get_metadata.h"
#include "compiler/build_tables/lex_item.h"
#include "compiler/parse_table.h"
#include "compiler/lexical_grammar.h"
#include "compiler/rules/built_in_symbols.h"
#include "compiler/rules/choice.h"
#include "compiler/rules/metadata.h"
#include "compiler/rules/repeat.h"
#include "compiler/rules/seq.h"
#include "compiler/rules/blank.h"

namespace tree_sitter {
namespace build_tables {

using std::make_shared;
using std::map;
using std::set;
using std::string;
using std::unordered_map;
using std::vector;
using rules::CharacterSet;
using rules::Symbol;

class LexTableBuilder {
  const LexicalGrammar lex_grammar;
  const LexConflictManager conflict_manager;
  ParseTable *parse_table;
  unordered_map<const LexItemSet, LexStateId, LexItemSet::Hash> lex_state_ids;
  LexTable lex_table;
  vector<rule_ptr> separator_rules;

 public:
  LexTableBuilder(ParseTable *parse_table, const LexicalGrammar &lex_grammar)
      : lex_grammar(lex_grammar),
        conflict_manager(lex_grammar),
        parse_table(parse_table) {
    for (const rule_ptr &rule : lex_grammar.separators)
      separator_rules.push_back(rules::Repeat::build(rule));
    separator_rules.push_back(rules::Blank::build());
  }

  LexTable build() {
    for (ParseState &parse_state : parse_table->states) {
      LexItemSet item_set = build_lex_item_set(parse_state.expected_inputs());
      parse_state.lex_state_id = add_lex_state(item_set);
    }

    LexItemSet error_item_set = build_lex_item_set(parse_table->symbols);
    populate_lex_state(error_item_set, LexTable::ERROR_STATE_ID);

    return lex_table;
  }

 private:
  LexItemSet build_lex_item_set(const set<Symbol> &symbols) {
    LexItemSet result;
    for (const Symbol &symbol : symbols) {
      vector<rule_ptr> rules;
      if (symbol == rules::ERROR()) {
        continue;
      } else if (symbol == rules::END_OF_INPUT()) {
        rules.push_back(CharacterSet().include(0).copy());
      } else if (symbol.is_token) {
        rule_ptr rule = lex_grammar.variables[symbol.index].rule;
        auto choice = rule->as<rules::Choice>();
        if (choice)
          for (const rule_ptr &element : choice->elements)
            rules.push_back(element);
        else
          rules.push_back(rule);
      }

      for (const rule_ptr &rule : rules)
        for (const rule_ptr &separator_rule : separator_rules)
          result.entries.insert(LexItem(
            symbol, rules::Seq::build({
                      rules::Metadata::build(
                        separator_rule,
                        {
                          { rules::START_TOKEN, 1 }, { rules::PRECEDENCE, -1 },
                        }),
                      rule,
                    })));
    }

    return result;
  }

  LexStateId add_lex_state(const LexItemSet &item_set) {
    const auto &pair = lex_state_ids.find(item_set);
    if (pair == lex_state_ids.end()) {
      LexStateId state_id = lex_table.add_state();
      lex_state_ids[item_set] = state_id;
      populate_lex_state(item_set, state_id);
      return state_id;
    } else {
      return pair->second;
    }
  }

  void populate_lex_state(const LexItemSet &item_set, LexStateId state_id) {
    add_accept_token_actions(item_set, state_id);
    add_advance_actions(item_set, state_id);
    add_token_start(item_set, state_id);
  }

  void add_advance_actions(const LexItemSet &item_set, LexStateId state_id) {
    for (const auto &transition : item_set.transitions()) {
      const CharacterSet &rule = transition.first;
      const LexItemSet &new_item_set = transition.second;
      LexStateId new_state_id = add_lex_state(new_item_set);
      auto action = LexAction::Advance(
        new_state_id, precedence_range_for_item_set(new_item_set));
      if (conflict_manager.resolve(action,
                                   lex_table.state(state_id).default_action))
        lex_table.state(state_id).actions[rule] = action;
    }
  }

  void add_accept_token_actions(const LexItemSet &item_set, LexStateId state_id) {
    for (const LexItem &item : item_set.entries) {
      CompletionStatus completion_status = get_completion_status(item.rule);
      if (completion_status.is_done) {
        auto current_action = lex_table.state(state_id).default_action;
        auto new_action =
          LexAction::Accept(item.lhs, completion_status.precedence);
        if (conflict_manager.resolve(new_action, current_action))
          lex_table.state(state_id).default_action = new_action;
      }
    }
  }

  void add_token_start(const LexItemSet &item_set, LexStateId state_id) {
    for (const auto &item : item_set.entries)
      if (item.is_token_start())
        lex_table.state(state_id).is_token_start = true;
  }

  PrecedenceRange precedence_range_for_item_set(const LexItemSet &item_set) const {
    PrecedenceRange result;
    for (const auto &item : item_set.entries) {
      auto precedence_range = get_metadata(item.rule, rules::PRECEDENCE);
      result.add(precedence_range.min);
      result.add(precedence_range.max);
    }
    return result;
  }
};

LexTable build_lex_table(ParseTable *table, const LexicalGrammar &grammar) {
  return LexTableBuilder(table, grammar).build();
}

}  // namespace build_tables
}  // namespace tree_sitter
