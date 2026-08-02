// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QThread>

namespace LLMQore {

inline void assertOwningThread(const QObject *self, const char *what)
{
    Q_ASSERT_X(self->thread() == QThread::currentThread(), what,
               "object accessed from a thread other than the one that owns it");
}

} // namespace LLMQore

#define LLMQORE_ASSERT_OWNING_THREAD() ::LLMQore::assertOwningThread(this, Q_FUNC_INFO)
