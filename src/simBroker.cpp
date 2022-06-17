#include "simBroker.hpp"
#include <stdexcept>
#include "math.h"

// TODO: implement order expirey
// TODO: simulate the effect that our own investment has on the price of the stock
// TODO: simulate slippage (related to the above, but not fully defined by it)

SimBroker::SimBroker(SimBrokerStockDataSource* dataSource, uint64_t startTime, bool margin) : 
  stockDataSource(dataSource),
  balance(0.0),
  clock(startTime),
  marginEnabled(margin),
  lastInterestTime(startTime)
{};

void SimBroker::chargeDayInterest() {
  // Charge interest on margin usage 
  double shortPositionSaleValue = 0.0; 
  for (auto p : this->getPositions()) {
    if (p.qty < 0) shortPositionSaleValue -= p.avgEntryPrice*p.qty;
  }

  double cash = this->balance-shortPositionSaleValue;
  if (cash < 0) {
    double interest = (fabs(cash)*this->interestRate)/360;
    this->balance -= interest;
  }

  // Charge short position borrow fees
  for (auto pos : this->getPositions()) {
    if (pos.qty < 0) {
      double price = this->stockDataSource->getPrice(pos.symbol, this->clock);
      uint64_t qty = labs(pos.qty);
      if (this->shortRoundLotFee) qty = (((qty-1)/100)*100)+100;

      this->balance -= ((price*qty)*this->stockDataSource->getAssetBorrowRate(pos.symbol, this->clock))/360;
    }
  }

  this->lastInterestTime = this->clock;
}

void SimBroker::updateClock(uint64_t time) {
  if (time < this->clock) throw std::logic_error("SimBroker instructed to travel back in time (this is not possible).");

  if (this->marginEnabled) {
    // We need updateClock to be called after every market close to charge interest,
    // because interest calculations require the current state calculated at that point in time 
    uint64_t nextt = 0;
    while (
      (nextt = this->stockDataSource->getNextMarketPhaseChangeTo(this->lastInterestTime+1, SimBrokerStockDataSource::MarketPhase::CLOSED).time) 
      < time
      ) {
      this->updateClock(nextt);
      this->chargeDayInterest();
    }
  }

  uint64_t oldtime = this->clock;
  this->clock = time;
  if (time != oldtime) this->updateState();
}

