#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define NTDDI_VERSION 0x0A000003
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "countdown.hpp"
#include <chrono>
#include <cmath>
#include <windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include <shellapi.h>
#include <windowsx.h>

using namespace tibo;
constexpr int W = 296, CLOSED = 32, OPEN = 270;
constexpr UINT TRAY = WM_APP + 1;
std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
  if (!n)
    throw std::runtime_error(tr("无效的 UTF-8 文本", "Invalid UTF-8 text"));
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(), n);
  return w;
}
std::string utf8(const std::wstring &s) {
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string b(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), b.data(), n, nullptr, nullptr);
  return b;
}
const wchar_t *stateKey = L"Software\\TiboRestPlugin";
const wchar_t *loc(const wchar_t *zh, const wchar_t *en) { return english ? en : zh; }
Config readState() {
  DWORD size = 0;
  auto result = RegGetValueW(HKEY_CURRENT_USER, stateKey, L"Countdown", RRF_RT_REG_BINARY, nullptr,
                             nullptr, &size);
  if (result == ERROR_FILE_NOT_FOUND)
    return {};
  if (result != ERROR_SUCCESS || size > 65536)
    throw std::runtime_error(tr("无法读取倒计时", "Unable to read countdown"));
  std::string data(size, 0);
  result = RegGetValueW(HKEY_CURRENT_USER, stateKey, L"Countdown", RRF_RT_REG_BINARY, nullptr,
                        data.data(), &size);
  if (result != ERROR_SUCCESS)
    throw std::runtime_error(tr("读取倒计时失败", "Unable to read countdown"));
  data.resize(size);
  return parseState(data);
}
void saveState(const Config &state) {
  auto data = serialize(state);
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, stateKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    throw std::runtime_error(tr("保存倒计时失败", "Unable to save countdown"));
  auto result =
      RegSetValueExW(key, L"Countdown", 0, REG_BINARY, reinterpret_cast<const BYTE *>(data.data()),
                     static_cast<DWORD>(data.size()));
  RegCloseKey(key);
  if (result != ERROR_SUCCESS)
    throw std::runtime_error(tr("保存倒计时失败", "Unable to save countdown"));
}
int64_t nowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
SYSTEMTIME utcTime(int64_t t) {
  ULARGE_INTEGER n{};
  n.QuadPart = (t + 11644473600LL) * 10000000ULL;
  FILETIME ft{n.LowPart, n.HighPart};
  SYSTEMTIME st{};
  FileTimeToSystemTime(&ft, &st);
  return st;
}
int64_t timestamp(const SYSTEMTIME &st) {
  FILETIME f{};
  if (!SystemTimeToFileTime(&st, &f))
    throw std::runtime_error(tr("日期超出范围", "Date out of range"));
  ULARGE_INTEGER n{};
  n.LowPart = f.dwLowDateTime;
  n.HighPart = f.dwHighDateTime;
  return (int64_t)(n.QuadPart / 10000000ULL) - 11644473600LL;
}
DYNAMIC_TIME_ZONE_INFORMATION localZone() {
  DYNAMIC_TIME_ZONE_INFORMATION tz{};
  if (GetDynamicTimeZoneInformation(&tz) == TIME_ZONE_ID_INVALID)
    throw std::runtime_error(tr("无法读取系统时区", "Unable to read system time zone"));
  return tz;
}
std::wstring displayTime(std::optional<int64_t> t, const DYNAMIC_TIME_ZONE_INFORMATION &tz,
                         const wchar_t *empty) {
  if (!t)
    return empty;
  auto utc = utcTime(*t);
  SYSTEMTIME local{};
  if (!SystemTimeToTzSpecificLocalTimeEx(&tz, &utc, &local))
    return loc(L"时间转换失败", L"Time conversion failed");
  wchar_t b[80];
  swprintf(b, 80, L"%02d/%02d %02d:%02d:%02d", local.wMonth, local.wDay, local.wHour, local.wMinute,
           local.wSecond);
  return b;
}
template <class T> void release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}
class App {
public:
  HWND hwnd = nullptr, editor = nullptr, picker = nullptr;
  HFONT editorFont = nullptr;
  Config config;
  std::string lastData, error;
  ID2D1Factory *factory = nullptr;
  IDWriteFactory *write = nullptr;
  ID2D1DCRenderTarget *target = nullptr;
  ID2D1SolidColorBrush *brush = nullptr;
  DYNAMIC_TIME_ZONE_INFORMATION tz{};
  NOTIFYICONDATAW tray{};
  bool expanded = false, animating = false;
  float height = CLOSED, from = CLOSED, to = CLOSED, scale = 1;
  ULONGLONG start = 0;
  UINT taskbarCreated = 0;
  ~App() {
    release(brush);
    release(target);
    release(write);
    release(factory);
  }
  void place() {
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(mon, &info);
    scale = GetDpiForWindow(hwnd) / 96.f;
    int width = (int)std::lround(W * scale), h = (int)std::lround(height * scale);
    SetWindowPos(hwnd, HWND_TOPMOST,
                 info.rcMonitor.left + (info.rcMonitor.right - info.rcMonitor.left - width) / 2,
                 info.rcMonitor.top, width, h, SWP_NOACTIVATE);
  }
  void reload() {
    try {
      auto c = readState();
      auto data = serialize(c);
      if (data == lastData && error.empty())
        return;
      config = c;
      selectLanguage(c.language);
      lastData = data;
      error.clear();
    } catch (const std::exception &e) {
      error = e.what();
    }
  }
  void notify() {
    tray = {};
    tray.cbSize = sizeof(tray);
    tray.hWnd = hwnd;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = TRAY;
    tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(tray.szTip, L"tibo rest plugin", 128);
    Shell_NotifyIconW(NIM_ADD, &tray);
    tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &tray);
  }
  void toggle() {
    if (animating)
      return;
    expanded = !expanded;
    from = height;
    to = expanded ? OPEN : CLOSED;
    start = GetTickCount64();
    animating = true;
    SetTimer(hwnd, 2, 15, nullptr);
  }
  D2D1_COLOR_F color(float r, float g, float b) { return D2D1::ColorF(r, g, b, 1); }
  void text(const std::wstring &s, float x, float y, float size, D2D1_COLOR_F c, bool mono = false,
            bool bold = false, float width = 264) {
    IDWriteTextFormat *fmt = nullptr;
    if (FAILED(write->CreateTextFormat(mono ? L"Consolas" : L"Microsoft YaHei UI", nullptr,
                                       (mono || bold) ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                                                      : DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                       english ? L"en-US" : L"zh-CN", &fmt)))
      return;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    brush->SetColor(c);
    target->DrawText(s.c_str(), (UINT32)s.size(), fmt, D2D1::RectF(x, y, x + width, y + 24), brush,
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    fmt->Release();
  }
  void button(float x, float width, const wchar_t *s) {
    brush->SetColor(color(.09f, .09f, .09f));
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, 228, x + width, 254), 7, 7),
                                 brush);
    text(s, x + 10, 234, 11, color(.83f, .83f, .83f));
  }
  void draw() {
    RECT client{};
    GetClientRect(hwnd, &client);
    int pw = client.right, ph = client.bottom;
    if (!pw || !ph)
      return;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = pw;
    bi.bmiHeader.biHeight = -ph;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
      DeleteDC(dc);
      ReleaseDC(nullptr, screen);
      return;
    }
    auto old = SelectObject(dc, bitmap);
    memset(bits, 0, (size_t)pw * ph * 4);
    if (!target) {
      auto props = D2D1::RenderTargetProperties(
          D2D1_RENDER_TARGET_TYPE_DEFAULT,
          D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96,
          D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
      if (FAILED(factory->CreateDCRenderTarget(&props, &target))) {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return;
      }
      target->CreateSolidColorBrush(color(0, 0, 0), &brush);
    }
    if (!brush || FAILED(target->BindDC(dc, &client))) {
      SelectObject(dc, old);
      DeleteObject(bitmap);
      DeleteDC(dc);
      ReleaseDC(nullptr, screen);
      return;
    }
    target->BeginDraw();
    target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
    target->Clear(D2D1::ColorF(0, 0, 0, 0));
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    brush->SetColor(color(0, 0, 0));
    float radius = height <= 32 ? 13 : 22;
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, W, height), radius, radius),
                                 brush);
    auto dates = config.expiries();
    auto earliest = dates.empty() ? std::optional<int64_t>{} : std::optional<int64_t>{dates[0]};
    auto cyan = color(50 / 255.f, 212 / 255.f, 229 / 255.f),
         orange = color(1, 159 / 255.f, 66 / 255.f), gray = color(.5f, .5f, .5f),
         light = color(.83f, .83f, .83f);
    text(wide(compact(config.deadline(), nowSeconds(), tr("未设置", "Unset").c_str())), 6, 7, 14,
         cyan, true, false, 50);
    text(wide(compact(earliest, nowSeconds(), tr("无卡", "None").c_str())), 241.5f, 7, 14, orange,
         true, false, 52);
    if (expanded && height >= OPEN - 1) {
      text(loc(L"RESET / 预告与到期", L"RESET / Deadlines"), 16, 50, 13, color(1, 1, 1), false,
           true);
      text(L"Tibo · " + displayTime(config.deadline(), tz, loc(L"未设置", L"Unset")), 16, 84, 11,
           cyan);
      text(std::wstring(loc(L"最早卡 · ", L"First card · ")) +
               displayTime(earliest, tz, loc(L"无卡", L"None")),
           16, 112, 11, orange);
      text(std::wstring(loc(L"其余卡 · ", L"Next card · ")) +
               displayTime(dates.size() > 1 ? std::optional<int64_t>{dates[1]}
                                            : std::optional<int64_t>{},
                           tz, loc(L"无", L"None")),
           16, 140, 11, light);
      text(error.empty() ? loc(L"由 Codex 设定", L"Set by Codex")
                         : loc(L"读取失败 · 保留上次数据", L"Keeping last valid values"),
           16, 192, 10, gray);
      button(12, 86, loc(L"修改时间", L"Edit time"));
      button(103, 78, loc(L"原帖 ↗", L"Source ↗"));
      button(224, 60, loc(L"退出", L"Quit"));
    }
    HRESULT hr = target->EndDraw();
    if (SUCCEEDED(hr)) {
      RECT rect{};
      GetWindowRect(hwnd, &rect);
      POINT dst{rect.left, rect.top}, src{0, 0};
      SIZE size{pw, ph};
      BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
      UpdateLayeredWindow(hwnd, screen, &dst, &size, dc, &src, 0, &blend, ULW_ALPHA);
    } else {
      release(brush);
      release(target);
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
  }
  void menu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 10, loc(L"展开 / 收起", L"Expand / Collapse"));
    AppendMenuW(m, MF_STRING, 11, loc(L"退出", L"Quit"));
    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(hwnd);
    int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    if (choice == 10)
      toggle();
    if (choice == 11)
      DestroyWindow(hwnd);
    PostMessageW(hwnd, WM_NULL, 0, 0);
  }
  void click(int px, int py) {
    if (animating)
      return;
    float x = px / scale, y = py / scale;
    if (!expanded || y <= 38) {
      toggle();
      return;
    }
    if (y >= 228 && y <= 254) {
      if (x >= 12 && x <= 98)
        edit();
      else if (x >= 103 && x <= 181 && config.sourceURL) {
        auto url = wide(*config.sourceURL);
        auto result = ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)result <= 32)
          MessageBoxW(hwnd, loc(L"无法打开原帖链接", L"Unable to open source"),
                      loc(L"打开失败", L"Open failed"), MB_OK | MB_ICONERROR);
      } else if (x >= 224 && x <= 284)
        DestroyWindow(hwnd);
    }
  }
  void edit() {
    if (editor) {
      SetForegroundWindow(editor);
      return;
    }
    editor = CreateWindowExW(WS_EX_TOPMOST, L"TiboEditor", loc(L"Tibo 截止时间", L"Tibo deadline"),
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 0, 0, 360, 150, hwnd, nullptr,
                             GetModuleHandleW(nullptr), this);
    RECT r{0, 0, (LONG)std::lround(360 * scale), (LONG)std::lround(150 * scale)};
    AdjustWindowRectEx(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOPMOST);
    RECT parent{};
    GetWindowRect(hwnd, &parent);
    SetWindowPos(editor, HWND_TOPMOST,
                 parent.left + (parent.right - parent.left - (r.right - r.left)) / 2,
                 parent.top + (int)std::lround(80 * scale), r.right - r.left, r.bottom - r.top,
                 SWP_SHOWWINDOW);
    layoutEditor(editor);
    SetForegroundWindow(editor);
  }
  void layoutEditor(HWND h) {
    float dpi = GetDpiForWindow(h) / 96.f;
    HFONT oldFont = editorFont;
    editorFont = CreateFontW(-(int)std::lround(13 * dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    struct Control {
      int id, x, y, w, h;
    } controls[] = {{100, 25, 32, 310, 26}, {IDOK, 250, 96, 80, 28}, {IDCANCEL, 160, 96, 80, 28}};
    for (auto c : controls) {
      auto child = GetDlgItem(h, c.id);
      SetWindowPos(child, nullptr, (int)std::lround(c.x * dpi), (int)std::lround(c.y * dpi),
                   (int)std::lround(c.w * dpi), (int)std::lround(c.h * dpi), SWP_NOZORDER);
      SendMessageW(child, WM_SETFONT, (WPARAM)editorFont, TRUE);
    }
    if (oldFont)
      DeleteObject(oldFont);
  }
  void saveEditor() {
    SYSTEMTIME local{};
    if (DateTime_GetSystemtime(picker, &local) != GDT_VALID)
      return;
    local.wMilliseconds = 0;
    try {
      SYSTEMTIME utc{}, back{};
      if (!TzSpecificLocalTimeToSystemTimeEx(&tz, &local, &utc) ||
          !SystemTimeToTzSpecificLocalTimeEx(&tz, &utc, &back) ||
          timestamp(back) != timestamp(local))
        throw std::runtime_error(
            tr("夏令时切换导致时间无效，请让 Codex 指定偏移",
               "Invalid daylight-saving time; ask Codex to set an explicit offset"));
      int64_t t = timestamp(utc);
      for (int delta : {-3600, 3600}) {
        auto other = utcTime(t + delta);
        SYSTEMTIME check{};
        if (SystemTimeToTzSpecificLocalTimeEx(&tz, &other, &check) &&
            timestamp(check) == timestamp(local))
          throw std::runtime_error(
              tr("此时间有夏令时歧义，请让 Codex 指定偏移",
                 "Ambiguous daylight-saving time; ask Codex to set an explicit offset"));
      }
      auto next = config;
      wchar_t value[40];
      swprintf(value, 40, L"%04d-%02d-%02dT%02d:%02d:%02dZ", utc.wYear, utc.wMonth, utc.wDay,
               utc.wHour, utc.wMinute, utc.wSecond);
      next.tiboResetAt = utf8(value);
      saveState(next);
      reload();
      draw();
      DestroyWindow(editor);
    } catch (const std::exception &e) {
      MessageBoxW(editor, wide(e.what()).c_str(), loc(L"保存失败", L"Save failed"),
                  MB_OK | MB_ICONERROR);
    }
  }
  static LRESULT CALLBACK EditorProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    App *a = (App *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
      a = (App *)((CREATESTRUCTW *)l)->lpCreateParams;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)a);
    }
    if (!a)
      return DefWindowProcW(h, msg, w, l);
    switch (msg) {
    case WM_CREATE: {
      a->picker = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_UPDOWN, 25, 32, 310, 26,
                                  h, (HMENU)100, GetModuleHandleW(nullptr), nullptr);
      SendMessageW(a->picker, DTM_SETFORMATW, 0, (LPARAM)L"yyyy-MM-dd HH:mm:ss");
      auto utc = utcTime(a->config.deadline().value_or(nowSeconds()));
      SYSTEMTIME local{};
      SystemTimeToTzSpecificLocalTimeEx(&a->tz, &utc, &local);
      DateTime_SetSystemtime(a->picker, GDT_VALID, &local);
      CreateWindowW(L"BUTTON", loc(L"保存", L"Save"),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 250, 96, 80, 28, h,
                    (HMENU)IDOK, GetModuleHandleW(nullptr), nullptr);
      CreateWindowW(L"BUTTON", loc(L"取消", L"Cancel"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 160, 96,
                    80, 28, h, (HMENU)IDCANCEL, GetModuleHandleW(nullptr), nullptr);
      return 0;
    }
    case WM_DPICHANGED: {
      RECT *r = (RECT *)l;
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      a->layoutEditor(h);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(w) == IDOK)
        a->saveEditor();
      if (LOWORD(w) == IDCANCEL)
        DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      a->editor = nullptr;
      a->picker = nullptr;
      if (a->editorFont) {
        DeleteObject(a->editorFont);
        a->editorFont = nullptr;
      }
      return 0;
    }
    return DefWindowProcW(h, msg, w, l);
  }
  static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    App *a = (App *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
      a = (App *)((CREATESTRUCTW *)l)->lpCreateParams;
      SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)a);
      a->hwnd = h;
    }
    if (!a)
      return DefWindowProcW(h, msg, w, l);
    if (msg == a->taskbarCreated && msg) {
      a->notify();
      return 0;
    }
    switch (msg) {
    case WM_TIMER:
      if (w == 1) {
        a->reload();
        a->draw();
      } else if (w == 2) {
        double t = std::min(1.0, (GetTickCount64() - a->start) / 260.0), e = t * t * (3 - 2 * t);
        a->height = (float)(a->from + (a->to - a->from) * e);
        a->place();
        a->draw();
        if (t >= 1) {
          KillTimer(h, 2);
          a->animating = false;
        }
      }
      return 0;
    case WM_LBUTTONUP:
      a->click(GET_X_LPARAM(l), GET_Y_LPARAM(l));
      return 0;
    case WM_CONTEXTMENU:
      a->menu();
      return 0;
    case TRAY:
      if (LOWORD(l) == WM_LBUTTONDBLCLK || LOWORD(l) == NIN_SELECT)
        a->toggle();
      if (LOWORD(l) == WM_CONTEXTMENU)
        a->menu();
      return 0;
    case WM_TIMECHANGE:
    case WM_SETTINGCHANGE:
      a->tz = localZone();
      a->draw();
      return 0;
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
      a->place();
      a->draw();
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(h, &ps);
      EndPaint(h, &ps);
      a->draw();
      return 0;
    }
    case WM_DESTROY:
      if (a->editor)
        DestroyWindow(a->editor);
      KillTimer(h, 1);
      KillTimer(h, 2);
      Shell_NotifyIconW(NIM_DELETE, &a->tray);
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(h, msg, w, l);
  }
  int run(HINSTANCE instance) {
    tz = localZone();
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory)) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   (IUnknown **)&write)))
      throw std::runtime_error(tr("无法初始化系统绘图接口", "Unable to initialize graphics"));
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_DATE_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = L"TiboIslandNative";
    RegisterClassW(&wc);
    wc.lpfnWndProc = EditorProc;
    wc.lpszClassName = L"TiboEditor";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"TiboIslandNative",
                           L"Reset Island", WS_POPUP, 0, 0, W, CLOSED, nullptr, nullptr, instance,
                           this);
    if (!hwnd)
      throw std::runtime_error(tr("创建窗口失败", "Unable to create window"));
    reload();
    place();
    draw();
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    notify();
    SetTimer(hwnd, 1, 1000, nullptr);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      if (!editor || !IsDialogMessageW(editor, &msg)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
    return 0;
  }
};
void output(const std::string &s, bool err = false) {
  HANDLE h = GetStdHandle(err ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
  DWORD mode, n;
  if (GetConsoleMode(h, &mode)) {
    auto w = wide(s);
    WriteConsoleW(h, w.c_str(), (DWORD)w.size(), &n, nullptr);
  } else
    WriteFile(h, s.data(), (DWORD)s.size(), &n, nullptr);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  systemEnglish = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_CHINESE;
  selectLanguage("auto");
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  int argc = 0;
  LPWSTR *args = CommandLineToArgvW(GetCommandLineW(), &argc);
  try {
    if (argc > 1) {
      HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
      if (h == nullptr || h == INVALID_HANDLE_VALUE)
        AttachConsole(ATTACH_PARENT_PROCESS);
      std::wstring command = args[1];
      auto state = command == L"--reset" || command == L"--help" ? Config{} : readState();
      selectLanguage(state.language);
      if (command == L"--set") {
        std::vector<std::string> options;
        for (int i = 2; i < argc; ++i)
          options.push_back(utf8(args[i]));
        state = setArguments(state, options);
        saveState(state);
      } else if (command == L"--reset" && argc == 2) {
        state = {};
        saveState(state);
      } else if (command == L"--help" && argc == 2) {
        output("--status | --set [--tibo <ISO8601|none>] [--cards <ISO8601,...|none>] [--source "
               "<URL|none>] [--language <zh|en|auto>] | --reset\n");
        LocalFree(args);
        return 0;
      } else if (command != L"--status" || argc != 2)
        throw std::runtime_error("Use --help for available commands");
      output(serialize(state));
      LocalFree(args);
      return 0;
    }
    LocalFree(args);
    args = nullptr;
    App app;
    return app.run(instance);
  } catch (const std::exception &e) {
    if (argc > 1)
      output(tr("设置失败: ", "Update failed: ") + e.what() + "\n", true);
    else
      MessageBoxW(nullptr, wide(e.what()).c_str(), L"Reset Island", MB_OK | MB_ICONERROR);
    if (args)
      LocalFree(args);
    return 1;
  }
}

// 大肥鲸验证完安全
