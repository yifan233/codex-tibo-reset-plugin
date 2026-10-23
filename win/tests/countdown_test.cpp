#include "../src/countdown.hpp"
#include <cassert>
#include <iostream>
using namespace tibo;
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
int main() {
  assert(parseDate("1970-01-01T00:00:00Z") == 0);
  assert(parseDate("2026-10-23T10:00:00-07:00") == parseDate("2026-10-23T17:00:00Z"));
  assert(parseDate("2024-02-29T00:00:00Z") - parseDate("2024-02-28T00:00:00Z") == 86400);
  for (auto date : {"tomorrow", "2026-10-23T10:00:00", "2026-02-29T00:00:00Z",
                    "2026-13-01T00:00:00Z", "2026-01-01T24:00:00Z", "2026-01-01T00:00:00+14:01",
                    "2026-01-01T00:00:00.Z", "2026-01-01T00:00:00Zjunk"})
    rejects([&] { parseDate(date); });
  auto state = setArguments({}, {"--tibo", "2026-10-23T17:00:00Z", "--cards",
                                 "2026-11-06T00:00:00Z,2026-10-29T00:00:00Z", "--source",
                                 "https://x.com/thsottiaux", "--language", "en"});
  assert(state.expiries()[0] == 1793232000);
  assert(serialize(parseState(serialize(state))) == serialize(state));
  auto updated = setArguments(state, {"--language", "zh"});
  assert(updated.tiboResetAt == state.tiboResetAt && updated.cards == state.cards);
  auto cleared = setArguments(updated, {"--tibo", "none", "--cards", "none", "--source", "none"});
  assert(!cleared.deadline() && cleared.cards.empty() && !cleared.sourceURL);
  for (auto args : std::vector<std::vector<std::string>>{{},
                                                         {"--tibo"},
                                                         {"--unknown", "x"},
                                                         {"--language", "fr"},
                                                         {"--language", "en", "--language", "zh"},
                                                         {"--cards", ""},
                                                         {"--cards", "2026-10-23T17:00:00Z,"},
                                                         {"--tibo", "bad"}})
    rejects([&] { setArguments(state, args); });
  for (auto url :
       {"https://", "https://?bad", "https://#bad", "https://evil.test", "https://x.com@evil.test/",
        "https://x.com:443/", "https://x.com\\bad", "https://x.com/\n"})
    rejects([&] { setArguments(state, {"--source", url}); });
  rejects(
      [&] { setArguments(state, {"--source", std::string("https://x.com/") + char(0) + "bad"}); });
  rejects([&] { parseState("tibo=none\nsource=none\n"); });
  rejects([&] { parseState("tibo=none\nsource=none\nlanguage=en\ntibo=none\n"); });
  rejects([&] { parseState(std::string(65537, 'x')); });
  auto tooMany = state;
  tooMany.cards.resize(129, "2026-10-23T17:00:00Z");
  rejects([&] { validate(tooMany); });
  selectLanguage("en");
  assert(compact(0, 0, "None") == "Due");
  selectLanguage("zh");
  assert(compact(0, 0, "无卡") == "到点");
  assert(compact(3600, 0, "无卡") == "01:00");
  assert(compact(86400 + 7200, 0, "无卡") == "1d02h");
  std::cout
      << "PASS: dates, command updates, state persistence, limits, links, bilingual countdowns\n";
}
