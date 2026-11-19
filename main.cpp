#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

const int HEADER_DRAG_H = 18;
const int CORNER_RESIZE = 14;

std::mutex state_mtx;
bool start_workspace = false;
std::vector<std::string> target_urls;
int file_picker_idx = -1;
int paste_target_idx = -1;

std::string escape_js(const std::string& s) {
    std::string res;
    for (char c : s) {
        if (c == '\\') res += "\\\\";
        else if (c == '\'') res += "\\'";
        else if (c == '\"') res += "\\\"";
        else if (c != '\n' && c != '\r') res += c;
    }
    return res;
}

std::string url_decode(const std::string& str) {
    std::string res;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int val = 0;
            std::istringstream is(str.substr(i + 1, 2));
            if (is >> std::hex >> val) {
                res += static_cast<char>(val);
                i += 2;
            } else {
                res += str[i];
            }
        } else if (str[i] == '+') {
            res += ' ';
        } else {
            res += str[i];
        }
    }
    return res;
}

std::string format_url(const std::string& raw) {
    std::string decoded = url_decode(raw);
    size_t sep = decoded.find("::");
    std::string mode = (sep != std::string::npos) ? decoded.substr(0, sep) : "url";
    std::string val  = (sep != std::string::npos) ? decoded.substr(sep + 2) : decoded;

    for (char& c : val) {
        if (c == '\\') c = '/';
    }

    if (mode == "file") {
        if (val.rfind("file://", 0) == 0) return val;
        if (!val.empty() && val[0] == '/') return "file://" + val;
        return "file:///" + val;
    }

    if (val.rfind("http://", 0) == 0 || val.rfind("https://", 0) == 0 || val.rfind("file://", 0) == 0) {
        return val;
    }
    return "https://" + val;
}

