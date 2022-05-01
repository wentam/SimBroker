#include <stdio.h>
#include <cstring>
#include "simBroker.hpp"
#include <stdexcept>
#include <functional>
#include <map>
#include <cmath>

// ANSI colors
#define BLK "\x1B[0;30m"
#define RED "\x1B[0;31m"
#define GRN "\x1B[0;32m"
#define YEL "\x1B[0;33m"
#define BLU "\x1B[0;34m"
#define MAG "\x1B[0;35m"
#define CYN "\x1B[0;36m"
#define WHT "\x1B[0;37m"

// ANSI bold colors
#define BBLK "\x1B[1;30m"
#define BRED "\x1B[1;31m"
#define BGRN "\x1B[1;32m"
#define BYEL "\x1B[1;33m"
#define BBLU "\x1B[1;34m"
#define BMAG "\x1B[1;35m"
#define BCYN "\x1B[1;36m"
#define BWHT "\x1B[1;37m"

// ANSI reset
#define RESET "\x1B[0m"

uint64_t pass = 0;
uint64_t fail = 0;

double dround( double f, int places ) {
    double n = std::pow(10.0, places);
    return std::round(f*n)/n;
}

bool assert(std::string msg, bool condition) {
  if (condition) {
    printf(BGRN "[ TRUE ]" RESET " %s\n", msg.c_str());
    pass++;
    return true;
  } else {
    printf(BRED "[ FALSE ]" RESET " %s\n", msg.c_str());
    fail++;
    return false;
  }
}

void testResults() {
  printf(BYEL "\n%ld/%ld tests passed\n" RESET, pass, pass+fail);
  printf(BYEL "%ld/%ld tests failed\n" RESET, fail, pass+fail);
}

class TestSimBrokerStockDataSource : SimBrokerStockDataSource {
  private:
    std::unordered_map<std::string, std::vector<SimBrokerStockDataSource::Bar>> bars;
    std::vector<std::pair<uint64_t, uint64_t>> calendar;
    std::map<uint64_t, MarketPhase> marketPhases;
    std::vector<MarketPhaseChange> marketPhaseChanges;