uint64_t SimBroker::placeOrder(OrderPlan p) {
  if (p.timeInForce == SimBroker::OrderTimeInForce::IMMEDIATE_OR_CANCEL ||
      p.timeInForce == SimBroker::OrderTimeInForce::FILL_OR_KILL ||
      p.timeInForce == SimBroker::OrderTimeInForce::ON_OPEN ||
      p.timeInForce == SimBroker::OrderTimeInForce::ON_CLOSE) 
    throw std::logic_error("Requested order timeInForce type currently unsupported");

  if (p.type == SimBroker::OrderType::STOP ||
      p.type == SimBroker::OrderType::STOP_LIMIT ||
      p.type == SimBroker::OrderType::TRAILING_STOP)
    throw std::logic_error("Requested order type currently unsupported");

  if (p.orderClass != SimBroker::OrderClass::SIMPLE)
    throw std::logic_error("Requested order class currently unsupported");

  Order o = {};
  o.id = (this->orders.size() > 0 ) ? this->orders.back().id+1 : 0;
  o.createdAt   = this->clock;
  o.updatedAt   = this->clock;
  o.submittedAt = this->clock;
  o.filledAt    = 0;
  o.expiredAt   = 0;
  o.canceledAt  = 0;
  o.failedAt    = 0;
  o.replacedAt  = 0;
  o.replacedBy  = 0;
  o.replaces    = 0;
  o.filledQty   = 0;
  o.filledAvgPrice = 0.0;
  o.symbol = p.symbol;
  o.qty    = p.qty;
  o.type   = p.type;
  o.timeInForce   = p.timeInForce;
  o.limitPrice    = p.limitPrice;
  o.stopPrice     = p.stopPrice;
  o.trailPrice    = p.trailPrice;
  o.trailPercent  = p.trailPercent;
  o.extendedHours = p.extendedHours;
  o.orderClass    = p.orderClass;

  if (o.qty > 0) {
    // Set status for buy orders
    double price = this->stockDataSource->getPrice(o.symbol, this->clock);
    if (price < 0) this->setOrderStatus(o, SimBroker::OrderStatus::REJECTED, this->clock);
    if (price > o.limitPrice && o.type == OrderType::LIMIT) price = o.limitPrice;

    bool me = this->marginEnabled;
    this->marginEnabled = (this->stockDataSource->isTickerMarginable(p.symbol, this->clock) && me);
    if (this->getBuyingPower() >= (o.qty*price)) {
      this->setOrderStatus(o,SimBroker::OrderStatus::OPEN, this->clock);
    } else {
      this->setOrderStatus(o, SimBroker::OrderStatus::REJECTED, this->clock);
    }

    this->marginEnabled = me;
  } else if (o.qty < 0) {
    // Set status for sell orders
    double price = this->stockDataSource->getPrice(o.symbol, this->clock);
    int64_t existingQty = 0;
    for (auto p : this->getPositions())  { if (p.symbol == o.symbol) existingQty += p.qty; }

    bool isShort = (existingQty+o.qty) < 0;

    if (isShort) {
      // Short order
      this->setOrderStatus(o,SimBroker::OrderStatus::OPEN, this->clock);

      if (!this->marginEnabled) 
        this->setOrderStatus(o, SimBroker::OrderStatus::REJECTED, this->clock);
      if (!this->stockDataSource->isTickerShortable(o.symbol, this->clock)) 
        this->setOrderStatus(o, SimBroker::OrderStatus::REJECTED, this->clock);
      if (!this->stockDataSource->isTickerETB(o.symbol, this->clock)) 
        this->setOrderStatus(o, SimBroker::OrderStatus::REJECTED, this->clock);
      if (existingQty > 0)
        this->setOrderStatus(o, SimBroker::OrderStatus::REJECTED, this->clock);
      if (this->getBuyingPower() < labs(o.qty*price))
        this->setOrderStatus(o, SimBroker::OrderStatus::REJECTED, this->clock);
    } else {
      // Long order
      this->setOrderStatus(o,SimBroker::OrderStatus::OPEN, this->clock);
    }
  } else {
    // Order quantity of zero is not valid
    this->setOrderStatus(o, SimBroker::OrderStatus::REJECTED, this->clock);
  }

  orders.push_back(o);

  if (this->instaFill) {
    this->updateState();
    return this->getOrder(o.id).id;
  }

  return o.id;
}


void SimBroker::eachBarChunk(std::string ticker,
                             uint64_t startTime,
                             std::function<bool(std::vector<SimBrokerStockDataSource::Bar> bars, uint64_t chunkStart, uint64_t chunkEnd)> func) {
  const uint64_t chunkSize = 1000;
  for (uint64_t t = startTime; true; t += chunkSize*60) {
    uint64_t thisEnd = t+(chunkSize*60);
    if (t+60 > this->clock) break;
    if (thisEnd > this->clock) thisEnd = this->clock;

		uint64_t roundedEnd = ((thisEnd/60)*60);
		if (roundedEnd < thisEnd) roundedEnd += 60;
		thisEnd = roundedEnd;

    auto bars = this->stockDataSource->getMinuteBars(ticker, t, thisEnd);
    if (!func(bars, t, thisEnd)) break;
  }
}

/*void SimBroker::eachBar(std::string ticker, uint64_t startTime, std::function<bool(SimBrokerStockDataSource::Bar b)> func) {
  const uint64_t barCount = 5000;
  while (true) {
    auto bars = this->stockDataSource->getMinuteBars(ticker, startTime, startTime+(barCount*60));
    if (bars.size() == 0) break;
    if (startTime > this->clock) { break; }

    bool end = false;
    for (auto bar : bars)  {
      if (bar.time > this->clock) continue;
      if (ticker == "PLUG") printf("bar %ld %ld %lf\n", bar.time, startTime, bar.openPrice);
      if (!func(bar)) { end = true; break; }
    }

    if (end) { break;}
    startTime += (barCount*60);
  }
}*/

