#ifndef ORIONPAY_SWITCH_ADQ_GRPC_SERVER_H
#define ORIONPAY_SWITCH_ADQ_GRPC_SERVER_H

#include "acquirer.grpc.pb.h"
#include "../switch/router.h" // Include the Router
#include <grpcpp/grpcpp.h>
#include <memory>

class GrpcServer final : public orionpay::merchant::grpc::acquiringswitch::v1::AcquiringSwitchService::Service {
public:
    // GrpcServer now needs a Router to delegate the business logic.
    explicit GrpcServer(std::shared_ptr<Router> router);

    ::grpc::Status AuthorizeTransaction(
        ::grpc::ServerContext* context,
        const ::orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeRequest* request,
        ::orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeResponse* response) override;

    void Run(const std::string& server_address);

private:
    std::unique_ptr<::grpc::Server> server_;
    std::shared_ptr<Router> router_; // The router that handles the logic
};

#endif //ORIONPAY_SWITCH_ADQ_GRPC_SERVER_H