    bool isTickerMarginable([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return true; };

    bool isTickerETB([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return true; };

    bool isTickerShortable([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return true; };

    void addCalTestDataLine(std::string line) {
      std::string startStr;
      std::string endStr;

      int i = 0;
      for (char c : line) {
        if (c == ',') { i++; continue; }
        if (i == 0) startStr.push_back(c);
        if (i == 1) endStr.push_back(c);
      }

      uint64_t start = std::stoll(startStr.c_str());
      uint64_t end = std::stoll(endStr.c_str());

      // Is this a duplicate entry?
      bool dupe = false;
      for (auto d : calendar) {
        if (d.first == start && d.second == end) { dupe = true; break; }
      }

      if (!dupe) calendar.push_back(std::pair(start, end));
    };

    void addTestDataLine(std::string line) {
      bool dataPart = false;
      uint64_t listIndex = 0;
      std::string key = "";

      std::string time = "";
      std::string openPrice = "";
      std::string closePrice = "";
      std::string highPrice = "";
      std::string lowPrice = "";
      std::string volume = "";
      for (char c : line) {
        if (c == ' ') continue;
        if (c == ':') { dataPart = true; listIndex = 0; continue; }
        if (c == ',') { listIndex++; continue; }
        if (!dataPart) { key.push_back(c); continue; }

        if (listIndex == 0) { time.push_back(c);       continue; };
        if (listIndex == 1) { openPrice.push_back(c);  continue; };
        if (listIndex == 2) { closePrice.push_back(c); continue; };
        if (listIndex == 3) { highPrice.push_back(c);  continue; };
        if (listIndex == 4) { lowPrice.push_back(c);   continue; };
        if (listIndex == 5) { volume.push_back(c);     continue; };
      }

      SimBrokerStockDataSource::Bar bar = {};
      bar.time = std::stol(time, nullptr, 10);
      bar.openPrice = std::stod(openPrice, nullptr);
      bar.closePrice = std::stod(closePrice, nullptr);
      bar.highPrice = std::stod(highPrice, nullptr);
      bar.lowPrice = std::stod(lowPrice, nullptr);
      bar.volume = std::stod(volume, nullptr);

      bars[key].push_back(bar);
    }

    void readLines(FILE* f,std::function<void(std::string)> line) {
      std::string lineBuffer;
      char buffer[8192];
      bool done = false;
      while(!done)  {
        size_t charCount = fread(buffer, 1, 8192, f);
        done = charCount != 8192;
        for (size_t i = 0; i < charCount; i++) {
          if (buffer[i] == '\n') {
            line(lineBuffer);
            lineBuffer.clear();
          } else {
            lineBuffer.push_back(buffer[i]);
          }
        }
      }
    }

    void readTestData() {
      FILE* f = fopen("test/data/bars.testdata","r");
      this->readLines(f, [this](std::string line) { this->addTestDataLine(line); });
      fclose(f);

      // Sort all bars by time ascending (oldest->newest)
      for (auto & [ key, value ] : bars) {
        std::sort(value.begin(), value.end(), [](const auto& a, const auto& b) -> bool {
          return a.time < b.time;
        });
      }

      FILE* fcal = fopen("test/data/calendar.testdata","r");
      this->readLines(f, [this](std::string line) { this->addCalTestDataLine(line); });
      fclose(fcal);

      // Make sure calendar is in order
      std::sort(this->calendar.begin(), this->calendar.end(), [](const auto& a, const auto& b) -> bool {
        return a.first < b.first; 
      });
    }

  public:
    TestSimBrokerStockDataSource() {
      this->readTestData();

      // Populate marketPhases
      for (auto openClose : calendar) {
        this->marketPhases[openClose.first-(5.5*3600)] = MarketPhase::PREMARKET;
        this->marketPhases[openClose.first] = MarketPhase::OPEN;
        this->marketPhases[openClose.second] = MarketPhase::POSTMARKET;
        this->marketPhases[openClose.second+(4*3600)] = MarketPhase::CLOSED;
      }

      // Populate marketPhaseChanges
      MarketPhase prevPhase;
      uint64_t i = 0;
      for (auto& [time, phase] : this->marketPhases) {
        if (i != 0) {
          MarketPhaseChange c = {}; 
          c.from = prevPhase;
          c.to   = phase;
          c.time = time;

          this->marketPhaseChanges.push_back(c);
        }

        prevPhase = phase;
        i++;
      }
    }

    std::vector<Bar> getMinuteBars(std::string ticker, uint64_t startTime, uint64_t endTime) {
      std::vector<Bar> r;
      for (auto bar : bars[ticker+"1Min"]) {
        if (bar.time > startTime && bar.time < (endTime-60)) {
          r.push_back(bar);
        }
      }

      return r;
    }

    double getPrice(std::string ticker, uint64_t time) {
      for (auto bar : bars[ticker+"1Min"]) {
        if (bar.time > time) return bar.openPrice;
      }
      throw std::logic_error("Failed to get price");
      return 0.0;
    }

    double getAssetBorrowRate([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return 0.03; }

    MarketPhase getMarketPhase(uint64_t time) {
      for (auto cal : calendar) {
        if (time >= cal.first && time <= cal.second) return MarketPhase::OPEN;
        if (time >= cal.first-(5.5*3600) && time < cal.first) return MarketPhase::PREMARKET;
        if (time >= cal.second && time < cal.second+(4*3600)) return MarketPhase::POSTMARKET;
      }

      return MarketPhase::CLOSED;
    }

    MarketPhaseChange getNextMarketPhaseChange(uint64_t time) {
      for (auto phaseChange : this->marketPhaseChanges) {
        if (time < phaseChange.time) return phaseChange;
      }

      throw std::logic_error("Not enough data to determine market phase");
      return {};
    }

    MarketPhaseChange getPrevMarketPhaseChange(uint64_t time) {
      for (int64_t i = this->marketPhaseChanges.size()-1; i >= 0; --i) {
        auto phaseChange = this->marketPhaseChanges.at(i);
        if (time >= phaseChange.time) return phaseChange;
      }

      throw std::logic_error("Not enough data to determine market phase");
      return {};
    }

    MarketPhaseChange getNextMarketPhaseChangeTo(uint64_t time, MarketPhase to) {
      for (auto phaseChange : this->marketPhaseChanges) {
        if (time < phaseChange.time && phaseChange.to == to) return phaseChange;
      }

      throw std::logic_error("Not enough data to determine market phase");
      return {};
    }

    MarketPhaseChange getPrevMarketPhaseChangeTo(uint64_t time, MarketPhase to) {
      for (int64_t i = this->marketPhaseChanges.size()-1; i >= 0; --i) {
        auto phaseChange = this->marketPhaseChanges.at(i);
        if (time >= phaseChange.time && phaseChange.to == to) return phaseChange;
      }

      throw std::logic_error("Not enough data to determine market phase");
      return {};
    }

    MarketPhaseChange getNextMarketPhaseChangeFrom(uint64_t time, MarketPhase from) {
      for (auto phaseChange : this->marketPhaseChanges) {
        if (time < phaseChange.time && phaseChange.from == from) {
          return phaseChange;
        }
      }

      throw std::logic_error("Not enough data to determine market phase");
      return {};
    }

    MarketPhaseChange getPrevMarketPhaseChangeFrom(uint64_t time, MarketPhase from) {
      for (int64_t i = this->marketPhaseChanges.size()-1; i >= 0; --i) {
        auto phaseChange = this->marketPhaseChanges.at(i);
        if (time >= phaseChange.time && phaseChange.from == from) return phaseChange;
      }

      throw std::logic_error("Not enough data to determine market phase");
      return {};
    }
};

// For margin/shorting tests
class neverMarginableSDC : TestSimBrokerStockDataSource {
  bool isTickerMarginable([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return false; }
};

class neverETBSDC : TestSimBrokerStockDataSource {
  bool isTickerETB([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return false; };
};

class neverShortableSDC : TestSimBrokerStockDataSource {
  bool isTickerShortable([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return false; };
};

// This data source is needed for market open/close tests, to ensure that the SimBroker is
// checking market state and not just passing open/close situation tests due to data availability
class AlwaysBarsSource : SimBrokerStockDataSource {
  private:
    TestSimBrokerStockDataSource mSource;

  public:
  AlwaysBarsSource() {}
  std::vector<Bar> getMinuteBars(std::string ticker, uint64_t startTime, uint64_t endTime) {
    auto r = mSource.getMinuteBars(ticker, startTime, endTime);
    if (r.size() < ((startTime-endTime)/60)-60) {
      std::vector<Bar> b;

      Bar bar = {};
      bar.time = startTime;
      bar.openPrice = 439.2;
      bar.closePrice = 442.7;
      bar.highPrice = 442.9;
      bar.lowPrice = 438.8;
      bar.volume = 4000;

      for (uint64_t i = startTime; i <= endTime;i += 60) {
        bar.time = i;
        b.push_back(bar);
      }
      return b;
    }
    return r;
  }

  double getPrice(std::string ticker, uint64_t time) {
    double p = mSource.getPrice(ticker, time);
    if (p == 0.0) return 440;
    return p;
  }

  double getAssetBorrowRate([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return 0.03; }

  MarketPhase getMarketPhase(uint64_t time) {
    return mSource.getMarketPhase(time);
  }


  MarketPhaseChange getNextMarketPhaseChange(uint64_t time) {
    return mSource.getNextMarketPhaseChange(time);
  }

  MarketPhaseChange getPrevMarketPhaseChange(uint64_t time) {
    return mSource.getPrevMarketPhaseChange(time);
  }

  MarketPhaseChange getNextMarketPhaseChangeTo(uint64_t time, MarketPhase to) {
    return mSource.getNextMarketPhaseChangeTo(time, to);
  }

  MarketPhaseChange getPrevMarketPhaseChangeTo(uint64_t time, MarketPhase to) {
    return mSource.getPrevMarketPhaseChangeTo(time, to);
  }

  MarketPhaseChange getNextMarketPhaseChangeFrom(uint64_t time, MarketPhase from) {
    return mSource.getNextMarketPhaseChangeFrom(time, from);
  }

  MarketPhaseChange getPrevMarketPhaseChangeFrom(uint64_t time, MarketPhase from) {
    return mSource.getPrevMarketPhaseChangeFrom(time, from);
  }

  bool isTickerMarginable([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return true; }
  bool isTickerETB([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return true; };
  bool isTickerShortable([[maybe_unused]]std::string ticker, [[maybe_unused]]uint64_t time) { return true; };
};

bool test(std::function<bool()> func, std::string msg) {
  try {
    return assert(msg, func());
  } catch (const std::exception &exc) {
    printf("Exception: %s\n", exc.what());
    return assert(msg, false);
  } catch (...) {
    printf("Unknown exception occured!\n");
    return assert(msg, false);
  }
}

int main() {
  TestSimBrokerStockDataSource mSource;
  AlwaysBarsSource alwaysBarsSource;
  neverMarginableSDC neverMarginableSource;
  neverETBSDC neverETBSource;
  neverShortableSDC neverShortableSource;

  // Balance/equity
  printf(BYEL "Balance/equity behavior: \n" RESET);
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 10, false);
    return simBroker.getBalance() == 0.0;
  }, "Balance starts at zero");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 10, false);
    simBroker.addFunds(5000.2);
    return simBroker.getBalance() == 5000.2; 
  }, "Adding funds to broker with zero funds results in correct quantity");
 
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 10, false);
    simBroker.addFunds(5000.2);
    return simBroker.getBalance() == simBroker.getEquity();
  }, "Without any trades having taken place, equity == balance");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 10, false);
    simBroker.addFunds(5000.2);
    simBroker.rmFunds(5000.2);
    return simBroker.getBalance() == 0.0 ;
  }, "Removing exactly our existing balance results in zero funds available");

  test([&mSource](){
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 10, false);
    simBroker.addFunds(5000);
    simBroker.rmFunds(2500);
    return simBroker.getBalance() == 2500 ;
  }, "Removing half of our funds results in half being available");

  test([&mSource](){
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 10, false);
    simBroker.addFunds(5000);
    return simBroker.getBuyingPower() == simBroker.getBalance();
  }, "After adding funds and no orders submitted, buying power == balance");

  // Clock 
  printf(BYEL "\nClock behavior: \n" RESET);

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 10, false);
    return simBroker.getClock() == 10;
  }, "Clock starts at time specified by constructer");

  test([&mSource](){
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 10, false);
    simBroker.updateClock(50);
    return simBroker.getClock() == 50;
  }, "updateClock(50) results in clock set to 50");

  test([&mSource](){
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    bool except = false;
    try {
      simBroker.updateClock(20); 
    } catch (const std::logic_error& e) {
      except = true;
    }
    return except; 
  }, "Trying to move the clock backwards throws a std::logic_error");

  // Orders
  printf(BYEL "\nOrders: \n" RESET);
  test([&mSource](){
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);

    simBroker.updateClock(1645508799);
    auto orders = simBroker.getOrders();
    for (auto order : orders) {
      if (order.symbol == "SPY" &&
          order.qty == 5) {
        return true;
      }
    }
    return false;
  }, "Valid order placed becomes accessible in getOrders()");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid = simBroker.placeOrder(marketp);
    simBroker.updateClock(1645508799);

    auto order = simBroker.getOrder(oid);
    return (order.symbol == "SPY" &&
            order.qty == 5);
  }, "Valid order placed becomes accessible with the return value of placeOrder()");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    auto oid = simBroker.placeOrder(marketp);
    simBroker.updateClock(1645508799);

    auto order = simBroker.getOrder(oid);
    return (order.type == SimBroker::OrderType::MARKET && order.extendedHours == false);
  }, "Order fills in default values");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000000000000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 50000;
    auto oid = simBroker.placeOrder(marketp);
    simBroker.cancelOrder(oid);
    simBroker.updateClock(simBroker.getClock()+10);

    auto order = simBroker.getOrder(oid);
    return (order.status == SimBroker::OrderStatus::CANCELLED);
  }, "Orders can be cancelled");


  printf(BYEL "\nMarket buy orders: \n" RESET);

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid = simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);
    
    auto order = simBroker.getOrder(oid);
    return (order.filledQty == 5);
  }, "Market buy orders fill");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::DAY;
    auto oid = simBroker.placeOrder(marketp); 
    simBroker.updateClock(1646404200+3600);
    
    auto order = simBroker.getOrder(oid);
    return (order.filledQty == 5);
  }, "Market buy orders fill even if we jump immediately beyond it's expiry");


  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid = simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);
  
    auto o = simBroker.getOrder(oid);
    return o.filledAvgPrice >= 430 && o.filledAvgPrice <= 450;
  }, "Filled market buy orders result in a valid filledAvgPrice");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);
   
    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" && pos.qty == 5) return true;
    }
    
    return false;
  }, "Filled market buy orders result in a position added");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid = simBroker.placeOrder(marketp);
    simBroker.updateClock(1645508799);

    auto o = simBroker.getOrder(oid);
    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" && 
          pos.qty == 5 && 
          pos.avgEntryPrice >= o.filledAvgPrice-0.1 &&
          pos.avgEntryPrice <= o.filledAvgPrice+0.1
          ) return true;
    }
   
    return false;
  }, "Filled market buy orders result in a position that has the correct avgEntryPrice");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 
    simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);
     
    return simBroker.getPositions().size() == 1;
  }, "Multiple filled market buy orders of the same ticker result in a single position");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 
    simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);
   
    return simBroker.getPositions().at(0).qty == 10;
  }, "Multiple filled market buy orders of the same ticker result in a position with the correct quantity");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid1 = simBroker.placeOrder(marketp);
    simBroker.updateClock(1645194600);
    auto oid2 = simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);
    auto oid3 = simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o1 = simBroker.getOrder(oid1);
    auto o2 = simBroker.getOrder(oid2);
    auto o3 = simBroker.getOrder(oid3);

    double correctAvgEntryPrice = (o1.filledAvgPrice+o2.filledAvgPrice+o3.filledAvgPrice)/3;

    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" &&
          pos.avgEntryPrice >= correctAvgEntryPrice-0.0001 &&
          pos.avgEntryPrice <= correctAvgEntryPrice+0.0001
          ) return true;
    }

    return false;
  }, "Multiple filled market buy orders of the same ticker result in a position that has the correct avgEntryPrice");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid = simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);

    auto o = simBroker.getOrder(oid);

    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" &&
          pos.qty == 5 &&
          pos.costBasis >= (o.filledAvgPrice*o.qty)-0.1 &&
          pos.costBasis <= (o.filledAvgPrice*o.qty)+0.1
          ) return true;
    }

    return false;
  }, "Filled market buy orders result in a position that has the correct cost basis");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);

    return simBroker.getBalance() < 500000-(440*5) &&
           simBroker.getBalance() > 500000-(445*5);
  }, "Market buy orders reduce our balance by the correct amount after being filled");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    double bp = simBroker.getBuyingPower();

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 500;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);

    return simBroker.getBuyingPower() < bp-(500*440) &&
           simBroker.getBuyingPower() > bp-(500*450);
  }, "Market buy orders reduce our buying power immediately by the correct amount (before fill)");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    auto oid = simBroker.placeOrder(marketp);
    auto order = simBroker.getOrder(oid);

    return order.status == SimBroker::OrderStatus::REJECTED; 
  }, "Market buy orders are rejected if we don't have enough cash balance");


  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(5000);
    simBroker.updateClock(1645108739);

    // Place limit order that will never fill, but consumes most of our money
    // (This should reduce buying power immediately)
    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 4900;
    limitp.limitPrice = 1.0;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(limitp);

    // Place market order that we won't possibly be able to afford with
    // available buying power, but would be able to afford with cash balance
    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 9; // SPY should be $400ish at this clock
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    auto oid = simBroker.placeOrder(marketp);
    auto o = simBroker.getOrder(oid);

    return o.status == SimBroker::OrderStatus::REJECTED;
  }, "Market buy orders are rejected if we have the cash balance "
  "but not the buying power to purchase");


  printf(BYEL "\nLimit buy orders: \n" RESET);

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 5;
    //limitp.side = SimBroker::OrderSide::BUY;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 1000.0;
    limitp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid = simBroker.placeOrder(limitp); 
    simBroker.updateClock(1645508799);
    
    auto order = simBroker.getOrder(oid);
    return (order.filledQty == 5);
  }, "Limit buy orders fill");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 500.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid = simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);
  
    auto o = simBroker.getOrder(oid);
    return o.filledAvgPrice >= 430 && o.filledAvgPrice <= 450;
  }, "Filled limit buy orders result in a valid filledAvgPrice");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 1000;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);
   
    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" && pos.qty == 5) return true;
    }
    
    return false;
  }, "Filled limit buy orders result in a valid position");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 500.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid = simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);
   
    auto o = simBroker.getOrder(oid);
    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" && 
          pos.qty == 5 && 
          pos.avgEntryPrice >= o.filledAvgPrice-0.1 &&
          pos.avgEntryPrice <= o.filledAvgPrice+0.1
          ) return true;
    }
    
    return false;
  }, "Filled limit buy orders result in a position that has the correct avgEntryPrice");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 500.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid = simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);
   
    auto o = simBroker.getOrder(oid);
    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" && 
          pos.qty == 5 && 
          pos.costBasis >= (o.filledAvgPrice*o.qty)-0.1 &&
          pos.costBasis <= (o.filledAvgPrice*o.qty)+0.1
          ) return true;
    }
    
    return false;
  }, "Filled limit buy orders result in a position that has the correct cost basis");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 1000.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);

    return simBroker.getBalance() < 500000;   
  }, "Limit buy orders reduce our balance after being filled");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    double bp = simBroker.getBuyingPower();

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5000;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 1.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 

    // We use a range to make sure FPU rounding is accounted for
    return (simBroker.getBuyingPower() >= (bp-5000.01) &&
            simBroker.getBuyingPower() <= (bp-4999.99));
  }, "Limit buy orders reduce our buying power immediately by the correct amount (before fill)");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 100.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    auto oid = simBroker.placeOrder(marketp);
    auto order = simBroker.getOrder(oid);

    return order.status == SimBroker::OrderStatus::REJECTED;
  }, "Limit buy orders are rejected if we don't have enough cash balance");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(5000);
    simBroker.updateClock(1645108739);

    // Place limit order that will never fill, but consumes most of our money
    // (This should reduce buying power immediately)
    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 4900;
    limitp.limitPrice = 1.0;
    //limitp.side = SimBroker::OrderSide::BUY;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(limitp);

    // Place market order that we won't possibly be able to afford with
    // available buying power, but would be able to afford with cash balance
    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 9;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 400.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED; 

    auto oid = simBroker.placeOrder(marketp);
    auto o = simBroker.getOrder(oid);

    return o.status == SimBroker::OrderStatus::REJECTED;
  }, "Limit buy orders are rejected if we have the cash balance "
  "but not the buying power to purchase");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 500.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);
    simBroker.placeOrder(marketp);
    simBroker.updateClock(1645508799);

    return simBroker.getPositions().size() == 1;
  }, "Multiple filled limit buy orders of the same ticker result in a single position");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 500.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 
    simBroker.placeOrder(marketp); 
    simBroker.updateClock(1645508799);
   
    return (simBroker.getPositions().size() > 0) ? simBroker.getPositions().at(0).qty == 10 : false;
  }, "Multiple filled limit buy orders of the same ticker result in a position with the correct quantity");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 500.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid1 = simBroker.placeOrder(marketp);
    simBroker.updateClock(1645194600);
    auto oid2 = simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o1 = simBroker.getOrder(oid1);
    auto o2 = simBroker.getOrder(oid2);
  
    double correctAvgEntryPrice = (o1.filledAvgPrice+o2.filledAvgPrice)/2;

    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" && 
          pos.avgEntryPrice >= correctAvgEntryPrice-0.0001 &&
          pos.avgEntryPrice <= correctAvgEntryPrice+0.0001
          ) return true;
    }
    
    return false;
  }, "Multiple filled limit buy orders of the same ticker result in a position that has the correct avgEntryPrice");

  test([&mSource](){
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 1;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 1000.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid1 = simBroker.placeOrder(marketp); 
    simBroker.updateClock(simBroker.getClock()+1);

    auto o1 = simBroker.getOrder(oid1);

    return (o1.filledQty == o1.qty &&
           o1.filledAt <= simBroker.getClock()+1);
  }, "Limit orders that should fill instantly do so");

  test([&mSource](){
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 1;
    marketp.type = SimBroker::OrderType::LIMIT;
    marketp.limitPrice = 1000.0;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    auto oid1 = simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+1);

    auto o1 = simBroker.getOrder(oid1);

    return (o1.filledQty == o1.qty &&
           o1.filledAt >= simBroker.getClock());
  }, "Limit orders that should fill instantly do not fill in the past");

  // Market sell orders
  printf(BYEL "\nMarket sell orders: \n" RESET);
  
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    simBroker.placeOrder(marketp); 
    simBroker.updateClock(simBroker.getClock()+3600);

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;

    auto oid = simBroker.placeOrder(sellp); 
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid);
    
    return (o.filledQty == -5 && o.filledQty == o.qty);
  }, "Market sell orders fill");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;

    auto oid = simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid); 
    return o.filledAvgPrice >= 430 && o.filledAvgPrice <= 450;
  }, "Filled market sell orders result in a valid filledAvgPrice");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    bool positionAdded = false;
    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" && pos.qty == 5) positionAdded = true;
    }

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;

    simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+3600);

    return ((simBroker.getPositions().size() == 0) && positionAdded);
  }, "Filled market sell orders that equal quantity of position result in a position removed");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 
    simBroker.updateClock(simBroker.getClock()+3600);
 
    bool positionAdded = false; 
    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" && pos.qty == 5) positionAdded = true;
    }

    SimBroker::OrderPlan sellp = marketp;
    //sellp.side = SimBroker::OrderSide::SELL;
    sellp.qty = -2;

    simBroker.placeOrder(sellp); 
    simBroker.updateClock(simBroker.getClock()+3600);
    
    return (positionAdded && (simBroker.getPositions().at(0).qty == 3));
  }, "Filled market sell orders less than quantity of position result in a position quantity shrunk by the correct amount");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 
    simBroker.updateClock(simBroker.getClock()+3600);

    double buyBalance = simBroker.getBalance();

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;

    simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+(3600*24));

    return simBroker.getBalance() > buyBalance+(440*5) &&
           simBroker.getBalance() < buyBalance+(445*5);

  }, "Market sell orders increase our balance by the correct amount after being filled");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(9000000000);
    simBroker.updateClock(1645108739);


    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 500000; // Huge order so it can't all have filled in the same second
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);

    double bp = simBroker.getBuyingPower();

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;
    //sellp.side = SimBroker::OrderSide::SELL;

    simBroker.placeOrder(sellp);

    return simBroker.getBuyingPower() > bp-0.1 &&
           simBroker.getBuyingPower() < bp+0.1;
  }, "Unfilled portion of market sell orders don't touch buying power");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(9000000);
    simBroker.updateClock(1645108739);


    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5000;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp); 
    simBroker.updateClock(simBroker.getClock()+3600);

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;
    //sellp.side = SimBroker::OrderSide::SELL;

    double bp = simBroker.getBuyingPower();
    auto oid = simBroker.placeOrder(sellp); 
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid);

    return simBroker.getBuyingPower() > bp+(llabs(o.filledQty)*440) &&
           simBroker.getBuyingPower() < bp+(llabs(o.filledQty)*450);
  }, "Filled market sell orders increase buying power by the correct amount");
 
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(9000000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    simBroker.placeOrder(marketp); 
    simBroker.updateClock(simBroker.getClock()+3600);

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -5;

    auto oid = simBroker.placeOrder(sellp); 
    simBroker.updateClock(simBroker.getClock()+3600); 

    auto o = simBroker.getOrder(oid);
    return o.status != SimBroker::OrderStatus::REJECTED; 
  }, "Market sell orders are not rejected if we have enough shares");

  // Limit sell orders
  printf(BYEL "\nLimit sell orders: \n" RESET);
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;
    sellp.type = SimBroker::OrderType::LIMIT;
    sellp.limitPrice = 1;

    auto oid = simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid);
    return (o.filledQty == -5 && o.filledQty == o.qty);
  }, "Limit sell orders fill");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;
    sellp.type = SimBroker::OrderType::LIMIT;
    sellp.limitPrice = 1;

    auto oid = simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+(3600*24));

    auto o = simBroker.getOrder(oid);
    return o.filledAvgPrice >= 430 && o.filledAvgPrice <= 450;
  }, "Filled limit sell orders result in a valid filledAvgPrice");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    bool positionAdded = false;
    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" && pos.qty == 5) positionAdded = true;
    }

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;
    sellp.type = SimBroker::OrderType::LIMIT;
    sellp.limitPrice = 1;

    simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+3600);

    return ((simBroker.getPositions().size() == 0) && positionAdded);
  }, "Filled limit sell orders that equal quantity of position result in a position removed");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    bool positionAdded = false;
    for (auto pos : simBroker.getPositions()) {
      if (pos.symbol == "SPY" && pos.qty == 5) positionAdded = true;
    }

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -2;
    sellp.type = SimBroker::OrderType::LIMIT;
    sellp.limitPrice = 1;

    simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+3600);

    return (positionAdded && (simBroker.getPositions().at(0).qty == 3));
  }, "Filled limit sell orders less than quantity of position result in a position quantity shrunk by the correct amount");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(500000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    double buyBalance = simBroker.getBalance();

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;
    sellp.type = SimBroker::OrderType::LIMIT;
    sellp.limitPrice = 1;

    simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+(3600*24));

    return simBroker.getBalance() > buyBalance+(440*5) &&
           simBroker.getBalance() < buyBalance+(445*5);

  }, "Limit sell orders increase our balance by the correct amount after being filled");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(9000000000);
    simBroker.updateClock(1645108739);


    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 500000; // Huge order so it can't all have filled in the same second
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);

    double bp = simBroker.getBuyingPower();

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;
    sellp.type = SimBroker::OrderType::LIMIT;
    sellp.limitPrice = 1;

    simBroker.placeOrder(sellp);

    return simBroker.getBuyingPower() > bp-0.1 &&
           simBroker.getBuyingPower() < bp+0.1;
  }, "Unfilled portion of limit sell orders don't touch buying power");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(9000000);
    simBroker.updateClock(1645108739);


    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5000;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);
    double bp1 = simBroker.getBuyingPower();

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;
    sellp.type = SimBroker::OrderType::LIMIT;
    sellp.limitPrice = 800;

    simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+3600);

    double bp2 = simBroker.getBuyingPower();
    //printf("%lf->%lf\n", bp1, bp2);
    return bp1 == bp2;
  }, "Unfilled limit sell orders don't touch buying power");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(9000000);
    simBroker.updateClock(1645108739);


    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5000;
    //marketp.side = SimBroker::OrderSide::BUY;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -marketp.qty;
    sellp.type = SimBroker::OrderType::LIMIT;
    sellp.limitPrice = 1;

    double bp = simBroker.getBuyingPower();
    auto oid = simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid);
    return simBroker.getBuyingPower() > bp+(llabs(o.filledQty)*440) &&
           simBroker.getBuyingPower() < bp+(llabs(o.filledQty)*450);
  }, "Filled limit sell orders increase buying power by the correct amount");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 50, false);
    simBroker.addFunds(9000000);
    simBroker.updateClock(1645108739);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 5;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    SimBroker::OrderPlan sellp = marketp;
    sellp.qty = -5;
    sellp.type = SimBroker::OrderType::LIMIT;
    sellp.limitPrice = 1;

    auto oid = simBroker.placeOrder(sellp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid);
    return o.status != SimBroker::OrderStatus::REJECTED;
  }, "Limit sell orders are not rejected if we have enough shares");

  // Market phases
  printf(BYEL "\nMarket phases: \n" RESET);

  test([&alwaysBarsSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&alwaysBarsSource, 1645650000+(8*3600), false); // 8 hours after market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 1;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    auto oid = simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid);
    return (o.filledQty == 0 && o.filledQty == 0);
  }, "Orders placed when the market is fully closed do not fill while closed");

  test([&alwaysBarsSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&alwaysBarsSource, 1645626600-(2*3600), false); // 2 hours before market open
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 1;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    auto oid = simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid);
    return (o.filledQty == 0);
  }, "Non-extended-hours orders placed premarket do not fill during premarket");

  test([&alwaysBarsSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&alwaysBarsSource, 1645650000+(2*3600), false); // 2 hours after market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 1;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    auto oid = simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid);
    return (o.filledQty == 0);
  }, "Non-extended-hours orders placed postmarket do not fill during postmarket");

  test([&alwaysBarsSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&alwaysBarsSource, 1645650000+(8*3600), false); // 8 hours after market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 1;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    auto oid = simBroker.placeOrder(marketp);
    simBroker.updateClock(1645713000+(0.5*3600)); // 30 minutes after the next market open

    auto o = simBroker.getOrder(oid);
    return (o.filledQty == 1);
  }, "Orders placed when the market is fully closed fill once open");

  test([&alwaysBarsSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&alwaysBarsSource, 1645626600-(2*3600), false); // 2 hours before market open
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 1;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    marketp.extendedHours = true;

    auto oid = simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid);
    return (o.filledQty == 1);
  }, "Extended-hours orders placed premarket fill during premarket");

  test([&alwaysBarsSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&alwaysBarsSource, 1645650000+(2*3600), false); // 2 hours after market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan marketp = {};
    marketp.symbol = "SPY";
    marketp.qty = 1;
    marketp.type = SimBroker::OrderType::MARKET;
    marketp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;
    marketp.extendedHours = true;

    auto oid = simBroker.placeOrder(marketp);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto o = simBroker.getOrder(oid);
    return (o.filledQty == 1);
  }, "Extended-hours orders placed postmarket fill during postmarket");

  // Time-in-force
  printf(BYEL "\nTime-in-force: \n" RESET);
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), false); // 4 hours before market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 9999999; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::DAY;

    auto oid = simBroker.placeOrder(limitp);

    simBroker.updateClock(1645713000); // Market open the next day

    auto o = simBroker.getOrder(oid);
    return o.status == SimBroker::OrderStatus::EXPIRED;
  }, "DAY time-in-force orders placed while market is open expire the next day");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), false); // 4 hours before market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 9999999; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::DAY;

    auto oid = simBroker.placeOrder(limitp);

    simBroker.updateClock(1645650000+(1*3600)); // Postmarket

    auto o = simBroker.getOrder(oid);
    return o.status == SimBroker::OrderStatus::EXPIRED;
  }, "DAY time-in-force orders placed while market is open expire postmarket");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), false); // 4 hours before market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 9999999; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::DAY;
    limitp.extendedHours = true;

    auto oid = simBroker.placeOrder(limitp);

    simBroker.updateClock(1645650000+(1*3600)); // Postmarket

    auto o = simBroker.getOrder(oid);
    return o.status != SimBroker::OrderStatus::EXPIRED;
  }, "Extended hours DAY time-in-force orders placed while market is open do not expire postmarket");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), false); // 4 hours before market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 9999999; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::DAY;
    limitp.extendedHours = true;

    auto oid = simBroker.placeOrder(limitp);

    simBroker.updateClock(1645713000); // Market open the next day

    auto o = simBroker.getOrder(oid);
    return o.status == SimBroker::OrderStatus::EXPIRED;
  }, "Extended hours DAY time-in-force orders placed while market is open expire the next day");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645626600-(4*3600), false); // 4 hours before market open
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 9999999; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::DAY;

    auto oid = simBroker.placeOrder(limitp);
    simBroker.updateClock(1645626600+(4*3600)); // 4 hours after market open same day

    auto o = simBroker.getOrder(oid);
    return o.status != SimBroker::OrderStatus::EXPIRED;
  }, "DAY time-in-force orders placed before market open do not expire same day");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645626600-(4*3600), false); // 4 hours before market open
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 9999999; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::DAY;
    limitp.extendedHours = true;

    auto oid = simBroker.placeOrder(limitp);
    simBroker.updateClock(1645626600+(4*3600)); // 4 hours after market open same day

    auto o = simBroker.getOrder(oid);
    return o.status != SimBroker::OrderStatus::EXPIRED;
  }, "Extended hours DAY time-in-force orders placed before market open do not expire same day");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000+(4*3600), false); // 4 hours after market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 9999999; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::DAY;

    auto oid = simBroker.placeOrder(limitp);

    simBroker.updateClock(1645713000+(4*3600)); // 4 hours after market open the next day

    auto o = simBroker.getOrder(oid);
    return o.status != SimBroker::OrderStatus::EXPIRED;
  }, "DAY time-in-force orders placed after market closed don't expire the next day");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000+(4*3600), false); // 4 hours after market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 9999999; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::DAY;
    limitp.extendedHours = true;

    auto oid = simBroker.placeOrder(limitp);

    simBroker.updateClock(1645713000+(4*3600)); // 4 hours after market open the next day

    auto o = simBroker.getOrder(oid);
    return o.status != SimBroker::OrderStatus::EXPIRED;
  }, "Extended hours DAY time-in-force orders placed after market closed don't expire the next day");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000+(4*3600), false); // 4 hours after market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 9999999; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::DAY;

    auto oid = simBroker.placeOrder(limitp);

    simBroker.updateClock(1645799400+(4*3600)); // 4 hours after market open the 2nd day

    auto o = simBroker.getOrder(oid);
    return o.status == SimBroker::OrderStatus::EXPIRED;
  }, "DAY time-in-force orders placed after market closed expire the 2nd market day");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000+(4*3600), false); // 4 hours after market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 9999999; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::GOOD_TILL_CANCELLED;

    auto oid = simBroker.placeOrder(limitp);

    simBroker.updateClock(1645799400+((24*3600)*365));

    auto o = simBroker.getOrder(oid);
    return o.status != SimBroker::OrderStatus::EXPIRED;
  }, "GOOD_TILL_CANCELLED time-in-force orders never expire");

  // Order status
  printf(BYEL "\nOrder status behavior: \n" RESET);
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), false); // 4 hours before market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan limitp = {};
    limitp.symbol = "SPY";
    limitp.qty = 1;
    limitp.type = SimBroker::OrderType::LIMIT;
    limitp.limitPrice = 1; // This order will never fill
    limitp.timeInForce = SimBroker::OrderTimeInForce::DAY;
    double bp = simBroker.getBuyingPower();
    auto oid = simBroker.placeOrder(limitp);

    simBroker.updateClock(1645713000+(4*3600)); // 4 hours after market open the next day

    auto o = simBroker.getOrder(oid);
    return simBroker.getBuyingPower() == bp && o.status == SimBroker::OrderStatus::EXPIRED;
  }, "Unfilled expired orders do not effect our buying power");

  // Margin
  printf(BYEL "\nMargin: \n" RESET);
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(1000);

    return simBroker.getBuyingPower() == 2000;
  }, "Enabling margin in the constructor results in a buying power that is double our balance (with default initial margin of 0.5)");
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.setInitialMarginRequirement(0.7);
    simBroker.addFunds(1000);

    return simBroker.getBuyingPower() == (1000/0.7);
  }, "With margin enabled and an initial margin requirement of 0.7 we have the correct buying power");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(1000);

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = 2;
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+200);

    return simBroker.getBuyingPower() >= 1100 && simBroker.getBuyingPower() < 2000 && 
           simBroker.getBuyingPower() <= 2000-800;
  }, "Enabling margin with funds of 1000 and a ~$800ish purchase made results in the correct buying power");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(1000);

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = 3;
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+200);

    return simBroker.getEquity() <= 1001 && simBroker.getEquity() >= 998;
  }, "When purchasing into margin, equity should correctly reflect cash+assets-loan");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(1000);

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = 3;
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+200);

    return simBroker.getBalance() < -250 && simBroker.getBalance() > -350;
  }, "When purchasing into margin, balance becomes a negative value by vaguely the correct amount");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), false); // 4 hours before market close
    simBroker.addFunds(1000);

    return simBroker.getBuyingPower() == 1000;
  }, "Not enabling margin in the constructor results in a buying power that is equal to our balance");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(1000);

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = 10;
    auto oid = simBroker.placeOrder(p);
    auto o = simBroker.getOrder(oid);

    return o.status == SimBroker::OrderStatus::REJECTED;
  }, "Trying to buy more than our margin-inclusive buying power results in a rejected order");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(1000);

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = 4;
    auto oid = simBroker.placeOrder(p);
    auto o = simBroker.getOrder(oid);

    return o.status != SimBroker::OrderStatus::REJECTED;
  }, "Trying to buy more than our balance, but within our margin-inclusive buying power results in a non-rejected order");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.setInitialMarginRequirement(0.7);
    return simBroker.getInitialMarginRequirement() == 0.7;
  }, "Initial margin requirement is set to the requirement specified by setInitialMarginRequirement");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    return simBroker.getInitialMarginRequirement() == 0.5;
  }, "Initial margin requirement is 0.5 by default");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(1000); 
    
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = 2;
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600); // 1 hour to fill the order
    double b1 = simBroker.getBalance(); 
    simBroker.updateClock(simBroker.getClock()+((3600*24)*7)); // 1 week to give it some time to accrue interest
    double b2 = simBroker.getBalance(); 

    return b1 == b2;
  }, "Buying stocks just within available cash does not charge interest");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(1000); 
    simBroker.setInitialMarginRequirement(0.5);
    
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = 4;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600*2)); // 2 hours to fill the order
    auto o = simBroker.getOrder(oid);
    double b1 = simBroker.getBalance(); 
    simBroker.updateClock(simBroker.getClock()+((3600*24)*7)); // 1 week to give it some time to accrue interest
    double b2 = simBroker.getBalance(); 

    double loaned = (o.filledQty*o.filledAvgPrice)-1000;
    // 5 days worth of interest (not 7 because weekend)
    double interestOwed = 0;
    for (int i = 0; i < 5; i++) {
      interestOwed += (((loaned+interestOwed)*0.0375)/360.0);
    }
    return (b2 > (b1-interestOwed)-0.001) &&
           (b2 < (b1-interestOwed)+0.001);
  }, "Buying stocks greater than our cash balance results in the correct default interest rate of 3.75% being charged on the loaned portion of the transaction");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(1000); 
    simBroker.setInitialMarginRequirement(0.5);
    simBroker.setInterestRate(0.05);
    
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = 4;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600*2)); // 2 hours to fill the order
    auto o = simBroker.getOrder(oid);
    double b1 = simBroker.getBalance(); 
    simBroker.updateClock(simBroker.getClock()+((3600*24)*7)); // 1 week to give it some time to accrue interest
    double b2 = simBroker.getBalance(); 

    double loaned = (o.filledQty*o.filledAvgPrice)-1000;
    // 5 days worth of interest (not 7 because weekend)
    double interestOwed = 0;
    for (int i = 0; i < 5; i++) {
      interestOwed += (((loaned+interestOwed)*0.05)/360.0);
    }
    return (b2 > (b1-interestOwed)-0.001) &&
           (b2 < (b1-interestOwed)+0.001);
  }, "Buying stocks greater than our cash balance results in the correct *user-defined* interest rate being charged on the loaned portion of the transaction");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1611856200, true); // Jan 28
    simBroker.addFunds(800); 
    simBroker.setInitialMarginRequirement(0.5);
  
    bool marginCalled = false; 
    simBroker.setMarginCallHandler([&marginCalled](){
      marginCalled = true; 
    });

    SimBroker::OrderPlan p = {};
    p.symbol = "GME";
    p.qty = 6;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);
    auto o = simBroker.getOrder(oid);

    simBroker.updateClock(1614085000); // Feb 23 morning before open
      
    return marginCalled;
  }, "If we go under the maintenance margin requirement with a large purchase into margin, we get margin called");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1611856200, true); // Jan 28
    simBroker.addFunds(800); 
    simBroker.setInitialMarginRequirement(0.5);
  
    bool marginCalled = false; 
    simBroker.setMarginCallHandler([&marginCalled](){
      marginCalled = true; 
    });

    SimBroker::OrderPlan p = {};
    p.symbol = "GME";
    p.qty = 3;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);
    auto o = simBroker.getOrder(oid);

    simBroker.updateClock(1614085000); // Feb 23 morning before open
      
    return !marginCalled;
  }, "If we make a massive loss with margin enabled but not purchased into margin, we *don't* get margin called");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1611856200, true); // Jan 28
    simBroker.addFunds(772); 
    simBroker.setInitialMarginRequirement(0.5);
  
    bool marginCalled = false; 
    simBroker.setMarginCallHandler([&marginCalled](){
      marginCalled = true; 
    });

    SimBroker::OrderPlan p = {};
    p.symbol = "GME";
    p.qty = 3;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);
    auto o = simBroker.getOrder(oid);

    simBroker.updateClock(1614085000); // Feb 23 morning before open
      
    return !marginCalled;
  }, "If we are only loaning $1, we don't get margin called with a massive on-margin loss"); // TODO: is this what it should do?
 
  test([&mSource, &neverMarginableSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&neverMarginableSource, 1611856200, true); 
    simBroker.addFunds(800); 
    simBroker.setInitialMarginRequirement(0.5);

    SimBroker::OrderPlan p = {};
    p.symbol = "GME";
    p.qty = 4;
    auto oid = simBroker.placeOrder(p);
    auto o = simBroker.getOrder(oid);

    SimBroker asimBroker((SimBrokerStockDataSource*)&mSource, 1611856200, true); 
    asimBroker.addFunds(800); 
    asimBroker.setInitialMarginRequirement(0.5);

    auto oid2 = asimBroker.placeOrder(p);
    auto o2 = asimBroker.getOrder(oid2);

    return (o.status == SimBroker::OrderStatus::REJECTED) && (o2.status != SimBroker::OrderStatus::REJECTED);
  }, "Only assets marked as marginable can be traded on margin"); 

  // TODO maintenance margin table?

  // Shorting
  printf(BYEL "\nShorting: \n" RESET);
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);

    auto pos = simBroker.getPositions().at(0);

    return pos.qty == -10; 
  }, "Selling a stock short results in a position with the correct negative quantity"); 

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), false); // 4 hours before market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);
    auto o = simBroker.getOrder(oid);

    return o.status == SimBroker::OrderStatus::REJECTED;     
  }, "Short orders are rejected if margin is disabled");

  test([&mSource, &neverETBSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);

    SimBroker simBroker2((SimBrokerStockDataSource*)&neverETBSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker2.addFunds(9000000);
    auto oid2 = simBroker2.placeOrder(p);
    simBroker2.updateClock(simBroker2.getClock()+3600);
     
    return simBroker.getOrder(oid).status != SimBroker::OrderStatus::REJECTED &&
           simBroker2.getOrder(oid2).status == SimBroker::OrderStatus::REJECTED;
  }, "Short orders are only accepted with easy-to-borrow securities");
 
  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);

    double bp1 = simBroker.getBuyingPower();
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;
    p.type = SimBroker::OrderType::LIMIT;
    p.limitPrice = 1000; // This order will never fill
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);
    double bp2 = simBroker.getBuyingPower();
   
    //double price1 = simBroker.getOrder(oid).filledAvgPrice;
    //double price2 = mSource.getPrice("SPY", simBroker.getClock());

    //printf("%lf->%lf\n", price1, price2);
    //printf("%lf->%lf\n", bp1, bp2);
    return bp2 == bp1-10000;
  }, "Unfilled short positions reduce our buying power by the correct amount"); 

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);

    double bp1 = simBroker.getBuyingPower();
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);
    double bp2 = simBroker.getBuyingPower();
 
    //double price1 = simBroker.getOrder(oid).filledAvgPrice;
    //double price2 = mSource.getPrice("SPY", simBroker.getClock());

    //printf("%lf->%lf\n", price1, price2);
    //printf("%lf->%lf\n", bp1, bp2);
    return bp2 <= bp1-4200 &&
           bp2 >= bp1-4300;
  }, "Filled short positions reduce our buying power by the correct amount"); 

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);

    double equity1 = simBroker.getEquity();
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;
    p.type = SimBroker::OrderType::LIMIT;
    p.limitPrice = 1000; // This order will never fill
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);
    double equity2 = simBroker.getEquity();

    return equity1 == equity2;
  }, "Unfilled short orders have no effect on equity");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);

    double equity1 = simBroker.getEquity();
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600*2));
    double equity2 = simBroker.getEquity();

    //double price1 = simBroker.getOrder(oid).filledAvgPrice;
    //double price2 = mSource.getPrice("SPY", simBroker.getClock());

    //printf("%lf->%lf\n", price1, price2);
    //printf("%lf->%lf\n", equity1, equity2);
    return equity2 >= equity1+23.99 &&
           equity2 <= equity1+24.01;
  }, "Shorting a stock that falls results in the correct higher equity"); 

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1646427600-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);

    double equity1 = simBroker.getEquity();
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600*2));
    double equity2 = simBroker.getEquity();

