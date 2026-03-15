#include "lob/matching_engine.hpp"
#include "lob/event_handler.hpp"
#include <format>
#include <iostream>
#include <sstream>
#include <atomic>

using namespace lob;

static std::atomic<OrderId> g_id{1};

class PrintHandler final : public IEventHandler {
public:
    void on_order_accepted(const Order& o) override {
        std::cout << std::format("[ACCEPTED] id={} {} {} px={} qty={}\n",
            o.id, to_string(o.side), to_string(o.type), o.price, o.quantity);
    }
    void on_order_rejected(const Order& o, RejectReason r) override {
        std::cout << std::format("[REJECTED] id={} reason={}\n", o.id, to_string(r));
    }
    void on_order_filled(const Order& o, const Trade&) override {
        std::cout << std::format("[FILLED]   id={} qty={}\n", o.id, o.filled_quantity);
    }
    void on_order_partially_filled(const Order& o, const Trade&) override {
        std::cout << std::format("[PARTIAL]  id={} filled={} remaining={}\n",
            o.id, o.filled_quantity, o.remaining());
    }
    void on_order_cancelled(const Order& o) override {
        std::cout << std::format("[CANCEL]   id={}\n", o.id);
    }
    void on_trade(const Trade& t) override {
        std::cout << std::format("[TRADE]    maker={} taker={} px={} qty={} seq={}\n",
            t.maker_order_id, t.taker_order_id, t.price, t.quantity, t.sequence_number);
    }
    void on_book_update(std::string_view, Side, Price, Quantity) override {}
};

static void print_depth(const MarketDepth& d) {
    std::cout << "\n";
    std::cout << std::format("  {:>12}  {:>12}  {:>12}\n", "Ask Qty", "Price", "Bid Qty");
    std::cout << std::string(44, '-') << "\n";
    auto asks = d.asks;
    std::reverse(asks.begin(), asks.end());
    for (auto& l : asks)
        std::cout << std::format("  {:>12}  {:>12.2f}  {:>12}\n", l.quantity, l.price / 100.0, "");
    std::cout << std::string(44, '-') << "\n";
    for (auto& l : d.bids)
        std::cout << std::format("  {:>12}  {:>12.2f}  {:>12}\n", "", l.price / 100.0, l.quantity);
    std::cout << std::string(44, '-') << "\n";
    if (d.spread)   std::cout << std::format("  spread={:.2f}\n", *d.spread / 100.0);
    if (d.mid_price) std::cout << std::format("  mid={:.2f}\n", *d.mid_price / 100.0);
    std::cout << "\n";
}

int main() {
    PrintHandler handler;
    MatchingEngine engine(handler);

    std::cout << "Limit Order Book CLI\n";
    std::cout << "Commands:\n";
    std::cout << "  add_sym <sym>\n";
    std::cout << "  buy <sym> <px> <qty>   (price in dollars e.g. 150.50 -> enter 15050)\n";
    std::cout << "  sell <sym> <px> <qty>\n";
    std::cout << "  market_buy <sym> <qty>\n";
    std::cout << "  market_sell <sym> <qty>\n";
    std::cout << "  cancel <sym> <id>\n";
    std::cout << "  depth <sym>\n";
    std::cout << "  quit\n\n";

    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string cmd; ss >> cmd;

        if (cmd == "quit") break;

        if (cmd == "add_sym") {
            std::string sym; ss >> sym;
            std::cout << (engine.add_instrument(sym) ? "Added: " : "Already exists: ") << sym << "\n";

        } else if (cmd == "buy" || cmd == "sell") {
            std::string sym; Price px; Quantity qty;
            ss >> sym >> px >> qty;
            auto r = engine.submit({
                g_id++, sym,
                cmd == "buy" ? Side::Buy : Side::Sell,
                OrderType::Limit, TimeInForce::GTC, px, qty
            });
            std::cout << std::format("  id={} status={} filled={} remaining={}\n",
                r.order_id, to_string(r.status), r.filled_qty, r.remaining_qty);

        } else if (cmd == "market_buy" || cmd == "market_sell") {
            std::string sym; Quantity qty;
            ss >> sym >> qty;
            Side side = (cmd == "market_buy") ? Side::Buy : Side::Sell;
            auto r = engine.submit({
                g_id++, sym, side, OrderType::Market, TimeInForce::GTC,
                side == Side::Buy ? std::numeric_limits<Price>::max()
                                  : std::numeric_limits<Price>::min(),
                qty
            });
            std::cout << std::format("  id={} status={} filled={}\n",
                r.order_id, to_string(r.status), r.filled_qty);

        } else if (cmd == "cancel") {
            std::string sym; OrderId id;
            ss >> sym >> id;
            auto r = engine.cancel({id, sym});
            std::cout << std::format("  status={} reason={}\n",
                to_string(r.status), to_string(r.reject_reason));

        } else if (cmd == "depth") {
            std::string sym; ss >> sym;
            print_depth(engine.depth(sym, 10));

        } else {
            std::cout << "Unknown command: " << cmd << "\n";
        }
    }
    return 0;
}
