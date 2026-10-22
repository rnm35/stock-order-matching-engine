// ome-cli: runs an order file through the book, in the original coursework file formats.
//
//   ome-cli <input> [-o output] [--mode legacy|modern] [--quiet]
//
// Input:  first line is the last traded price, then one "<id> <B|S> <qty> [price]" per line.
// Output: "order <id> <qty> shares purchased|sold at price <p>" for every fill (buy side
//         first), then "order <id> <qty> shares unexecuted" for every order with shares left
//         over, in arrival order.

#include "ome/order_book.hpp"
#include "ome/text_format.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path input;
    fs::path output;
    ome::MatchingMode mode = ome::MatchingMode::Legacy;
    bool quiet = false;
    bool help = false;
};

constexpr const char* kUsage =
    "usage: ome-cli <input> [-o output] [--mode legacy|modern] [--quiet]\n"
    "  -o <file>       output file (default: input name with 'input' -> 'output',\n"
    "                  otherwise 'output_<name>', in the input's directory)\n"
    "  --mode <m>      legacy (original coursework rules, default) or modern\n"
    "  -q, --quiet     don't print the book after every order\n";

// The original derived the name from the first digit anywhere in the path, which crashed
// on names without a digit and mangled paths like "data2/input1.txt".
fs::path defaultOutputPath(const fs::path& input) {
    const std::string name = input.filename().string();
    const std::string prefix = "input";
    const std::string outName =
        name.starts_with(prefix) ? "output" + name.substr(prefix.size()) : "output_" + name;
    return input.parent_path() / outName;
}

bool parseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            opts.help = true;
            return true;
        }
        if (arg == "-q" || arg == "--quiet") {
            opts.quiet = true;
        } else if (arg == "-o" && i + 1 < argc) {
            opts.output = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode == "legacy") {
                opts.mode = ome::MatchingMode::Legacy;
            } else if (mode == "modern") {
                opts.mode = ome::MatchingMode::Modern;
            } else {
                std::cerr << "unknown mode '" << mode << "'\n";
                return false;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown or incomplete option '" << arg << "'\n";
            return false;
        } else if (opts.input.empty()) {
            opts.input = arg;
        } else {
            std::cerr << "unexpected argument '" << arg << "'\n";
            return false;
        }
    }
    if (opts.input.empty()) return false;
    if (opts.output.empty()) opts.output = defaultOutputPath(opts.input);
    return true;
}

// Turns book events into the original output file's lines. The book uses numeric ids;
// `names` maps them back to the ids from the input file.
class OutputRecorder : public ome::EventListener {
public:
    explicit OutputRecorder(const std::vector<std::string>& names) : names_(names) {}

    void onAccepted(const ome::OrderAccepted& e) override {
        if (sequences_.size() <= e.id) sequences_.resize(e.id + 1);
        sequences_[e.id] = e.sequence;
    }

    void onTrade(const ome::Trade& t) override {
        const std::string price = ome::text::formatPrice(t.price);
        const std::string qty = std::to_string(t.quantity);
        executions_.push_back("order " + names_[t.buyOrderId] + " " + qty +
                              " shares purchased at price " + price);
        executions_.push_back("order " + names_[t.sellOrderId] + " " + qty +
                              " shares sold at price " + price);
    }

    // Modern-mode market/IOC/FOK remainders never rest, but their shares were still not
    // executed, so they are reported in the "unexecuted" section like resting orders.
    void onCancelled(const ome::OrderCancelled& e) override {
        unexecuted_.push_back({sequences_[e.id], e.id, e.cancelledQuantity});
    }

    void onRejected(const ome::OrderRejected& e) override {
        std::cerr << "order " << names_[e.id] << " rejected: " << ome::toString(e.reason) << '\n';
    }

    void addResting(const ome::Order& o) { unexecuted_.push_back({o.sequence, o.id, o.remaining}); }

    void write(std::ostream& out) {
        for (const auto& line : executions_) out << line << '\n';
        std::ranges::sort(unexecuted_, {}, &Left::sequence);
        for (const auto& u : unexecuted_) {
            out << "order " << names_[u.id] << ' ' << u.quantity << " shares unexecuted\n";
        }
    }

private:
    struct Left {
        ome::Sequence sequence;
        ome::OrderId id;
        ome::Quantity quantity;
    };
    const std::vector<std::string>& names_;
    std::vector<ome::Sequence> sequences_;
    std::vector<std::string> executions_;
    std::vector<Left> unexecuted_;
};

