#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <map>
#include <vector>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>


struct MarketState {
    double bestBid;
    double bestAsk;
    int bestBidQuantity;
    double bestAskQuantity;
};

// Order Data Layout Struct
struct Order {
    int orderId;    
    int botId;      
    bool isBuy;     
    double price;   
    int quantity;   
};

struct TraderAccount {
    int botId;
    double balance = 10000;
    int stockQuantity = 100;
};

class OrderBook {
private:
    
    // Bids are sorted greatest to least using std::greater so the highest buy offer sits at the top
    std::map<double, std::list<Order>, std::greater<double>> bids;
    
    // Asks are sorted least to greatest by default so the cheapest sell offer sits at the top
    std::map<double, std::list<Order>> asks;
    
    // Fast O(1) hash map shortcut pointing directly to an order's location inside queues
    std::unordered_map<int, std::list<Order>::iterator> orderLookup;

    // All trader accounts mapped by botID encapsulating their accounts
    std::unordered_map<int, TraderAccount> traderAccounts;

    std::mutex bookMutex;

    // helper function to execute trades 
    void executeTradeBalances(const Order& buyerOrder, const Order& sellerOrder, double executionPrice, int quantity);

    void internalCancelOrder(int orderId);

    void internalAddOrder(Order order);

    std::atomic<long> nextOrderID{0};


public:
    OrderBook(double startingPrice);
    void addOrder(Order order);
    void cancelOrder(int orderId);
    double getBestBid();
    double getBestAsk();
    double getBestBidQuantity();
    double getBestAskQuantity();
    void replaceOrder(int orderId, Order newOrder);

    // For testing purposes
    TraderAccount& getAccount(int botId) {
        std::lock_guard<std::mutex> lock(bookMutex);
        return traderAccounts[botId];
    }

    TraderAccount getAccountCopy(int botId) {
        std::lock_guard<std::mutex> lock(bookMutex);
        return traderAccounts[botId];
    }

    long generateOrderID() {
        return nextOrderID.fetch_add(1); 
    }

    void displayBook();

    MarketState getMarketState(int botId) {
        std::lock_guard<std::mutex> lock(bookMutex);
        MarketState state;
        
        state.bestBid = bids.empty() ? 0.0 : bids.begin()->first;
        state.bestAsk = asks.empty() ? 999999.0 : asks.begin()->first;
        state.bestBidQuantity = bids.empty() ? 0 : bids.begin()->second.front().quantity;
        state.bestAskQuantity = asks.empty() ? 0 : asks.begin()->second.front().quantity;
        
        return state;
};

    Order getOrder(int orderId) {
        std::lock_guard<std::mutex> lock(bookMutex);
        auto it = orderLookup.find(orderId);
        if (it != orderLookup.end()) {
            return *(it->second);
        } else {
            throw std::runtime_error("Order ID not found");
        }
    }
};


#endif