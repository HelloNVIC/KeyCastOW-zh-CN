/*
基于 "Simple C++ Timer Wrapper"
http://www.codeproject.com/Articles/146617/Simple-C-Timer-Wrapper
作者 ken.loveday

v1.0 2013 ArmyOfPirates

timer.h — 定时器封装类
基于 Windows 定时器队列 (Timer Queue) 实现的轻量级定时器，
支持周期触发和单次触发两种模式。
回调函数运行在定时器线程 (WT_EXECUTEINTIMERTHREAD) 中。
*/

// 周期触发定时器的回调函数（前向声明）
static void CALLBACK TimerProc(void*, BOOLEAN);
// 单次触发定时器的回调函数（前向声明）
static void CALLBACK TimerProcOnce(void* param, BOOLEAN timerCalled);

///////////////////////////////////////////////////////////////////////////////
//
// class CTimer — 定时器封装类
//
// 封装 CreateTimerQueueTimer / DeleteTimerQueueTimer，
// 提供简单的 Start/Stop 接口和线程安全的计数器。
//
class CTimer
{
public:
    CTimer()
    {
        m_hTimer = NULL;
        m_mutexCount = 0;
    }

    virtual ~CTimer()
    {
        Stop();
    }

    // 启动定时器
    // interval    - 定时间隔（毫秒）
    // immediately - 是否立即触发首次回调（true=立即，false=等待 interval 后触发）
    // once        - 是否仅触发一次（true=单次，false=周期）
    bool Start(unsigned int interval,
               bool immediately = false,
               bool once = false)
    {
        if( m_hTimer )
        {
            Stop();
        }

        SetCount(0);

        BOOL success = CreateTimerQueueTimer( &m_hTimer,
                                              NULL,
                                              once ? TimerProcOnce : TimerProc,
                                              this,
                                              immediately ? 0 : interval,
                                              once ? 0 : interval,
                                              WT_EXECUTEINTIMERTHREAD);

        return( success != 0 );
    }

    // 停止定时器并释放资源
    void Stop()
    {
        DeleteTimerQueueTimer( NULL, m_hTimer, NULL );
        m_hTimer = NULL ;
    }

    void (*OnTimedEvent)();  // 定时器触发时的回调函数指针，由外部设置

    // 线程安全地设置内部计数器值
    void SetCount(int value)
    {
        InterlockedExchange( &m_mutexCount, value );
    }

    // 线程安全地获取内部计数器值
    int GetCount()
    {
        return InterlockedExchangeAdd( &m_mutexCount, 0 );
    }

    // 定时器是否处于运行状态
    bool Enabled()
    {
        return m_hTimer != NULL;
    }

private:
    HANDLE m_hTimer;       // 定时器队列定时器句柄
    long m_mutexCount;     // 内部计数器，使用原子操作保证线程安全
};

///////////////////////////////////////////////////////////////////////////////
//
// TimerProc — 周期触发回调
//
static void CALLBACK TimerProc(void* param, BOOLEAN timerCalled)
{
    CTimer* timer = static_cast<CTimer*>(param);
    timer->SetCount( timer->GetCount()+1 );
    timer->OnTimedEvent();
};


// TimerProcOnce — 单次触发回调
// 触发一次后自动停止定时器
static void CALLBACK TimerProcOnce(void* param, BOOLEAN timerCalled)
{
    CTimer* timer = static_cast<CTimer*>(param);
    timer->SetCount( timer->GetCount()+1 );
    timer->OnTimedEvent();
    if( timer->Enabled() )
        timer->Stop();
};