std::string get_menu_html(const std::string& bg_uri, const std::string& def_file) {
    std::string html = R"html(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<style>
  * { box-sizing: border-box; user-select: none; }
  body {
    margin: 0; padding: 0;
    background-color: #2b2b2b;
    background-image: url('__BG_URI__');
    background-size: cover;
    background-position: center;
    color: #222;
    font-family: Tahoma, Arial, sans-serif;
    display: flex; align-items: center; justify-content: center;
    height: 100vh; overflow: hidden;
  }
  .box {
    width: 760px;
    background: #f0f0f0;
    border: 1px solid #888;
    border-radius: 4px;
    padding: 18px 20px;
    box-shadow: 0 4px 12px rgba(0,0,0,0.45);
    display: flex; flex-direction: column; gap: 14px;
  }
  .top {
    display: flex; align-items: center; justify-content: space-between;
    border-bottom: 1px solid #ccc; padding-bottom: 10px;
  }
  .title { font-size: 16px; font-weight: bold; }
  .counts { display: flex; align-items: center; gap: 6px; font-size: 13px; }
  .list {
    display: flex; flex-direction: column; gap: 8px;
    max-height: 360px; overflow-y: auto;
  }
  .row {
    background: #e4e4e4; border: 1px solid #ccc;
    padding: 8px 10px; border-radius: 3px;
    display: flex; align-items: center; gap: 6px; font-size: 13px;
  }
  .lbl { font-weight: bold; width: 62px; }
  .inp {
    flex: 1; padding: 5px 8px; font-size: 13px;
    border: 1px solid #999; background: #fff;
    font-family: Consolas, monospace;
  }
  button {
    padding: 5px 10px; font-size: 12px;
    background: #dcdcdc; border: 1px solid #888;
    border-radius: 2px; cursor: pointer;
  }
  button:hover { background: #c8c8c8; }
  button.active {
    background: #557799; color: #fff; border-color: #335577; font-weight: bold;
  }
  .bottom {
    display: flex; justify-content: space-between; align-items: center;
    border-top: 1px solid #ccc; padding-top: 12px;
  }
  .info { font-size: 11px; color: #555; line-height: 1.35; }
  .go {
    padding: 8px 22px; font-size: 13px; font-weight: bold;
    background: #d0e0f0; border: 1px solid #557799;
  }
  .go:hover { background: #bcd4ec; }
</style>
</head>
<body>
  <div class="box">
    <div class="top">
      <div class="title">Настройка окон браузера</div>
      <div class="counts">
        <span>Количество окон:</span>
        <button id="cnt-1" onclick="setCount(1)">1</button>
        <button id="cnt-2" onclick="setCount(2)">2</button>
        <button id="cnt-3" onclick="setCount(3)">3</button>
        <button id="cnt-4" class="active" onclick="setCount(4)">4</button>
        <button id="cnt-6" onclick="setCount(6)">6</button>
      </div>
    </div>
    <div class="list" id="rows"></div>
    <div class="bottom">
      <div class="info">
        Перемещение: за верхний край (или Alt + ЛКМ), размер: правый нижний угол.<br>
        F1 — меню, F2 — сетка, F5 — тест JS, ESC — выход.
      </div>
      <button class="go" onclick="startApp()">Запустить</button>
    </div>
  </div>

<script>
  const items = [
    { mode: 'url',  val: 'https://www.google.com' },
    { mode: 'file', val: '__DEF_FILE__' },
    { mode: 'url',  val: 'https://www.wikipedia.org' },
    { mode: 'url',  val: 'https://news.ycombinator.com' },
    { mode: 'url',  val: 'https://example.com' },
    { mode: 'url',  val: 'https://example.org' }
  ];

  let count = 4;
  let activeIdx = 0;
  let seq = 0;

  function sendCmd(cmd, data) {
    seq++;
    document.title = cmd + '|' + seq + '|' + data;
  }

  function saveInputs() {
    for (let i = 0; i < count; i++) {
      const el = document.getElementById('inp-' + i);
      if (el) items[i].val = el.value;
    }
  }

  function setCount(n) {
    saveInputs();
    count = n;
    [1, 2, 3, 4, 6].forEach(v => {
      const b = document.getElementById('cnt-' + v);
      if (b) b.className = (v === n) ? 'active' : '';
    });
    render();
  }

  function render() {
    const root = document.getElementById('rows');
    root.innerHTML = '';
    for (let i = 0; i < count; i++) {
      const it = items[i];
      const isFile = it.mode === 'file';
      const row = document.createElement('div');
      row.className = 'row';
      row.innerHTML = `
        <div class="lbl">Окно #${i + 1}:</div>
        <button class="${!isFile ? 'active' : ''}" onclick="setMode(${i}, 'url')">Ссылка</button>
        <button class="${isFile ? 'active' : ''}" onclick="setMode(${i}, 'file')">Файл</button>
        <input class="inp" id="inp-${i}" value="${it.val}"
               onfocus="activeIdx = ${i}"
               oninput="items[${i}].val = this.value" />
        <button onclick="pasteText(${i})">Вставить</button>
        <button onclick="clearText(${i})">Очистить</button>
        ${isFile ? `<button onclick="pickFile(${i})">Обзор...</button>` : ''}
      `;
      root.appendChild(row);
    }
  }

  function setMode(i, m) {
    saveInputs();
    items[i].mode = m;
    if (m === 'file' && items[i].val.startsWith('http')) {
      items[i].val = '__DEF_FILE__';
    } else if (m === 'url' && !items[i].val.startsWith('http')) {
      items[i].val = 'https://example.com';
    }
    render();
  }

  function clearText(i) {
    items[i].val = '';
    const el = document.getElementById('inp-' + i);
    if (el) { el.value = ''; el.focus(); }
  }

  function pasteText(i) {
    activeIdx = i;
    sendCmd('CMD_PASTE', String(i));
  }

  window.setClipboardText = (i, txt) => {
    const idx = (i >= 0 && i < count) ? i : activeIdx;
    const el = document.getElementById('inp-' + idx);
    if (el) {
      el.value = txt;
      items[idx].val = txt;
      el.focus();
    }
  };

  function pickFile(i) {
    saveInputs();
    sendCmd('CMD_BROWSE', String(i));
  }

  window.setFilePath = (i, path) => {
    items[i].mode = 'file';
    items[i].val = path;
    render();
  };

  function startApp() {
    saveInputs();
    const out = [];
    for (let i = 0; i < count; i++) {
      let v = (items[i].val || '').trim();
      if (!v) v = 'https://example.com';
      out.push(encodeURIComponent(items[i].mode + '::' + v));
    }
    sendCmd('CMD_LAUNCH', out.join('|'));
  }

  render();
</script>
</body>
</html>
)html";

    size_t p1 = html.find("__BG_URI__");
    if (p1 != std::string::npos) html.replace(p1, 10, bg_uri);

    size_t p2 = 0;
    while ((p2 = html.find("__DEF_FILE__", p2)) != std::string::npos) {
        html.replace(p2, 12, def_file);
        p2 += def_file.length();
    }
    return html;
}

GLuint load_background(const std::string& dir, std::string& out_path) {
    const char* names[] = {
        "background.png", "background.jpg", "background.jpeg", "background.bmp",
        "../background.png", "../background.jpg", "../background.bmp"
    };

    int w = 0, h = 0, ch = 0;
    unsigned char* data = nullptr;

    for (const char* name : names) {
        std::string p = dir + name;
        data = stbi_load(p.c_str(), &w, &h, &ch, 4);
        if (data) {
            out_path = p;
            break;
        }
    }

    if (!data) {
        std::string bmp = dir + "background.bmp";
        w = 640; h = 400;
        std::vector<unsigned char> buf(w * h * 4);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                unsigned char c = ((x % 32 == 0) || (y % 32 == 0)) ? 52 : 42;
                size_t idx = static_cast<size_t>(y * w + x) * 4;
                buf[idx + 0] = c;
                buf[idx + 1] = c + 4;
                buf[idx + 2] = c + 10;
                buf[idx + 3] = 255;
            }
        }

        SDL_Surface* s = SDL_CreateRGBSurfaceFrom(
            buf.data(), w, h, 32, w * 4,
            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
        );
        if (s) {
            SDL_SaveBMP(s, bmp.c_str());
            SDL_FreeSurface(s);
            out_path = bmp;
        }

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        return tex;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return tex;
}

void draw_background(GLuint tex, int w, int h) {
    if (!tex) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(static_cast<float>(w), 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(static_cast<float>(w), static_cast<float>(h));
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, static_cast<float>(h));
    glEnd();

    glDisable(GL_TEXTURE_2D);
}

class AppConfig : public CefApp {
public:
    void OnBeforeCommandLineProcessing(
        const CefString& process_type,
        CefRefPtr<CefCommandLine> cmd) override 
    {
        cmd->AppendSwitchWithValue("renderer-process-limit", "4");
        cmd->AppendSwitch("process-per-site");
        cmd->AppendSwitchWithValue("js-flags", "--lite-mode");
        cmd->AppendSwitch("allow-file-access-from-files");
        cmd->AppendSwitch("disable-gpu");
        cmd->AppendSwitch("disable-gpu-compositing");
        cmd->AppendSwitch("disable-gpu-sandbox");
    }

    IMPLEMENT_REFCOUNTING(AppConfig);
};

class BrowserWin : public CefClient,
                   public CefRenderHandler,
                   public CefLifeSpanHandler,
                   public CefDisplayHandler,
                   public CefLoadHandler {
public:
    int id;
    int x = 0, y = 0;
    int w = 500, h = 350;
    GLuint tex = 0;

    std::mutex mtx;
    std::vector<uint8_t> buf;
    bool updated = false;

    CefRefPtr<CefBrowser> browser;

    BrowserWin(int win_id, int px, int py, int pw, int ph)
        : id(win_id), x(px), y(py), w(pw), h(ph) 
    {
        buf.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 240);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, buf.data());
    }

    ~BrowserWin() {
        if (tex) glDeleteTextures(1, &tex);
    }

    bool hit(int px, int py) const {
        return px >= x && px < (x + w) && py >= y && py < (y + h);
    }

    bool hit_drag(int px, int py) const {
        return hit(px, py) && (py - y) <= HEADER_DRAG_H;
    }

    bool hit_resize(int px, int py) const {
        return hit(px, py) && (px >= x + w - CORNER_RESIZE) && (py >= y + h - CORNER_RESIZE);
    }

    void resize(int nw, int nh) {
        nw = std::max(200, nw);
        nh = std::max(140, nh);
        if (nw == w && nh == h) return;

        {
            std::lock_guard<std::mutex> lock(mtx);
            w = nw;
            h = nh;
        }
        if (browser) {
            browser->GetHost()->WasResized();
            browser->GetHost()->Invalidate(PET_VIEW);
        }
    }

    void update_tex() {
        std::lock_guard<std::mutex> lock(mtx);
        if (!updated || buf.empty()) return;
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, buf.data());
        updated = false;
    }

    void run_js(const std::string& code) {
        if (browser && browser->GetMainFrame()) {
            browser->GetMainFrame()->ExecuteJavaScript(code, browser->GetMainFrame()->GetURL(), 0);
        }
    }

    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

    void OnAfterCreated(CefRefPtr<CefBrowser> b) override {
        browser = b;
        b->GetHost()->WasResized();
        b->GetHost()->Invalidate(PET_VIEW);
    }

    void OnLoadEnd(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> frame, int status) override {
        if (frame->IsMain()) {
            b->GetHost()->WasResized();
            b->GetHost()->Invalidate(PET_VIEW);
        }
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> b) override { browser = nullptr; }

    void OnTitleChange(CefRefPtr<CefBrowser> b, const CefString& title) override {
        std::string t = title.ToString();

        auto get_arg = [](const std::string& s) -> std::string {
            size_t p1 = s.find('|');
            if (p1 == std::string::npos) return "";
            size_t p2 = s.find('|', p1 + 1);
            if (p2 == std::string::npos) return "";
            return s.substr(p2 + 1);
        };

        if (t.rfind("CMD_LAUNCH|", 0) == 0) {
            std::stringstream ss(get_arg(t));
            std::string item;
            std::vector<std::string> list;
            while (std::getline(ss, item, '|')) {
                if (!item.empty()) list.push_back(format_url(item));
            }
            if (!list.empty()) {
                std::lock_guard<std::mutex> lock(state_mtx);
                target_urls = list;
                start_workspace = true;
            }
        } else if (t.rfind("CMD_BROWSE|", 0) == 0) {
            std::lock_guard<std::mutex> lock(state_mtx);
            file_picker_idx = std::atoi(get_arg(t).c_str());
        } else if (t.rfind("CMD_PASTE|", 0) == 0) {
            std::lock_guard<std::mutex> lock(state_mtx);
            paste_target_idx = std::atoi(get_arg(t).c_str());
        }
    }

    void GetViewRect(CefRefPtr<CefBrowser> b, CefRect& rect) override {
        std::lock_guard<std::mutex> lock(mtx);
        rect.Set(0, 0, w, h);
    }

    void OnPaint(CefRefPtr<CefBrowser> b, PaintElementType type,
                 const RectList& rects, const void* buffer, int pw, int ph) override 
    {
        if (type != PET_VIEW || !buffer || pw <= 0 || ph <= 0) return;
        std::lock_guard<std::mutex> lock(mtx);
        w = pw;
        h = ph;
        buf.resize(static_cast<size_t>(pw) * static_cast<size_t>(ph) * 4);
        std::memcpy(buf.data(), buffer, buf.size());
        updated = true;
    }

    IMPLEMENT_REFCOUNTING(BrowserWin);
};

