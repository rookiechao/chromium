// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/client_status.h"

#include "base/no_destructor.h"

namespace autofill_assistant {

ClientStatus::ClientStatus() : status_(UNKNOWN_ACTION_STATUS) {}
ClientStatus::ClientStatus(ProcessedActionStatusProto status)
    : status_(status) {}
ClientStatus::~ClientStatus() = default;

void ClientStatus::FillProto(ProcessedActionProto* proto) const {
  proto->set_status(status_);
  // TODO(b/129387787): Fill extra debugging information collected in the
  // ClientStatus.
}

std::ostream& operator<<(std::ostream& out, const ClientStatus& status) {
  return out << status.status_;
}

const ClientStatus& OkClientStatus() {
  static base::NoDestructor<ClientStatus> ok_(ACTION_APPLIED);
  return *ok_.get();
}

std::ostream& operator<<(std::ostream& out,
                         const ProcessedActionStatusProto& status) {
#ifdef NDEBUG
  out << static_cast<int>(status);
  return out;
#else
  switch (status) {
    case ProcessedActionStatusProto::UNKNOWN_ACTION_STATUS:
      out << "UNKNOWN_ACTION_STATUS";
      break;
    case ProcessedActionStatusProto::ELEMENT_RESOLUTION_FAILED:
      out << "ELEMENT_RESOLUTION_FAILED";
      break;
    case ProcessedActionStatusProto::ACTION_APPLIED:
      out << "ACTION_APPLIED";
      break;
    case ProcessedActionStatusProto::OTHER_ACTION_STATUS:
      out << "OTHER_ACTION_STATUS";
      break;
    case ProcessedActionStatusProto::PAYMENT_REQUEST_ERROR:
      out << "PAYMENT_REQUEST_ERROR";
      break;
    case ProcessedActionStatusProto::UNSUPPORTED_ACTION:
      out << "UNSUPPORTED_ACTION";
      break;
    case ProcessedActionStatusProto::MANUAL_FALLBACK:
      out << "MANUAL_FALLBACK";
      break;
    case ProcessedActionStatusProto::INTERRUPT_FAILED:
      out << "INTERRUPT_FAILED";
      break;
    case ProcessedActionStatusProto::USER_ABORTED_ACTION:
      out << "USER_ABORTED_ACTION";
      break;

    case ProcessedActionStatusProto::GET_FULL_CARD_FAILED:
      out << "GET_FULL_CARD_FAILED";
      break;

    case ProcessedActionStatusProto::PRECONDITION_FAILED:
      out << "PRECONDITION_FAILED";
      break;

    case ProcessedActionStatusProto::INVALID_ACTION:
      out << "INVALID_ACTION";
      break;

    case ProcessedActionStatusProto::UNSUPPORTED:
      out << "UNSUPPORTED";
      break;

    case ProcessedActionStatusProto::TIMED_OUT:
      out << "TIMED_OUT";
      break;

    case ProcessedActionStatusProto::ELEMENT_UNSTABLE:
      out << "ELEMENT_UNSTABLE";
      break;

      // Intentionally no default case to make compilation fail if a new value
      // was added to the enum but not to this list.
  }
  return out;
#endif  // NDEBUG
}

}  // namespace autofill_assistant