// Same layout as the original console table, but in priority order (best first).
void printBook(const ome::OrderBook& book, const std::vector<std::string>& names) {
    std::cout << "\nLast trading price: "
              << ome::text::formatPrice(book.lastTradePrice().value_or(0)) << "\n\n";
    std::cout << std::left << std::setw(10) << "Buy ID" << std::setw(10) << "Price" << std::setw(10)
              << "Quantity" << "| " << std::setw(10) << "Sell ID" << std::setw(10) << "Price"
              << std::setw(10) << "Quantity" << '\n';
    std::cout << std::string(60, '-') << '\n';

    const auto buys = book.orders(ome::Side::Buy);
    const auto sells = book.orders(ome::Side::Sell);
    auto cell = [&](const ome::Order& o) {
        const std::string price =
            o.type == ome::OrderType::Market ? "M" : ome::text::formatPrice(o.price);
        std::cout << std::setw(10) << names[o.id] << std::setw(10) << price << std::setw(10)
                  << o.remaining;
    };
    for (std::size_t i = 0; i < std::max(buys.size(), sells.size()); ++i) {
        if (i < buys.size()) {
            cell(buys[i]);
        } else {
            std::cout << std::string(30, ' ');
        }
        std::cout << "| ";
        if (i < sells.size()) cell(sells[i]);
        std::cout << '\n';
    }
    std::cout << '\n';
}

int run(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        std::cerr << kUsage;
        return 1;
    }
    if (opts.help) {
        std::cout << kUsage;
        return 0;
    }

    std::ifstream in(opts.input);
    if (!in) {
        std::cerr << "cannot open input file " << opts.input << '\n';
        return 1;
    }

    // First non-blank line: the last traded price. Read as a whole line so the rest of it
    // isn't mistaken for an (invalid) order, which the original did on every run.
    std::string line;
    std::size_t lineNo = 0;
    std::optional<ome::Price> lastTraded;
    while (std::getline(in, line)) {
        ++lineNo;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        const auto last = line.find_last_not_of(" \t\r");
        lastTraded = ome::text::parsePrice(std::string_view(line).substr(first, last - first + 1));
        break;
    }
    if (!lastTraded) {
        std::cerr << opts.input.string() << ':' << lineNo
                  << ": first line must be the last traded price (a positive decimal)\n";
        return 1;
    }

    std::vector<std::string> names;  // book OrderId -> id from the file
    OutputRecorder recorder(names);
    ome::OrderBook book({opts.mode, lastTraded}, recorder);
    std::unordered_set<std::string> seenIds;

    while (std::getline(in, line)) {
        ++lineNo;
        auto parsed = ome::text::parseOrderLine(line);
        if (parsed.status == ome::text::LineResult::Status::Blank) continue;
        if (parsed.status == ome::text::LineResult::Status::Error) {
            std::cerr << opts.input.string() << ':' << lineNo << ": " << parsed.error
                      << ", skipped: " << line << '\n';
            continue;
        }
        auto& order = parsed.order;
        if (!seenIds.insert(order.id).second) {
            std::cerr << opts.input.string() << ':' << lineNo << ": duplicate order id '"
                      << order.id << "', skipped\n";
            continue;
        }

        const ome::OrderId id = names.size();
        names.push_back(order.id);
        book.submit(order.price ? ome::NewOrder::limit(id, order.side, order.quantity, *order.price)
                                : ome::NewOrder::market(id, order.side, order.quantity));
        if (!opts.quiet) printBook(book, names);
    }

    for (ome::Side side : {ome::Side::Buy, ome::Side::Sell}) {
        for (const auto& o : book.orders(side)) recorder.addResting(o);
    }

    std::ofstream out(opts.output);
    if (!out) {
        std::cerr << "cannot open output file " << opts.output << '\n';
        return 1;
    }
    recorder.write(out);
    return out ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    // Report unexpected failures (e.g. out of memory, filesystem errors) instead of letting
    // the exception escape main and abort.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "ome-cli: " << e.what() << '\n';
        return 1;
    }
}