void draw_window(CefRefPtr<BrowserWin>& win, bool active, bool border = true) {
    win->update_tex();

    float x1 = static_cast<float>(win->x);
    float y1 = static_cast<float>(win->y);
    float x2 = static_cast<float>(win->x + win->w);
    float y2 = static_cast<float>(win->y + win->h);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, win->tex);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x1, y1);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x2, y1);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x2, y2);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x1, y2);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    if (!border) return;

    if (active) glColor4f(0.85f, 0.85f, 0.85f, 1.0f);
    else glColor4f(0.35f, 0.35f, 0.35f, 0.9f);

    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(x1, y1);
        glVertex2f(x2, y1);
        glVertex2f(x2, y2);
        glVertex2f(x1, y2);
    glEnd();
}

void layout_grid(std::vector<CefRefPtr<BrowserWin>>& wins, int screen_w, int screen_h) {
    int n = static_cast<int>(wins.size());
    if (n == 0) return;

    int cols = (n == 1) ? 1 : (n <= 4 ? 2 : 3);
    int rows = (n + cols - 1) / cols;

    int gap = 18;
    int w = (screen_w - gap * (cols + 1)) / cols;
    int h = (screen_h - gap * (rows + 1)) / rows;

    for (int i = 0; i < n; ++i) {
        int c = i % cols;
        int r = i / cols;
        wins[i]->x = gap + c * (w + gap);
        wins[i]->y = gap + r * (h + gap);
        wins[i]->resize(w, h);
    }
}

