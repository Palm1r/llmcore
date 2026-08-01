// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/HttpTransport.hpp>

namespace LLMQore {

HttpStreamHandle::HttpStreamHandle(QObject *parent)
    : QObject(parent)
{}

HttpStreamHandle::~HttpStreamHandle() = default;

HttpTransport::HttpTransport(QObject *parent)
    : QObject(parent)
{}

HttpTransport::~HttpTransport() = default;

void HttpTransport::setTransferTimeout(int milliseconds)
{
    m_transferTimeoutMs = milliseconds;
}

int HttpTransport::transferTimeoutMs() const noexcept
{
    return m_transferTimeoutMs;
}

} // namespace LLMQore
