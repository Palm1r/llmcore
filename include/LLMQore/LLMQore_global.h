// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QtGlobal>

#if defined(LLMQORE_STATIC_LIB)
#define LLMQORE_EXPORT
#elif defined(LLMQORE_LIBRARY)
#define LLMQORE_EXPORT Q_DECL_EXPORT
#else
#define LLMQORE_EXPORT Q_DECL_IMPORT
#endif