std::string choose_file_dialog() {
#ifdef _WIN32
    FILE* p = _popen(
        "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; "
        "$f = New-Object System.Windows.Forms.OpenFileDialog; "
        "$f.Filter = 'HTML Files (*.html;*.htm)|*.html;*.htm|All Files (*.*)|*.*'; "
        "if($f.ShowDialog() -eq 'OK'){Write-Output $f.FileName}\"", "r");
    if (!p) return "";
    char buf[1024];
    std::string res;
    if (fgets(buf, sizeof(buf), p) != nullptr) {
        res = buf;
        while (!res.empty() && (res.back() == '\n' || res.back() == '\r')) res.pop_back();
    }
    _pclose(p);
    for (char& c : res) if (c == '\\') c = '/';
    return res;
#else
    FILE* p = popen("zenity --file-selection --title='Выберите HTML файл' --file-filter='*.html *.htm *' 2>/dev/null || kdialog --getopenfilename ~ '*.html *.htm' 2>/dev/null", "r");
    if (!p) return "";
    char buf[1024];
    std::string res;
    if (fgets(buf, sizeof(buf), p) != nullptr) {
        res = buf;
        while (!res.empty() && (res.back() == '\n' || res.back() == '\r')) res.pop_back();
    }
    pclose(p);
    return res;
#endif
}

