#pragma once
#include <string>
#include <vector>
#include <functional>

// TODO: terminology for assets is inconsistant: "symbol" "ticker". "asset" is probably a better
// term

// Implement this to provide SimBroker with data.
// If your data comes from an external resource such as an API,
// it is recommended to build an on-disk cache mechanism as we pull
// a lot of data.
class SimBrokerStockDataSource {
  public:
    struct Bar {
      uint64_t time;
      double openPrice;
      double closePrice;
      double highPrice;
      double lowPrice;
      uint64_t volume;
    };

    enum MarketPhase {
      PREMARKET  = 0,
      OPEN       = 1,
      POSTMARKET = 2,
      CLOSED     = 3
    };

    struct MarketPhaseChange {
      MarketPhase from;
      MarketPhase to;
      uint64_t time;
    };

    // Should not include *any* data after endTime or before startTime 
    // The duration that the last bar covers should entirely reside in the specified window.
    virtual std::vector<Bar> getMinuteBars(std::string ticker, uint64_t startTime, uint64_t endTime) = 0;

    // TODO: it would be better to split getPrice up into getBidPrice and getAskPrice.
    // The problem: the data source in use is likely to be "bars", which don't hold that concept.
    // Need to look into this in more detail.
    //
    // getPrice should fall back on hour bars followed by day bars if it can't find the price with minute bars -
    // or at least look back a very long time in min bars (a month).
    // There can be very large gaps in minute bar data.
    virtual double getPrice(std::string ticker, uint64_t time) = 0;        // Return < 0 if price is not available

    virtual double getAssetBorrowRate(std::string ticker, uint64_t time) = 0;

    virtual MarketPhase getMarketPhase(uint64_t time) = 0;                 // Throw exception if data not available
    virtual MarketPhaseChange getNextMarketPhaseChange(uint64_t time) = 0; // Throw exception if data not available
    virtual MarketPhaseChange getPrevMarketPhaseChange(uint64_t time) = 0; // Throw exception if data not available
    virtual MarketPhaseChange getNextMarketPhaseChangeTo(uint64_t time, MarketPhase to) = 0; // Throw exception if data not available
    virtual MarketPhaseChange getPrevMarketPhaseChangeTo(uint64_t time, MarketPhase to) = 0; // Throw exception if data not available
    virtual MarketPhaseChange getNextMarketPhaseChangeFrom(uint64_t time, MarketPhase from) = 0; // Throw exception if data not available
    virtual MarketPhaseChange getPrevMarketPhaseChangeFrom(uint64_t time, MarketPhase from) = 0; // Throw exception if data not available

    virtual bool isTickerMarginable(std::string ticker, uint64_t time) = 0; // Throw exception if data not available
    virtual bool isTickerETB(std::string ticker, uint64_t time) = 0;
    virtual bool isTickerShortable(std::string ticker, uint64_t time) = 0;
};

class SimBroker {
  public:
    enum OrderType {
      MARKET        = 0,
      LIMIT         = 1,
      STOP          = 2, // Currently unsupported
      STOP_LIMIT    = 3, // Currently unsupported
      TRAILING_STOP = 4  // Currently unsupported
    };

    enum OrderTimeInForce {
      DAY                 = 0,
      GOOD_TILL_CANCELLED = 1,
      ON_OPEN             = 2, // Currently unsupported
      ON_CLOSE            = 3, // Currently unsupported
      IMMEDIATE_OR_CANCEL = 4, // Currently unsupported
      FILL_OR_KILL        = 5  // Currently unsupported
    };

    enum OrderClass {
      SIMPLE             = 0,
      BRACKET            = 1, // Currently unsupported
      ONE_CANCELS_OTHER  = 2, // Currently unsupported
      ONE_TRIGGERS_OTHER = 3  // Currently unsupported
    };

    enum OrderStatus { OPEN, CANCELLED, EXPIRED, REJECTED };

    struct Position {
      uint64_t id;
      std::string symbol;
      double avgEntryPrice;
      int64_t qty; // Positive value = long, negative value = short
      double costBasis;
      uint64_t createdTime = 0;

			int64_t lastChange = 0;
			int64_t lastChangeTime = 0;
    };

    struct OrderPlan {
      std::string symbol = "SPY";
      int64_t qty = 0; // Use a negative value to sell
      OrderType type = OrderType::MARKET;
      OrderTimeInForce timeInForce = OrderTimeInForce::DAY;
      double limitPrice = 0.0;
      double stopPrice = 0.0;
      double trailPrice = 0.0;
      double trailPercent = 0.0;
      bool extendedHours = false;
      OrderClass orderClass = OrderClass::SIMPLE;
    };

