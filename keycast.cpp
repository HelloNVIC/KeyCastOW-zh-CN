// Copyright © 2014 Brook Hong. All Rights Reserved.
//
// keycast.cpp — 显示引擎与应用程序主体
// 负责按键标签的渲染、生命周期管理、画布/窗口管理、设置持久化及应用程序主循环。

// 构建命令参考：
// msbuild /p:platform=win32 /p:Configuration=Release
// msbuild keycastow.vcxproj /t:Clean
// rc keycastow.rc && cl -DUNICODE -D_UNICODE keycast.cpp keylog.cpp keycastow.res user32.lib shell32.lib gdi32.lib Comdlg32.lib comctl32.lib

#include <windows.h>
#include <windowsx.h>
#include <Commctrl.h>
#include <stdio.h>
#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")

#include <gdiplus.h>
using namespace Gdiplus;

#include "resource.h"
#include "timer.h"
CTimer showTimer;      // 标签动画定时器，驱动淡入/停留/淡出
CTimer previewTimer;   // 设置对话框中的预览定时器

WCHAR iniFile[MAX_PATH];  // INI 配置文件路径（与 exe 同名 .ini）

#define MAXCHARS 4096
WCHAR textBuffer[MAXCHARS];        // 所有标签共享的文本缓冲区，避免频繁内存分配
LPCWSTR textBufferEnd = textBuffer + MAXCHARS;  // 缓冲区末尾指针，用于溢出检测

// KeyLabel — 单个按键标签
// 每个标签对应屏幕上一行按键显示，包含位置、文本指针、计时和淡出标志
struct KeyLabel {
    RectF rect;       // 标签在画布上的绘制区域
    WCHAR *text;      // 指向 textBuffer 中此标签文本的起始位置
    DWORD length;     // 文本字符数
    int time;         // 剩余显示时间（毫秒），递减至 0 后标签被清除
    BOOL fade;        // 是否正在淡出（TRUE=正在淡出，FALSE=保持显示）
    KeyLabel() {
        text = textBuffer;
        length = 0;
    }
};

// LabelSettings — 标签外观设置
// 包含字体、颜色、不透明度、边框和圆角等参数
struct LabelSettings {
    DWORD keyStrokeDelay;   // 按键组合延迟：在此时间内连续按键会合并到同一标签
    DWORD lingerTime;       // 标签停留时间（毫秒）：淡出前保持完全显示的时间
    DWORD fadeDuration;     // 淡出持续时间（毫秒）：从完全显示到完全透明的时间
    LOGFONT font;           // 标签字体（GDI LOGFONT 结构）
    COLORREF bgColor, textColor, borderColor;  // 背景色、文字色、边框色（BGR 格式）
    DWORD bgOpacity, textOpacity, borderOpacity; // 背景不透明度、文字不透明度、边框不透明度（0~255）
    int borderSize;         // 边框粗细（像素）
    int cornerSize;         // 圆角半径（像素），0 为直角矩形
};
LabelSettings labelSettings, previewLabelSettings;  // 当前生效设置 / 设置对话框预览设置

// BrandingSettings — 品牌标识外观设置
// 包含字体、颜色、不透明度和背景留白大小等参数
struct BrandingSettings {
    LOGFONT font;           // 品牌标识字体（GDI LOGFONT 结构）
    COLORREF bgColor, textColor;  // 背景色、文字色（BGR 格式）
    DWORD bgOpacity;        // 背景不透明度（0~255）
    int borderSize;         // 背景留白大小（像素）
};
BrandingSettings brandingSettings, previewBrandingSettings;  // 当前生效设置 / 设置对话框预览设置
DWORD labelSpacing;        // 标签行间距（像素）
BOOL visibleShift = FALSE;  // 是否单独显示 Shift 键（默认不单独显示）
BOOL visibleModifier = TRUE; // 是否单独显示修饰键（Ctrl/Alt/Win）
BOOL mouseCapturing = TRUE;  // 是否捕获并显示鼠标事件
BOOL mouseCapturingMod = FALSE; // 是否仅在修饰键按下时捕获鼠标
BOOL keyAutoRepeat = TRUE;   // 是否允许按键自动重复显示（长按连续触发）
BOOL mergeMouseActions = TRUE; // 是否合并鼠标按下/释放为单击/双击
int alignment = 1;           // 对齐方式：0=左对齐，1=右对齐
BOOL down = FALSE;            // 是否从定位点向下增长显示标签
BOOL onlyCommandKeys = FALSE; // 是否仅显示组合键（含修饰键的按键）
BOOL positioning = FALSE;    // 是否处于定位模式（用户拖拽调整显示区域）
BOOL draggableLabel = FALSE; // 标签是否可拖拽
UINT tcModifiers = MOD_ALT;  // 切换开关热键的修饰键（默认 Alt）
UINT tcKey = 0x42;      // 切换开关热键的主键（0x42 = 'B'，即 Alt+B）
Color clearColor(0, 127, 127, 127);  // 画布清除色（ARGB，alpha=0 完全透明）
#define BRANDINGMAX 256
WCHAR branding[BRANDINGMAX];  // 品牌标识文本（显示在屏幕角落，双击可打开设置）
WCHAR comboChars[4];          // 组合键连接符方案，如 "[+]" → [Ctrl + C]
POINT deskOrigin;             // 显示区域原点坐标（屏幕坐标）

#define MAXLABELS 60           // 标签数组最大容量
KeyLabel keyLabels[MAXLABELS]; // 标签数组
DWORD maximumLines = 10;       // 最大显示行数（用户可配置）
DWORD labelCount = 0;          // 当前活跃标签数（由 prepareLabels 计算）
RECT desktopRect;              // 当前显示器工作区域
SIZE canvasSize;               // 画布像素尺寸
POINT canvasOrigin;            // 画布在屏幕上的起始坐标

#include "keycast.h"
#include "keylog.h"

WCHAR *szWinName = L"KeyCastOW";  // 主窗口类名
HWND hMainWnd;           // 主显示窗口句柄（分层透明窗口）
HWND hDlgSettings;       // 设置对话框句柄
RECT settingsDlgRect;    // 设置对话框位置
HWND hWndStamp;          // 品牌/配置标识小窗口句柄（可拖拽）
HINSTANCE hInstance;     // 应用程序实例句柄
Graphics * gCanvas = NULL;  // GDI+ 画布 Graphics 对象
Font * fontPlus = NULL;     // GDI+ 字体对象（由 labelSettings.font 创建）

// 自定义消息和菜单 ID
#define IDI_TRAY       100   // 系统托盘图标 ID
#define WM_TRAYMSG     101   // 托盘消息自定义消息值
#define MENU_CONFIG    32    // 菜单：设置
#define MENU_EXIT      33    // 菜单：退出
#define MENU_RESTORE   34    // 菜单：恢复默认设置

// 前向声明：将文本显示为按键标签
void showText(LPCWSTR text, int behavior);

#ifdef _DEBUG
// ---- 调试专用：录制/回放功能 ----
WCHAR capFile[MAX_PATH];      // 按键录制文件路径
FILE *capStream = NULL;       // 按键录制文件流
WCHAR recordFN[MAX_PATH];     // 回放文件路径
int replayStatus = 0;         // 回放状态：0=停止，1=播放中，2=停止请求
#define MENU_REPLAY    35     // 菜单：回放

// Displayed — 录制文件中的一条按键记录
struct Displayed {
    DWORD tm;         // 时间戳（GetTickCount）
    int behavior;     // 显示行为（0=追加，1=新行，2=替换）
    size_t len;       // 文本长度
    Displayed(DWORD t, int b, size_t l) {
        tm = t;
        behavior = b;
        len = l;
    }
};

// replay — 在独立线程中回放录制的按键
DWORD WINAPI replay(LPVOID ptr)
{
    replayStatus = 1;
    FILE *stream;
    WCHAR tmp[256];
    errno_t err = _wfopen_s(&stream, (LPCWSTR)ptr, L"rb");
    Displayed dp(0, 0, 0);
    fread(&dp, sizeof(Displayed), 1, stream);
    fread(tmp, sizeof(WCHAR), dp.len, stream);
    showText(tmp, dp.behavior);
    DWORD lastTm = dp.tm;
    while(replayStatus == 1 && fread(&dp, sizeof(Displayed), 1, stream) == 1) {
        Sleep(dp.tm - lastTm);
        lastTm = dp.tm;
        fread(tmp, sizeof(WCHAR), dp.len, stream);
        tmp[dp.len] = '\0';
        showText(tmp, dp.behavior);
    }
    fclose(stream);
    replayStatus = 0;
    return 0;
}
#include <sstream>
WCHAR logFile[MAX_PATH];
FILE *logStream;
// log — 调试日志输出
void log(const std::stringstream & line) {
    fprintf(logStream,"%s",line.str().c_str());
}
#endif

// BR — 将 BGR 格式的 COLORREF 与 alpha 合成为 GDI+ ARGB 颜色值
// Windows COLORREF 是 0x00BBGGRR，GDI+ Color 需要 0xAARRGGBB
#define BR(alpha, bgr) (alpha<<24|bgr>>16|(bgr&0x0000ff00)|(bgr&0x000000ff)<<16)

