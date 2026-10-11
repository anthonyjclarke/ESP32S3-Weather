#include <ImprovWiFiLibrary.h>
#include <cassert>
#include <deque>
#include <iostream>
#include <string>
class MemoryStream : public Stream {
public:
  std::deque<uint8_t> input;
  std::vector<uint8_t> output;
  int available() override { return input.size(); }
  int read() override { auto value=input.front(); input.pop_front(); return value; }
  size_t write(const uint8_t* bytes, size_t count) override {
    output.insert(output.end(),bytes,bytes+count);return count;
  }
};
std::vector<uint8_t> frame(std::vector<uint8_t> payload) {
  std::vector<uint8_t> bytes={'I','M','P','R','O','V',1,3,static_cast<uint8_t>(payload.size())};
  bytes.insert(bytes.end(),payload.begin(),payload.end());
  uint8_t checksum=0;for(auto byte:bytes)checksum+=byte;
  bytes.push_back(checksum);return bytes;
}
int connections=0;
int main() {
  MemoryStream stream;
  ImprovWiFi improv(&stream);
  improv.setDeviceInfo(ImprovTypes::CF_ESP32_S3,"ESP32S3-Weather","1.5.8","Weather-test","http://{LOCAL_IPV4}/");
  improv.setCustomConnectWiFi([](const char*,const char*){++connections;return true;});
  improv.onImprovConnected([](const char*,const char*){});
  improv.onImprovError([](ImprovTypes::Error){});
  auto send=[&](const std::vector<uint8_t>& bytes){for(auto b:bytes)stream.input.push_back(b);while(stream.available())improv.handleSerial();};
  send(frame({2,0})); // state followed immediately by info: shared parser regression
  stream.output.clear();send(frame({3,0}));
  std::string info(stream.output.begin(),stream.output.end());
  assert(info.find("ESP32S3-Weather")!=std::string::npos);
  assert(info.find("ESP32-S3")!=std::string::npos && info.find("1.5.8")!=std::string::npos);
  send(frame({}));send(frame({1}));send(frame({1,2,255,0})); // truncated RPC/SSID
  std::vector<uint8_t> oversized={'I','M','P','R','O','V',1,3,255};oversized.resize(264,0);send(oversized);
  assert(connections==0);
  stream.output.clear();send(frame({3,0}));
  info.assign(stream.output.begin(),stream.output.end());assert(info.find("1.5.8")!=std::string::npos);
  send(frame({1,4,1,'a',1,'b'}));assert(connections==1);
  std::cout<<"Improv adjacent frames, S3 identity and malformed-input bounds passed\n";
}
