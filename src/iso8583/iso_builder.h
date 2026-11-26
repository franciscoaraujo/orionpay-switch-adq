#pragma once

#include "iso_message.h"
#include "acquirer.pb.h"
#include <memory>
#include <string>

namespace IsoBuilder {

// Builds a financial request message (0200) from the gRPC request data
std::unique_ptr<IsoMessage> build_financial_request(
    const std::string& mti,
    const orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeRequest* request,
    const std::string& translated_pin_block
);

} // namespace IsoBuilder