// stamp — 在品牌标识小窗口上绘制文本
// 使用分层窗口 (UpdateLayeredWindow) 实现带透明度的文本渲染
void stamp(HWND hwnd, LPCWSTR text) {
    RECT rt;
    GetWindowRect(hwnd,&rt);
    HDC hdc = GetDC(hwnd);
    HDC memDC = ::CreateCompatibleDC(hdc);
    HBITMAP memBitmap = ::CreateCompatibleBitmap(hdc, desktopRect.right - desktopRect.left, desktopRect.bottom - desktopRect.top);
    ::SelectObject(memDC,memBitmap);
    Graphics g(memDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.Clear(clearColor);

    HFONT hStampFont = CreateFontIndirect(&brandingSettings.font);
    SelectObject(memDC, hStampFont);
    Font stampFont(memDC, hStampFont);

    RectF rc((REAL)brandingSettings.borderSize, (REAL)brandingSettings.borderSize, 0.0, 0.0);
    SizeF stringSize, layoutSize((REAL)desktopRect.right - desktopRect.left-2*brandingSettings.borderSize, (REAL)desktopRect.bottom - desktopRect.top-2*brandingSettings.borderSize);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    g.MeasureString(text, wcslen(text), &stampFont, layoutSize, &format, &stringSize);
    rc.Width = stringSize.Width;
    rc.Height = stringSize.Height;
    SIZE wndSize = {2*brandingSettings.borderSize+(LONG)rc.Width, 2*brandingSettings.borderSize+(LONG)rc.Height};
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, wndSize.cx, wndSize.cy, SWP_NOMOVE|SWP_NOACTIVATE);

    SolidBrush bgBrush(Color::Color(BR(brandingSettings.bgOpacity, brandingSettings.bgColor)));
    g.FillRectangle(&bgBrush, 0.0f, 0.0f, (REAL)wndSize.cx, (REAL)wndSize.cy);
    SolidBrush brushPlus(Color::Color(BR(255, brandingSettings.textColor)));
    g.DrawString(text, wcslen(text), &stampFont, rc, &format, &brushPlus);

    POINT ptSrc = {0, 0};
    POINT ptDst = {rt.left, rt.top};
    BLENDFUNCTION blendFunction;
    blendFunction.AlphaFormat = AC_SRC_ALPHA;
    blendFunction.BlendFlags = 0;
    blendFunction.BlendOp = AC_SRC_OVER;
    blendFunction.SourceConstantAlpha = 255;
    ::UpdateLayeredWindow(hwnd,hdc,&ptDst,&wndSize,memDC,&ptSrc,0,&blendFunction,2);
    ::DeleteDC(memDC);
    ::DeleteObject(memBitmap);
    DeleteObject(hStampFont);
    ReleaseDC(hwnd, hdc);
}
// updateLayeredWindow — 将离屏画布刷新到分层窗口
// gCanvas 先在内存 DC 中绘制，最后通过 UpdateLayeredWindow 一次性合成到屏幕，
// 这样可以获得逐像素 Alpha 透明效果，并避免闪烁。
void updateLayeredWindow(HWND hwnd) {
    POINT ptSrc = {0, 0};
    BLENDFUNCTION blendFunction;
    blendFunction.AlphaFormat = AC_SRC_ALPHA;
    blendFunction.BlendFlags = 0;
    blendFunction.BlendOp = AC_SRC_OVER;
    blendFunction.SourceConstantAlpha = 255;
    HDC hdcBuf = gCanvas->GetHDC();
    HDC hdc = GetDC(hwnd);
    ::UpdateLayeredWindow(hwnd,hdc,&canvasOrigin,&canvasSize,hdcBuf,&ptSrc,0,&blendFunction,2);
    ReleaseDC(hwnd, hdc);
    gCanvas->ReleaseHDC(hdcBuf);
}
// eraseLabel — 擦除指定标签的绘制区域
// 用 clearColor（完全透明色）覆盖标签占据的矩形区域，包括边框范围
void eraseLabel(int i) {
    RectF &rt = keyLabels[i].rect;
    RectF rc(rt.X-labelSettings.borderSize, rt.Y-labelSettings.borderSize, rt.Width+2*labelSettings.borderSize+1, rt.Height+2*labelSettings.borderSize+1);
    gCanvas->SetClip(rc);
    gCanvas->Clear(clearColor);
    gCanvas->ResetClip();
}
// drawLabelFrame — 绘制标签的背景矩形和边框
// cornerSize > 0 时绘制圆角矩形，否则绘制直角矩形
void drawLabelFrame(Graphics* g, const Pen* pen, const Brush* brush, RectF &rc, REAL cornerSize) {
    if(cornerSize > 0) {
        GraphicsPath path;
        REAL dx = rc.Width - cornerSize, dy = rc.Height - cornerSize;
        path.AddArc(rc.X, rc.Y, cornerSize, cornerSize, 170, 90);
        path.AddArc(rc.X + dx, rc.Y, cornerSize, cornerSize, 270, 90);
        path.AddArc(rc.X + dx, rc.Y + dy, cornerSize, cornerSize, 0, 90);
        path.AddArc(rc.X, rc.Y + dy, cornerSize, cornerSize, 90, 90);
        path.CloseFigure();

        g->DrawPath(pen, &path);
        g->FillPath(brush, &path);
    } else {
        g->DrawRectangle(pen, rc.X, rc.Y, rc.Width, rc.Height);
        g->FillRectangle(brush, rc.X, rc.Y, rc.Width, rc.Height);
    }
}
// updateLabel — 重新绘制指定索引的标签
// 先擦除旧内容，再根据当前 time 计算淡出比例 r，绘制背景、边框和文字
void updateLabel(int i) {
    eraseLabel(i);

    if(keyLabels[i].length > 0) {
        RectF &rc = keyLabels[i].rect;
        REAL r = 1.0f*keyLabels[i].time/labelSettings.fadeDuration;
        r = (r > 1.0f) ? 1.0f : r;
        PointF origin(rc.X, rc.Y);
        gCanvas->MeasureString(keyLabels[i].text, keyLabels[i].length, fontPlus, origin, &rc);
        rc.Width = (rc.Width < labelSettings.cornerSize) ? labelSettings.cornerSize : rc.Width;
        if(alignment) {
            rc.X = canvasSize.cx - rc.Width - labelSettings.borderSize;
        } else {
            rc.X = (REAL)labelSettings.borderSize;
        }
        rc.Height = (rc.Height < labelSettings.cornerSize) ? labelSettings.cornerSize : rc.Height;
        int bgAlpha = (int)(r*labelSettings.bgOpacity), textAlpha = (int)(r*labelSettings.textOpacity), borderAlpha = (int)(r*labelSettings.borderOpacity);
        Pen penPlus(Color::Color(BR(borderAlpha, labelSettings.borderColor)), labelSettings.borderSize+0.0f);
        SolidBrush brushPlus(Color::Color(BR(bgAlpha, labelSettings.bgColor)));
        drawLabelFrame(gCanvas, &penPlus, &brushPlus, rc, (REAL)labelSettings.cornerSize);
        SolidBrush textBrushPlus(Color(BR(textAlpha, labelSettings.textColor)));
        gCanvas->DrawString( keyLabels[i].text,
                keyLabels[i].length,
                fontPlus,
                PointF(rc.X, rc.Y),
                &textBrushPlus);
    }
}

// fadeLastLabel — 设置最后一个标签的淡出标志
// whether=TRUE 表示标签将在停留时间结束后开始淡出
// whether=FALSE 表示标签保持显示（如修饰键持续按下时）
void fadeLastLabel(BOOL whether) {
    keyLabels[labelCount-1].fade = whether;
}

static int newStrokeCount = 0;   // 新按键组合倒计时，用于判断后续按键是否合并到当前标签
#define SHOWTIMER_INTERVAL 40    // 动画定时器间隔（毫秒），约 25 FPS
static int deferredTime;         // 延迟标签的剩余等待时间
WCHAR deferredLabel[64];         // 延迟标签文本（用于区分单击/双击，延迟显示鼠标按下）

// startFade — 定时器回调，驱动所有标签的动画
// 每次调用处理：延迟标签更新、标签时间递减、淡出绘制、过期标签清除
static void startFade() {
    if(newStrokeCount > 0) {
        newStrokeCount -= SHOWTIMER_INTERVAL;
    }
    DWORD i = 0;
    BOOL dirty = FALSE;

    if (wcslen(deferredLabel) > 0) {
        // update deferred label if it exists
        if (deferredTime > 0) {
            deferredTime -= SHOWTIMER_INTERVAL;
        } else {
            showText(deferredLabel, 1);
            fadeLastLabel(FALSE);
            deferredLabel[0] = '\0';
        }
    }

    for(i = 0; i < labelCount; i++) {
        RectF &rt = keyLabels[i].rect;
        if(keyLabels[i].time > (int)labelSettings.fadeDuration) {
            if(keyLabels[i].fade) {
                keyLabels[i].time -= SHOWTIMER_INTERVAL;
            }
        } else if(keyLabels[i].time >= SHOWTIMER_INTERVAL) {
            if(keyLabels[i].fade) {
                keyLabels[i].time -= SHOWTIMER_INTERVAL;
            }
            updateLabel(i);
            dirty = TRUE;
        } else {
            keyLabels[i].time = 0;
            if(keyLabels[i].length){
                eraseLabel(i);
                // erase keyLabels[i].length times to avoid remaining shadow
                keyLabels[i].length--;
                dirty = TRUE;
            }
        }
    }
    if(dirty) {
        updateLayeredWindow(hMainWnd);
    }
}

