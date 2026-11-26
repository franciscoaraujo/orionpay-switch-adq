#include <iostream>
#include <thread>
#include <vector>
#include <asio.hpp>
#include <memory>
#include <functional>
#include <grpc/grpc.h> // Include for grpc_version_string
#include <google/protobuf/stubs/common.h> // Include for GOOGLE_PROTOBUF_VERSION

#include "common/logger.h"
#include "config/config_manager.h"
#include "grpc/grpc_server.h"
#include "crypto/hsm_worker.h"
#include "database/pg_worker.h"
#include "saf/saf_manager.h"
#include "network/visanet_client.h"
#include "switch/router.h"

int main() {
    try {
        Logger::init();
        LOG_INFO("==========================================================");
        LOG_INFO("Iniciando OrionPay Adquirer Switch (Modo gRPC)...");
        LOG_INFO("Versão do gRPC: {}", grpc_version_string());
        LOG_INFO("Versão do Protocol Buffers: {}", google::protobuf::internal::VersionString(GOOGLE_PROTOBUF_VERSION));
        LOG_INFO("==========================================================");

        ConfigManager::getInstance().load("config.json");
        const auto& config = ConfigManager::getInstance().getConfig();
        LOG_INFO("Configuração carregada com sucesso.");

        // --- Inicia os Serviços de Infraestrutura ---
        std::string db_conn_str = config["database"]["connection_string"];
        PgWorker::get_instance().start(db_conn_str);
        HsmWorker::get_instance().start();

        //SafManager::get_instance().start();

        // --- Inicia os Serviços de Rede I/O ---
        asio::io_context io_context;

        std::string visanet_host = config.value("/visanet/host"_json_pointer, "127.0.0.1");
        int visanet_port = config.value("/visanet/port"_json_pointer, 8889);
        LOG_INFO("Configuração do Cliente VisaNet: Host={}, Porta={}", visanet_host, visanet_port);

        auto visanet_client = VisaNetClient::create(io_context, visanet_host, visanet_port);
        visanet_client->start();

        std::thread asio_thread([&io_context]() {
            try {
                io_context.run();
            } catch (const std::exception& e) {
                LOG_ERROR("Exceção na thread Asio: {}", e.what());
            }
        });
        asio_thread.detach();

        // --- Cria a Lógica Principal da Aplicação ---
        auto router = std::make_shared<Router>(visanet_client);

        // --- Inicia o Ponto de Entrada (Servidor gRPC) ---
        std::string server_address("0.0.0.0:50051");
        GrpcServer service(router);

        LOG_INFO("Iniciando servidor gRPC em {}...", server_address);
        service.Run(server_address);

    } catch (const std::exception &e) {
        LOG_ERROR("Erro fatal na main: {}", e.what());
        return 1;
    }

    return 0;
}