// Iterates forward until no more bars are available. Return false in lambda to stop.
// Will fill empty spaces in data with the most recently known price/bar
void SimBroker::eachBar(std::string ticker, uint64_t startTime, std::function<bool(SimBrokerStockDataSource::Bar b)> func) {
  uint64_t lastChunkEnd = 0;
  uint64_t clock = this->clock;

  SimBrokerStockDataSource::Bar myPrevBar;
  bool myPrevBarExists = false;

  this->eachBarChunk(ticker, 
                     startTime, 
                     [&startTime, &clock, &lastChunkEnd, *this, &ticker, &func, &myPrevBar, &myPrevBarExists]
                     (auto bars, uint64_t chunkStart, uint64_t chunkEnd) {
    int64_t barIndex = -1;
    SimBrokerStockDataSource::Bar bar;
    if (bars.size() > 0) { barIndex++; bar = bars.at(barIndex); }
    for (uint64_t bt = (chunkStart/60)*60; bt < chunkEnd; bt += 60) {
      if (bt < chunkStart) continue;
      if (bt <= lastChunkEnd && lastChunkEnd != 0) continue;
      while (barIndex >= 0 && bt >= bar.time+60 && barIndex+1 < (int64_t)bars.size()) { barIndex++; bar = bars.at(barIndex);}
      if (bt > clock) { return false; }

      SimBrokerStockDataSource::Bar myBar;
      if (barIndex >= 0 && bar.time <= bt) { 
        myBar = bar;
        myBar.time = bt;
      } else {
        // We didn't find any bars yet, so we call getPrice() to fill the bar (which should fall back to hour bars etc)
        // volume of zero, because if a trade occured there would be a bar
        // TODO: we could fall back on hour/day bars ourselves if we don't want to trust the stockDataSource to do it right
        double price;
        if (!myPrevBarExists) price = this->stockDataSource->getPrice(ticker, bt);
        else price = myPrevBar.closePrice;

        if (price > 0) myBar = {bt,price,price,price,price,0};
        else continue;
      }

      if (!func(myBar)) return false;

      if (myPrevBarExists && myPrevBar.time+60 != myBar.time) 
        throw std::runtime_error("Gap in data in bars in eachBar() "
                                 +std::to_string(myPrevBar.time)
                                 +"->"+std::to_string(myBar.time)
                                 +" (perhaps SimBrokerStockDataSource::getPrice returned < 0?)");
      myPrevBar = myBar;
      myPrevBarExists = true;
    }

		if (bars.size() > 0) lastChunkEnd = bars.back().time;
    return true; 
  });
}

double SimBroker::estimateFillRate(SimBrokerStockDataSource::Bar b) {
  // TODO incomplete model, as this assumes the entire market is trading exclusively with us.
  // (this is a good upper bound, however)
  //return (b.volume/60);
	return 999999999999999;
}

void SimBroker::updateOrderFillState(Order& o) {
  if (o.filledQty == o.qty || o.qty == 0 || o.doneFilling) return; // Nothing to do in these situations

  int64_t startQty = o.filledQty;
  uint64_t filledSeconds = 0;
  double avgPrice = 0.0;
  int64_t filledShares = 0;

  uint32_t i = 0;
  for (auto hist : o.orderStatusHistory) {
    if (hist.status != OrderStatus::OPEN) { i++; continue; }
    uint64_t nextStatus = 0;
    if (o.orderStatusHistory.size() > i+1) nextStatus = o.orderStatusHistory.at(i+1).time;

    this->eachBar(o.symbol, ((o.createdAt/60)*60)-60, [&filledSeconds, &avgPrice, &o, this, &filledShares, &nextStatus](auto bar) {
      if (nextStatus > 0 && bar.time > nextStatus) return false;
      if (bar.time+60 <= o.createdAt) return true;
      if (bar.time > this->clock) return false;

      // TODO: wouldn't we want to return true here?
      if (o.type == OrderType::LIMIT && ((o.qty > 0 && bar.openPrice > o.limitPrice) ||
                                         (o.qty < 0 && bar.openPrice < o.limitPrice))) return false;

      uint64_t relevantStart = bar.time;
      uint64_t relevantEnd = bar.time+60;

      bool back = false;
      for (uint64_t i = relevantStart; i < relevantEnd; i++) {
        bool relevantSecond = false;
        auto phase = this->stockDataSource->getMarketPhase(i);
        if (phase == SimBrokerStockDataSource::MarketPhase::OPEN) relevantSecond = true;
        if (phase == SimBrokerStockDataSource::MarketPhase::PREMARKET &&
            o.extendedHours == true) relevantSecond = true;
        if (phase == SimBrokerStockDataSource::MarketPhase::POSTMARKET &&
            o.extendedHours == true) relevantSecond = true;
        if (phase == SimBrokerStockDataSource::MarketPhase::CLOSED) relevantSecond = false;

        if (!relevantSecond && !back) relevantStart++;
        if (!relevantSecond && back) relevantEnd--;
        if (relevantSecond) back = true;
      }

      if (o.createdAt > relevantStart && !this->instaFill) relevantStart += o.createdAt-bar.time;
      if (this->clock < relevantEnd && !this->instaFill) relevantEnd -= (bar.time+60)-this->clock;

      int64_t relevantSeconds = relevantEnd-relevantStart;
      if (relevantSeconds < 0)  relevantSeconds = 0;

      if (relevantSeconds > 0) {
        filledSeconds += relevantSeconds;
        avgPrice += bar.closePrice*relevantSeconds;
        if (this->instaFill) filledShares = llabs(o.qty);
        else filledShares += (estimateFillRate(bar)*relevantSeconds);
      }

      if (filledShares >= llabs(o.qty)) {
        return false;
      }

      return true;
    });


    if (o.status != OrderStatus::OPEN) o.doneFilling = true;
    i++;
  }

  avgPrice /= filledSeconds;
  if (filledShares > llabs(o.qty)) filledShares = llabs(o.qty);

  if (o.qty > 0) o.filledQty = filledShares;
  if (o.qty < 0) o.filledQty = -filledShares;

  if (o.filledQty == o.qty) {
    o.filledAt = this->clock;
    o.filledAvgPrice = avgPrice;
  } else if (o.filledQty > 0) {
    o.filledAvgPrice = avgPrice;
  }

  this->addToPosition(o.symbol, o.filledQty-startQty, o.filledAvgPrice);
  this->balance -= (o.filledQty-startQty)*o.filledAvgPrice;
}

