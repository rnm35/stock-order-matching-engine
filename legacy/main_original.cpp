#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <string>
#include <limits>

class Order { // is a single order
public:
    std::string orderID; //Identifier for the order
    char orderType;      // B for buy S for sell
    int quantity;       //Number of shares to be sold/bought
    double price;        //Price per share
    bool isMarketOrder;  // (Flag) True if its a market order

    //Constructor to initialize order
    Order(std::string id, char type, int qty, double pr = 0.0):
        orderID(id), orderType(type), quantity(qty), price(pr) {
        //checks if order is a market order
        isMarketOrder = (pr == 0.0); 
    }
};

//Global vars
std::vector<Order> pendingOrders;           //orders that haven't been processed yet
double lastTradedPrice;                     //Price the last trade occured at
std::vector<std::string> executedOrders;    //log of executed orders

//Method to reads the orders from the input file
void readOrdersFromFile(const std::string& filename) {
    std::ifstream file(filename);

    //Read the last traded price from the first line
    file >> lastTradedPrice;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string id;
        char type;
        int qty;
        double price = 0.0;

        //Parse the order from the csv style line
        iss >> id >> type >> qty;
        
        //valadation for the order format
        if (id.empty() || (type != 'B' && type != 'S') || qty <= 0) {
            std::cerr << "Invalid order format: " << line << std::endl;
            continue;
        }

        //if a price is not provided its a market order
        if (iss >> price) {
            pendingOrders.emplace_back(id, type, qty, price);
        } else {
            pendingOrders.emplace_back(id, type, qty); 
        }
    }
    file.close();
}