    struct Order : OrderPlan {
      uint64_t id;
      uint64_t createdAt;   // epoch time
      uint64_t updatedAt;   // epoch time
      uint64_t submittedAt; // epoch time
      uint64_t filledAt;    // epoch time
      uint64_t expiredAt;   // epoch time
      uint64_t canceledAt;  // epoch time
      uint64_t failedAt;    // epoch time
      uint64_t replacedAt;  // epoch time
      uint64_t replacedBy;  // order id
      uint64_t replaces;    // order id
      int64_t filledQty;    // will be negative if this is a sell order
      double filledAvgPrice;

      OrderStatus status;

      struct OrderStatusHistoryEntry {
        OrderStatus status;
        uint64_t time; // The start time/time at which this order obtained this status
      };

      std::vector<OrderStatusHistoryEntry> orderStatusHistory;

      bool doneFilling = false;
    };

    SimBroker(SimBrokerStockDataSource* dataSource, uint64_t startTime, bool margin);
    void updateClock(uint64_t time);
    // TODO: wouldn't it make more sense to return Order? It still contains the id.
    uint64_t placeOrder(OrderPlan p);
    void cancelOrder(uint64_t orderId);
    Order getOrder(uint64_t id);
    std::vector<Order> getOrders();
    std::vector<Position> getPositions();
    double getBalance();
    double getEquity();
    double getBuyingPower();
    double getTotalCostBasis();
    uint64_t getClock();
    void addFunds(double chedda);
    void rmFunds(double cheeze);


		// PDT

		// Called when your account becomes flagged as a pattern day trader (but was not before)
		void setPDTCallHandler(std::function<void()> func);

		// The quantity of remaining day trades if you don't want to get flagged as a pattern day trader
		// If you're already a pattern day trader, this will still be reported as though you weren't
		// Can be negative.
		int8_t remainingDayTrades(); 
		bool PDT(); // true if your account is considered a pattern day trader

    // Margin
    void setInitialMarginRequirement(double req);
    double getInitialMarginRequirement();
    void setMaintenanceMarginRequirement(double req);
    double getMaintenanceMarginRequirement();

    void setInterestRate(double rate);
    double getInterestRate();

    // Note: this requires us to obtain the value of all of our positions via the data source
    // for every updateClock() call while we have any kind of loan.
    //
    // This can make it resource expensive to use this handler
    //
    // Also note that this only checks against the current clock - so if you make a large updateClock 
    // and skip over an event that would have caused a margin call, you might not get called.
    // (though it will be checked at least once per night after market closed, even with large jumps)
    void setMarginCallHandler(std::function<void()> func);

    // Returns true if the current state (checked only at the current clock!) would result
    // in a margin call
    //
    // Will obtain the value of all of our positions via the stock data source, so be aware of any 
    // API time costs.
    //
    // Using this function instead of the setMarginCallHandler callback can allow you better control
    // over the resource costs of your data source.
    bool checkForMarginCall();

    double getLoan();

    void enableShortRoundLotFee();
    void disableShortRoundLotFee();
    bool shortRoundLotFeeEnabled();

    void enableInstaFill();
    void disableInstaFill();
    bool instaFillEnabled();
  private:
    void cleanStuckOrders();
    void updateState();
    void chargeDayInterest();
    double estimateFillRate(SimBrokerStockDataSource::Bar b);

    void eachBarChunk(std::string ticker, 
                      uint64_t startTime,
                      std::function<bool(std::vector<SimBrokerStockDataSource::Bar> bars, uint64_t chunkStart, uint64_t chunkEnd)> func);
    void eachBar(std::string ticker, uint64_t startTime, std::function<bool(SimBrokerStockDataSource::Bar b)> func);
    void updateOrderFillState(Order& o);
    void updateOrderTIF(Order& o);

    // Updates order history as well as sets the status on the order (DO NOT SET ORDER STATUS DIRECTLY)
    // time: the time at which this status became active
    void setOrderStatus(Order& o, OrderStatus status, uint64_t time); 

    // Will create position if it doesn't exist
    // Will remove position if it ends up at a qty of zero
    void addToPosition(std::string symbol, int64_t qty, double avgPrice);

    SimBrokerStockDataSource* stockDataSource;
    double balance;
    uint64_t clock = 0;
    std::vector<Order>    orders;
    std::vector<Position> positions;
    bool marginEnabled = false;
    bool shortRoundLotFee = true;
    bool instaFill = false;
    double initialMarginRequirement = 0.5;
    double maintenanceMarginRequirement = 0.35;
    bool marginCallHandlerDefined = false;
    bool PDTCallHandlerDefined = false;
    uint64_t lastInterestTime = 0;
    double interestRate = 0.0375;
    std::function<void()> marginCallHandler;
    std::function<void()> PDTCallHandler;
		std::vector<int64_t> roundTrips;
		bool isPDT = false;
};