void SimBroker::updateOrderTIF(Order& o) {
  if (o.status != OrderStatus::OPEN) return;

  // if TIF is day, expires at whenever the next phase change away from open is
  // if TIF is day + o.extendedHours = true, expires whenever the next phase change to closed (after postmarket) is
  if (o.timeInForce == OrderTimeInForce::DAY) {
    uint64_t expires;
    if (o.extendedHours)  {
      expires = this->stockDataSource
        ->getNextMarketPhaseChangeTo(o.createdAt, SimBrokerStockDataSource::MarketPhase::CLOSED).time;
    } else {
      expires = this->stockDataSource
        ->getNextMarketPhaseChangeFrom(o.createdAt, SimBrokerStockDataSource::MarketPhase::OPEN).time;
    }

    if (this->clock >= expires && o.status == OrderStatus::OPEN)
      this->setOrderStatus(o, OrderStatus::EXPIRED, expires);
  }
}

double SimBroker::getTotalCostBasis() {
  double r = 0;
  for (auto& p : this->positions) {
    r += p.costBasis; 
  }

  return r;
}

void SimBroker::updateState() {
  for (auto& o : this->orders) {
    this->updateOrderTIF(o);
    this->updateOrderFillState(o);
  }

  // Send margin call if necessary
  if (this->marginEnabled && this->marginCallHandlerDefined) {
    if (this->checkForMarginCall()) this->marginCallHandler();
  }
}

double SimBroker::getLoan() { 
  if (!this->marginEnabled) return 0;
  double loan = 0;

  double shortPositionSaleValue = 0.0; 
  for (auto p : this->getPositions()) {
    if (p.qty < 0) shortPositionSaleValue -= p.avgEntryPrice*p.qty;
  }

  double marginLoan = -(this->balance-shortPositionSaleValue);
  if (marginLoan < 0) marginLoan = 0;
  loan += marginLoan;

  for (auto p : this->getPositions()) {
    if (p.qty < 0) loan += (this->stockDataSource->getPrice(p.symbol, this->clock)*labs(p.qty));
  }

  return loan;
}

bool SimBroker::checkForMarginCall() {
  double loan = this->getLoan();
  if (!this->marginEnabled || loan <= 0) return false;
  return this->getEquity()/loan < this->maintenanceMarginRequirement;
}

double SimBroker::getBalance() {
  return this->balance;
}

double SimBroker::getEquity() {
  double equity = this->balance;
  
  for (auto& p : this->positions) {
    double value = this->stockDataSource->getPrice(p.symbol, this->clock);
    equity += p.qty*value;
  }

  return equity;
}

void SimBroker::cancelOrder(uint64_t oid) { 
  for (auto& o : this->orders) { if (o.id == oid) { setOrderStatus(o, OrderStatus::CANCELLED, this->clock); return; } }
  throw std::logic_error("Invalid order ID");
}

SimBroker::Order SimBroker::getOrder(uint64_t id) {
  for (auto o : this->orders) { if (o.id == id) return o; }
  throw std::logic_error("Invalid order id provided");
  return {};
}