//displays all pending buy and sell orders in a nice table :)
void displayPendingOrders() {
    std::cout << std::fixed << std::setprecision(2);  // Set precision for floating point nums
    
    std::cout << "\nLast trading price: " << lastTradedPrice << "\n\n";
    
    //print table headers
    std::cout << std::left 
              << std::setw(10) << "Buy ID"
              << std::setw(10) << "Price" 
              << std::setw(10) << "Quantity" 
              << "| " 
              << std::left 
              << std::setw(10) << "Sell ID"
              << std::setw(10) << "Price"
              << std::setw(10) << "Quantity" 
              << std::endl;
    
    std::cout << std::string(60, '-') << std::endl;

    //creates the buy and sell vectors 
    std::vector<const Order*> buyOrders, sellOrders; 
    for (const auto& order : pendingOrders) {
        if (order.orderType == 'B') {
            buyOrders.push_back(&order);
        } else {
            sellOrders.push_back(&order);
        }
    }

    //displays the buy and sell orders by eachothers side (buy left sell right)
    size_t maxRows = std::max(buyOrders.size(), sellOrders.size());   // Display orders
    for (size_t i = 0; i < maxRows; ++i) {
        //displays buy order
        if (i < buyOrders.size()) {
            const Order* buy = buyOrders[i];
            std::cout << std::left 
                     << std::setw(10) << buy->orderID;
            if (buy->isMarketOrder) {
                std::cout << std::setw(10) << "M";
            } else {
                std::cout << std::setw(10) << std::fixed << std::setprecision(2) << buy->price;
            }
            std::cout << std::setw(10) << buy->quantity;
        } else {
            std::cout << std::string(30, ' ');
        }

        std::cout << "| ";
        //displays sell order
        if (i < sellOrders.size()) {
            const Order* sell = sellOrders[i];
            std::cout << std::left 
                     << std::setw(10) << sell->orderID;
            if (sell->isMarketOrder) {
                std::cout << std::setw(10) << "M";
            } else {
                std::cout << std::setw(10) << std::fixed << std::setprecision(2) << sell->price;
            }
            std::cout << std::setw(10) << sell->quantity;
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

//executes the matched buy/sell orders 
void executeOrders(Order& buyOrder, Order& sellOrder, const std::vector<Order>& allOrders) {
    int executedQuantity = std::min(buyOrder.quantity, sellOrder.quantity);
    double executionPrice;

    if (buyOrder.isMarketOrder && sellOrder.isMarketOrder) { // If both are market orders, uses last traded price
        executionPrice = lastTradedPrice;
    } else if (buyOrder.isMarketOrder) { // If one is market order, uses limit price of other order
        executionPrice = sellOrder.price;
    } else if (sellOrder.isMarketOrder) { // If both are limit orders, uses price of first order received
        executionPrice = buyOrder.price;
    } else {
        //both are limit orders, use the price of the earlier order
        size_t buyIndex = allOrders.size(), sellIndex = allOrders.size();
        
        for (size_t i = 0; i < allOrders.size(); ++i) {
            if (allOrders[i].orderID == buyOrder.orderID) {
                buyIndex = i;
            }
            if (allOrders[i].orderID == sellOrder.orderID) {
                sellIndex = i;
            }
        }
        
        // Use price of the order that arrived first
        executionPrice = (buyIndex < sellIndex) ? buyOrder.price : sellOrder.price;
    }


    //log the exectued orders to a vector 
    std::ostringstream purchaseStream;
    purchaseStream << std::fixed << std::setprecision(2) << executionPrice;
    if (executedQuantity > 0) {
        executedOrders.push_back("order " + buyOrder.orderID + " " + std::to_string(executedQuantity) + " shares purchased at price " + purchaseStream.str());
        executedOrders.push_back("order " + sellOrder.orderID + " " + std::to_string(executedQuantity) + " shares sold at price " + purchaseStream.str());
        
        lastTradedPrice = executionPrice;
    }

    //update the last traded price and remaining quantity/s
    buyOrder.quantity -= executedQuantity;
    sellOrder.quantity -= executedQuantity;
}

// Handle order matching logic
struct OrderMatcher {
    using OrderIt = std::vector<Order>::iterator; //iterator type for orders
    
    OrderIt bestMatch = pendingOrders.end(); //iterator pointing to the bets matched order
    double bestPrice; //best price found for the match
    bool isBuyOrder; //flag for whether buy order (True = yes)

    //constructor to for matcher
    OrderMatcher(bool isBuy) : isBuyOrder(isBuy) {
        bestPrice = isBuy ? std::numeric_limits<double>::max() : -1.0;
    }

    // For buy orders: lower price is better, for sell orders, higher price is better
    bool isBetterPrice(double newPrice) const {
        return isBuyOrder ? newPrice < bestPrice : newPrice > bestPrice;
    }

    //checks if the price is acceptable for a match
    bool isPriceAcceptable(const Order& newOrder, double effective) const {
        if (newOrder.isMarketOrder) return true;
        return isBuyOrder ? 
            effective <= newOrder.price : 
            effective >= newOrder.price;
    }
};

// Attempts to match new order with existing orders
// Best price gets matched first, for same price earlier orders get priority
void matchOrders(Order& newOrder, const std::vector<Order>& allOrders, size_t currentIndex) {
    while (newOrder.quantity > 0) {
        OrderMatcher match(newOrder.orderType == 'B');
        char matchType = match.isBuyOrder ? 'S' : 'B';

        // Find best matching order
        for (auto it = pendingOrders.begin(); it != pendingOrders.end(); ++it) {
            bool isEarlierOrder = false;
            for (size_t i = 0; i < currentIndex; ++i) {
                if (allOrders[i].orderID == it->orderID) {
                    isEarlierOrder = true;
                    break;
                }
            }

            //if the match is in an earlier order or is an opposite type skip to next order
            if (!isEarlierOrder) continue;
            if (it->orderType != matchType) continue;

            
            double effective = it->price; // Calculate effective price
            if (it->isMarketOrder) {
                effective = match.isBuyOrder ? 0.0 : std::numeric_limits<double>::max();
            }

            if (match.isPriceAcceptable(newOrder, effective)) { // Check if valid match
                if (match.bestMatch == pendingOrders.end() || 
                    match.isBetterPrice(effective) ||
                    (effective == match.bestPrice && it->orderID < match.bestMatch->orderID)) {
                    match.bestPrice = effective;
                    match.bestMatch = it;
                }
            }
        }

        if (match.bestMatch != pendingOrders.end()) { // Execute match if found
            Order matchedOrder = *match.bestMatch;
            pendingOrders.erase(match.bestMatch);
            
            if (match.isBuyOrder) { //execute the trade
                executeOrders(newOrder, matchedOrder, allOrders);
                if (matchedOrder.quantity > 0) {
                    pendingOrders.push_back(matchedOrder);
                }
            } else {
                //if any quant remains add it back to the pending orders 
                executeOrders(matchedOrder, newOrder, allOrders);
                if (matchedOrder.quantity > 0) {
                    pendingOrders.push_back(matchedOrder);
                }
            }
        } else {
            //exit if no valid match is found for the order
            break; 
        }
    }
}

// Writes results to output file
void writeExecutedOrdersToFile(const std::string& filename) {
    std::ofstream file(filename); //opens the file for writing
    if (!file.is_open()) {
        std::cerr << "Error opening output file: " << filename << std::endl;
        return;
    }
    //writes all executed orders to the file
    for (const auto& order : executedOrders) {
        file << order << std::endl;
    }

    //then write the remaining unexecuted orders to file
    for (const auto& order : pendingOrders) {
        if (order.quantity > 0) {
            file << "order " << order.orderID << " " << order.quantity << " shares unexecuted" << std::endl;
        }
    }

    file.close(); //closes th file
}


int main(int argc, char* argv[]) {
    //user inputs file name
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>" << std::endl;
        return 1;
    }

    readOrdersFromFile(argv[1]);

    //copy orders to process into another vector
    std::vector<Order> ordersToProcess = pendingOrders;
    pendingOrders.clear();

    //processes each order and attempts to match them
    for (size_t i = 0; i < ordersToProcess.size(); ++i) {
        matchOrders(ordersToProcess[i], ordersToProcess, i);
        if (ordersToProcess[i].quantity > 0) {
            pendingOrders.push_back(ordersToProcess[i]);
        }

        displayPendingOrders();
    }

    //generates the out file nam and writes to the file
    std::string inputFileName(argv[1]);
    std::string outputFileName = "output" + inputFileName.substr(inputFileName.find_first_of("0123456789"));
    writeExecutedOrdersToFile(outputFileName);
    return 0;
}