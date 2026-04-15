// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/HttpTransportError.hpp>

#include <utility>

namespace LLMQore {

HttpTransportError::HttpTransportError(QString message, QNetworkReply::NetworkError code)
    : m_message(std::move(message))
    , m_stdMessage(m_message.toStdString())
    , m_code(code)
{}

} // namespace LLMQore
