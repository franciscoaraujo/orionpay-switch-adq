#include "grpc_server.h"
#include "../common/logger.h"
#include "acquirer.pb.h"

GrpcServer::GrpcServer(std::shared_ptr<Router> router)
    : router_(std::move(router)) {}

::grpc::Status GrpcServer::AuthorizeTransaction(
    ::grpc::ServerContext* context,
    const ::orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeRequest* request,
    ::orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeResponse* response) {

    // Log detalhado da requisição recebida
    LOG_INFO("==========================================================");
    LOG_INFO("Requisição de Transação Recebida do Cliente Java");
    LOG_INFO("  - ID da Transação do Cliente: {}", request->id());
    LOG_INFO("  - Merchant ID: {}", request->merchant_id());
    LOG_INFO("  - PAN (Completo): {}", request->pan());
    LOG_INFO("  - Valor: {}", request->amount());
    LOG_INFO("==========================================================");

    // Delega toda a lógica de negócio para o Roteador
    *response = router_->process_authorization_sync(request);

    LOG_INFO("Servidor gRPC: Roteador finalizou o processamento. Enviando resposta para o cliente.");

    return ::grpc::Status::OK;
}

void GrpcServer::Run(const std::string& server_address) {
    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials());
    builder.RegisterService(this);

    server_ = builder.BuildAndStart();
    LOG_INFO("Servidor gRPC escutando em {}", server_address);

    server_->Wait();
}
