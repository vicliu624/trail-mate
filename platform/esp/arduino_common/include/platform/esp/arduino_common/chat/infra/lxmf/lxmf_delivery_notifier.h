/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/Reticulum
 * Copyright (c) 2016-2026 Mark Qvist (Reticulum-derived portions).
 * Derived from https://github.com/markqvist/LXMF
 * Copyright (c) 2020-2026 Mark Qvist (LXMF-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

/**
 * @file lxmf_delivery_notifier.h
 * @brief Reticulum/LXMF outbound delivery status publisher.
 */

#pragma once

#include "chat/domain/chat_types.h"

namespace chat::lxmf::runtime
{

class LxmfDeliveryNotifier
{
  public:
    LxmfDeliveryNotifier() = default;
    LxmfDeliveryNotifier(const LxmfDeliveryNotifier&) = delete;
    LxmfDeliveryNotifier& operator=(const LxmfDeliveryNotifier&) = delete;
    LxmfDeliveryNotifier(LxmfDeliveryNotifier&&) = delete;
    LxmfDeliveryNotifier& operator=(LxmfDeliveryNotifier&&) = delete;

    void publish(MessageId message_id, MessageStatus status) const;
    void publish(MessageId message_id, bool success) const;
    void queued(MessageId message_id) const;
    void sent(MessageId message_id) const;
    void delivered(MessageId message_id) const;
    void failed(MessageId message_id) const;
};

} // namespace chat::lxmf::runtime