void paste_clipboard(CefRefPtr<BrowserWin>& menu_win, int idx) {
    if (!menu_win || !SDL_HasClipboardText()) return;
    char* clip = SDL_GetClipboardText();
    if (clip) {
        std::string safe = escape_js(clip);
        SDL_free(clip);
        menu_win->run_js("window.setClipboardText(" + std::to_string(idx) + ", '" + safe + "');");
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    CefMainArgs main_args(GetModuleHandle(nullptr));
#else
    CefMainArgs main_args(argc, argv);
#endif

    CefRefPtr<AppConfig> app(new AppConfig());
    int code = CefExecuteProcess(main_args, app, nullptr);
    if (code >= 0) return code;

    CefSettings settings;
    settings.windowless_rendering_enabled = true;
    settings.no_sandbox = true;
    if (!CefInitialize(main_args, settings, app, nullptr)) return -1;

    SDL_Init(SDL_INIT_VIDEO);

    int screen_w = 1360;
    int screen_h = 840;

    SDL_Window* window = SDL_CreateWindow(
        "Workspace",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        screen_w, screen_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
    );
    SDL_SetWindowBordered(window, SDL_FALSE);

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1);

    char* base_ptr = SDL_GetBasePath();
    std::string base_dir = base_ptr ? base_ptr : "./";
    if (base_ptr) SDL_free(base_ptr);
    for (char& c : base_dir) if (c == '\\') c = '/';

    std::string bg_path;
    GLuint bg_tex = load_background(base_dir, bg_path);
    std::string bg_uri = format_url("file::" + bg_path);

    const std::string demo_file = base_dir + "demo_local.html";
    const std::string menu_file = base_dir + "launcher.html";
    {
        std::ofstream df(demo_file);
        df << "<!DOCTYPE html><html><body style='background:#f5f5f5;color:#222;font-family:sans-serif;padding:20px;'>"
              "<h3>Локальный файл (demo_local.html)</h3><p>Загружен из папки программы.</p></body></html>";
    }
    {
        std::ofstream mf(menu_file);
        mf << get_menu_html(bg_uri, demo_file);
    }

    bool in_menu = true;
    CefRefPtr<BrowserWin> menu_win;
    std::vector<CefRefPtr<BrowserWin>> wins;

    auto open_menu = [&]() {
        for (auto& w : wins) {
            if (w->browser) w->browser->GetHost()->CloseBrowser(true);
        }
        wins.clear();

        menu_win = new BrowserWin(0, 0, 0, screen_w, screen_h);
        CefWindowInfo info;
        info.SetAsWindowless(0);
        CefBrowserSettings bset;
        bset.windowless_frame_rate = 60;

        CefBrowserHost::CreateBrowser(info, menu_win, format_url("file::" + menu_file), bset, nullptr, nullptr);
        in_menu = true;
    };

    auto open_workspace = [&](const std::vector<std::string>& urls) {
        if (menu_win && menu_win->browser) {
            menu_win->browser->GetHost()->CloseBrowser(true);
        }
        menu_win = nullptr;

        wins.clear();
        for (size_t i = 0; i < urls.size(); ++i) {
            wins.push_back(new BrowserWin(static_cast<int>(i + 1), 0, 0, 500, 350));
        }

        layout_grid(wins, screen_w, screen_h);

        for (size_t i = 0; i < urls.size(); ++i) {
            CefWindowInfo info;
            info.SetAsWindowless(0);
            CefBrowserSettings bset;
            bset.windowless_frame_rate = 60;
            bset.background_color = CefColorSetARGB(255, 255, 255, 255);

            CefBrowserHost::CreateBrowser(info, wins[i], urls[i], bset, nullptr, nullptr);
        }

        in_menu = false;
    };

    open_menu();

    CefRefPtr<BrowserWin> dragged = nullptr;
    CefRefPtr<BrowserWin> resized = nullptr;
    int off_x = 0, off_y = 0;
    int start_w = 0, start_h = 0, start_mx = 0, start_my = 0;

    bool running = true;
    while (running) {
        {
            std::vector<std::string> to_open;
            int pick_i = -1;
            int paste_i = -1;

            {
                std::lock_guard<std::mutex> lock(state_mtx);
                if (start_workspace) {
                    start_workspace = false;
                    to_open = target_urls;
                }
                if (file_picker_idx >= 0) {
                    pick_i = file_picker_idx;
                    file_picker_idx = -1;
                }
                if (paste_target_idx >= 0) {
                    paste_i = paste_target_idx;
                    paste_target_idx = -1;
                }
            }

            if (!to_open.empty()) {
                open_workspace(to_open);
            }
            if (pick_i >= 0 && in_menu && menu_win) {
                std::string path = choose_file_dialog();
                if (!path.empty()) {
                    menu_win->run_js("window.setFilePath(" + std::to_string(pick_i) + ", '" + escape_js(path) + "');");
                }
            }
            if (paste_i >= 0 && in_menu && menu_win) {
                paste_clipboard(menu_win, paste_i);
            }
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;

            if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode sym = ev.key.keysym.sym;
                bool ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;

                if (sym == SDLK_ESCAPE) {
                    running = false;
                } else if (sym == SDLK_F1 && !in_menu) {
                    open_menu();
                } else if (sym == SDLK_F2 && !in_menu) {
                    layout_grid(wins, screen_w, screen_h);
                } else if (sym == SDLK_F5 && !in_menu) {
                    for (auto& w : wins) {
                        w->run_js("document.body.style.backgroundColor = '#222'; document.body.style.color = '#fff';");
                    }
                } else if (ctrl && sym == SDLK_v) {
                    if (in_menu && menu_win) {
                        paste_clipboard(menu_win, -1);
                    } else if (!wins.empty() && wins.back()->browser) {
                        wins.back()->browser->GetMainFrame()->Paste();
                    }
                }
            }

            if (in_menu && menu_win && menu_win->browser) {
                if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    SDL_RaiseWindow(window);
                    SDL_SetWindowInputFocus(window);
                    CefMouseEvent cev;
                    cev.x = ev.button.x;
                    cev.y = ev.button.y;
                    menu_win->browser->GetHost()->SetFocus(true);
                    menu_win->browser->GetHost()->SendMouseClickEvent(cev, MBT_LEFT, false, 1);
                } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                    CefMouseEvent cev;
                    cev.x = ev.button.x;
                    cev.y = ev.button.y;
                    menu_win->browser->GetHost()->SendMouseClickEvent(cev, MBT_LEFT, true, 1);
                } else if (ev.type == SDL_MOUSEMOTION) {
                    CefMouseEvent cev;
                    cev.x = ev.motion.x;
                    cev.y = ev.motion.y;
                    menu_win->browser->GetHost()->SendMouseMoveEvent(cev, false);
                } else if (ev.type == SDL_MOUSEWHEEL) {
                    int mx, my;
                    SDL_GetMouseState(&mx, &my);
                    CefMouseEvent cev;
                    cev.x = mx;
                    cev.y = my;
                    menu_win->browser->GetHost()->SendMouseWheelEvent(cev, ev.wheel.x * 120, ev.wheel.y * 120);
                }
                continue;
            }

            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int mx = ev.button.x;
                int my = ev.button.y;
                bool alt = (SDL_GetModState() & KMOD_ALT) != 0;

                for (int i = static_cast<int>(wins.size()) - 1; i >= 0; --i) {
                    auto w = wins[i];
                    if (w->hit(mx, my)) {
                        wins.erase(wins.begin() + i);
                        wins.push_back(w);

                        if (w->hit_resize(mx, my)) {
                            resized = w;
                            start_w = w->w;
                            start_h = w->h;
                            start_mx = mx;
                            start_my = my;
                        } else if (alt || w->hit_drag(mx, my)) {
                            dragged = w;
                            off_x = mx - w->x;
                            off_y = my - w->y;
                        } else if (w->browser) {
                            CefMouseEvent cev;
                            cev.x = mx - w->x;
                            cev.y = my - w->y;
                            w->browser->GetHost()->SetFocus(true);
                            w->browser->GetHost()->SendMouseClickEvent(cev, MBT_LEFT, false, 1);
                        }
                        break;
                    }
                }
            }

            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                if (dragged) {
                    dragged = nullptr;
                } else if (resized) {
                    resized = nullptr;
                } else if (!wins.empty() && wins.back()->browser) {
                    auto active = wins.back();
                    CefMouseEvent cev;
                    cev.x = ev.button.x - active->x;
                    cev.y = ev.button.y - active->y;
                    active->browser->GetHost()->SendMouseClickEvent(cev, MBT_LEFT, true, 1);
                }
            }

            if (ev.type == SDL_MOUSEMOTION) {
                int mx = ev.motion.x;
                int my = ev.motion.y;

                if (dragged) {
                    int max_x = std::max(0, screen_w - dragged->w);
                    int max_y = std::max(0, screen_h - dragged->h);
                    dragged->x = std::clamp(mx - off_x, 0, max_x);
                    dragged->y = std::clamp(my - off_y, 0, max_y);
                } else if (resized) {
                    int max_w = screen_w - resized->x;
                    int max_h = screen_h - resized->y;
                    int nw = std::clamp(start_w + (mx - start_mx), 200, max_w);
                    int nh = std::clamp(start_h + (my - start_my), 140, max_h);
                    resized->resize(nw, nh);
                } else {
                    for (int i = static_cast<int>(wins.size()) - 1; i >= 0; --i) {
                        if (wins[i]->hit(mx, my) && wins[i]->browser) {
                            CefMouseEvent cev;
                            cev.x = mx - wins[i]->x;
                            cev.y = my - wins[i]->y;
                            wins[i]->browser->GetHost()->SendMouseMoveEvent(cev, false);
                            break;
                        }
                    }
                }
            }

            if (ev.type == SDL_MOUSEWHEEL) {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                for (int i = static_cast<int>(wins.size()) - 1; i >= 0; --i) {
                    if (wins[i]->hit(mx, my) && wins[i]->browser) {
                        CefMouseEvent cev;
                        cev.x = mx - wins[i]->x;
                        cev.y = my - wins[i]->y;
                        wins[i]->browser->GetHost()->SendMouseWheelEvent(cev, ev.wheel.x * 120, ev.wheel.y * 120);
                        break;
                    }
                }
            }
        }

        CefDoMessageLoopWork();

        glViewport(0, 0, screen_w, screen_h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, screen_w, screen_h, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glClearColor(0.18f, 0.18f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        draw_background(bg_tex, screen_w, screen_h);

        if (in_menu && menu_win) {
            draw_window(menu_win, true, false);
        } else {
            for (size_t i = 0; i < wins.size(); ++i) {
                draw_window(wins[i], i == wins.size() - 1, true);
            }
        }

        SDL_GL_SwapWindow(window);
    }

    if (bg_tex) glDeleteTextures(1, &bg_tex);
    if (menu_win && menu_win->browser) menu_win->browser->GetHost()->CloseBrowser(true);
    menu_win = nullptr;
    wins.clear();
    CefShutdown();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}