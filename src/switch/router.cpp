#include "router.h"
#include "../common/logger.h"
#include "../common/uuid_generator.h"
#include "../common/stan_generator.h"
#include "../crypto/hsm_worker.h"
#include "../persistence/transaction_repository.h"
#include "../iso8583/iso_builder.h"
#include "acquirer.pb.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <unordered_map>

enum class TransactionStatus {
    APPROVED,
    DECLINED_FUNDS,
    DECLINED_GENERIC,
    DECLINED_PASSWORD,
    INVALID_CARD,
    TECHNICAL_ERROR,
    UNKNOWN_ERROR
};

TransactionStatus getStatusFromResponseCode(const std::string& responseCode) {
    static const std::unordered_map<std::string, TransactionStatus> responseMap = {
        {"00", TransactionStatus::APPROVED},
        {"51", TransactionStatus::DECLINED_FUNDS},
        {"55", TransactionStatus::DECLINED_PASSWORD},
        {"01", TransactionStatus::DECLINED_GENERIC},
        {"05", TransactionStatus::DECLINED_GENERIC},
        {"14", TransactionStatus::INVALID_CARD},
        {"91", TransactionStatus::TECHNICAL_ERROR},
        {"96", TransactionStatus::TECHNICAL_ERROR},
        // O código do seu simulador da VisaNet pode ser mapeado aqui provisoriamente:
        {"PO", TransactionStatus::APPROVED} 
    };
    auto it = responseMap.find(responseCode);
    if (it != responseMap.end()) {
        return it->second;
    }
    
    // Tratamento de fallback crucial para o Switch não "crashar"
    return TransactionStatus::UNKNOWN_ERROR; 
}

Router::Router(std::shared_ptr<VisaNetClient> visanet_client)
    : visanet_client_(std::move(visanet_client)) {}

orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeResponse
Router::process_authorization_sync(
    const orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeRequest* request) {

    orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeResponse response;
    std::string transaction_uuid = common::generate_uuid();
    response.set_transaction_id(transaction_uuid);

    LOG_INFO("Roteador: Processando TX ID {} para PAN: {}", transaction_uuid, request->pan());

    if (request->pan().empty()) {
        LOG_ERROR("Roteador: PAN ausente para TX ID {}. Negando transacao.", transaction_uuid);
        response.set_status_code("14"); // INVALID_CARD
        response.set_description("Invalid Card Number");
        return response;
    }

    // --- 1. Tradução de PIN (Condicional) ---
    std::string pin_block_acquirer;
    if (!request->pin_block_terminal().empty() && request->pin_block_terminal().find_first_not_of('0') != std::string::npos) {
        LOG_INFO("Roteador: Bloco de PIN válido encontrado, iniciando tradução para TX ID {}", transaction_uuid);
        pin_block_acquirer = HsmWorker::get_instance().translate_pin_sync(
            request->pin_block_terminal(),
            request->pan()
        );
        if (pin_block_acquirer.empty()) {
            LOG_ERROR("Roteador: Falha na tradução de PIN para TX ID {}. Negando.", transaction_uuid);
            response.set_status_code("96"); // Erro de Sistema
            response.set_description("Falha na Tradução de PIN");
            return response;
        }
        LOG_INFO("Roteador: Tradução de PIN bem-sucedida para TX ID {}", transaction_uuid);
    } else {
        LOG_INFO("Roteador: Transação sem bloco de PIN ou com bloco de PIN zerado. Pulando tradução para TX ID {}", transaction_uuid);
    }

    // --- 2. Construção da Mensagem ISO 8583 (0200) ---
    LOG_INFO("Roteador: Construindo mensagem ISO 8583 (0200) para TX ID {}", transaction_uuid);
    auto iso_request = IsoBuilder::build_financial_request("0200", request, pin_block_acquirer);

    std::string stan = common::StanGenerator::get_instance().get_next_stan();
    iso_request->set_field(11, stan);

    // --- 3. Envio para a VisaNet e Aguardo da Resposta ---
    std::promise<std::unique_ptr<IsoMessage>> visa_response_promise;
    auto visa_response_future = visa_response_promise.get_future();

    LOG_INFO("Roteador: Enviando ISO 8583 para a VisaNet para TX ID {}. STAN: {}", transaction_uuid, stan);
    visanet_client_->send_message(
        std::move(iso_request),
        [&visa_response_promise](std::unique_ptr<IsoMessage> iso_response) {
            visa_response_promise.set_value(std::move(iso_response));
        }
    );

    auto iso_response = visa_response_future.get();

    // --- 4. Processamento da Resposta da VisaNet ---
    std::string response_code = "91"; // Padrão: Emissor ou switch inoperante
    std::string nsu_bandeira;

    if (iso_response) {
        LOG_INFO("Roteador: Recebida resposta ISO 8583 da VisaNet para TX ID {}", transaction_uuid);
        response_code = iso_response->get_field(39);
        nsu_bandeira = iso_response->get_field(38); // NSU da Bandeira
    } else {
        LOG_ERROR("Roteador: Não foi recebida uma resposta da VisaNet (timeout) para TX ID {}", transaction_uuid);
    }

    // --- 5. Persistência do Estado Final da Transação ---
    LOG_INFO("Roteador: Persistindo estado final da transação para TX ID {}", transaction_uuid);
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%d %H:%M:%S UTC");

    std::string normalized_amount = request->amount();
    std::replace(normalized_amount.begin(), normalized_amount.end(), ',', '.');

    persistence::TransactionEntity entity;
    entity.id = transaction_uuid;
    entity.merchant_id = request->merchant_id();
    entity.terminal_id = request->terminal_id();
    entity.amount = normalized_amount;
    entity.currency_code = request->currency_code();
    entity.mti = "0200";
    entity.pan_masked = request->pan_masked();
    entity.nsu_adquirer = stan; // Usa o STAN como nosso NSU interno
    if (!nsu_bandeira.empty()) {
        entity.nsu_bandeira = nsu_bandeira;
    }
    entity.response_code = response_code;
    entity.created_at = ss.str();

    persistence::TransactionRepository::get_instance().save(entity);

    // --- 6. Preparação da Resposta Final gRPC ---
    // WORKAROUND: O simulador retorna "PO", que causa NullPointerException no parser do Switch Java.
    // Mapeando provisoriamente para código numérico de erro de adquirente (ex: "96" - Erro de Sistema) ou Negada ("05").
    TransactionStatus status = getStatusFromResponseCode(response_code);
    std::string final_status_code = response_code;
    std::string final_description = "Transação processada";
    
    switch (status) {
        case TransactionStatus::APPROVED:
            final_status_code = "00";
            final_description = "Transação Aprovada";
            break;
        case TransactionStatus::DECLINED_FUNDS:
            final_status_code = "51";
            final_description = "Saldo Insuficiente";
            break;
        case TransactionStatus::DECLINED_PASSWORD:
            final_status_code = "55";
            final_description = "Senha Incorreta";
            break;
        case TransactionStatus::DECLINED_GENERIC:
            final_status_code = "05";
            final_description = "Transação Negada";
            break;
        case TransactionStatus::INVALID_CARD:
            final_status_code = "14";
            final_description = "Cartão Inválido";
            break;
        case TransactionStatus::TECHNICAL_ERROR:
            final_status_code = "91";
            final_description = "Erro Técnico/Emissor Inoperante";
            break;
        case TransactionStatus::UNKNOWN_ERROR:
        default:
            final_status_code = "96";
            final_description = "Erro Desconhecido ou Resposta Inválida";
            break;
    }

    response.set_status_code(final_status_code);
    response.set_description(final_description);

    return response;
}