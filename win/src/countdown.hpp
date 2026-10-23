#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace tibo {
inline bool systemEnglish = true;
inline bool english = true;
inline std::string tr(const char *zh, const char *en) { return english ? en : zh; }
inline void selectLanguage(const std::string &value) {
  english = value == "en" || (value == "auto" && systemEnglish);
}
inline int64_t civilDays(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return era * 146097 + static_cast<int>(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
}
inline int digits(const std::string &s, size_t pos, size_t n) {
  int v = 0;
  if (pos + n > s.size())
    throw std::runtime_error(tr("时间格式错误", "Invalid time format"));
  for (size_t i = 0; i < n; i++) {
    char c = s[pos + i];
    if (c < '0' || c > '9')
      throw std::runtime_error(tr("时间格式错误", "Invalid time format"));
    v = v * 10 + c - '0';
  }
  return v;
}
inline int64_t parseDate(const std::string &s) {
  if (s.size() > 128 || s.size() < 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' ||
      s[13] != ':' || s[16] != ':')
    throw std::runtime_error(
        tr("时间必须是带时区的 ISO 8601 格式", "Use ISO 8601 with a time zone"));
  int y = digits(s, 0, 4), m = digits(s, 5, 2), d = digits(s, 8, 2), h = digits(s, 11, 2),
      min = digits(s, 14, 2), sec = digits(s, 17, 2);
  bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
  int md[] = {31, 28 + (leap ? 1 : 0), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (y < 1 || m < 1 || m > 12 || d < 1 || d > md[m - 1] || h > 23 || min > 59 || sec > 59)
    throw std::runtime_error(tr("时间日期超出范围", "Date or time out of range"));
  size_t p = 19;
  if (p < s.size() && s[p] == '.') {
    size_t first = ++p;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9')
      p++;
    if (first == p)
      throw std::runtime_error(tr("秒的小数部分无效", "Invalid fractional seconds"));
  }
  int offset = 0;
  if (p + 1 == s.size() && s[p] == 'Z') {
  } else if (p + 6 == s.size() && (s[p] == '+' || s[p] == '-') && s[p + 3] == ':') {
    int oh = digits(s, p + 1, 2), om = digits(s, p + 4, 2);
    if (oh > 14 || om > 59 || (oh == 14 && om != 0))
      throw std::runtime_error(tr("时区偏移超出范围", "Time zone offset out of range"));
    offset = (oh * 60 + om) * 60 * (s[p] == '+' ? 1 : -1);
  } else
    throw std::runtime_error(
        tr("时间必须包含 Z 或 ±HH:MM 时区", "Time requires Z or a UTC offset"));
  return civilDays(y, m, d) * 86400 + h * 3600 + min * 60 + sec - offset;
}
struct Config {
  std::optional<std::string> tiboResetAt, sourceURL;
  std::vector<std::string> cards;
  std::string language = "auto";
  std::optional<int64_t> deadline() const {
    return tiboResetAt ? std::optional<int64_t>(parseDate(*tiboResetAt)) : std::nullopt;
  }
  std::vector<int64_t> expiries() const {
    std::vector<int64_t> out;
    for (auto &s : cards)
      out.push_back(parseDate(s));
    std::sort(out.begin(), out.end());
    return out;
  }
};
inline void validate(const Config &c) {
  if (c.language != "auto" && c.language != "zh" && c.language != "en")
    throw std::runtime_error("Language must be auto, zh or en");
  if (c.tiboResetAt)
    parseDate(*c.tiboResetAt);
  if (c.cards.size() > 128)
    throw std::runtime_error("Too many card deadlines");
  for (const auto &card : c.cards)
    parseDate(card);
  if (c.sourceURL) {
    const auto &u = *c.sourceURL;
    if (u.size() < 8 || u.size() > 4096)
      throw std::runtime_error(tr("sourceURL 无效", "Invalid source URL"));
    const auto end = u.find_first_of("/?#", 8);
    std::string host = u.substr(8, end == std::string::npos ? end : end - 8);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; });
    if (u.compare(0, 8, "https://") != 0 ||
        (host != "x.com" && host != "www.x.com" && host != "twitter.com" &&
         host != "www.twitter.com") ||
        std::any_of(u.begin(), u.end(),
                    [](unsigned char ch) { return ch <= 32 || ch >= 127 || ch == '\\'; }))
      throw std::runtime_error(tr("sourceURL 必须是 X / Twitter 的 HTTPS 链接",
                                  "Source must be an X / Twitter HTTPS URL"));
  }
}
inline std::string serialize(const Config &c) {
  validate(c);
  std::string result = "tibo=" + c.tiboResetAt.value_or("none") + "\n";
  for (const auto &card : c.cards)
    result += "card=" + card + "\n";
  result += "source=" + c.sourceURL.value_or("none") + "\nlanguage=" + c.language + "\n";
  return result;
}
inline Config parseState(const std::string &input) {
  if (input.size() > 65536)
    throw std::runtime_error("State is too large");
  Config c;
  std::set<std::string> seen;
  size_t position = 0;
  while (position < input.size()) {
    auto newline = input.find('\n', position);
    auto line = input.substr(position, newline == std::string::npos ? newline : newline - position);
    position = newline == std::string::npos ? input.size() : newline + 1;
    auto equals = line.find('=');
    if (equals == std::string::npos)
      throw std::runtime_error("Invalid saved state");
    auto key = line.substr(0, equals), value = line.substr(equals + 1);
    if (key != "card" && !seen.insert(key).second)
      throw std::runtime_error("Duplicate state field");
    if (key == "tibo")
      c.tiboResetAt = value == "none" ? std::nullopt : std::optional<std::string>(value);
    else if (key == "source")
      c.sourceURL = value == "none" ? std::nullopt : std::optional<std::string>(value);
    else if (key == "language")
      c.language = value;
    else if (key == "card")
      c.cards.push_back(value);
    else
      throw std::runtime_error("Unknown state field");
  }
  if (!seen.count("tibo") || !seen.count("source") || !seen.count("language"))
    throw std::runtime_error("Incomplete saved state");
  validate(c);
  return c;
}
inline Config setArguments(Config c, const std::vector<std::string> &args) {
  if (args.empty() || args.size() % 2)
    throw std::runtime_error("Expected option/value pairs");
  std::set<std::string> seen;
  for (size_t i = 0; i < args.size(); i += 2) {
    const auto &key = args[i], &value = args[i + 1];
    if (!seen.insert(key).second)
      throw std::runtime_error("Duplicate option");
    if (key == "--tibo")
      c.tiboResetAt = value == "none" ? std::nullopt : std::optional<std::string>(value);
    else if (key == "--source")
      c.sourceURL = value == "none" ? std::nullopt : std::optional<std::string>(value);
    else if (key == "--language")
      c.language = value;
    else if (key == "--cards") {
      c.cards.clear();
      if (value != "none") {
        size_t start = 0;
        do {
          auto end = value.find(',', start);
          c.cards.push_back(value.substr(start, end == std::string::npos ? end : end - start));
          if (end == std::string::npos)
            break;
          start = end + 1;
        } while (true);
      }
    } else
      throw std::runtime_error("Unknown option");
  }
  validate(c);
  return c;
}
inline std::string compact(std::optional<int64_t> when, int64_t now, const char *empty) {
  if (!when)
    return empty;
  int64_t s = std::max<int64_t>(0, *when - now);
  if (!s)
    return tr("到点", "Due");
  char b[64];
  if (s >= 86400)
    std::snprintf(b, sizeof(b), "%lldd%02lldh", static_cast<long long>(s / 86400),
                  static_cast<long long>(s % 86400 / 3600));
  else {
    int64_t min = (s + 59) / 60;
    std::snprintf(b, sizeof(b), "%02lld:%02lld", static_cast<long long>(min / 60),
                  static_cast<long long>(min % 60));
  }
  return b;
}
} // namespace tibo
