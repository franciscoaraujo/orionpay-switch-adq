#include "iso_builder.h"
#include "acquirer.pb.h"
#include <algorithm>
#include <string>
#include <sstream>
#include <iomanip>

namespace IsoBuilder {
    std::unique_ptr<IsoMessage> build_financial_request(
        const std::string &mti,
        const orionpay::merchant::grpc::acquiringswitch::v1::AuthorizeRequest *request,
        const std::string &translated_pin_block) {
        auto msg = std::make_unique<IsoMessage>();
        msg->set_mti(mti);

        // Use the unmasked PAN (mandatory for financial transaction)
        msg->set_field(2, request->pan());

        std::string amount_str = request->amount();
        amount_str.erase(std::remove(amount_str.begin(), amount_str.end(), '.'), amount_str.end());
        amount_str.erase(std::remove(amount_str.begin(), amount_str.end(), ','), amount_str.end());

        std::stringstream amount_ss;
        amount_ss << std::setw(12) << std::setfill('0') << amount_str;
        msg->set_field(4, amount_ss.str());

        msg->set_field(49, std::to_string(request->currency_code()));

        if (!translated_pin_block.empty()) {
            msg->set_field(52, translated_pin_block);
        }

        msg->set_field(41, request->terminal_id());

        return msg;
    }
}
