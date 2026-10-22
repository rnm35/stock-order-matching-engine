#include "ome/events.hpp"
#include "ome/types.hpp"

namespace ome {

std::string_view toString(Side s) noexcept { return s == Side::Buy ? "Buy" : "Sell"; }

std::string_view toString(OrderType t) noexcept {
    return t == OrderType::Limit ? "Limit" : "Market";
}

std::string_view toString(TimeInForce tif) noexcept {
    switch (tif) {
        case TimeInForce::GTC: return "GTC";
        case TimeInForce::IOC: return "IOC";
        case TimeInForce::FOK: return "FOK";
    }
    return "?";
}

std::string_view toString(MatchingMode m) noexcept {
    return m == MatchingMode::Legacy ? "legacy" : "modern";
}

std::string_view toString(CancelReason r) noexcept {
    switch (r) {
        case CancelReason::UserRequest: return "user request";
        case CancelReason::ImmediateRemainder: return "unfilled immediate remainder";
        case CancelReason::FillOrKill: return "fill-or-kill not fillable";
    }
    return "?";
}

std::string_view toString(RequestType r) noexcept {
    switch (r) {
        case RequestType::New: return "new";
        case RequestType::Cancel: return "cancel";
        case RequestType::Modify: return "modify";
    }
    return "?";
}

std::string_view toString(RejectReason r) noexcept {
    switch (r) {
        case RejectReason::DuplicateOrderId: return "duplicate order id";
        case RejectReason::InvalidQuantity: return "invalid quantity";
        case RejectReason::InvalidPrice: return "invalid price";
        case RejectReason::UnknownOrder: return "unknown order";
        case RejectReason::MarketOrderNotModifiable: return "market orders cannot be modified";
    }
    return "?";
}

}  // namespace ome
