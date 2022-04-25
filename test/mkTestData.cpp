#include <stdio.h>
#include <alpaca_api_client/AlpacaApiClient.hpp>  
#include <alpaca_api_client/SimulatedAlpacaApiClient.hpp>  
#include <util.hpp>

void addBars(std::string ticker, uint64_t start, uint64_t end, std::string timeframe, FILE* f, sdc::MarketDataAlpacaApiClient& alpaca) {
  json bars = alpaca.bars(ticker,epochToRfc3339(start),epochToRfc3339(end),5000,timeframe)["bars"];

  for (auto bar : bars) {
    fprintf(f,"%s,%s:%ld,%lf,%lf,%lf,%lf,%ld\n",
            ticker.c_str(),
            timeframe.c_str(),
            rfc3339ToEpoch(json::string_t(bar["t"])),  
            json::number_float_t(bar["o"]),
            json::number_float_t(bar["c"]),
            json::number_float_t(bar["h"]),
            json::number_float_t(bar["l"]),
            json::number_integer_t(bar["v"])
           );
  }
}

void addBarsRange(sdc::MarketDataAlpacaApiClient& alpaca, std::string ticker, uint64_t start, uint64_t end, FILE* f) {
  for (uint64_t i = start; i < end; i += (60*2000) ) {
    printf("Grabbing bars %ld-%ld\n",i,i+(60*2000));
    addBars(ticker,i,i+(60*2000),"1Min", f, alpaca);
    sleep(2);
  }
}

void addCal(uint64_t start, uint64_t end, FILE* fcal, sdc::MarketDataAlpacaApiClient& alpaca) {
  json calr = alpaca.calendar(epochToYmdDate(start), epochToYmdDate(end));

  for (json calDay : calr) {
    std::string openDateTime = json::string_t(calDay["date"]) + " " +
      json::string_t(calDay["open"]);

    std::string closeDateTime = json::string_t(calDay["date"]) + " " +
      json::string_t(calDay["close"]);

    uint64_t unixOpenTime = toUnixTime(openDateTime,"%Y-%m-%d %H:%M", "America/New_York");
    uint64_t unixCloseTime = toUnixTime(closeDateTime,"%Y-%m-%d %H:%M", "America/New_York");

    fprintf(fcal, "%ld,%ld\n", unixOpenTime, unixCloseTime);
  }
}

int main() {
  std::string alpacaKey;
  std::string alpacaSecret;

  printf("Enter alpaca key id:");
  std::getline(std::cin, alpacaKey);
  printf("Enter alpaca secret:");
  std::getline(std::cin, alpacaSecret);

  const uint64_t end   = 1646700737;
  const uint64_t start = end-((24*3600)*30);
  const uint64_t GMEend   = 1614042804;
  const uint64_t GMEstart = GMEend-((24*3600)*30);


  sdc::MarketDataAlpacaApiClient alpaca(alpacaKey,alpacaSecret,true);

  FILE* f = fopen("test/data/bars.testdata","w");

  printf("Grabbing SPY bars\n");
  addBarsRange(alpaca, "SPY", start, end, f);
  printf("Grabbing GME bars\n");
  addBarsRange(alpaca, "GME", GMEstart, GMEend, f);

  fclose(f);

  FILE* fcal = fopen("test/data/calendar.testdata","w");
  printf("Grabbing SPY calender\n");
  addCal(start, end, fcal, alpaca);
  printf("Grabbing GME calender\n");
  addCal(GMEstart, GMEend, fcal, alpaca);
  fclose(fcal);

  return 0;
}

// File format test/data/bars.testdata:
// ticker,timeframe:time,openprice,closeprice,highprice,lowprice,volume\n

// File format test/data/calendar.testdata:
// open,close\n
