// Copyright © 2014 Brook Hong. All Rights Reserved.
//
// keylog.h — 输入捕获模块头文件
// 声明全局低级键盘/鼠标钩子句柄及回调函数，供 keycast.cpp 引用

#ifndef KEYLOG_H_INCLUDED
#define KEYLOG_H_INCLUDED

extern HHOOK kbdhook;   // 全局低级键盘钩子句柄
extern HHOOK moshook;   // 全局低级鼠标钩子句柄

// 低级键盘钩子回调，处理按键按下/释放事件
LRESULT CALLBACK LLKeyboardProc(int nCode, WPARAM wp, LPARAM lp);

// 低级鼠标钩子回调，处理鼠标点击/滚轮事件
LRESULT CALLBACK LLMouseProc(int nCode, WPARAM wp, LPARAM lp);

#endif // KEYLOG_H_INCLUDED
