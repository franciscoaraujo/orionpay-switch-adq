#pragma once

#include "../iso8583/iso_message.h"
#include "../network/visanet_client.h"
#include "acquirer.pb.h" // Corrected include path
#include <string>
#include <memory>
#include <future>

class Router {
public:
    explicit Router(std::shared_ptr<VisaNetClient> visanet_client);

    orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeResponse
    process_authorization_sync(
        const orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeRequest* request);

private:
    std::shared_ptr<VisaNetClient> visanet_client_;
};