// outOfLine — 检测追加文本后标签是否会超出画布宽度
// 返回 true 表示追加后超出画布宽度，需要换行显示
bool outOfLine(LPCWSTR text) {
    size_t newLen = wcslen(text);
    if(keyLabels[labelCount-1].text+keyLabels[labelCount-1].length+newLen >= textBufferEnd) {
        wcscpy_s(textBuffer, MAXCHARS, keyLabels[labelCount-1].text);
        keyLabels[labelCount-1].text = textBuffer;
    }
    LPWSTR tmp = keyLabels[labelCount-1].text + keyLabels[labelCount-1].length;
    wcscpy_s(tmp, (textBufferEnd-tmp), text);
    RectF box;
    PointF origin(0, 0);
    gCanvas->MeasureString(keyLabels[labelCount-1].text, keyLabels[labelCount-1].length, fontPlus, origin, &box);
    int cx = (int)box.Width+2*labelSettings.cornerSize+labelSettings.borderSize*2;
    bool out = cx >= canvasSize.cx;
    return out;
}
/*
 * showText — 将文本加入显示队列
 *
 * behavior 0: append text to last label（追加到上一行）
 * behavior 1: create a new label with text（新建一行）
 * behavior 2: replace last label with text（替换上一行）
 * behavior 3: deferred label（延迟显示，用于鼠标按下事件，等待是否形成单击/双击）
 */
void showText(LPCWSTR text, int behavior = 0) {
    SetWindowPos(hMainWnd,HWND_TOPMOST,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE);
    size_t newLen = wcslen(text);

#ifdef _DEBUG
    if(replayStatus == 0 && capStream) {
        Displayed dp(GetTickCount(), behavior, newLen);
        fwrite(&dp, sizeof(Displayed), 1, capStream);
        fwrite(text, sizeof(WCHAR), newLen, capStream);
        fflush(capStream);
    }
#endif

    DWORD i;
    if (behavior == 2) {
        wcscpy_s(keyLabels[labelCount-1].text, textBufferEnd-keyLabels[labelCount-1].text, text);
        keyLabels[labelCount-1].length = newLen;
    } else if (behavior == 3) {
        wcscpy_s(deferredLabel, 64, text);
        deferredTime = 120;
    } else if (behavior == 1 || (newStrokeCount <= 0) || outOfLine(text)) {
        for (i = 1; i < labelCount; i++) {
            if(keyLabels[i].time > 0) {
                break;
            }
        }
        for (; i < labelCount; i++) {
            eraseLabel(i-1);
            keyLabels[i-1].text = keyLabels[i].text;
            keyLabels[i-1].length = keyLabels[i].length;
            keyLabels[i-1].time = keyLabels[i].time;
            keyLabels[i-1].rect.X = keyLabels[i].rect.X;
            keyLabels[i-1].fade = TRUE;
            updateLabel(i-1);
            eraseLabel(i);
        }
        if(labelCount > 1) {
            keyLabels[labelCount-1].text = keyLabels[labelCount-2].text + keyLabels[labelCount-2].length;
        }
        if(keyLabels[labelCount-1].text+newLen >= textBufferEnd) {
            keyLabels[labelCount-1].text = textBuffer;
        }
        wcscpy_s(keyLabels[labelCount-1].text, textBufferEnd-keyLabels[labelCount-1].text, text);
        keyLabels[labelCount-1].length = newLen;
    } else {
        LPWSTR tmp = keyLabels[labelCount-1].text + keyLabels[labelCount-1].length;
        if(tmp+newLen >= textBufferEnd) {
            tmp = textBuffer;
            keyLabels[labelCount-1].text = tmp;
            keyLabels[labelCount-1].length = newLen;
        } else {
            keyLabels[labelCount-1].length += newLen;
        }
        wcscpy_s(tmp, (textBufferEnd-tmp), text);
    }
    keyLabels[labelCount-1].time = labelSettings.lingerTime+labelSettings.fadeDuration;
    keyLabels[labelCount-1].fade = TRUE;
    updateLabel(labelCount-1);
    newStrokeCount = labelSettings.keyStrokeDelay;
    updateLayeredWindow(hMainWnd);
}

// updateCanvasSize — 根据显示原点重新计算画布尺寸和位置
// pt 表示标签右下角/定位点，画布从当前工作区左侧延伸到 pt.x，
// 高度覆盖整个工作区。定位变化时会清空现有标签。
void updateCanvasSize(const POINT &pt) {
    for(DWORD i = 0; i < labelCount; i ++) {
        if(keyLabels[i].time > 0) {
            eraseLabel(i);
            keyLabels[i].time = 0;
        }
    }
    canvasSize.cy = desktopRect.bottom - desktopRect.top;
    canvasOrigin.y = down ? pt.y : pt.y - desktopRect.bottom + desktopRect.top;
    canvasSize.cx = pt.x - desktopRect.left;
    canvasOrigin.x = desktopRect.left;

#ifdef _DEBUG
    std::stringstream line;
    line << "desktopRect: {left: " << desktopRect.left << ", top: " <<  desktopRect.top << ", right: " <<  desktopRect.right << ", bottom: " <<  desktopRect.bottom << "};\n";
    line << "canvasSize: {" << canvasSize.cx << "," <<  canvasSize.cy << "};\n";
    line << "canvasOrigin: {" << canvasOrigin.x << "," <<  canvasOrigin.y << "};\n";
    line << "pt: {" << pt.x << "," <<  pt.y << "};\n";
    log(line);
#endif
}
// createCanvas — 创建或重建离屏 GDI+ 画布
// 根据当前工作区尺寸创建与屏幕等大的兼容位图，
// 作为 Graphics 对象的绘制表面，用于分层窗口合成
void createCanvas() {
    HDC hdc = GetDC(hMainWnd);
    HDC hdcBuffer = CreateCompatibleDC(hdc);
    HBITMAP hbitmap = CreateCompatibleBitmap(hdc, desktopRect.right - desktopRect.left, desktopRect.bottom - desktopRect.top);
    HBITMAP hBitmapOld = (HBITMAP)SelectObject(hdcBuffer, (HGDIOBJ)hbitmap);
    ReleaseDC(hMainWnd, hdc);
    DeleteObject(hBitmapOld);
    if(gCanvas) {
        delete gCanvas;
    }
    gCanvas = new Graphics(hdcBuffer);
    gCanvas->SetSmoothingMode(SmoothingModeAntiAlias);
    gCanvas->SetTextRenderingHint(TextRenderingHintAntiAlias);
}
// prepareLabels — 初始化标签数组布局
// 根据当前字体大小、边框、间距计算可容纳的标签行数，
// 受 maximumLines 限制，并在画布上设置每个标签的 Y 坐标位置。
// 同时创建 GDI+ Font 对象并更新品牌标识窗口。
void prepareLabels() {
    HDC hdc = GetDC(hMainWnd);
    HFONT hlabelFont = CreateFontIndirect(&labelSettings.font);
    HFONT hFontOld = (HFONT)SelectObject(hdc, hlabelFont);
    DeleteObject(hFontOld);

    if(fontPlus) {
        delete fontPlus;
    }
    fontPlus = new Font(hdc, hlabelFont);
    ReleaseDC(hMainWnd, hdc);
    RectF box;
    PointF origin(0, 0);
    gCanvas->MeasureString(L"\u263b - KeyCastOW OFF", 16, fontPlus, origin, &box);
    REAL unitH = box.Height+2*labelSettings.borderSize+labelSpacing;
    labelCount = (desktopRect.bottom - desktopRect.top) / (int)unitH;
    REAL paddingH = (desktopRect.bottom - desktopRect.top) - unitH*labelCount;

    DWORD offset = 0;
    if(labelCount > maximumLines) {
        offset = labelCount-maximumLines;
        labelCount = maximumLines;
    } else if(labelCount == 0) {
        offset = labelCount-1;
        labelCount = 1;
    }

    gCanvas->Clear(clearColor);
    for(DWORD i = 0; i < labelCount; i ++) {
        DWORD labelIndex = down ? labelCount - i - 1 : i;
        keyLabels[labelIndex].rect.X = (REAL)labelSettings.borderSize;
        if(down) {
            keyLabels[labelIndex].rect.Y = unitH*i + labelSpacing + labelSettings.borderSize;
        } else {
            keyLabels[labelIndex].rect.Y = paddingH + unitH*(i+offset) + labelSpacing + labelSettings.borderSize;
        }
        if(keyLabels[labelIndex].time > (int)(labelSettings.lingerTime+labelSettings.fadeDuration)) {
            keyLabels[labelIndex].time = labelSettings.lingerTime+labelSettings.fadeDuration;
        }
        if(keyLabels[labelIndex].time > 0) {
            updateLabel(labelIndex);
        }
    }

    stamp(hWndStamp, branding);
}

// GetWorkAreaByOrigin — 获取指定屏幕坐标所在显示器的工作区
// 多显示器场景下用于确定标签应显示在哪个显示器上
void GetWorkAreaByOrigin(const POINT &pt, MONITORINFO &mi) {
    RECT rc = {pt.x-1, pt.y-1, pt.x+1, pt.y+1};
    HMONITOR hMonitor = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(hMonitor, &mi);
}