double SimBroker::getBuyingPower() {
  double buyingPower = this->balance;

  if (this->marginEnabled) {
    double assetValue = 0; 
    for (auto p : this->getPositions()) {
      double price = this->stockDataSource->getPrice(p.symbol, this->clock);
      assetValue += p.qty*price;
    }

    // We can't use the cash we earned from selling borrowed shares as collateral before the short is filled
    double shortPositionSaleValue = 0.0; 
    for (auto p : this->getPositions()) {
      if (p.qty < 0) shortPositionSaleValue -= p.avgEntryPrice*p.qty;
    }

    double availableCollateral = (this->getBalance()-shortPositionSaleValue)+assetValue;

    buyingPower = (availableCollateral/this->initialMarginRequirement)-assetValue;
  }

  for (auto& o : this->orders) {
    if (o.filledQty == o.qty) continue; // Filled orders don't effect buying power
    if (o.status != SimBroker::OrderStatus::OPEN) continue; // Only open orders effect buying power
    
    // Long sell orders don't effect buying power
    if (o.qty < 0) {
      int64_t pqty = 0;
      for (auto p : this->getPositions()) { if (p.symbol == o.symbol) pqty += p.qty; }
      if (pqty > 0) continue; 
    }

    int64_t sharesToBeFilled = labs(o.qty-o.filledQty);

    if (sharesToBeFilled != 0) {
      double price = this->stockDataSource->getPrice(o.symbol, this->clock); 
      if (o.type == SimBroker::OrderType::LIMIT) {
        buyingPower -= o.limitPrice*sharesToBeFilled;
      } else {
        buyingPower -= price*sharesToBeFilled;
      }
    }
  }

  return buyingPower;
}

void SimBroker::addToPosition(std::string symbol, int64_t qty, double avgPrice) {
  // Try to apply this to an existing position
  bool exists = false;
  for (auto& p : this->positions) {
    if (p.symbol == symbol) {
      p.avgEntryPrice = ((p.qty*p.avgEntryPrice)+(qty*avgPrice))/((p.qty+qty)*1.0);
      p.costBasis = p.avgEntryPrice*p.qty;
      p.qty += qty;
      exists = true;
      break;
    }
  }

  // New position
  if (!exists) {
    Position p = {};
    p.id = (this->positions.size() > 0) ? this->positions.back().id+1 : 0;
    p.symbol = symbol;
    p.avgEntryPrice = avgPrice;
    p.qty = qty;
    p.costBasis = p.avgEntryPrice*p.qty;
    p.createdTime = this->clock;

    this->positions.push_back(p);
  }

  // Remove empty positions
  int i = 0;
  for (auto& p : this->positions) {
    if (p.qty == 0) this->positions.erase(this->positions.begin() + i);
    i++;
  }
}

void SimBroker::setOrderStatus(Order& o, OrderStatus s, uint64_t time) {
  o.status = s;
  o.orderStatusHistory.push_back({s, time});

  std::sort(o.orderStatusHistory.begin(), o.orderStatusHistory.end(), [](const auto& a, const auto& b) -> bool {
    return a.time < b.time;  
  });
}



void SimBroker::enableInstaFill() { this->instaFill = true; }
void SimBroker::disableInstaFill() { this->instaFill = false; }
bool SimBroker::instaFillEnabled() { return this->instaFill; }
void SimBroker::enableShortRoundLotFee() { this->shortRoundLotFee = true; }
void SimBroker::disableShortRoundLotFee() { this->shortRoundLotFee = false; }
bool SimBroker::shortRoundLotFeeEnabled() { return this->shortRoundLotFee; }
uint64_t SimBroker::getClock() { return this->clock; }
void SimBroker::addFunds(double chedda) { this->balance += chedda; }
void SimBroker::rmFunds(double chedda) { this->balance -= chedda; }
std::vector<SimBroker::Order> SimBroker::getOrders() { return this->orders; }
std::vector<SimBroker::Position> SimBroker::getPositions() { return this->positions; }
void SimBroker::setInterestRate(double rate) { this->interestRate = rate; }
double SimBroker::getInterestRate() { return this->interestRate; }
void SimBroker::setInitialMarginRequirement(double req) { this->initialMarginRequirement = req; }
double SimBroker::getInitialMarginRequirement() { return this->initialMarginRequirement; }
void SimBroker::setMarginCallHandler(std::function<void()> func) {
  this->marginCallHandler = func;  
  this->marginCallHandlerDefined = true;
}