//printf("%lf->%lf\n", equity1, equity2);
 //   printf("%lf->%lf\n", price1, price2);
    return equity2 >= equity1-12.3 &&
           equity2 <= equity1-12.1;
  }, "Shorting a stock that rises results in the correct lower equity"); 

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1646427600-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);

    double balance1 = simBroker.getBalance();
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;
    p.type = SimBroker::OrderType::LIMIT;
    p.limitPrice = 1000; // This order will never fill
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600*2));
    double balance2 = simBroker.getBalance();

    return balance2 == balance1;
  }, "Unfilled short orders have no effect on balance");

  test([&mSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1646427600-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);

    double balance1 = simBroker.getBalance();
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600*2));
    double price1 = simBroker.getOrder(oid).filledAvgPrice;
    double balance2 = simBroker.getBalance();

    return balance2 >= (balance1+(price1*10))-1 &&
           balance2 <= (balance1+(price1*10))+1;
  }, "Filled short orders increase our balance by the value of the sale");

  test([&mSource, &neverShortableSource]() {

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -10;

    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);
    auto oid1 = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);

    SimBroker simBroker2((SimBrokerStockDataSource*)&neverShortableSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker2.addFunds(9000000);
    auto oid2 = simBroker2.placeOrder(p);
    simBroker2.updateClock(simBroker2.getClock()+3600);

    auto o1 = simBroker.getOrder(oid1);
    auto o2 = simBroker2.getOrder(oid2);

    return o1.status != SimBroker::OrderStatus::REJECTED &&
           o2.status == SimBroker::OrderStatus::REJECTED;
  }, "Only tickers that are marked as shortable can be shorted");

  test([&mSource, &neverShortableSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);

    // Buy 10 shares
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = 10;
    auto oid1 = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+3600);

    // Sell 20 shares
    p.qty = -20;
    auto oid2 = simBroker.placeOrder(p);

    auto o1 = simBroker.getOrder(oid1);
    auto o2 = simBroker.getOrder(oid2);

    return o2.status == SimBroker::OrderStatus::REJECTED;
  }, "If a short order is submitted with a long position already open, the order is rejected");

  test([&mSource, &neverShortableSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds((19*440)/2);

    // Sell 20 shares
    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -20;
    auto o1 = simBroker.getOrder(simBroker.placeOrder(p));
    return o1.status == SimBroker::OrderStatus::REJECTED;
  }, "If a short order that exceeds our available buying power is submitted, the order is rejected");

  test([&mSource, &neverShortableSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);
    simBroker.disableShortRoundLotFee();

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -5;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600));
    double filledPrice = simBroker.getOrder(oid).filledAvgPrice;

    simBroker.updateClock(simBroker.getClock()+(3600*9)); // Couple hours after postmarket
    double price = mSource.getPrice("SPY", simBroker.getClock());
    double interestOwed = ((price*5)*mSource.getAssetBorrowRate("SPY", simBroker.getClock()))/360;
    double equityShouldBe = (9000000.0+((filledPrice-price)*5.0))-interestOwed;
 
   // printf("%lf->%lf\n", filledPrice, price);
    return dround(simBroker.getEquity(),5) == dround(equityShouldBe,5);
  }, "Filled short orders with round lot fee disabled result in the correct interest being charged");

  test([&mSource, &neverShortableSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);
    simBroker.enableShortRoundLotFee();

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -5;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600));
    double filledPrice = simBroker.getOrder(oid).filledAvgPrice;

    simBroker.updateClock(simBroker.getClock()+(3600*9)); // Couple hours after postmarket
    double price = mSource.getPrice("SPY", simBroker.getClock());
    double interestOwed = ((price*100)*mSource.getAssetBorrowRate("SPY", simBroker.getClock()))/360;
    double equityShouldBe = (9000000.0+((filledPrice-price)*5.0))-interestOwed;
 
    //printf("%lf->%lf\n", filledPrice, price);
    //printf("%lf == %lf\n", simBroker.getEquity(), equityShouldBe);
    return dround(simBroker.getEquity(),5) == dround(equityShouldBe,5);
  }, "Filled short orders of qty 5 with round lot fee enabled result in the correct (qty of 100) interest being charged");

  test([&mSource, &neverShortableSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);
    simBroker.enableShortRoundLotFee();

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -100;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600));
    double filledPrice = simBroker.getOrder(oid).filledAvgPrice;

    simBroker.updateClock(simBroker.getClock()+(3600*9)); // Couple hours after postmarket
    double price = mSource.getPrice("SPY", simBroker.getClock());
    double interestOwed = ((price*100)*mSource.getAssetBorrowRate("SPY", simBroker.getClock()))/360;
    double equityShouldBe = (9000000.0+((filledPrice-price)*100.0))-interestOwed;
 
    //printf("%lf->%lf\n", filledPrice, price);
    //printf("%lf == %lf\n", simBroker.getEquity(), equityShouldBe);
    return dround(simBroker.getEquity(),5) == dround(equityShouldBe,5);
    return simBroker.getEquity() == equityShouldBe;
  }, "Filled short orders of qty 100 with round lot fee enabled result in the correct (qty of 100) interest being charged");

  test([&mSource, &neverShortableSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);
    simBroker.enableShortRoundLotFee();

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.qty = -110;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600));
    double filledPrice = simBroker.getOrder(oid).filledAvgPrice;

    simBroker.updateClock(simBroker.getClock()+(3600*9)); // Couple hours after postmarket
    double price = mSource.getPrice("SPY", simBroker.getClock());
    double interestOwed = ((price*200)*mSource.getAssetBorrowRate("SPY", simBroker.getClock()))/360;
    //printf("%lf\n", interestOwed);
    double equityShouldBe = (9000000.0+((filledPrice-price)*110))-interestOwed;
 
    //printf("%lf->%lf\n", filledPrice, price);
    //printf("%lf == %lf\n", simBroker.getEquity(), equityShouldBe);
    return dround(simBroker.getEquity(),5) == dround(equityShouldBe,5);
  }, "Filled short orders of qty 110 with round lot fee enabled result in the correct (qty of 200) interest being charged");

  test([&mSource, &neverShortableSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);
    return simBroker.shortRoundLotFeeEnabled();
  }, "Shorting round lot fee based interest rates are enabled by default");

  test([&mSource, &neverShortableSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1645650000-(4*3600), true); // 4 hours before market close
    simBroker.addFunds(9000000);
    simBroker.disableShortRoundLotFee();

    SimBroker::OrderPlan p = {};
    p.symbol = "SPY";
    p.type = SimBroker::OrderType::LIMIT;
    p.limitPrice = 900;
    p.qty = -5;
    simBroker.placeOrder(p);
    simBroker.updateClock(simBroker.getClock()+(3600*10)); // Couple hours after postmarket 
    return simBroker.getEquity() == 9000000;
  }, "Unfilled short orders do not result in interest charged");

  test([&mSource, &neverShortableSource]() {
    SimBroker simBroker((SimBrokerStockDataSource*)&mSource, 1610053200-(4*3600), true); // 4 hours before market close on jan 7
    simBroker.addFunds(200);

    bool marginCalled = false; 
    simBroker.setMarginCallHandler([&marginCalled](){
      marginCalled = true; 
    });

    // Short 5 shares of GME
    SimBroker::OrderPlan p = {};
    p.symbol = "GME";
    p.qty = -5;
    auto oid = simBroker.placeOrder(p);
    simBroker.updateClock(1611844200+(3600*2));
    auto o = simBroker.getOrder(oid);
    //double price = mSource.getPrice("GME", simBroker.getClock());

    //printf("%lf %ld %lf status: %d\n", price, o.filledQty, o.filledAvgPrice, o.status);

    return marginCalled;
  }, "If the price rises enough while we are holding a short position, we get margin called");

  // TODO: test that unfilled short orders cost us the correct borrow fee daily
  // TODO: round trip short trades equity/balance/buying power
  // TODO: short position margin calls
  // TODO: test that exception is thrown on a margin call if no handler is defined

  // TODO: test that margin interest is charged on weekend days too (it should be!)
  // TODO: test that short borrow fees are charged on weekend days too (it should be!)

  // TODO: accrue daily but charge borrow fee only at end of month like alpaca?
  
  // TODO: use dround instead of < and > for float comparisons in these tests

  // TODO: submitting a limit order without a limit price should throw an exception

  // TODO: test that an order status history item is added immediatly upon creating the order
  // TODO: test that an order status history item for rejected is added when an order is rejected

  // Shorting test
  //
  // Unfilled short: LIMIT SHORT: 5000 (10*500)
  //
  // Equity: 49,559.36->49,559.36
  // Buying power: 99,118.72->94,118.72
  // Cash: 49,559.36->49,559.36
  // Value: 0.00->0.00
  //
  //
  // Filled short: LIMIT SHORT: 4000 (10*400) Real 4,390.9
  //
  // Equity:       49,559.36->49,559.06 (stock went up)
  // Buying power: 99,118.72->94,726.92 (changing as stock moves around)
  // Cash:         49,559.36->53,950.26
  // Value:             0.00->4391.20


  // TODO: test that DAY time in force orders with extended hours expire only after postmarket,
  // while without extended hours expires right at market close

  // TODO: test that purchasing stocks that increase in value increases equity (and inverse)
  // TODO: Test that multiple calls to addToPosition with the same position still does the correct thing
  // TODO: test that orders never overfill
  // TODO: test get getOrder with invalid ID throws exception
  // TODO: test that expired/cancelled orders do not continue to fill
  // TODO: tests that compare with real-world behavior on brokerage, such as buying power over time after shorting a stock

  // Longer term tasks:
  // TODO: test orders quantities near int64_t max
  // TODO: test that orders near the end of our existing data partially fill, but also fully fill once the data becomes available

  // Some tests with paper trading:
  // Buying power is reduced upon placing limit  order without fill
  // Buying power is reduced upon placing market order without fill
  // Cash balance is not reduced upon placing limit  order without fill
  // Cash balance is not reduced upon placing market order without fill
  // Buying power changes dynamically without any fills to follow current market price
  // Buying into margin results in a negative cash value 49557.42 -> -38004.58
  // https://alpaca.markets/docs/trading/user-protections/ - Selling short and covering the short on the same day is also considered a day trade.

  testResults();
};



// LIMIT BUY: 97000.00 (100 * 970)
//
// Equity:       49,557.42->49,557.42
// Buying power: 99,114.84->2,114.84
// Cash:         49,557.42->49,557.42
// Value:        0.00
//
//LIMIT BUY: 10000.00 (100*100)
//
// Equity:       49,557.42->49,557.42
// Buying power: 99,114.84->89,114.84
// Cash:         49,557.42->49,557.42
// Value:        0.00
//
//