// positionOrigin — 处理显示区域定位交互
// action=0: 拖拽中，实时更新画布尺寸并显示坐标；检测是否跨越显示器
// action=其他: 拖拽结束，确认最终位置，清空画布完成定位
void positionOrigin(int action, POINT &pt) {
    if (action == 0) {
        updateCanvasSize(pt);

        MONITORINFO mi;
        GetWorkAreaByOrigin(pt, mi);
        if(mi.rcWork.left != desktopRect.left || mi.rcWork.top != desktopRect.top) {
            CopyMemory(&desktopRect, &mi.rcWork, sizeof(RECT));
            MoveWindow(hMainWnd, desktopRect.left, desktopRect.top, 1, 1, TRUE);
            createCanvas();
            prepareLabels();
        }
#ifdef _DEBUG
        std::stringstream line;
        line << "rcWork: {" << mi.rcWork.left << "," <<  mi.rcWork.top << "," <<  mi.rcWork.right << "," <<  mi.rcWork.bottom << "};\n";
        line << "desktopRect: {" << desktopRect.left << "," <<  desktopRect.top << "," <<  desktopRect.right << "," <<  desktopRect.bottom << "};\n";
        line << "canvasSize: {" << canvasSize.cx << "," <<  canvasSize.cy << "};\n";
        line << "canvasOrigin: {" << canvasOrigin.x << "," <<  canvasOrigin.y << "};\n";
        line << "labelCount: " << labelCount << "\n";
        log(line);
#endif
        WCHAR tmp[256];
        swprintf(tmp, 256, L"%d, %d", pt.x, pt.y);
        showText(tmp, 2);
    } else {
        positioning = FALSE;
        deskOrigin.x = pt.x;
        deskOrigin.y = pt.y;
        updateCanvasSize(pt);
        clearColor.SetValue(0x007f7f7f);
        gCanvas->Clear(clearColor);
    }
}
// ColorDialog — 弹出 Windows 颜色选择对话框
// 修改 clr 引用的颜色值，用户确认后更新
BOOL ColorDialog ( HWND hWnd, COLORREF &clr ) {
    DWORD dwCustClrs[16] = {
        RGB(0,0,0),
        RGB(0,0,255),
        RGB(0,255,0),
        RGB(128,255,255),
        RGB(255,0,0),
        RGB(255,0,255),
        RGB(255,255,0),
        RGB(192,192,192),
        RGB(127,127,127),
        RGB(0,0,128),
        RGB(0,128,0),
        RGB(0,255,255),
        RGB(128,0,0),
        RGB(255,0,128),
        RGB(128,128,64),
        RGB(255,255,255)
    };
    CHOOSECOLOR dlgColor;
    dlgColor.lStructSize = sizeof(CHOOSECOLOR);
    dlgColor.hwndOwner = hWnd;
    dlgColor.hInstance = NULL;
    dlgColor.lpTemplateName = NULL;
    dlgColor.rgbResult =  clr;
    dlgColor.lpCustColors =  dwCustClrs;
    dlgColor.Flags = CC_ANYCOLOR|CC_RGBINIT;
    dlgColor.lCustData = 0;
    dlgColor.lpfnHook = NULL;

    if(ChooseColor(&dlgColor)) {
        clr = dlgColor.rgbResult;
    }
    return TRUE;
}
// CreateToolTip — 为对话框控件创建气泡提示
HWND CreateToolTip(HWND hDlg, int toolID, LPWSTR pszText) {
    // Get the window of the tool.
    HWND hwndTool = GetDlgItem(hDlg, toolID);

    // Create the tooltip. g_hInst is the global instance handle.
    HWND hwndTip = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL,
            WS_POPUP |TTS_ALWAYSTIP | TTS_BALLOON,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            hDlg, NULL,
            hInstance, NULL);

    if (!hwndTool || !hwndTip) {
        return (HWND)NULL;
    }

    // Associate the tooltip with the tool.
    TOOLINFO toolInfo = { 0 };
    toolInfo.cbSize = sizeof(toolInfo);
    toolInfo.hwnd = hDlg;
    toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    toolInfo.uId = (UINT_PTR)hwndTool;
    toolInfo.lpszText = pszText;
    SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);

    return hwndTip;
}
// writeSettingInt — 将一个 DWORD 值写入 INI 配置文件的 [KeyCastOW] 节
void writeSettingInt(LPCTSTR lpKeyName, DWORD dw) {
    WCHAR tmp[256];
    swprintf(tmp, 256, L"%d", dw);
    WritePrivateProfileString(L"KeyCastOW", lpKeyName, tmp, iniFile);
}
// saveSettings — 将当前所有设置持久化到 INI 文件
// 包括标签外观、功能开关、热键配置和品牌标识等
void saveSettings() {
    writeSettingInt(L"keyStrokeDelay", labelSettings.keyStrokeDelay);
    writeSettingInt(L"lingerTime", labelSettings.lingerTime);
    writeSettingInt(L"fadeDuration", labelSettings.fadeDuration);
    writeSettingInt(L"bgColor", labelSettings.bgColor);
    writeSettingInt(L"textColor", labelSettings.textColor);
    WritePrivateProfileStruct(L"KeyCastOW", L"labelFont", (LPVOID)&labelSettings.font, sizeof(labelSettings.font), iniFile);
    writeSettingInt(L"bgOpacity", labelSettings.bgOpacity);
    writeSettingInt(L"textOpacity", labelSettings.textOpacity);
    writeSettingInt(L"borderOpacity", labelSettings.borderOpacity);
    writeSettingInt(L"borderColor", labelSettings.borderColor);
    writeSettingInt(L"borderSize", labelSettings.borderSize);
    writeSettingInt(L"cornerSize", labelSettings.cornerSize);
    writeSettingInt(L"labelSpacing", labelSpacing);
    writeSettingInt(L"maximumLines", maximumLines);
    writeSettingInt(L"offsetX", deskOrigin.x);
    writeSettingInt(L"offsetY", deskOrigin.y);
    writeSettingInt(L"visibleShift", visibleShift);
    writeSettingInt(L"visibleModifier", visibleModifier);
    writeSettingInt(L"mouseCapturing", mouseCapturing);
    writeSettingInt(L"mouseCapturingMod", mouseCapturingMod);
    writeSettingInt(L"keyAutoRepeat", keyAutoRepeat);
    writeSettingInt(L"mergeMouseActions", mergeMouseActions);
    writeSettingInt(L"alignment", alignment);
    writeSettingInt(L"down", down);
    writeSettingInt(L"onlyCommandKeys", onlyCommandKeys);
    writeSettingInt(L"draggableLabel", draggableLabel);
    if (draggableLabel) {
        SetWindowLong(hMainWnd, GWL_EXSTYLE, GetWindowLong(hMainWnd, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT);
    } else {
        SetWindowLong(hMainWnd, GWL_EXSTYLE, GetWindowLong(hMainWnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
    }
    writeSettingInt(L"tcModifiers", tcModifiers);
    writeSettingInt(L"tcKey", tcKey);
    WritePrivateProfileString(L"KeyCastOW", L"branding", branding, iniFile);
    WritePrivateProfileStruct(L"KeyCastOW", L"brandingFont", (LPVOID)&brandingSettings.font, sizeof(brandingSettings.font), iniFile);
    writeSettingInt(L"brandingBgColor", brandingSettings.bgColor);
    writeSettingInt(L"brandingTextColor", brandingSettings.textColor);
    writeSettingInt(L"brandingBgOpacity", brandingSettings.bgOpacity);
    writeSettingInt(L"brandingBorderSize", brandingSettings.borderSize);
    WritePrivateProfileString(L"KeyCastOW", L"comboChars", comboChars, iniFile);
}
// fixDeskOrigin — 修正显示原点，确保其位于当前工作区内
// 当显示器配置变化或 INI 中保存的位置失效时使用
void fixDeskOrigin() {
    if(deskOrigin.x > desktopRect.right || deskOrigin.x < desktopRect.left + labelSettings.borderSize) {
        deskOrigin.x = desktopRect.right - labelSettings.borderSize;
    }
    if(deskOrigin.y > desktopRect.bottom || deskOrigin.y < desktopRect.top + labelSettings.borderSize) {
        deskOrigin.y = desktopRect.bottom;
    }
}
// loadSettings — 从 INI 文件加载所有设置
// 读取失败时使用硬编码的默认值。
// 同时初始化桌面区域、窗口位置、标签字体和拖拽穿透属性。
void loadSettings() {
    labelSettings.keyStrokeDelay = GetPrivateProfileInt(L"KeyCastOW", L"keyStrokeDelay", 500, iniFile);
    labelSettings.lingerTime = GetPrivateProfileInt(L"KeyCastOW", L"lingerTime", 1200, iniFile);
    labelSettings.fadeDuration = GetPrivateProfileInt(L"KeyCastOW", L"fadeDuration", 310, iniFile);
    labelSettings.bgColor = GetPrivateProfileInt(L"KeyCastOW", L"bgColor", RGB(17, 24, 39), iniFile);
    labelSettings.textColor = GetPrivateProfileInt(L"KeyCastOW", L"textColor", RGB(248, 250, 252), iniFile);
    labelSettings.bgOpacity = GetPrivateProfileInt(L"KeyCastOW", L"bgOpacity", 224, iniFile);
    labelSettings.textOpacity = GetPrivateProfileInt(L"KeyCastOW", L"textOpacity", 255, iniFile);
    labelSettings.borderOpacity = GetPrivateProfileInt(L"KeyCastOW", L"borderOpacity", 235, iniFile);
    labelSettings.borderColor = GetPrivateProfileInt(L"KeyCastOW", L"borderColor", RGB(232, 93, 63), iniFile);
    labelSettings.borderSize = GetPrivateProfileInt(L"KeyCastOW", L"borderSize", 3, iniFile);
    labelSettings.cornerSize = GetPrivateProfileInt(L"KeyCastOW", L"cornerSize", 16, iniFile);
    labelSpacing = GetPrivateProfileInt(L"KeyCastOW", L"labelSpacing", 6, iniFile);
    maximumLines = GetPrivateProfileInt(L"KeyCastOW", L"maximumLines", 10, iniFile);
    if (maximumLines == 0) {
        maximumLines = 1;
    }
    deskOrigin.x = GetPrivateProfileInt(L"KeyCastOW", L"offsetX", 2, iniFile);
    deskOrigin.y = GetPrivateProfileInt(L"KeyCastOW", L"offsetY", 2, iniFile);
    MONITORINFO mi;
    GetWorkAreaByOrigin(deskOrigin, mi);
    CopyMemory(&desktopRect, &mi.rcWork, sizeof(RECT));
    MoveWindow(hMainWnd, desktopRect.left, desktopRect.top, 1, 1, TRUE);
    fixDeskOrigin();
    visibleShift = GetPrivateProfileInt(L"KeyCastOW", L"visibleShift", 0, iniFile);
    visibleModifier = GetPrivateProfileInt(L"KeyCastOW", L"visibleModifier", 1, iniFile);
    mouseCapturing = GetPrivateProfileInt(L"KeyCastOW", L"mouseCapturing", 1, iniFile);
    mouseCapturingMod = GetPrivateProfileInt(L"KeyCastOW", L"mouseCapturingMod", 0, iniFile);
    keyAutoRepeat = GetPrivateProfileInt(L"KeyCastOW", L"keyAutoRepeat", 1, iniFile);
    mergeMouseActions = GetPrivateProfileInt(L"KeyCastOW", L"mergeMouseActions", 1, iniFile);
    alignment = GetPrivateProfileInt(L"KeyCastOW", L"alignment", 1, iniFile);
    down = GetPrivateProfileInt(L"KeyCastOW", L"down", 0, iniFile);
    onlyCommandKeys = GetPrivateProfileInt(L"KeyCastOW", L"onlyCommandKeys", 0, iniFile);
    draggableLabel = GetPrivateProfileInt(L"KeyCastOW", L"draggableLabel", 0, iniFile);
    if (draggableLabel) {
        SetWindowLong(hMainWnd, GWL_EXSTYLE, GetWindowLong(hMainWnd, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT);
    } else {
        SetWindowLong(hMainWnd, GWL_EXSTYLE, GetWindowLong(hMainWnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
    }
    tcModifiers = GetPrivateProfileInt(L"KeyCastOW", L"tcModifiers", MOD_ALT, iniFile);
    tcKey = GetPrivateProfileInt(L"KeyCastOW", L"tcKey", 0x42, iniFile);
    GetPrivateProfileString(L"KeyCastOW", L"branding", L"双击此处修改配置", branding, BRANDINGMAX, iniFile);
    brandingSettings.bgColor = GetPrivateProfileInt(L"KeyCastOW", L"brandingBgColor", RGB(0, 124, 254), iniFile);
    brandingSettings.textColor = GetPrivateProfileInt(L"KeyCastOW", L"brandingTextColor", RGB(255, 255, 255), iniFile);
    brandingSettings.bgOpacity = GetPrivateProfileInt(L"KeyCastOW", L"brandingBgOpacity", 175, iniFile);
    brandingSettings.bgOpacity = min(brandingSettings.bgOpacity, 255);
    brandingSettings.borderSize = GetPrivateProfileInt(L"KeyCastOW", L"brandingBorderSize", 8, iniFile);
    GetPrivateProfileString(L"KeyCastOW", L"comboChars", L"<->", comboChars, 4, iniFile);
    memset(&labelSettings.font, 0, sizeof(labelSettings.font));
    labelSettings.font.lfCharSet = DEFAULT_CHARSET;
    labelSettings.font.lfHeight = -34;
    labelSettings.font.lfPitchAndFamily = DEFAULT_PITCH;
    labelSettings.font.lfWeight  = FW_SEMIBOLD;
    labelSettings.font.lfOutPrecision = OUT_DEFAULT_PRECIS;
    labelSettings.font.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    labelSettings.font.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(labelSettings.font.lfFaceName, LF_FACESIZE, TEXT("Microsoft YaHei UI"));
    GetPrivateProfileStruct(L"KeyCastOW", L"labelFont", &labelSettings.font, sizeof(labelSettings.font), iniFile);
    CopyMemory(&brandingSettings.font, &labelSettings.font, sizeof(brandingSettings.font));
    brandingSettings.font.lfHeight = -20;
    brandingSettings.font.lfWeight = FW_BLACK;
    wcscpy_s(brandingSettings.font.lfFaceName, LF_FACESIZE, TEXT("Arial"));
    GetPrivateProfileStruct(L"KeyCastOW", L"brandingFont", &brandingSettings.font, sizeof(brandingSettings.font), iniFile);
}
// renderSettingsData — 将预览设置数据刷新到设置对话框的各控件
// 使用 previewLabelSettings（而非 labelSettings），以便在确认前预览效果
void renderSettingsData(HWND hwndDlg) {
    WCHAR tmp[256];
    swprintf(tmp, 256, L"%d", previewLabelSettings.keyStrokeDelay);
    SetDlgItemText(hwndDlg, IDC_KEYSTROKEDELAY, tmp);
    swprintf(tmp, 256, L"%d", previewLabelSettings.lingerTime);
    SetDlgItemText(hwndDlg, IDC_LINGERTIME, tmp);
    swprintf(tmp, 256, L"%d", previewLabelSettings.fadeDuration);
    SetDlgItemText(hwndDlg, IDC_FADEDURATION, tmp);
    swprintf(tmp, 256, L"%d", previewLabelSettings.bgOpacity);
    SetDlgItemText(hwndDlg, IDC_BGOPACITY, tmp);
    swprintf(tmp, 256, L"%d", previewLabelSettings.textOpacity);
    SetDlgItemText(hwndDlg, IDC_TEXTOPACITY, tmp);
    swprintf(tmp, 256, L"%d", previewLabelSettings.borderOpacity);
    SetDlgItemText(hwndDlg, IDC_BORDEROPACITY, tmp);
    swprintf(tmp, 256, L"%d", previewLabelSettings.borderSize);
    SetDlgItemText(hwndDlg, IDC_BORDERSIZE, tmp);
    swprintf(tmp, 256, L"%d", previewLabelSettings.cornerSize);
    SetDlgItemText(hwndDlg, IDC_CORNERSIZE, tmp);
    swprintf(tmp, 256, L"%d", previewBrandingSettings.bgOpacity);
    SetDlgItemText(hwndDlg, IDC_BGOPACITY2, tmp);
    swprintf(tmp, 256, L"%d", previewBrandingSettings.font.lfHeight < 0 ? -previewBrandingSettings.font.lfHeight : previewBrandingSettings.font.lfHeight);
    SetDlgItemText(hwndDlg, IDC_TEXTOPACITY2, tmp);
    swprintf(tmp, 256, L"%d", previewBrandingSettings.borderSize);
    SetDlgItemText(hwndDlg, IDC_BORDERSIZE2, tmp);

    swprintf(tmp, 256, L"%d", labelSpacing);
    SetDlgItemText(hwndDlg, IDC_LABELSPACING, tmp);
    swprintf(tmp, 256, L"%d", maximumLines);
    SetDlgItemText(hwndDlg, IDC_MAXIMUMLINES, tmp);
    SetDlgItemText(hwndDlg, IDC_BRANDING, branding);
    SetDlgItemText(hwndDlg, IDC_COMBSCHEME, comboChars);
    CheckDlgButton(hwndDlg, IDC_VISIBLESHIFT, visibleShift ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_VISIBLEMODIFIER, visibleModifier ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_MOUSECAPTURING, mouseCapturing ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_MOUSECAPTURINGMOD, mouseCapturingMod ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_KEYAUTOREPEAT, keyAutoRepeat ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_MERGEMOUSEACTIONS, mergeMouseActions ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_ONLYCOMMANDKEYS, onlyCommandKeys ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_DRAGGABLELABEL, draggableLabel ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_DOWN, down ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_MODCTRL, (tcModifiers & MOD_CONTROL) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_MODALT, (tcModifiers & MOD_ALT) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_MODSHIFT, (tcModifiers & MOD_SHIFT) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_MODWIN, (tcModifiers & MOD_WIN) ? BST_CHECKED : BST_UNCHECKED);
    swprintf(tmp, 256, L"%c", MapVirtualKey(tcKey, MAPVK_VK_TO_CHAR));
    SetDlgItemText(hwndDlg, IDC_TCKEY, tmp);
    ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_ALIGNMENT), alignment);
}
// getLabelSettings — 从设置对话框控件中读取当前值到 LabelSettings 结构
// 仅读取数值型控件（延迟、停留、淡出、不透明度、边框、圆角）
void getLabelSettings(HWND hwndDlg, LabelSettings &lblSettings) {
    WCHAR tmp[256];
    GetDlgItemText(hwndDlg, IDC_KEYSTROKEDELAY, tmp, 256);
    lblSettings.keyStrokeDelay = _wtoi(tmp);
    GetDlgItemText(hwndDlg, IDC_LINGERTIME, tmp, 256);
    lblSettings.lingerTime = _wtoi(tmp);
    GetDlgItemText(hwndDlg, IDC_FADEDURATION, tmp, 256);
    lblSettings.fadeDuration = _wtoi(tmp);
    if(lblSettings.fadeDuration < SHOWTIMER_INTERVAL*5) {
        lblSettings.fadeDuration = SHOWTIMER_INTERVAL*5;
    }
    GetDlgItemText(hwndDlg, IDC_BGOPACITY, tmp, 256);
    lblSettings.bgOpacity = _wtoi(tmp);
    lblSettings.bgOpacity = min(lblSettings.bgOpacity, 255);
    GetDlgItemText(hwndDlg, IDC_TEXTOPACITY, tmp, 256);
    lblSettings.textOpacity = _wtoi(tmp);
    lblSettings.textOpacity = min(lblSettings.textOpacity, 255);
    GetDlgItemText(hwndDlg, IDC_BORDEROPACITY, tmp, 256);
    lblSettings.borderOpacity = _wtoi(tmp);
    lblSettings.borderOpacity = min(lblSettings.borderOpacity, 255);
    GetDlgItemText(hwndDlg, IDC_BORDERSIZE, tmp, 256);
    lblSettings.borderSize = _wtoi(tmp);
    GetDlgItemText(hwndDlg, IDC_CORNERSIZE, tmp, 256);
    lblSettings.cornerSize = _wtoi(tmp);
}

// getBrandingSettings — 从设置对话框控件中读取品牌标识外观设置
// IDC_TEXTOPACITY2 当前用作文字大小输入框。
void getBrandingSettings(HWND hwndDlg, BrandingSettings &brdSettings) {
    WCHAR tmp[256];
    GetDlgItemText(hwndDlg, IDC_BGOPACITY2, tmp, 256);
    brdSettings.bgOpacity = _wtoi(tmp);
    brdSettings.bgOpacity = min(brdSettings.bgOpacity, 255);
    GetDlgItemText(hwndDlg, IDC_TEXTOPACITY2, tmp, 256);
    int fontSize = _wtoi(tmp);
    if(fontSize > 0) {
        brdSettings.font.lfHeight = -fontSize;
    }
    GetDlgItemText(hwndDlg, IDC_BORDERSIZE2, tmp, 256);
    brdSettings.borderSize = _wtoi(tmp);
}
DWORD previewTime = 0;          // 预览动画计时器
#define PREVIEWTIMER_INTERVAL 5  // 预览定时器间隔（毫秒）

// previewLabel — 在设置对话框中渲染标签预览
// 循环执行：保持显示 → 淡出 → 重置，让用户看到标签动画效果
static void previewLabel() {
    RECT rt = {12, 58, 222, 238};

    getLabelSettings(hDlgSettings, previewLabelSettings);
    DWORD mg = previewLabelSettings.lingerTime+previewLabelSettings.fadeDuration+600;
    double r;
    if(previewTime < PREVIEWTIMER_INTERVAL || previewTime > mg) {
        previewTime = mg;
    }
    if(previewTime > mg-600) {
        previewTime -= PREVIEWTIMER_INTERVAL;
        r = 0;
    }
    else if(previewTime > previewLabelSettings.fadeDuration) {
        r = 1;
        previewTime -= PREVIEWTIMER_INTERVAL;
    } else if(previewTime >= PREVIEWTIMER_INTERVAL) {
        previewTime -= PREVIEWTIMER_INTERVAL;
        r = 1.0*previewTime/previewLabelSettings.fadeDuration;
    }
    HDC hdc = GetDC(hDlgSettings);
    int rtWidth = rt.right-rt.left;
    int rtHeight = rt.bottom-rt.top;
    RectF rc(0, 0, (REAL)rtWidth, (REAL)rtHeight);
    HDC memDC = ::CreateCompatibleDC(hdc);
    HBITMAP memBitmap = ::CreateCompatibleBitmap(hdc, (int)rc.Width, (int)rc.Height);
    ::SelectObject(memDC,memBitmap);
    Graphics g(memDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    WCHAR text[] = L"字A";
    HFONT hFont = CreateFontIndirect(&previewLabelSettings.font);
    SelectObject(memDC, hFont);
    Font font(memDC, hFont);

    PointF origin(rc.X+previewLabelSettings.borderSize, rc.Y+previewLabelSettings.borderSize);
    g.MeasureString(text, 2, &font, origin, &rc);

    rc.X += (rtWidth-(int)rc.Width)/2-previewLabelSettings.borderSize;
    rc.Y += (rtHeight-(int)rc.Height)/2-previewLabelSettings.borderSize;
    origin.X = rc.X;
    origin.Y = rc.Y;

    int bgAlpha = (int)(r*previewLabelSettings.bgOpacity), textAlpha = (int)(r*previewLabelSettings.textOpacity), borderAlpha = (int)(r*previewLabelSettings.borderOpacity);
    Pen penPlus(Color::Color(BR(borderAlpha, previewLabelSettings.borderColor)), previewLabelSettings.borderSize+0.0f);
    SolidBrush brushPlus(Color::Color(BR(bgAlpha, previewLabelSettings.bgColor)));
    drawLabelFrame(&g, &penPlus, &brushPlus, rc, (REAL)previewLabelSettings.cornerSize);
    SolidBrush textBrushPlus(Color(BR(textAlpha, previewLabelSettings.textColor)));
    g.DrawString(text, wcslen(text), &font, origin, &textBrushPlus);
    BitBlt(hdc, rt.left, rt.top, rtWidth, rtHeight, memDC, 0,0, SRCCOPY);
    DeleteDC(memDC);
    DeleteObject(memBitmap);
    DeleteObject(hFont);
    ReleaseDC(hDlgSettings, hdc);
}

// SettingsWndProc — 设置对话框的窗口过程
// 处理初始化、通知、按钮命令等消息。
// 字体/颜色变更会立即预览；点击"确定"后保存所有设置。
// 点击“确定”后保存设置并保持窗口打开，点击“取消”关闭对话框。
BOOL CALLBACK SettingsWndProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WCHAR tmp[256];
    switch (msg)
    {
        case WM_INITDIALOG:
            {
                renderSettingsData(hwndDlg);
                GetWindowRect(hwndDlg, &settingsDlgRect);
                SetWindowPos(hwndDlg, 0,
                        desktopRect.right - desktopRect.left - settingsDlgRect.right + settingsDlgRect.left,
                        desktopRect.bottom - desktopRect.top - settingsDlgRect.bottom + settingsDlgRect.top, 0, 0, SWP_NOSIZE);
                GetWindowRect(hwndDlg, &settingsDlgRect);
                CreateToolTip(hwndDlg, IDC_COMBSCHEME, L"[+] to display combination keys like [Alt + Tab].");
                HWND hCtrl = GetDlgItem(hwndDlg, IDC_ALIGNMENT);
                ComboBox_InsertString(hCtrl, 0, L"左对齐");
                ComboBox_InsertString(hCtrl, 1, L"右对齐");
            }
            return TRUE;
        case WM_NOTIFY:
            switch (((LPNMHDR)lParam)->code)
            {

                case NM_CLICK:          // Fall through to the next case.
                case NM_RETURN:
                    {
                        PNMLINK pNMLink = (PNMLINK)lParam;
                        LITEM   item    = pNMLink->item;
                        ShellExecute(NULL, L"open", item.szUrl, NULL, NULL, SW_SHOW);
                        break;
                    }
            }

            break;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_TEXTFONT:
                    {
                        CHOOSEFONT cf ;
                        cf.lStructSize    = sizeof (CHOOSEFONT) ;
                        cf.hwndOwner      = hwndDlg ;
                        cf.hDC            = NULL ;
                        cf.lpLogFont      = &previewLabelSettings.font ;
                        cf.iPointSize     = 0 ;
                        cf.Flags          = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS ;
                        cf.rgbColors      = 0 ;
                        cf.lCustData      = 0 ;
                        cf.lpfnHook       = NULL ;
                        cf.lpTemplateName = NULL ;
                        cf.hInstance      = NULL ;
                        cf.lpszStyle      = NULL ;
                        cf.nFontType      = 0 ;               // Returned from ChooseFont
                        cf.nSizeMin       = 0 ;
                        cf.nSizeMax       = 0 ;

                        if(ChooseFont (&cf)) {
                            prepareLabels();
                            saveSettings();
                        }
                    }
                    return TRUE;
                case IDC_TEXTCOLOR:
                    if( ColorDialog(hwndDlg, previewLabelSettings.textColor) ) {
                        prepareLabels();
                        saveSettings();
                    }
                    return TRUE;
                case IDC_BGCOLOR:
                    if( ColorDialog(hwndDlg, previewLabelSettings.bgColor) ) {
                        prepareLabels();
                        saveSettings();
                    }
                    return TRUE;
                case IDC_BORDERCOLOR:
                    if( ColorDialog(hwndDlg, previewLabelSettings.borderColor) ) {
                        prepareLabels();
                        saveSettings();
                    }
                    return TRUE;
                case IDC_TEXTFONT2:
                    {
                        CHOOSEFONT cf ;
                        cf.lStructSize    = sizeof (CHOOSEFONT) ;
                        cf.hwndOwner      = hwndDlg ;
                        cf.hDC            = NULL ;
                        cf.lpLogFont      = &previewBrandingSettings.font ;
                        cf.iPointSize     = 0 ;
                        cf.Flags          = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS ;
                        cf.rgbColors      = 0 ;
                        cf.lCustData      = 0 ;
                        cf.lpfnHook       = NULL ;
                        cf.lpTemplateName = NULL ;
                        cf.hInstance      = NULL ;
                        cf.lpszStyle      = NULL ;
                        cf.nFontType      = 0 ;               // Returned from ChooseFont
                        cf.nSizeMin       = 0 ;
                        cf.nSizeMax       = 0 ;

                        if(ChooseFont (&cf)) {
                            swprintf(tmp, 256, L"%d", previewBrandingSettings.font.lfHeight < 0 ? -previewBrandingSettings.font.lfHeight : previewBrandingSettings.font.lfHeight);
                            SetDlgItemText(hwndDlg, IDC_TEXTOPACITY2, tmp);
                        }
                    }
                    return TRUE;
                case IDC_TEXTCOLOR2:
                    ColorDialog(hwndDlg, previewBrandingSettings.textColor);
                    return TRUE;
                case IDC_BGCOLOR2:
                    ColorDialog(hwndDlg, previewBrandingSettings.bgColor);
                    return TRUE;
                case IDC_POSITION:
                    {
                        alignment = ComboBox_GetCurSel(GetDlgItem(hwndDlg, IDC_ALIGNMENT));
                        down = (BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_DOWN));
                        clearColor.SetValue(0x7f7f7f7f);
                        gCanvas->Clear(clearColor);
                        showText(L"\u254b", 1);
                        fadeLastLabel(FALSE);
                        positioning = TRUE;
                    }
                    return TRUE;
                case IDOK:
                    getLabelSettings(hwndDlg, previewLabelSettings);
                    getBrandingSettings(hwndDlg, previewBrandingSettings);
                    labelSettings = previewLabelSettings;
                    brandingSettings = previewBrandingSettings;
                    GetDlgItemText(hwndDlg, IDC_LABELSPACING, tmp, 256);
                    labelSpacing = _wtoi(tmp);
                    if(labelSpacing > (DWORD)(desktopRect.bottom - desktopRect.top)/3) {
                        labelSpacing = (DWORD)(desktopRect.bottom - desktopRect.top)/3;
                    }
                    GetDlgItemText(hwndDlg, IDC_MAXIMUMLINES, tmp, 256);
                    maximumLines = _wtoi(tmp);
                    if (maximumLines > MAXLABELS) {
                        maximumLines = MAXLABELS;
                    } else if (maximumLines == 0) {
                        maximumLines = 1;
                    }
                    GetDlgItemText(hwndDlg, IDC_BRANDING, branding, BRANDINGMAX);
                    GetDlgItemText(hwndDlg, IDC_COMBSCHEME, comboChars, 4);
                    visibleShift = (BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_VISIBLESHIFT));
                    visibleModifier = (BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_VISIBLEMODIFIER));
                    mouseCapturing = (BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MOUSECAPTURING));
                    mouseCapturingMod = (BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MOUSECAPTURINGMOD));
                    keyAutoRepeat = (BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_KEYAUTOREPEAT));
                    mergeMouseActions = (BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MERGEMOUSEACTIONS));
                    onlyCommandKeys = (BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_ONLYCOMMANDKEYS));
                    draggableLabel = (BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_DRAGGABLELABEL));
                    down = (BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_DOWN));
                    tcModifiers = 0;
                    if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MODCTRL)) {
                        tcModifiers |= MOD_CONTROL;
                    }
                    if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MODALT)) {
                        tcModifiers |= MOD_ALT;
                    }
                    if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MODSHIFT)) {
                        tcModifiers |= MOD_SHIFT;
                    }
                    if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MODWIN)) {
                        tcModifiers |= MOD_WIN;
                    }
                    GetDlgItemText(hwndDlg, IDC_TCKEY, tmp, 256);
                    alignment = ComboBox_GetCurSel(GetDlgItem(hwndDlg, IDC_ALIGNMENT));
                    if(tcModifiers != 0 && tmp[0] != '\0') {
                        tcKey = VkKeyScanEx(tmp[0], GetKeyboardLayout(0));
                        UnregisterHotKey(NULL, 1);
                        if (!RegisterHotKey( NULL, 1, tcModifiers | MOD_NOREPEAT, tcKey)) {
                            MessageBox(NULL, L"Unable to register hotkey, you probably need go to settings to redefine your hotkey for toggle capturing.", L"Warning", MB_OK|MB_ICONWARNING);
                        }
                    }
                    updateCanvasSize(deskOrigin);
                    prepareLabels();
                    saveSettings();
                    return TRUE;
                case IDCANCEL:
                    EndDialog(hwndDlg, wParam);
                    previewTimer.Stop();
                    return TRUE;
            }
    }
    return FALSE;
}
// DraggableWndProc — 品牌标识小窗口的窗口过程
// 支持按住鼠标左键拖动标识位置，双击打开设置对话框
LRESULT CALLBACK DraggableWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static POINT s_last_mouse;
    switch(message)
    {
        // hold mouse to move
        case WM_LBUTTONDOWN:
            SetCapture(hWnd);
            GetCursorPos(&s_last_mouse);
            showTimer.Stop();
            break;
        case WM_MOUSEMOVE:
            if (GetCapture()==hWnd)
            {
                POINT p;
                GetCursorPos(&p);
                int dx= p.x - s_last_mouse.x;
                int dy= p.y - s_last_mouse.y;
                if (dx||dy)
                {
                    s_last_mouse=p;
                    RECT r;
                    GetWindowRect(hWnd,&r);
                    SetWindowPos(hWnd,HWND_TOPMOST,r.left+dx,r.top+dy,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
                }
            }
            break;
        case WM_LBUTTONUP:
            ReleaseCapture();
            showTimer.Start(SHOWTIMER_INTERVAL);
            break;
        case WM_LBUTTONDBLCLK:
            SendMessage( hMainWnd, WM_COMMAND, MENU_CONFIG, 0 );
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// WindowFunc — 主窗口窗口过程
// 负责系统托盘菜单、热键切换、显示器变化、定位拖拽等应用级消息处理
LRESULT CALLBACK WindowFunc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static POINT s_last_mouse;
    static HMENU hPopMenu;
    static NOTIFYICONDATA nid;

    switch(message) {
        // trayicon
        case WM_CREATE:
            {
                memset( &nid, 0, sizeof( nid ) );

                nid.cbSize              = sizeof( nid );
                nid.hWnd                = hWnd;
                nid.uID                 = IDI_TRAY;
                nid.uFlags              = NIF_ICON | NIF_MESSAGE | NIF_TIP;
                nid.uCallbackMessage    = WM_TRAYMSG;
                nid.hIcon = LoadIcon( hInstance, MAKEINTRESOURCE(IDI_ICON1));
                lstrcpy( nid.szTip, L"KeyCastOW 按键显示工具-NVIC" );
                Shell_NotifyIcon( NIM_ADD, &nid );

                hPopMenu = CreatePopupMenu();
                AppendMenu( hPopMenu, MF_STRING, MENU_CONFIG,  L"设置(&S)..." );
                AppendMenu( hPopMenu, MF_STRING, MENU_RESTORE,  L"恢复默认设置(&R)" );
#ifdef _DEBUG
                AppendMenu( hPopMenu, MF_STRING, MENU_REPLAY,  L"回放(&P)" );
#endif
                AppendMenu( hPopMenu, MF_STRING, MENU_EXIT,    L"退出(&X)" );
                SetMenuDefaultItem( hPopMenu, MENU_CONFIG, FALSE );
            }
            break;
        case WM_TRAYMSG:
            {
                switch ( lParam )
                {
                    case WM_RBUTTONUP:
                        {
                            POINT pnt;
                            GetCursorPos( &pnt );
                            SetForegroundWindow( hWnd ); // needed to get keyboard focus
                            TrackPopupMenu( hPopMenu, TPM_LEFTALIGN, pnt.x, pnt.y, 0, hWnd, NULL );
                        }
                        break;
                    case WM_LBUTTONDBLCLK:
                        SendMessage( hWnd, WM_COMMAND, MENU_CONFIG, 0 );
                        return 0;
                }
            }
            break;
        case WM_COMMAND:
            {
                switch ( LOWORD( wParam ) )
                {
                    case MENU_CONFIG:
                        CopyMemory(&previewLabelSettings, &labelSettings, sizeof(previewLabelSettings));
                        CopyMemory(&previewBrandingSettings, &brandingSettings, sizeof(previewBrandingSettings));
                        renderSettingsData(hDlgSettings);
                        ShowWindow(hDlgSettings, SW_SHOW);
                        SetForegroundWindow(hDlgSettings);
                        previewTimer.Start(PREVIEWTIMER_INTERVAL);
                        break;
                    case MENU_RESTORE:
                        DeleteFile(iniFile);
                        loadSettings();
                        updateCanvasSize(deskOrigin);
                        createCanvas();
                        prepareLabels();
                        break;
#ifdef _DEBUG
                    case MENU_REPLAY:
                        {
                            if(replayStatus == 1) {
                                replayStatus = 2;
                                ModifyMenu( hPopMenu, MENU_REPLAY, MF_STRING, MENU_REPLAY, L"Re&play");
                            } else {
                                OPENFILENAME ofn;
                                ZeroMemory(&ofn, sizeof(OPENFILENAME));
                                ofn.lStructSize = sizeof(ofn);
                                ofn.hwndOwner   = NULL;
                                ofn.hInstance   = hInstance;
                                ofn.lpstrFile   = recordFN;
                                ofn.nMaxFile    = sizeof(recordFN);
                                ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
                                if(GetOpenFileName(&ofn)) {
                                    unsigned long id = 1;
                                    CreateThread(NULL,0,replay,recordFN,0,&id);
                                    ModifyMenu( hPopMenu, MENU_REPLAY, MF_STRING, MENU_REPLAY, L"Stop re&play");
                                }
                            }
                        }
                        break;
#endif
                    case MENU_EXIT:
                        Shell_NotifyIcon( NIM_DELETE, &nid );
                        ExitProcess(0);
                        break;
                    default:
                        break;
                }
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        case WM_DISPLAYCHANGE:
            {
                MONITORINFO mi;
                GetWorkAreaByOrigin(deskOrigin, mi);
                CopyMemory(&desktopRect, &mi.rcWork, sizeof(RECT));
                MoveWindow(hMainWnd, desktopRect.left, desktopRect.top, 1, 1, TRUE);
                fixDeskOrigin();
                updateCanvasSize(deskOrigin);
                createCanvas();
                prepareLabels();
            }
            break;
        // hold mouse to move
        case WM_LBUTTONDOWN:
            SetCapture(hWnd);
            GetCursorPos(&s_last_mouse);
            showTimer.Stop();
            break;
        case WM_MOUSEMOVE:
            if (GetCapture()==hWnd)
            {
                POINT p;
                GetCursorPos(&p);
                int dx= p.x - s_last_mouse.x;
                int dy= p.y - s_last_mouse.y;
                if (dx||dy)
                {
                    s_last_mouse=p;
                    positionOrigin(0, p);
                }
            }
            break;
        case WM_LBUTTONUP:
            ReleaseCapture();
            showTimer.Start(100);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
// MyRegisterClassEx — 注册窗口类
// 用于主显示窗口和品牌标识窗口。启用 CS_DBLCLKS 以接收双击消息。
ATOM MyRegisterClassEx(HINSTANCE hInst, LPCWSTR className, WNDPROC wndProc) {
    WNDCLASSEX wcl;
    wcl.cbSize = sizeof(WNDCLASSEX);
    wcl.hInstance = hInst;
    wcl.lpszClassName = className;
    wcl.lpfnWndProc = wndProc;
    wcl.style = CS_DBLCLKS;
    wcl.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcl.hIconSm = LoadIcon(NULL, IDI_WINLOGO);
    wcl.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcl.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wcl.lpszMenuName = NULL;
    wcl.cbWndExtra = 0;
    wcl.cbClsExtra = 0;

    return RegisterClassEx(&wcl);
}
// CreateMiniDump — 发生未处理异常时生成 MiniDump.dmp
// 便于后续用调试器分析崩溃现场
void CreateMiniDump( LPEXCEPTION_POINTERS lpExceptionInfo) {
    // Open a file
    HANDLE hFile = CreateFile(L"MiniDump.dmp", GENERIC_READ | GENERIC_WRITE,
        0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if ( hFile != NULL &&  hFile != INVALID_HANDLE_VALUE ) {

        // Create the minidump
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId          = GetCurrentThreadId();
        mdei.ExceptionPointers = lpExceptionInfo;
        mdei.ClientPointers    = FALSE;

        MINIDUMP_TYPE mdt      = MiniDumpNormal;
        BOOL retv = MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(),
            hFile, mdt, ( lpExceptionInfo != 0 ) ? &mdei : 0, 0, 0);

        if ( !retv ) {
            printf( ("MiniDumpWriteDump failed. Error: %u \n"), GetLastError() );
        } else {
            printf( ("Minidump created.\n") );
        }

        // Close the file
        CloseHandle( hFile );

    } else {
        printf( ("CreateFile failed. Error: %u \n"), GetLastError() );
    }
}

// MyUnhandledExceptionFilter — 全局未处理异常过滤器
// 捕获崩溃后写出 minidump，并交由系统执行异常处理流程
LONG __stdcall MyUnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo)
{
    CreateMiniDump(pExceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
}
// WinMain — 应用程序入口
// 初始化公共控件、GDI+、主窗口、设置对话框、托盘图标、热键、键鼠钩子，
// 然后进入消息循环。退出时释放钩子、热键、GDI+ 对象和调试文件流。
int WINAPI WinMain(HINSTANCE hThisInst, HINSTANCE hPrevInst,
        LPSTR lpszArgs, int nWinMode)
{
    MSG        msg;

    hInstance = hThisInst;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LINK_CLASS|ICC_LISTVIEW_CLASSES|ICC_PAGESCROLLER_CLASS
        |ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES|ICC_TAB_CLASSES|ICC_TREEVIEW_CLASSES
        |ICC_UPDOWN_CLASS|ICC_USEREX_CLASSES|ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    GetModuleFileName(NULL, iniFile, MAX_PATH);
    iniFile[wcslen(iniFile)-4] = '\0';
    wcscat_s(iniFile, MAX_PATH, L".ini");
#ifdef _DEBUG
    wcscpy_s(capFile, MAX_PATH, iniFile);
    capFile[wcslen(capFile)-4] = '\0';
    wcscat_s(capFile, MAX_PATH, L".cap");

    wcscpy_s(logFile, MAX_PATH, iniFile);
    logFile[wcslen(logFile)-4] = '\0';
    wcscat_s(logFile, MAX_PATH, L".txt");
    errno_t err = _wfopen_s(&capStream, capFile, L"wb");
    err = _wfopen_s(&logStream, logFile, L"a");
#endif

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR           gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    if(!MyRegisterClassEx(hThisInst, szWinName, WindowFunc)) {
        MessageBox(NULL, L"Could not register window class", L"Error", MB_OK);
        return 0;
    }

    hMainWnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            szWinName,
            szWinName,
            WS_POPUP,
            0, 0,            //X and Y position of window
            1, 1,            //Width and height of window
            NULL,
            NULL,
            hThisInst,
            NULL
            );
    if( !hMainWnd)    {
        MessageBox(NULL, L"Could not create window", L"Error", MB_OK);
        return 0;
    }

    loadSettings();
    updateCanvasSize(deskOrigin);
    hDlgSettings = CreateDialog(hThisInst, MAKEINTRESOURCE(IDD_DLGSETTINGS), NULL, (DLGPROC)SettingsWndProc);
    MyRegisterClassEx(hThisInst, L"STAMP", DraggableWndProc);
    hWndStamp = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_NOACTIVATE,
            L"STAMP", L"STAMP", WS_VISIBLE|WS_POPUP,
            0, 0, 1, 1,
            NULL, NULL, hThisInst, NULL);

    if (!RegisterHotKey( NULL, 1, tcModifiers | MOD_NOREPEAT, tcKey)) {
        MessageBox(NULL, L"Unable to register hotkey, you probably need go to settings to redefine your hotkey for toggle capturing.", L"Warning", MB_OK|MB_ICONWARNING);
    }
    UpdateWindow(hMainWnd);

    createCanvas();
    prepareLabels();
    ShowWindow(hMainWnd, SW_SHOW);
    HFONT hlabelFont = CreateFont(20,10,0,0,FW_BLACK,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_OUTLINE_PRECIS,
                CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY, VARIABLE_PITCH,TEXT("Arial"));
    HWND hlink = GetDlgItem(hDlgSettings, IDC_SYSLINK1);
    SendMessage(hlink, WM_SETFONT, (WPARAM)hlabelFont, TRUE);

    showTimer.OnTimedEvent = startFade;
    showTimer.Start(SHOWTIMER_INTERVAL);
    previewTimer.OnTimedEvent = previewLabel;

    kbdhook = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, hThisInst, NULL);
    moshook = SetWindowsHookEx(WH_MOUSE_LL, LLMouseProc, hThisInst, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0,_WRITE_ABORT_MSG);
    SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);

    while( GetMessage(&msg, NULL, 0, 0) )    {
        if (msg.message == WM_HOTKEY) {
            if(kbdhook) {
                showText(L"\u263b - KeyCastOW OFF", 1);
                UnhookWindowsHookEx(kbdhook);
                kbdhook = NULL;
                UnhookWindowsHookEx(moshook);
                moshook = NULL;
            } else {
                showText(L"\u263b - KeyCastOW ON", 1);
                kbdhook = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, hInstance, NULL);
                moshook = SetWindowsHookEx(WH_MOUSE_LL, LLMouseProc, hThisInst, 0);
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    UnhookWindowsHookEx(kbdhook);
    UnhookWindowsHookEx(moshook);
    UnregisterHotKey(NULL, 1);
    delete gCanvas;
    delete fontPlus;
#ifdef _DEBUG
    fclose(capStream);
    fclose(logStream);
#endif

    GdiplusShutdown(gdiplusToken);
    return msg.wParam;
}
