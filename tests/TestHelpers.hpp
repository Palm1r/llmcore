// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>
#include <QSignalSpy>
#include <QTimer>

#include <chrono>

inline constexpr std::chrono::milliseconds kDefaultWaitTimeout{5000};

template<typename T>
T waitForFuture(
    const QFuture<T> &future, std::chrono::milliseconds timeout = kDefaultWaitTimeout)
{
    if (future.isFinished())
        return future.result();
    QEventLoop loop;
    QFutureWatcher<T> watcher;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
    loop.exec();
    return future.result();
}

inline void waitForVoidFuture(
    const QFuture<void> &future, std::chrono::milliseconds timeout = kDefaultWaitTimeout)
{
    if (future.isFinished())
        return;
    QEventLoop loop;
    QFutureWatcher<void> watcher;
    QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
    loop.exec();
}

inline void pumpEventLoop(std::chrono::milliseconds duration)
{
    QEventLoop loop;
    QTimer::singleShot(duration, &loop, &QEventLoop::quit);
    loop.exec();
}

inline bool waitForSignal(
    QSignalSpy &spy, int count, std::chrono::milliseconds timeout = kDefaultWaitTimeout)
{
    QElapsedTimer timer;
    timer.start();
    while (spy.size() < count && timer.elapsed() < timeout.count())
        pumpEventLoop(std::chrono::milliseconds{20});
    return spy.size() >= count;
}
