#pragma once

// The words mini-daw uses, explained (P23, SPEC-UI-17): the Terms tab shows
// them by topic, with a field to find a word. Kept here, apart from the GUI,
// so they can be checked (every "see also" exists, no name twice).

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

struct Term {
    std::string name;                  // as written in mini-daw ("Arm", "Take")
    std::string group;                 // the topic it is listed under
    std::string text;                  // what it means
    std::string where;                 // where it is found in mini-daw ("" if nowhere in particular)
    std::vector<std::string> seeAlso;  // names of other terms
};

// The topics, in the order they are shown.
const std::vector<std::string>& termGroups();
// Every term, by topic (in termGroups() order), within a topic in reading order.
const std::vector<Term>& terms();

// The terms that contain `query` (letters A-Z in any case; spaces at the ends
// ignored) in their name, text or place, as indexes into terms(): those whose
// name contains it first, then the others, each in terms() order. All of them
// for an empty query.
std::vector<std::size_t> findTerms(std::string_view query);
// `query` without spaces at the ends: what findTerms() looks for. The Terms
// tab shows the topic view only when this is empty (Z8).
std::string trimmedTermQuery(std::string_view query);
// The index of the term called `name` (any case), or terms().size() if none.
std::size_t termIndex(std::string_view name);

} // namespace minidaw
