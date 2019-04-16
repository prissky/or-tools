// Copyright 2010-2018 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ortools/sat/table.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "ortools/base/int_type.h"
#include "ortools/base/logging.h"
#include "ortools/base/map_util.h"
#include "ortools/base/stl_util.h"
#include "ortools/sat/sat_base.h"
#include "ortools/sat/sat_solver.h"
#include "ortools/util/sorted_interval_list.h"

namespace operations_research {
namespace sat {

namespace {

// Converts the vector representation returned by FullDomainEncoding() to a map.
absl::flat_hash_map<IntegerValue, Literal> GetEncoding(IntegerVariable var,
                                                       Model* model) {
  absl::flat_hash_map<IntegerValue, Literal> encoding;
  IntegerEncoder* encoder = model->GetOrCreate<IntegerEncoder>();
  for (const auto& entry : encoder->FullDomainEncoding(var)) {
    encoding[entry.value] = entry.literal;
  }
  return encoding;
}

// Add the implications and clauses to link one column of a table to the Literal
// controling if the lines are possible or not. The column has the given values,
// and the Literal of the column variable can be retrieved using the encoding
// map. Thew tuples_with_any vector provides a list of line_literals that will
// support any value.
void ProcessOneColumn(
    const std::vector<Literal>& line_literals,
    const std::vector<IntegerValue>& values,
    const absl::flat_hash_map<IntegerValue, Literal>& encoding,
    const std::vector<Literal>& tuples_with_any, Model* model) {
  CHECK_EQ(line_literals.size(), values.size());
  absl::flat_hash_map<IntegerValue, std::vector<Literal>>
      value_to_list_of_line_literals;

  // If a value is false (i.e not possible), then the tuple with this value
  // is false too (i.e not possible). Conversely, if the tuple is selected,
  // the value must be selected.
  for (int i = 0; i < values.size(); ++i) {
    const IntegerValue v = values[i];
    if (!encoding.contains(v)) {
      model->Add(ClauseConstraint({line_literals[i].Negated()}));
    } else {
      value_to_list_of_line_literals[v].push_back(line_literals[i]);
      model->Add(Implication(line_literals[i], gtl::FindOrDie(encoding, v)));
    }
  }

  // If all the tuples containing a value are false, then this value must be
  // false too.
  for (const auto& entry : value_to_list_of_line_literals) {
    std::vector<Literal> clause = entry.second;
    clause.insert(clause.end(), tuples_with_any.begin(), tuples_with_any.end());
    clause.push_back(gtl::FindOrDie(encoding, entry.first).Negated());
    model->Add(ClauseConstraint(clause));
  }
}

}  // namespace

void CompressTuples(const std::vector<int64>& domain_sizes, int64 any_value,
                    std::vector<std::vector<int64>>* tuples) {
  if (tuples->empty()) return;

  // Remove duplicates if any.
  gtl::STLSortAndRemoveDuplicates(tuples);

  const int num_vars = (*tuples)[0].size();

  std::vector<int> to_remove;
  for (int i = 0; i < num_vars; ++i) {
    const int domain_size = domain_sizes[i];
    if (domain_size == 1) continue;
    absl::flat_hash_map<const std::vector<int64>, std::vector<int>>
        masked_tuples_to_indices;
    for (int t = 0; t < tuples->size(); ++t) {
      CHECK_NE((*tuples)[t][i], any_value);
      std::vector<int64> masked_copy;
      masked_copy.reserve(num_vars - 1);
      for (int j = 0; j < num_vars; ++j) {
        if (i == j) continue;
        masked_copy.push_back((*tuples)[t][j]);
      }
      masked_tuples_to_indices[masked_copy].push_back(t);
    }
    to_remove.clear();
    for (const auto& it : masked_tuples_to_indices) {
      if (it.second.size() != domain_size) continue;
      (*tuples)[it.second.front()][i] = any_value;
      to_remove.insert(to_remove.end(), it.second.begin() + 1, it.second.end());
    }
    std::sort(to_remove.begin(), to_remove.end(), std::greater<int>());
    for (const int t : to_remove) {
      (*tuples)[t] = tuples->back();
      tuples->pop_back();
    }
  }
}

// Makes a static decomposition of a table constraint into clauses.
// This uses an auxiliary vector of Literals tuple_literals.
// For every column col, and every value val of that column,
// the decomposition uses clauses corresponding to the equivalence:
// (\/_{row | tuples[row][col] = val} tuple_literals[row]) <=> (vars[col] = val)
void AddTableConstraint(const std::vector<IntegerVariable>& vars,
                        std::vector<std::vector<int64>> tuples, Model* model) {
  const int n = vars.size();
  IntegerTrail* integer_trail = model->GetOrCreate<IntegerTrail>();
  const int num_original_tuples = tuples.size();

  // Compute the set of possible values for each variable (from the table).
  // Remove invalid tuples along the way.
  std::vector<absl::flat_hash_set<int64>> values_per_var(n);
  int index = 0;
  while (index < tuples.size()) {
    bool remove = false;
    for (int i = 0; i < n; ++i) {
      const int64 value = tuples[index][i];
      if (!values_per_var[i].contains(value) /* cached */ &&
          !integer_trail->InitialVariableDomain(vars[i]).Contains(value)) {
        remove = true;
        break;
      }
    }
    if (remove) {
      tuples[index] = tuples.back();
      tuples.pop_back();
    } else {
      for (int i = 0; i < n; ++i) {
        values_per_var[i].insert(tuples[index][i]);
      }
      index++;
    }
  }
  const int num_valid_tuples = tuples.size();

  if (tuples.empty()) {
    model->GetOrCreate<SatSolver>()->NotifyThatModelIsUnsat();
    return;
  }

  // Detect the case when the first n-1 columns are all different.
  // This encodes the implication table (tuple of size n - 1) implies value.
  absl::flat_hash_set<std::vector<int64>> prefixes;
  std::vector<int64> prefix(n);
  for (const std::vector<int64>& tuple : tuples) {
    prefix = tuple;
    prefix.pop_back();
    prefixes.insert(prefix);
  }
  const int num_prefix_tuples = prefixes.size();
  // Compute the maximum number of such prefix tuples.
  double max_num_prefix_tuples = 1.0;
  for (int i = 0; i + 1 < n; ++i) {
    max_num_prefix_tuples *= values_per_var[i].size();
  }
  // Detect if prefix tuples are all different.
  const bool prefixes_are_all_different = num_prefix_tuples == num_valid_tuples;

  // Compress tuples.
  const int64 any_value = kint64min;
  std::vector<int64> domain_sizes;
  for (int i = 0; i < n; ++i) {
    domain_sizes.push_back(values_per_var[i].size());
  }
  CompressTuples(domain_sizes, any_value, &tuples);
  const int num_compressed_tuples = tuples.size();

  if (VLOG_IS_ON(2)) {
    std::string message = absl::StrCat(
        "Table: ", n, " variables, original tuples = ", num_original_tuples);
    if (num_valid_tuples != num_original_tuples) {
      absl::StrAppend(&message, ", valid tuples = ", num_valid_tuples);
    }
    if (prefixes_are_all_different) {
      if (num_prefix_tuples < max_num_prefix_tuples) {
        absl::StrAppend(&message, ", partial prefix = ", num_prefix_tuples, "/",
                        max_num_prefix_tuples);
      } else {
        absl::StrAppend(&message, ", full prefix = true");
      }
    } else {
      absl::StrAppend(&message, ", num prefix tuples = ", prefixes.size());
    }
    if (num_compressed_tuples != num_valid_tuples) {
      absl::StrAppend(&message,
                      ", compressed tuples = ", num_compressed_tuples);
    }
    LOG(INFO) << message;
  }
  // Domains have be propagated. Fully encode variables.
  std::vector<absl::flat_hash_map<IntegerValue, Literal>> encodings(n);
  for (int i = 0; i < n; ++i) {
    const std::vector<int64> reached_values(values_per_var[i].begin(),
                                            values_per_var[i].end());
    integer_trail->UpdateInitialDomain(vars[i],
                                       Domain::FromValues(reached_values));
    if (values_per_var.size() > 1) {
      model->Add(FullyEncodeVariable(vars[i]));
      encodings[i] = GetEncoding(vars[i], model);
    }
  }

  // Create one Boolean variable per tuple to indicate if it can still be
  // selected or not. Note that we don't enforce exactly one tuple to be
  // selected because these variables are just used by this constraint, so
  // only the information "can't be selected" is important.
  //
  // TODO(user): If a value in one column is unique, we don't need to create
  // a new BooleanVariable corresponding to this line since we can use the one
  // corresponding to this value in that column.
  //
  // Note that if there is just one tuple, there is no need to create such
  // variables since they are not used.
  std::vector<Literal> tuple_literals;
  tuple_literals.reserve(tuples.size());
  if (tuples.size() == 1) {
    tuple_literals.push_back(Literal(kTrueLiteralIndex));
  } else if (tuples.size() == 2) {
    tuple_literals.emplace_back(model->Add(NewBooleanVariable()), true);
    tuple_literals.emplace_back(tuple_literals[0].Negated());
  } else if (tuples.size() > 2) {
    for (int i = 0; i < tuples.size(); ++i) {
      tuple_literals.emplace_back(model->Add(NewBooleanVariable()), true);
    }
    model->Add(ClauseConstraint(tuple_literals));
  }

  // Fully encode the variables using all the values appearing in the tuples.
  std::vector<Literal> active_tuple_literals;
  std::vector<IntegerValue> active_values;
  std::vector<Literal> any_tuple_literals;
  for (int i = 0; i < n; ++i) {
    if (values_per_var[i].size() == 1) continue;

    active_tuple_literals.clear();
    active_values.clear();
    any_tuple_literals.clear();
    for (int j = 0; j < tuple_literals.size(); ++j) {
      const int64 v = tuples[j][i];

      if (v == any_value) {
        any_tuple_literals.push_back(tuple_literals[j]);
      } else {
        active_tuple_literals.push_back(tuple_literals[j]);
        active_values.push_back(IntegerValue(v));
      }
    }

    if (!active_tuple_literals.empty()) {
      ProcessOneColumn(active_tuple_literals, active_values, encodings[i],
                       any_tuple_literals, model);
    }
  }

  if (prefixes_are_all_different) {
    // For each tuple, we add a clause prefix => last value.
    std::vector<Literal> clause;
    for (int j = 0; j < tuples.size(); ++j) {
      clause.clear();
      bool tuple_is_valid = true;
      for (int i = 0; i + 1 < n; ++i) {
        // Ignore fixed variables.
        if (values_per_var[i].size() == 1) continue;

        const int64 v = tuples[j][i];
        // Ignored 'any' created during compression.
        if (v == any_value) continue;

        // Check the validity of the tuple.
        const IntegerValue value(v);
        if (!encodings[i].contains(value)) {
          tuple_is_valid = false;
          break;
        }
        clause.push_back(gtl::FindOrDie(encodings[i], value).Negated());
      }

      // Add the target of the implication.
      const IntegerValue target_value = IntegerValue(tuples[j][n - 1]);
      if (tuple_is_valid && encodings[n - 1].contains(target_value)) {
        const Literal target_literal =
            gtl::FindOrDie(encodings[n - 1], target_value);
        clause.push_back(target_literal);
        model->Add(ClauseConstraint(clause));
      }
    }
  }

  if (prefixes_are_all_different && num_prefix_tuples < max_num_prefix_tuples &&
      max_num_prefix_tuples < 4 * num_prefix_tuples) {
    // If we have a table of 'unique prefix' => value tuples.
    // This table will likely not be negated, as the density of tuples will be
    // less than 1 / size of the domain of the last variable.
    // Still, just on the prefix part, it can be close to complete.
    // For each missing prefix, we can add their negation as a valid clause.
    // For this, we complement the prefix tuples, and add a negative table
    // constraint on these.
    std::vector<std::vector<int64>> var_domains(n - 1);
    for (int j = 0; j + 1 < n; ++j) {
      var_domains[j].assign(values_per_var[j].begin(), values_per_var[j].end());
      std::sort(var_domains[j].begin(), var_domains[j].end());
    }
    std::vector<std::vector<int64>> negated_tuples;
    std::vector<int64> tmp_tuple;
    for (int i = 0; i < max_num_prefix_tuples; ++i) {
      tmp_tuple.assign(n - 1, 0);
      int index = i;
      for (int j = 0; j + 1 < n; ++j) {
        tmp_tuple[j] = var_domains[j][index % var_domains[j].size()];
        index /= var_domains[j].size();
      }
      if (!prefixes.contains(tmp_tuple)) {
        negated_tuples.push_back(tmp_tuple);
      }
    }
    std::vector<IntegerVariable> prefix_vars = vars;
    prefix_vars.pop_back();
    VLOG(2) << " . add negated table with " << negated_tuples.size()
            << " tuples";
    AddNegatedTableConstraint(prefix_vars, negated_tuples, model);
  }
}

void AddNegatedTableConstraint(const std::vector<IntegerVariable>& vars,
                               std::vector<std::vector<int64>> tuples,
                               Model* model) {
  const int n = vars.size();
  IntegerTrail* const integer_trail = model->GetOrCreate<IntegerTrail>();
  IntegerEncoder* const integer_encoder = model->GetOrCreate<IntegerEncoder>();

  // Remove unreachable tuples.
  int index = 0;
  while (index < tuples.size()) {
    bool remove = false;
    for (int i = 0; i < n; ++i) {
      if (!integer_trail->InitialVariableDomain(vars[i]).Contains(
              tuples[index][i])) {
        remove = true;
        break;
      }
    }
    if (remove) {
      tuples[index] = tuples.back();
      tuples.pop_back();
    } else {
      index++;
    }
  }

  if (tuples.empty()) {
    return;
  }

  // Compress tuples.
  const int64 any_value = kint64min;
  std::vector<int64> domain_sizes;
  for (int i = 0; i < n; ++i) {
    domain_sizes.push_back(
        integer_trail->InitialVariableDomain(vars[i]).Size());
  }
  CompressTuples(domain_sizes, any_value, &tuples);

  std::vector<absl::flat_hash_map<int64, Literal>> mapping(n);
  for (int i = 0; i < n; ++i) {
    if (integer_encoder->VariableIsFullyEncoded(vars[i]) ||
        integer_trail->InitialVariableDomain(vars[i]).Size() <= 8) {
      for (const auto pair : model->Add(FullyEncodeVariable(vars[i]))) {
        mapping[i][pair.value.value()] = pair.literal;
      }
    }
  }

  // For each tuple, forbid the variables values to be this tuple.
  std::vector<Literal> clause;
  for (const std::vector<int64>& tuple : tuples) {
    bool add_tuple = true;
    clause.clear();
    for (int i = 0; i < n; ++i) {
      const int64 value = tuple[i];
      if (value == any_value) continue;
      if (!mapping[i].empty()) {  // Variable is fully encoded.
        if (mapping[i].contains(value)) {
          clause.push_back(gtl::FindOrDie(mapping[i], value).Negated());
        } else {
          add_tuple = false;
          break;
        }
      } else {  // Mapping is empty, variable is not fully encoded.
        const int64 lb = model->Get(LowerBound(vars[i]));
        const int64 ub = model->Get(UpperBound(vars[i]));
        // TODO(user): test the full initial domain instead of just checking
        // the bounds.
        if (value < lb || value > ub) {
          add_tuple = false;
          break;
        }
        if (value > lb) {
          clause.push_back(integer_encoder->GetOrCreateAssociatedLiteral(
              IntegerLiteral::LowerOrEqual(vars[i], IntegerValue(value - 1))));
        }
        if (value < ub) {
          clause.push_back(integer_encoder->GetOrCreateAssociatedLiteral(
              IntegerLiteral::GreaterOrEqual(vars[i],
                                             IntegerValue(value + 1))));
        }
      }
    }
    if (add_tuple) model->Add(ClauseConstraint(clause));
  }
}

std::function<void(Model*)> LiteralTableConstraint(
    const std::vector<std::vector<Literal>>& literal_tuples,
    const std::vector<Literal>& line_literals) {
  return [=](Model* model) {
    CHECK_EQ(literal_tuples.size(), line_literals.size());
    const int num_tuples = line_literals.size();
    if (num_tuples == 0) return;
    const int tuple_size = literal_tuples[0].size();
    if (tuple_size == 0) return;
    for (int i = 1; i < num_tuples; ++i) {
      CHECK_EQ(tuple_size, literal_tuples[i].size());
    }

    absl::flat_hash_map<LiteralIndex, std::vector<LiteralIndex>>
        line_literals_per_literal;
    for (int i = 0; i < num_tuples; ++i) {
      const LiteralIndex selected_index = line_literals[i].Index();
      for (const Literal l : literal_tuples[i]) {
        line_literals_per_literal[l.Index()].push_back(selected_index);
      }
    }

    // line_literals[i] == true => literal_tuples[i][j] == true.
    // literal_tuples[i][j] == false => line_literals[i] == false.
    for (int i = 0; i < num_tuples; ++i) {
      const Literal line_is_selected = line_literals[i];
      for (const Literal lit : literal_tuples[i]) {
        model->Add(Implication(line_is_selected, lit));
      }
    }

    // Exactly one selected literal is true.
    model->Add(ExactlyOneConstraint(line_literals));

    // If all selected literals of the lines containing a literal are false,
    // then the literal is false.
    for (const auto& p : line_literals_per_literal) {
      std::vector<Literal> clause;
      for (const auto& index : p.second) {
        clause.push_back(Literal(index));
      }
      clause.push_back(Literal(p.first).Negated());
      model->Add(ClauseConstraint(clause));
    }
  };
}

std::function<void(Model*)> TransitionConstraint(
    const std::vector<IntegerVariable>& vars,
    const std::vector<std::vector<int64>>& automaton, int64 initial_state,
    const std::vector<int64>& final_states) {
  return [=](Model* model) {
    IntegerTrail* integer_trail = model->GetOrCreate<IntegerTrail>();
    const int n = vars.size();
    CHECK_GT(n, 0) << "No variables in TransitionConstraint().";

    // Test precondition.
    {
      std::set<std::pair<int64, int64>> unique_transition_checker;
      for (const std::vector<int64>& transition : automaton) {
        CHECK_EQ(transition.size(), 3);
        const std::pair<int64, int64> p{transition[0], transition[1]};
        CHECK(!gtl::ContainsKey(unique_transition_checker, p))
            << "Duplicate outgoing transitions with value " << transition[1]
            << " from state " << transition[0] << ".";
        unique_transition_checker.insert(p);
      }
    }

    // Construct a table with the possible values of each vars.
    std::vector<absl::flat_hash_set<int64>> possible_values(n);
    for (int time = 0; time < n; ++time) {
      const auto domain = integer_trail->InitialVariableDomain(vars[time]);
      for (const std::vector<int64>& transition : automaton) {
        // TODO(user): quadratic algo, improve!
        if (domain.Contains(transition[1])) {
          possible_values[time].insert(transition[1]);
        }
      }
    }

    // Compute the set of reachable state at each time point.
    std::vector<std::set<int64>> reachable_states(n + 1);
    reachable_states[0].insert(initial_state);
    reachable_states[n] = {final_states.begin(), final_states.end()};

    // Forward.
    //
    // TODO(user): filter using the domain of vars[time] that may not contain
    // all the possible transitions.
    for (int time = 0; time + 1 < n; ++time) {
      for (const std::vector<int64>& transition : automaton) {
        if (!gtl::ContainsKey(reachable_states[time], transition[0])) continue;
        if (!gtl::ContainsKey(possible_values[time], transition[1])) continue;
        reachable_states[time + 1].insert(transition[2]);
      }
    }

    // Backward.
    for (int time = n - 1; time > 0; --time) {
      std::set<int64> new_set;
      for (const std::vector<int64>& transition : automaton) {
        if (!gtl::ContainsKey(reachable_states[time], transition[0])) continue;
        if (!gtl::ContainsKey(possible_values[time], transition[1])) continue;
        if (!gtl::ContainsKey(reachable_states[time + 1], transition[2]))
          continue;
        new_set.insert(transition[0]);
      }
      reachable_states[time].swap(new_set);
    }

    // We will model at each time step the current automaton state using Boolean
    // variables. We will have n+1 time step. At time zero, we start in the
    // initial state, and at time n we should be in one of the final states. We
    // don't need to create Booleans at at time when there is just one possible
    // state (like at time zero).
    absl::flat_hash_map<IntegerValue, Literal> encoding;
    absl::flat_hash_map<IntegerValue, Literal> in_encoding;
    absl::flat_hash_map<IntegerValue, Literal> out_encoding;
    for (int time = 0; time < n; ++time) {
      // All these vector have the same size. We will use them to enforce a
      // local table constraint representing one step of the automaton at the
      // given time.
      std::vector<Literal> tuple_literals;
      std::vector<IntegerValue> in_states;
      std::vector<IntegerValue> transition_values;
      std::vector<IntegerValue> out_states;
      for (const std::vector<int64>& transition : automaton) {
        if (!gtl::ContainsKey(reachable_states[time], transition[0])) continue;
        if (!gtl::ContainsKey(possible_values[time], transition[1])) continue;
        if (!gtl::ContainsKey(reachable_states[time + 1], transition[2]))
          continue;

        // TODO(user): if this transition correspond to just one in-state or
        // one-out state or one variable value, we could reuse the corresponding
        // Boolean variable instead of creating a new one!
        tuple_literals.push_back(
            Literal(model->Add(NewBooleanVariable()), true));
        in_states.push_back(IntegerValue(transition[0]));

        transition_values.push_back(IntegerValue(transition[1]));

        // On the last step we don't need to distinguish the output states, so
        // we use zero.
        out_states.push_back(time + 1 == n ? IntegerValue(0)
                                           : IntegerValue(transition[2]));
      }

      // Fully instantiate vars[time].
      // Tricky: because we started adding constraints that can propagate, the
      // possible values returned by encoding might not contains all the value
      // computed in transition_values.
      {
        std::vector<IntegerValue> s = transition_values;
        gtl::STLSortAndRemoveDuplicates(&s);

        encoding.clear();
        if (s.size() > 1) {
          std::vector<int64> values;
          values.reserve(s.size());
          for (IntegerValue v : s) values.push_back(v.value());
          integer_trail->UpdateInitialDomain(vars[time],
                                             Domain::FromValues(values));
          model->Add(FullyEncodeVariable(vars[time]));
          encoding = GetEncoding(vars[time], model);
        } else {
          // Fix vars[time] to its unique possible value.
          CHECK_EQ(s.size(), 1);
          const int64 unique_value = s.begin()->value();
          model->Add(LowerOrEqual(vars[time], unique_value));
          model->Add(GreaterOrEqual(vars[time], unique_value));
        }
      }

      // For each possible out states, create one Boolean variable.
      {
        std::vector<IntegerValue> s = out_states;
        gtl::STLSortAndRemoveDuplicates(&s);

        out_encoding.clear();
        if (s.size() == 2) {
          const BooleanVariable var = model->Add(NewBooleanVariable());
          out_encoding[s.front()] = Literal(var, true);
          out_encoding[s.back()] = Literal(var, false);
        } else if (s.size() > 1) {
          for (const IntegerValue state : s) {
            const Literal l = Literal(model->Add(NewBooleanVariable()), true);
            out_encoding[state] = l;
          }
        }
      }

      // Now we link everything together.
      //
      // Note that we do not need the ExactlyOneConstraint(tuple_literals)
      // because it is already implicitely encoded since we have exactly one
      // transition value.
      if (!in_encoding.empty()) {
        ProcessOneColumn(tuple_literals, in_states, in_encoding, {}, model);
      }
      if (!encoding.empty()) {
        ProcessOneColumn(tuple_literals, transition_values, encoding, {},
                         model);
      }
      if (!out_encoding.empty()) {
        ProcessOneColumn(tuple_literals, out_states, out_encoding, {}, model);
      }
      in_encoding = out_encoding;
    }
  };
}

}  // namespace sat
}  // namespace operations_research
